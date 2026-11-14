// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `TreeInterceptor`: the thing that stands between the user and an
// application's own object tree.
//
// Port of NVDA's `treeInterceptorHandler.py` as architecture.md §8.5
// specifies it: the lifecycle, `isReady`, the `passThrough` policy,
// `rootNVDAObject`, `RootProxyTextInfo`, and creation through
// `Object::treeInterceptorClass`.
//
// WHAT IT IS FOR. In a plain application the user moves the focus and ALUS
// reports it. In a document the user moves a caret ALUS owns, through content
// no single object represents -- so something must own that caret, answer the
// movement keys, and decide which keys reach the application at all. That is
// this object. Phase 5 already built the hooks that ask for it
// (`Object::setTreeInterceptor`, `shouldReportTextContent`,
// `Review::documentPosition`); this fills them.
//
// `isReady` IS NOT `prepare`. §9.1: there is nothing to build, because the
// browser maintains the buffer. So `prepare()` is a no-op, `shouldPrepare` is
// false, and `isReady` is one question -- has the root stopped being BUSY.
// That is the whole of what NVDA's three-stage loading becomes on Linux, and
// `alus+f5` (refresh buffer) has no meaning and is not bound.
//
// THE N49 RULE IS WHY THE CARET IS A BOOKMARK. An interceptor lives alongside
// the `a11y::Object` it intercepts (the object holds a raw pointer to it), so
// an interceptor that stored a `TextRange` over that object would close the
// reference ring phase 5 found under valgrind. The caret is therefore a
// `text::Bookmark` plus the root, and a range is rebuilt from it on demand --
// which costs nothing, because a range is cheap and a stale one is a lie.
//
// THREADING: main thread only.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/script/scriptable.hpp"
#include "alus/text/fields.hpp"
#include "alus/text/range.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace alus::script {
class Catalog;
}

namespace alus::nav {

class Service;

class TreeInterceptor : public script::ScriptableObject {
public:
    // `catalog` is what gives the bound scripts their descriptions,
    // categories and flags. Passing it is not cosmetic: a script with no
    // description is HIDDEN from the gesture dialog (NVDA's rule, T9.3) and
    // input help has nothing to say about it. Browse mode's ~150 commands had
    // no catalogue at all until this parameter was threaded through
    // (docs/history/phase-9.md N101).
    TreeInterceptor(std::vector<std::string> classChain, a11y::ObjectPtr root,
                    Service& service, const script::Catalog* catalog = nullptr);
    ~TreeInterceptor() override;

    const a11y::ObjectPtr& rootObject() const noexcept { return root_; }

    // NVDA: `isAlive`. The root is not DEFUNCT and is still in the tree.
    virtual bool isAlive() const;
    // NVDA: `isReady`. §9.1: the root's BUSY state is over. Until then the
    // document is still loading and reading it would announce half a page.
    virtual bool isReady() const;
    // NVDA: `shouldPrepare` / `prepare`. False and a no-op; see the file
    // comment.
    virtual bool shouldPrepare() const { return false; }
    virtual void prepare() {}

    // NVDA: `passThrough`. When true the user's keys go to the application and
    // ALUS reports the focus; when false ALUS owns the caret. This is the
    // single most consequential boolean in browse mode, and step 6 of the
    // script lookup reads it through `ScriptableObject::passThrough`.
    bool passThrough() const override { return passThrough_; }
    // Returns whether it changed, so a caller can decide whether to announce.
    virtual bool setPassThrough(bool value, bool announce = true);

    // NVDA: `makeTextInfo`. The range over the intercepted content.
    virtual text::TextRangePtr makeTextRange(text::Position position) const = 0;

    // NVDA: `RootProxyTextInfo`. A range over the ROOT OBJECT that reports the
    // interceptor's content instead of the object's own -- which is what makes
    // `alus+upArrow` on a document say the current line of the DOCUMENT, not
    // the root's U+FFFC placeholders (N47).
    text::TextRangePtr makeRootProxyRange(text::Position position) const;

