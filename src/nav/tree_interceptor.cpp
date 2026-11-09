// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/tree_interceptor.hpp.
#include "alus/nav/tree_interceptor.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/nav/service.hpp"

#include <algorithm>
#include <utility>

namespace alus::nav {
namespace {

constexpr int kMaxAncestorDepth = 64;

}  // namespace

// ---------------------------------------------------------------------------
// TreeInterceptor
// ---------------------------------------------------------------------------

TreeInterceptor::TreeInterceptor(std::vector<std::string> classChain, a11y::ObjectPtr root,
                                 Service& service, const script::Catalog* catalog)
    : script::ScriptableObject(std::move(classChain), catalog),
      root_(std::move(root)),
      service_(service) {}

TreeInterceptor::~TreeInterceptor() = default;

bool TreeInterceptor::isAlive() const {
    return root_ != nullptr && root_->isAlive();
}

bool TreeInterceptor::isReady() const {
    // §9.1, in one line: there is nothing to build, so being ready means the
    // document has stopped loading. A BUSY root is a page mid-render, and
    // reading it announces half a page and then contradicts itself.
    if (!isAlive()) {
        return false;
    }
    return !root_->states().has(a11y::State::Busy);
}

bool TreeInterceptor::setPassThrough(bool value, bool announce) {
    if (passThrough_ == value) {
        return false;
    }
    passThrough_ = value;
    ALUS_DEBUG("tree interceptor: pass through {}", value ? "on" : "off");
    if (announce) {
        // Translators: spoken when switching between browse mode and focus
        // mode.
        service_.message(value ? _("Focus mode") : _("Browse mode"));
    }
    return true;
}

namespace {

// NVDA: `RootProxyTextInfo`. A range over the root object that answers with
// the INTERCEPTOR's content. It is a thin forwarder and not a subclass of the
// document range, because its identity must be the root object's: phase 5's
// `shouldReportTextContent` and the caret handling both compare a range's
// object against the focus, and a proxy that claimed to be the document would
// make a focus change inside a document look like a change of document.
class RootProxyRange final : public text::TextRange {
public:
    RootProxyRange(a11y::ObjectPtr root, text::TextRangePtr inner)
        : root_(std::move(root)), inner_(std::move(inner)) {}

    std::string_view rangeKind() const override { return "root-proxy"; }

    std::unique_ptr<text::TextRange> copy() const override {
        return std::unique_ptr<text::TextRange>(
            new RootProxyRange(root_, inner_->copy()));
    }

    std::string text() const override { return inner_->text(); }
    text::TextWithFields getTextWithFields(
        const text::FormatReportSettings& settings) const override {
        return inner_->getTextWithFields(settings);
    }
    int compareEndPoints(const text::TextRange& other,
                         text::EndPointPair which) const override {
        if (const auto* proxy = dynamic_cast<const RootProxyRange*>(&other)) {
            return inner_->compareEndPoints(*proxy->inner_, which);
        }
        return inner_->compareEndPoints(other, which);
    }
    bool setEndPoint(const text::TextRange& other, text::EndPointPair which) override {
        if (const auto* proxy = dynamic_cast<const RootProxyRange*>(&other)) {
            return inner_->setEndPoint(*proxy->inner_, which);
        }
        return inner_->setEndPoint(other, which);
    }
    bool expand(text::TextUnit unit) override { return inner_->expand(unit); }
    void collapse(bool toEnd) override { inner_->collapse(toEnd); }
    int move(text::TextUnit unit, int direction,
             std::optional<text::EndPoint> endPoint) override {
        return inner_->move(unit, direction, endPoint);
    }
    bool find(std::string_view needle, bool caseSensitive, bool reverse) override {
        return inner_->find(needle, caseSensitive, reverse);
    }
    bool updateCaret() override { return inner_->updateCaret(); }
    bool updateSelection() override { return inner_->updateSelection(); }
    text::Bookmark bookmark() const override { return inner_->bookmark(); }
    bool moveToBookmark(const text::Bookmark& mark) override {
        return inner_->moveToBookmark(mark);
    }
    std::vector<core::Rect> boundingRects() const override {
        return inner_->boundingRects();
    }
    std::optional<core::Point> pointAtStart() const override {
        return inner_->pointAtStart();
    }
    std::optional<int> unitIndex(text::TextUnit unit) const override {
        return inner_->unitIndex(unit);
    }
    std::optional<int> unitCount(text::TextUnit unit) const override {
        return inner_->unitCount(unit);
    }
    std::string locationText() const override { return inner_->locationText(); }

