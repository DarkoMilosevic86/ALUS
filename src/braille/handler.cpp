// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/handler.hpp.

#include "alus/braille/handler.hpp"

#include "alus/a11y/api.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/braille/formatting.hpp"
#include "alus/braille/properties.hpp"
#include "alus/braille/text_region.hpp"
#include "alus/core/log.hpp"
#include "alus/nav/browse_mode.hpp"
#include "alus/nav/review.hpp"
#include "alus/nav/service.hpp"
#include "alus/nav/tree_interceptor.hpp"

#include <algorithm>
#include <utility>

namespace alus::braille {
namespace {

const a11y::Settings& defaultObjectSettings() {
    static const a11y::Settings settings;
    return settings;
}

const text::FormatReportSettings& defaultFormatSettings() {
    static const text::FormatReportSettings settings;
    return settings;
}

}  // namespace

Handler::Handler(core::Loop& loop, Louis& louis, HandlerHooks hooks)
    : loop_(&loop),
      louis_(&louis),
      hooks_(std::move(hooks)),
      // The worker thread exists because a write to a disconnected Bluetooth
      // display can block for seconds (display.hpp).
      writer_(/*worker=*/true),
      mainBuffer_(*this),
      messageBuffer_(*this) {
    buffer_ = &mainBuffer_;
    mainBuffer_.setUpdateHook([this] {
        if (buffer_ == &mainBuffer_) {
            update();
        }
    });
    messageBuffer_.setUpdateHook([this] {
        if (buffer_ == &messageBuffer_) {
            update();
        }
    });
}

Handler::~Handler() { stop(); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Handler::start(DisplayPtr display, Settings settings) {
    stop();
    settings_ = std::move(settings);
    display_ = std::move(display);
    refreshDimensions();
    tether_ = settings_.tetherTo == TetherTo::Auto ? TetherTo::Focus : settings_.tetherTo;
    started_ = true;
    // The braille stage of the pump: §3.1 fixes the order, and braille runs
    // after the core work of the cycle is done, so the frame it writes is the
    // cycle's final state and not a frame from the middle of it.
    pumpHandler_ = loop_->addStageHandler(core::PumpStage::Braille, "braille",
                                          [this] { pump(); });
    ALUS_INFO("braille: {}", describe());
}

void Handler::stop() {
    if (!started_) {
        return;
    }
    started_ = false;
    setAutoScroll(false);
    stopCursorBlink();
    if (messageTimer_ != 0) {
        loop_->cancelLater(messageTimer_);
        messageTimer_ = 0;
    }
    if (pumpHandler_ != 0) {
        loop_->removeStageHandler(pumpHandler_);
        pumpHandler_ = 0;
    }
    regionsPendingUpdate_.clear();
    speechTexts_.clear();
    mainBuffer_.clear();
    messageBuffer_.clear();
    buffer_ = &mainBuffer_;
    writer_.clear();
    writer_.drain();
    if (display_ != nullptr) {
        display_->terminate();
        display_.reset();
    }
    dimensions_ = DisplayDimensions{};
    cells_.clear();
    rawText_.clear();
    cursorPos_.reset();
}

void Handler::applySettings(Settings settings) {
    const bool tableChanged = settings.translationTable != settings_.translationTable ||
                              settings.language != settings_.language;
    settings_ = std::move(settings);
    if (settings_.tetherTo != TetherTo::Auto) {
        tether_ = settings_.tetherTo;
    }
    refreshDimensions();
    if (tableChanged) {
        // Every cell on the display came out of the old table; none of them
        // mean the same thing now.
        for (const RegionPtr& region : mainBuffer_.regions) {
            region->update();
        }
        mainBuffer_.update();
    }
    restartCursorBlink();
    update();
}

bool Handler::isEnabled() const {
    return started_ && display_ != nullptr && dimensions_.size() > 0 &&
           settings_.mode != Mode::SpeechOutput;
}

void Handler::refreshDimensions() {
    const DisplayDimensions now =
        display_ != nullptr ? display_->dimensions() : DisplayDimensions{};
    if (now != dimensions_) {
        ALUS_INFO("braille display size: {} rows x {} cols", now.numRows, now.numCols);
        dimensions_ = now;
    }
}

// ---------------------------------------------------------------------------
// RegionHost
// ---------------------------------------------------------------------------

const a11y::Settings& Handler::objectSettings() const {
    if (hooks_.nav) {
        if (nav::Service* nav = hooks_.nav(); nav != nullptr && nav->isRunning()) {
            return nav->settings();
        }
    }
    return defaultObjectSettings();
}

const text::FormatReportSettings& Handler::formatSettings() const {
    if (hooks_.nav) {
        if (nav::Service* nav = hooks_.nav(); nav != nullptr && nav->isRunning()) {
            return nav->formatSettings();
        }
    }
    return defaultFormatSettings();
}

std::vector<std::string> Handler::outputTables() const {
    std::string table = settings_.translationTable;
    if (table.empty() || table == "auto") {
        table = louis_->registry().defaultTableForLanguage(settings_.language,
                                                            /*forInput=*/false);
    }
    // `braille-patterns.cti` is always last: it maps the Unicode braille
    // characters ALUS puts in the text -- the state shapes, the formatting
    // tags, the untranslated-input indicator -- to themselves, so they survive
    // whatever the output table would otherwise do to them.
    return {table, "braille-patterns.cti"};
}

std::string Handler::untranslatedBraille() const {
    return input_ != nullptr ? input_->untranslatedBraille() : std::string{};
}

int Handler::untranslatedCursorPos() const {
    return input_ != nullptr ? input_->untranslatedCursorPos() : 0;
}

void Handler::setUntranslatedCursorPos(int position) {
    if (input_ != nullptr) {
        input_->setUntranslatedCursorPos(position);
    }
}

void Handler::updateForUntranslatedInput() {
    if (mainBuffer_.regions.empty()) {
        return;
    }
    Region* last = mainBuffer_.regions.back().get();
    if (dynamic_cast<TextInfoRegion*>(last) == nullptr) {
        return;
    }
    regionsPendingUpdate_.insert(last);
    // Untranslated input is the one case NVDA updates OUT of the pump: the
    // user is typing and the cells must appear under their fingers now, not
    // after the next event.
    handlePendingUpdate();
}

void Handler::speakCharacterAt(const text::TextRange& at) {
    if (!hooks_.spellRange) {
        return;
    }
    text::TextRangePtr character = at.copy();
    if (character == nullptr) {
        return;
    }
    character->expand(text::TextUnit::Character);
    hooks_.spellRange(*character);
}

void Handler::speakUnitAt(const text::TextRange& at, text::TextUnit unit) {
    if (!hooks_.speakRange) {
        return;
    }
    text::TextRangePtr range = at.copy();
    if (range == nullptr) {
        return;
    }
    range->expand(unit);
    if (hooks_.cancelSpeech) {
        hooks_.cancelSpeech();
    }
    hooks_.speakRange(*range, unit);
}

void Handler::stopAutoScroll() { setAutoScroll(false); }

bool Handler::routingShouldMoveSystemCaret() const {
    // NVDA: `_routingShouldMoveSystemCaret`. Two ways to say yes, and they
    // differ in what the user configured the TETHER to, not in what it is now.
    const ReviewRoutingMovesCaret setting = settings_.reviewRoutingMovesCaret;
    if (settings_.tetherTo == TetherTo::Review) {
        return setting == ReviewRoutingMovesCaret::Always;
    }
    if (settings_.tetherTo == TetherTo::Auto) {
        return setting != ReviewRoutingMovesCaret::Never;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Building the regions
// ---------------------------------------------------------------------------

nav::BrowseMode* Handler::activeBrowseMode() const {
    if (!hooks_.nav) {
        return nullptr;
    }
    nav::Service* nav = hooks_.nav();
    if (nav == nullptr || nav->interceptors() == nullptr) {
        return nullptr;
    }
    nav::TreeInterceptor* current = nav->interceptors()->current();
    if (current == nullptr || current->passThrough() || !current->isReady() ||
        !current->isAlive()) {
        return nullptr;
    }
    return dynamic_cast<nav::BrowseMode*>(current);
}

a11y::ObjectPtr Handler::effectiveFocus(const a11y::ObjectPtr& object) const {
    if (nav::BrowseMode* browse = activeBrowseMode()) {
        // The display shows the DOCUMENT, not the control the focus happens to
        // be on inside it: that is what browse mode means.
        return browse->rootObject();
    }
    return object;
}

std::vector<RegionPtr> Handler::focusRegions(const a11y::ObjectPtr& object, bool review) {
    std::vector<RegionPtr> built;
    if (object == nullptr || !object->isAlive()) {
        return built;
    }
    nav::BrowseMode* browse = activeBrowseMode();
    const bool documentRoot = browse != nullptr && browse->rootObject() != nullptr &&
                              browse->rootObject()->sameAs(*object);

    // The object's own properties come first: name, role, states. Then, when
    // there is text worth reading, a second region for the text itself.
    RegionPtr textRegion;
    if (documentRoot) {
        textRegion = std::make_unique<CursorRegion>(*this, *browse, review);
    } else if (hasUsefulText(*object)) {
        textRegion = std::make_unique<TextInfoRegion>(
            *this, std::make_unique<ObjectTextSource>(object), review);
    }
    if (review) {
        if (nav::Service* nav = hooks_.nav ? hooks_.nav() : nullptr;
            nav != nullptr && nav->review() != nullptr) {
            // Tethered to review, the TEXT comes from the review position
            // rather than from the object's caret.
            textRegion = std::make_unique<ReviewTextRegion>(*this, *nav->review(), object);
        }
    }

    RegionPtr properties =
        review ? RegionPtr{std::make_unique<ReviewObjectRegion>(
                     *this, object, textRegion != nullptr ? std::string{kTextSeparator}
                                                          : std::string{})}
               : RegionPtr{std::make_unique<ObjectRegion>(
                     *this, object, textRegion != nullptr ? std::string{kTextSeparator}
                                                          : std::string{})};
    properties->update();
    built.push_back(std::move(properties));
    if (textRegion != nullptr) {
        textRegion->update();
        built.push_back(std::move(textRegion));
    }
    return built;
}

std::vector<RegionPtr> Handler::focusContextRegions(const a11y::ObjectPtr& object,
                                                    std::vector<RegionPtr>& oldRegions) {
    std::vector<RegionPtr> built;
    a11y::Registry* registry = hooks_.registry ? hooks_.registry() : nullptr;
    if (registry == nullptr || object == nullptr) {
        return built;
    }
    const std::vector<a11y::ObjectPtr>& ancestors = registry->api().focusAncestors();
    int ancestorsEnd = static_cast<int>(ancestors.size());

    // In browse mode the regions are built for the DOCUMENT's ancestors, not
    // for the focused control's: everything below the document root is inside
    // what the display is already showing. So when the object is not the focus
    // itself, the chain is cut at the object's own place in it.
    const a11y::ObjectPtr& focus = registry->api().focus();
    if (focus == nullptr || !focus->sameAs(*object)) {
        for (int index = static_cast<int>(ancestors.size()) - 1; index > 0; --index) {
            if (ancestors[static_cast<std::size_t>(index)]->sameAs(*object)) {
                ancestorsEnd = index;
                break;
            }
        }
    }

    int newAncestorsStart = 1;
    std::size_t commonRegionsEnd = 0;
    if (!oldRegions.empty()) {
        // The regions from the previous focus are a CACHE: an ancestor that
        // has not changed keeps the region that was built for it, so moving
        // between two items of the same list does not rebuild the window
        // title, the dialog and the list.
        newAncestorsStart = std::max(std::min(cachedFocusAncestorsEnd_, ancestorsEnd), 1);
        bool found = false;
        for (int index = static_cast<int>(oldRegions.size()) - 1; index >= 0; --index) {
            const int ancestorIndex =
                oldRegions[static_cast<std::size_t>(index)]->focusAncestorIndex;
            if (ancestorIndex < 0) {
                continue;
            }
            if (ancestorIndex < newAncestorsStart) {
                newAncestorsStart = ancestorIndex + 1;
                commonRegionsEnd = static_cast<std::size_t>(index) + 1;
                found = true;
                break;
            }
        }
        if (!found) {
            commonRegionsEnd = 0;
            newAncestorsStart = 1;
        }
        for (std::size_t index = 0; index < commonRegionsEnd; ++index) {
            // Cleared for every reused region, because `showRegions` decides
            // again which one is pinned hard left.
            oldRegions[index]->focusToHardLeft = false;
            built.push_back(std::move(oldRegions[index]));
        }
    }

    bool hardLeftSet = false;
    for (int index = newAncestorsStart; index < ancestorsEnd; ++index) {
        const a11y::ObjectPtr& parent = ancestors[static_cast<std::size_t>(index)];
        if (parent == nullptr || !parent->isAlive() || !parent->isPresentableFocusAncestor()) {
            continue;
        }
        auto region =
            std::make_unique<ObjectRegion>(*this, parent, std::string{kTextSeparator});
        region->focusAncestorIndex = index;
        if (settings_.focusContextPresentation == FocusContext::ChangedContext &&
            !hardLeftSet) {
            // Presenting context CHANGES: scroll back only as far as the first
            // ancestor that is new, so the display opens on what the user has
            // just entered rather than on the window title.
            region->focusToHardLeft = true;
            hardLeftSet = true;
        }
        region->update();
        built.push_back(std::move(region));
    }
    cachedFocusAncestorsEnd_ = ancestorsEnd;
    return built;
}

// ---------------------------------------------------------------------------
// The events
// ---------------------------------------------------------------------------

void Handler::handleGainFocus(const a11y::ObjectPtr& object, bool shouldAutoTether) {
    if (!isEnabled() || object == nullptr || !object->isAlive()) {
        return;
    }
    if (shouldAutoTether) {
        setTether(TetherTo::Focus, /*automatic=*/true);
    }
    if (tether_ != TetherTo::Focus) {
        return;
    }
    const a11y::ObjectPtr target = effectiveFocus(object);
    std::vector<RegionPtr> regions = focusContextRegions(target, mainBuffer_.regions);
    for (RegionPtr& region : focusRegions(target, /*review=*/false)) {
        regions.push_back(std::move(region));
    }
    showRegions(std::move(regions));
}

void Handler::handleCaretMove(const a11y::ObjectPtr& object, bool shouldAutoTether) {
    if (!isEnabled() || object == nullptr || !object->isAlive()) {
        return;
    }
    const TetherTo previous = tether_;
    if (shouldAutoTether) {
        setTether(TetherTo::Focus, /*automatic=*/true);
    }
    if (tether_ != TetherTo::Focus) {
        return;
    }
    const a11y::ObjectPtr target = effectiveFocus(object);
    Region* last = mainBuffer_.regions.empty() ? nullptr : mainBuffer_.regions.back().get();
    const a11y::ObjectPtr shown = last != nullptr ? last->object() : nullptr;
    if (last != nullptr && shown != nullptr && target != nullptr && shown->sameAs(*target)) {
        // THE COALESCING PATH (T7.7). The region is marked, not updated: N
        // caret movements in one cycle are one update and one write.
        last->pendingCaretUpdate = true;
        regionsPendingUpdate_.insert(last);
        ++stats_.coalescedRegionUpdates;
        return;
    }
    if (previous == TetherTo::Review) {
        // The caret moved somewhere other than where the review cursor was,
        // and the tether has just followed it: build the regions for it.
        showRegions(focusRegions(target, /*review=*/false));
    }
}

void Handler::handleUpdate(const a11y::ObjectPtr& object) {
    if (!isEnabled() || object == nullptr) {
        return;
    }
    // The focused object is the likely one, and it is the LAST region, so the
    // scan goes backwards.
    const std::vector<Region*> visible = mainBuffer_.visibleRegions();
    for (auto it = visible.rbegin(); it != visible.rend(); ++it) {
        const a11y::ObjectPtr shown = (*it)->object();
        if (shown != nullptr && shown->sameAs(*object)) {
            regionsPendingUpdate_.insert(*it);
            ++stats_.coalescedRegionUpdates;
            return;
        }
    }
}

void Handler::handleReviewMove(bool shouldAutoTether) {
    if (!isEnabled()) {
        return;
    }
    nav::Service* nav = hooks_.nav ? hooks_.nav() : nullptr;
    if (nav == nullptr || nav->review() == nullptr) {
        return;
    }
    if (shouldAutoTether) {
        setTether(TetherTo::Review, /*automatic=*/true);
    }
    if (tether_ != TetherTo::Review) {
        return;
    }
    const a11y::ObjectPtr object = nav->review()->objectAtPosition();
    Region* last = mainBuffer_.regions.empty() ? nullptr : mainBuffer_.regions.back().get();
    const a11y::ObjectPtr shown = last != nullptr ? last->object() : nullptr;
    if (last != nullptr && shown != nullptr && object != nullptr && shown->sameAs(*object)) {
        last->pendingCaretUpdate = true;
        regionsPendingUpdate_.insert(last);
        ++stats_.coalescedRegionUpdates;
        return;
    }
    showRegions(focusRegions(object, /*review=*/true));
}

void Handler::initialDisplay() {
    if (!isEnabled()) {
        return;
    }
    a11y::Registry* registry = hooks_.registry ? hooks_.registry() : nullptr;
    if (registry == nullptr) {
        return;
    }
    if (tether_ == TetherTo::Focus) {
        handleGainFocus(registry->api().focus(), /*shouldAutoTether=*/false);
    } else {
        handleReviewMove(/*shouldAutoTether=*/false);
    }
}

// ---------------------------------------------------------------------------
// Showing and updating
// ---------------------------------------------------------------------------

void Handler::showRegions(std::vector<RegionPtr> regions) {
    setAutoScroll(false);
    mainBuffer_.clear();
    bool hardLeftSet = false;
    Region* last = nullptr;
    for (RegionPtr& region : regions) {
        if (tether_ == TetherTo::Focus &&
            settings_.focusContextPresentation == FocusContext::ChangedContext) {
            if (region->focusToHardLeft) {
                hardLeftSet = true;
            } else if (!hardLeftSet && region->focusAncestorIndex < 0) {
                // A new object with the same ancestry as the last one: the
                // display opens on it. This is what makes walking a list show
                // the item rather than the list's own context every time.
                region->focusToHardLeft = true;
                hardLeftSet = true;
            }
        }
        last = region.get();
        mainBuffer_.regions.push_back(std::move(region));
    }
    mainBuffer_.update();
    if (last != nullptr) {
        mainBuffer_.focus(last);
        scrollToCursorOrSelection(last);
    }
    if (buffer_ == &mainBuffer_) {
        update();
    } else {
        // A message is showing. It stays until it times out or the user has
        // pressed a key since it appeared -- NVDA's `_keyCountForLastMessage`
        // rule, simplified: anything that builds new regions IS such an
        // interaction.
        dismissMessage();
    }
}

void Handler::handlePendingUpdate() {
    if (regionsPendingUpdate_.empty()) {
        return;
    }
    ++stats_.pendingUpdateBatches;
    Region* scrollTo = nullptr;
    // The window is saved against a REGION, not against a buffer position,
    // because the regions are about to be re-translated (T7.8).
    mainBuffer_.saveWindow();
    for (Region* region : regionsPendingUpdate_) {
        if (!region->isAlive()) {
            ALUS_DEBUG("braille: skipping the update of a region whose object is gone");
            continue;
        }
        region->update();
        ++stats_.regionUpdates;
        if (region->pendingCaretUpdate) {
            scrollTo = region;
            region->pendingCaretUpdate = false;
        }
    }
    regionsPendingUpdate_.clear();
    mainBuffer_.update();
    mainBuffer_.restoreWindow();
    if (scrollTo != nullptr) {
        scrollToCursorOrSelection(scrollTo);
    }
    if (buffer_ == &mainBuffer_) {
        update();
    }
}

void Handler::scrollToCursorOrSelection(Region* region) {
    if (region == nullptr) {
        return;
    }
    if (region->brailleCursorPos.has_value()) {
        mainBuffer_.scrollTo(region, *region->brailleCursorPos);
        return;
    }
    auto* textRegion = dynamic_cast<TextInfoRegion*>(region);
    const bool anchoredAtStart =
        textRegion != nullptr && textRegion->source().selectionAnchoredAtStart();
    if (!anchoredAtStart) {
        if (region->brailleSelectionStart.has_value()) {
            mainBuffer_.scrollTo(region, *region->brailleSelectionStart);
        }
        return;
    }
    if (region->brailleSelectionEnd.has_value()) {
        // Anchored at the start means the user is extending the selection
        // forwards, so the END is where they are.
        mainBuffer_.scrollTo(region, *region->brailleSelectionEnd - 1);
    }
}

void Handler::pump() {
    if (!started_) {
        return;
    }
    // Order matters: the pending region updates produce the cycle's final
    // frame, and only then is the single slot drained (T7.6, T7.7).
    handlePendingUpdate();
    if (display_ != nullptr) {
        writer_.flush(display_.get());
    }
}

void Handler::update() {
    if (!started_ || display_ == nullptr) {
        return;
    }
    refreshDimensions();
    cells_ = buffer_->windowBrailleCells();
    rawText_ = buffer_->windowRawText();
    cursorPos_ = buffer_->cursorWindowPos();
    const int size = dimensions_.size();
    cells_.resize(static_cast<std::size_t>(std::max(size, 0)), 0);
    restartCursorBlink();
}

void Handler::writeWithCursor() {
    if (!started_ || display_ == nullptr) {
        return;
    }
    std::vector<std::uint8_t> frame = cells_;
    if (cursorPos_.has_value() && cursorBlinkUp_ && settings_.showCursor) {
        const int position = *cursorPos_;
        if (position >= 0 && position < static_cast<int>(frame.size())) {
            frame[static_cast<std::size_t>(position)] |=
                tether_ == TetherTo::Focus ? settings_.cursorShapeFocus
                                           : settings_.cursorShapeReview;
        }
    }
    writer_.store(std::move(frame));
    ++stats_.framesStored;
    // The slot is drained in the pump stage, not here: a scroll or a message
    // outside a pump cycle would otherwise write immediately and the cycle's
    // own frame would be the second write.
    loop_->requestPump();
}

void Handler::restartCursorBlink() {
    stopCursorBlink();
    cursorBlinkUp_ = settings_.showCursor;
    writeWithCursor();
    if (!cursorPos_.has_value() || !settings_.showCursor || !settings_.cursorBlink ||
        settings_.cursorBlinkRate.count() == 0) {
        return;
    }
    const auto blink = [this]() {
        cursorBlinkUp_ = !cursorBlinkUp_;
        writeWithCursor();
        cursorBlinkTimer_ = 0;
        restartCursorBlink();
    };
    // A chain of one-shot timers rather than a repeating source: the blink has
    // to stop the moment the cursor moves, and a one-shot that re-arms itself
    // cannot outlive the state it was started for.
    cursorBlinkTimer_ = loop_->callLater(settings_.cursorBlinkRate, blink, "braille-blink");
}

void Handler::stopCursorBlink() {
    if (cursorBlinkTimer_ != 0) {
        loop_->cancelLater(cursorBlinkTimer_);
        cursorBlinkTimer_ = 0;
    }
}

// ---------------------------------------------------------------------------
// Scrolling, routing, messages
// ---------------------------------------------------------------------------

void Handler::scrollForward() {
    if (!isEnabled()) {
        return;
    }
    buffer_->scrollForward();
    if (buffer_ == &messageBuffer_) {
        resetMessageTimer();
    }
    if (autoScrollTimer_ != 0) {
        setAutoScroll(true);
    }
}

void Handler::scrollBack() {
    if (!isEnabled()) {
        return;
    }
    buffer_->scrollBack();
    if (buffer_ == &messageBuffer_) {
        resetMessageTimer();
    }
    if (autoScrollTimer_ != 0) {
        setAutoScroll(true);
    }
}

void Handler::routeTo(int windowPos) {
    if (!isEnabled()) {
        return;
    }
    setAutoScroll(false);
    buffer_->routeTo(windowPos);
    if (buffer_ == &messageBuffer_) {
        dismissMessage();
    }
}

text::TextRangePtr Handler::rangeForWindowPos(int windowPos) const {
    if (buffer_ != &mainBuffer_) {
        return nullptr;
    }
    return mainBuffer_.rangeForWindowPos(windowPos);
}

void Handler::message(const std::string& text) {
    if (!isEnabled() || settings_.showMessages == ShowMessages::Disabled || text.empty()) {
        return;
    }
    setAutoScroll(false);
    if (buffer_ == &messageBuffer_) {
        messageBuffer_.clear();
    } else {
        buffer_ = &messageBuffer_;
    }
    auto region = std::make_unique<TextRegion>(*this, text);
    region->update();
    messageBuffer_.regions.push_back(std::move(region));
    messageBuffer_.update();
    update();
    resetMessageTimer();
}

void Handler::resetMessageTimer() {
    if (messageTimer_ != 0) {
        loop_->cancelLater(messageTimer_);
        messageTimer_ = 0;
    }
    if (settings_.showMessages == ShowMessages::ShowIndefinitely) {
        return;
    }
    messageTimer_ = loop_->callLater(settings_.messageTimeout,
                                     [this] {
                                         messageTimer_ = 0;
                                         dismissMessage();
                                     },
                                     "braille-message");
}

void Handler::dismissMessage(bool shouldUpdate) {
    if (buffer_ != &messageBuffer_) {
        return;
    }
    messageBuffer_.clear();
    buffer_ = &mainBuffer_;
    if (messageTimer_ != 0) {
        loop_->cancelLater(messageTimer_);
        messageTimer_ = 0;
    }
    if (shouldUpdate) {
        update();
    }
}

// ---------------------------------------------------------------------------
// Tethering
// ---------------------------------------------------------------------------

bool Handler::shouldAutoTether() const {
    return isEnabled() && settings_.tetherTo == TetherTo::Auto;
}

void Handler::setTether(TetherTo tether, bool automatic) {
    if (automatic && !shouldAutoTether()) {
        return;
    }
    if (!automatic) {
        settings_.tetherTo = tether;
    }
    if (tether == tether_) {
        return;
    }
    tether_ = tether;
    // The regions belong to the thing that is no longer being followed.
    mainBuffer_.clear();
    regionsPendingUpdate_.clear();
}

// ---------------------------------------------------------------------------
// Automatic scroll
// ---------------------------------------------------------------------------

void Handler::setAutoScroll(bool enable) {
    if (autoScrollTimer_ != 0) {
        loop_->cancelLater(autoScrollTimer_);
        autoScrollTimer_ = 0;
    }
    if (!enable || !isEnabled()) {
        return;
    }
    const int rate = std::max(settings_.autoScrollRate, 1);
    const auto delay =
        std::chrono::milliseconds{std::max(1, dimensions_.size() * 1000 / rate)};
    autoScrollTimer_ = loop_->callLater(delay,
                                        [this] {
                                            autoScrollTimer_ = 0;
                                            scrollForward();
                                            setAutoScroll(true);
                                        },
                                        "braille-autoscroll");
}

// ---------------------------------------------------------------------------
// Braille as speech output
// ---------------------------------------------------------------------------

void Handler::showSpeech(const std::string& text) {
    if (!started_ || settings_.mode != Mode::SpeechOutput || display_ == nullptr ||
        dimensions_.size() == 0) {
        return;
    }
    // A cap, as NVDA has one: a say-all of a long document would otherwise
    // grow this without end.
    std::size_t total = 0;
    for (const std::string& one : speechTexts_) {
        total += one.size();
    }
    if (total > 100000) {
        return;
    }
    const bool hadRegions = !speechTexts_.empty();
    speechTexts_.push_back(hadRegions ? std::string{kTextSeparator} + text : text);

    mainBuffer_.regions.clear();
    for (const std::string& one : speechTexts_) {
        auto region = std::make_unique<TextRegion>(*this, one);
        region->update();
        mainBuffer_.regions.push_back(std::move(region));
    }
    mainBuffer_.update();
    if (!hadRegions && !mainBuffer_.regions.empty()) {
        mainBuffer_.focus(mainBuffer_.regions.front().get());
    }
    update();
}

void Handler::clearSpeechRegions() { speechTexts_.clear(); }

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

Handler::Stats Handler::stats() const {
    Stats out = stats_;
    out.framesStored = writer_.storedCount();
    out.framesWritten = writer_.writtenCount();
    return out;
}

std::string Handler::describe() const {
    std::string out = "display ";
    out += display_ != nullptr ? std::string{display_->name()} : std::string{"none"};
    out += ", ";
    out += std::to_string(dimensions_.numRows);
    out += " rows x ";
    out += std::to_string(dimensions_.numCols);
    out += " cols, table ";
    const std::vector<std::string> tables = outputTables();
    out += tables.empty() ? "none" : tables.front();
    out += ", tethered ";
    out += tetherName(tether_);
    out += ", wrap ";
    out += textWrapName(settings_.textWrap);
    return out;
}

}  // namespace alus::braille