    Service& service() const noexcept { return service_; }

    // NVDA: `event_treeInterceptor_gainFocus`. What is announced when the
    // user arrives in the document.
    virtual void onGainFocus() {}
    // Called when the focus moves inside the intercepted content; the
    // interceptor decides whether that means focus mode.
    virtual void onInnerFocus(const a11y::ObjectPtr& object) { (void)object; }
    virtual void onCaretMove(const a11y::ObjectPtr& object) { (void)object; }

protected:
    void setRoot(a11y::ObjectPtr root) { root_ = std::move(root); }

    a11y::ObjectPtr root_;
    Service& service_;
    bool passThrough_ = false;
};

using TreeInterceptorPtr = std::unique_ptr<TreeInterceptor>;

// NVDA's `Object::treeInterceptorClass` plus `treeInterceptorHandler.update`:
// which objects get an interceptor, which kind, and who owns them.
//
// THE REGISTRY OWNS THEM, not the object, and that is the N49 rule again: the
// object holds a raw pointer (`Object::setTreeInterceptor`) and this map holds
// the interceptor. When the object dies the registry drops it; if the object
// owned it, the interceptor's own reference to the root would keep both alive
// for ever.
class TreeInterceptorRegistry {
public:
    // Answers with an interceptor for this object, or null when it is not a
    // document. Several choosers may be installed (phase 8's app modules add
    // their own); the FIRST non-null answer wins, so a more specific chooser
    // is installed later.
    using Chooser = std::function<TreeInterceptorPtr(const a11y::ObjectPtr&, Service&)>;

    explicit TreeInterceptorRegistry(Service& service);
    ~TreeInterceptorRegistry();

    TreeInterceptorRegistry(const TreeInterceptorRegistry&) = delete;
    TreeInterceptorRegistry& operator=(const TreeInterceptorRegistry&) = delete;

    void addChooser(Chooser chooser);

    // NVDA: an app module returning None from `treeInterceptorClass`. A veto
    // answers "this object must NOT be intercepted", which a chooser cannot
    // say: a chooser declining only passes the question to the next one.
    //
    // Phase 8 needs it for the Electron case. VS Code exposes its editor as a
    // document with thousands of nodes and a caret of its own; intercepting it
    // means ALUS rebuilding an ancestor chain on every keystroke in a file
    // that is already read correctly by the plain text layer. NVDA blocks
    // browse mode there for the same reason, and the symptom when it is not
    // blocked is the one the phase 8 manual test looks for: an editor that
    // freezes while typing.
    using Veto = std::function<bool(const a11y::ObjectPtr&)>;
    void addVeto(Veto veto);
    // True when some veto refuses this object. Public for the tests and for
    // `alus-cli app-info`.
    bool isVetoed(const a11y::ObjectPtr& object) const;

    // NVDA: `treeInterceptorHandler.update(obj)`. Creates an interceptor for
    // this object or for the nearest ancestor that wants one, attaches it, and
    // returns it. Null when nothing in the chain is a document.
    TreeInterceptor* update(const a11y::ObjectPtr& object);
    // The interceptor for this object, without creating one.
    TreeInterceptor* forObject(const a11y::Object& object) const;
    // The one the focus is currently inside, which is what the command group
    // and the review cursor ask for.
    TreeInterceptor* current() const;
    void setCurrent(TreeInterceptor* interceptor);

    // NVDA: `killTreeInterceptor`. Called when a document goes away; also
    // sweeps the ones whose root has died, which is how a closed tab's
    // interceptor is collected.
    void remove(const a11y::Object& object);
    void sweep();
    void clear();

    std::size_t size() const noexcept { return interceptors_.size(); }

private:
    Service& service_;
    std::vector<Chooser> choosers_;
    std::vector<Veto> vetoes_;
    // Keyed by the root's platform reference, which is object identity (§6.2).
    std::map<a11y::Ref, TreeInterceptorPtr> interceptors_;
    TreeInterceptor* current_ = nullptr;
};

}  // namespace alus::nav