    const a11y::ObjectPtr& object() const noexcept { return root_; }

private:
    a11y::ObjectPtr root_;
    text::TextRangePtr inner_;
};

}  // namespace

text::TextRangePtr TreeInterceptor::makeRootProxyRange(text::Position position) const {
    text::TextRangePtr inner = makeTextRange(position);
    if (inner == nullptr) {
        return nullptr;
    }
    return text::TextRangePtr(new RootProxyRange(root_, std::move(inner)));
}

// ---------------------------------------------------------------------------
// TreeInterceptorRegistry
// ---------------------------------------------------------------------------

TreeInterceptorRegistry::TreeInterceptorRegistry(Service& service) : service_(service) {}

TreeInterceptorRegistry::~TreeInterceptorRegistry() { clear(); }

void TreeInterceptorRegistry::addChooser(Chooser chooser) {
    if (chooser != nullptr) {
        // Later choosers are more specific, so they are asked first.
        choosers_.insert(choosers_.begin(), std::move(chooser));
    }
}

void TreeInterceptorRegistry::addVeto(Veto veto) {
    if (veto != nullptr) {
        vetoes_.push_back(std::move(veto));
    }
}

bool TreeInterceptorRegistry::isVetoed(const a11y::ObjectPtr& object) const {
    if (object == nullptr) {
        return false;
    }
    for (const Veto& veto : vetoes_) {
        if (veto(object)) {
            return true;
        }
    }
    return false;
}

TreeInterceptor* TreeInterceptorRegistry::forObject(const a11y::Object& object) const {
    const auto it = interceptors_.find(object.ref());
    return it != interceptors_.end() ? it->second.get() : nullptr;
}

TreeInterceptor* TreeInterceptorRegistry::update(const a11y::ObjectPtr& object) {
    if (object == nullptr || !object->isAlive()) {
        return nullptr;
    }
    // NVDA walks UP from the object: the focus is usually a paragraph or a
    // link inside the document, not the document itself.
    a11y::ObjectPtr candidate = object;
    for (int depth = 0; depth < kMaxAncestorDepth && candidate != nullptr; ++depth) {
        if (TreeInterceptor* existing = forObject(*candidate)) {
            if (existing->isAlive()) {
                setCurrent(existing);
                object->setTreeInterceptor(existing, existing->isReady());
                return existing;
            }
            remove(*candidate);
        }
        if (isVetoed(candidate)) {
            // Not "no interceptor for this object" but "no interceptor in
            // this chain": an app module that blocks its editor would be
            // defeated by the document ancestor one level up.
            ALUS_DEBUG("tree interceptor vetoed for {}", candidate->path());
            return nullptr;
        }
        for (const Chooser& chooser : choosers_) {
            TreeInterceptorPtr made = chooser(candidate, service_);
            if (made == nullptr) {
                continue;
            }
            TreeInterceptor* raw = made.get();
            interceptors_.emplace(candidate->ref(), std::move(made));
            // The object that OWNS the content gets the pointer, and so does
            // the object the focus is on -- phase 5's `shouldReportTextContent`
            // asks the focused object, not the root.
            candidate->setTreeInterceptor(raw, raw->isReady());
            object->setTreeInterceptor(raw, raw->isReady());
            setCurrent(raw);
            ALUS_INFO("tree interceptor created for {} ({})", candidate->path(),
                      raw->scriptClasses().empty() ? "?" : raw->scriptClasses().front());
            return raw;
        }
        candidate = candidate->parent();
    }
    return nullptr;
}

TreeInterceptor* TreeInterceptorRegistry::current() const {
    if (current_ != nullptr && !current_->isAlive()) {
        return nullptr;
    }
    return current_;
}

void TreeInterceptorRegistry::setCurrent(TreeInterceptor* interceptor) {
    current_ = interceptor;
}

void TreeInterceptorRegistry::remove(const a11y::Object& object) {
    const auto it = interceptors_.find(object.ref());
    if (it == interceptors_.end()) {
        return;
    }
    if (current_ == it->second.get()) {
        current_ = nullptr;
    }
    // The object must forget the pointer BEFORE the interceptor is destroyed,
    // or a later event handler reads freed memory. Phase 4's own rule: a raw
    // pointer is only ever as good as the thing that clears it.
    if (const a11y::ObjectPtr root = it->second->rootObject()) {
        root->setTreeInterceptor(nullptr, false);
    }
    interceptors_.erase(it);
}

void TreeInterceptorRegistry::sweep() {
    for (auto it = interceptors_.begin(); it != interceptors_.end();) {
        if (it->second->isAlive()) {
            ++it;
            continue;
        }
        if (current_ == it->second.get()) {
            current_ = nullptr;
        }
        if (const a11y::ObjectPtr root = it->second->rootObject()) {
            root->setTreeInterceptor(nullptr, false);
        }
        it = interceptors_.erase(it);
    }
}

void TreeInterceptorRegistry::clear() {
    current_ = nullptr;
    for (auto& [ref, interceptor] : interceptors_) {
        if (const a11y::ObjectPtr root = interceptor->rootObject()) {
            root->setTreeInterceptor(nullptr, false);
        }
    }
    interceptors_.clear();
}

}  // namespace alus::nav
