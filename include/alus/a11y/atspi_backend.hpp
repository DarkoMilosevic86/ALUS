// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The one real backend: libatspi.
//
// Everything AT-SPI-specific in ALUS is behind this class. Three decisions in
// it are not obvious and are the difference between a usable screen reader and
// a hung one:
//
//  1. THE CALL TIMEOUT IS BOUNDED (atspi_set_timeout). libatspi's default
//     method timeout is generous, and every getter here is a synchronous
//     round trip into another process. Phase 3 learned what an unbounded
//     synchronous call from the main loop does (N19/N26): the screen reader
//     stops, holding whatever it was holding. A frozen application must cost
//     ALUS a few hundred milliseconds, once, not the session.
//  2. THE MAIN CONTEXT IS OURS (atspi_set_main_context). The core loop runs
//     its own GMainContext (§3.1); without telling libatspi about it, event
//     delivery would wait for a default context nobody iterates.
//  3. THE DEVICE API IS NOT USED. No atspi_device, no key grabs through
//     libatspi -- that code is the cause of N19 and N7, and phase 3 replaced
//     it with direct D-Bus to the compositor (docs/history/phase-3.md O1).
#pragma once

#include "alus/a11y/backend.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

typedef struct _GMainContext GMainContext;

namespace alus::a11y {

// At namespace scope for the usual reason (a default argument of a struct with
// default member initializers).
struct AtspiBackendOptions {
    // How long any one AT-SPI call may take before it is abandoned. 500 ms is
    // long enough for a busy application on a loaded machine and short enough
    // that a user notices a hiccup rather than a hang.
    std::chrono::milliseconds callTimeout{500};
    // The start-up timeout libatspi uses while it finds the bus.
    std::chrono::milliseconds startupTimeout{3000};
    // The loop's context, so that events arrive in the pump (decision 2
    // above). Null means the default context, which is what a test wants.
    GMainContext* mainContext = nullptr;
    // N151: make sure applications expose themselves. Gecko (Firefox,
    // Thunderbird), Qt (Telegram) and others only do when org.a11y.Status
    // says a screen reader or accessibility is on; Alt+Super+S switches the
    // desktop's screen reader OFF, and then ALUS could read nothing in them.
    // When both are off, the daemon sets IsEnabled (toolkit-accessibility,
    // which does NOT start the other reader) and restores it on exit.
    bool ensureAccessibilityEnabled = false;
};

class AtspiBackend final : public Backend {
public:
    using Options = AtspiBackendOptions;

    explicit AtspiBackend(Options options = {});
    ~AtspiBackend() override;

    // atspi_init(). Returns false when there is no accessibility bus, which is
    // a condition the daemon announces rather than hides: on that system ALUS
    // can speak but cannot read anything.
    bool initialize(std::string* error);
    bool isInitialized() const noexcept { return initialized_; }
    // The version libatspi reports, for the log and for `alus-cli a11y-info`.
    static std::string platformVersion();

    const char* backendName() const override { return "atspi"; }

    Ref addRef(Ref ref) override;
    void release(Ref ref) override;

    bool alive(Ref ref) override;
    std::string path(Ref ref) override;

    Ref parent(Ref ref) override;
    Ref childAt(Ref ref, int index) override;
    int childCount(Ref ref) override;
    int indexInParent(Ref ref) override;

    std::string name(Ref ref) override;
    std::string description(Ref ref) override;
    PlatformRole role(Ref ref) override;
    std::uint64_t states(Ref ref) override;
    Attributes attributes(Ref ref) override;
    InterfaceSet interfaces(Ref ref) override;
    Rect extents(Ref ref) override;
    std::vector<Ref> relation(Ref ref, PlatformRelation relation) override;

    std::optional<ValueInfo> value(Ref ref) override;

    int characterCount(Ref ref) override;
    std::string text(Ref ref, int start, int end) override;
    int caretOffset(Ref ref) override;
    bool setCaretOffset(Ref ref, int offset) override;
    std::optional<TextSegment> stringAtOffset(Ref ref, int offset,
                                              TextGranularity granularity) override;
    int selectionCount(Ref ref) override;
    std::optional<TextSegment> selection(Ref ref, int index) override;
    bool setSelection(Ref ref, int start, int end) override;
    AttributeRun attributeRun(Ref ref, int offset, bool includeDefaults) override;
    int offsetAtPoint(Ref ref, int x, int y) override;
    Rect characterExtents(Ref ref, int offset) override;
    Rect rangeExtents(Ref ref, int start, int end) override;
    int hyperlinkCount(Ref ref) override;
    int linkIndexAtOffset(Ref ref, int offset) override;
    std::optional<LinkInfo> hyperlink(Ref ref, int index) override;

    int actionCount(Ref ref) override;
    std::string actionName(Ref ref, int index) override;
    std::string actionKeyBinding(Ref ref, int index) override;
    bool doAction(Ref ref, int index) override;

    bool grabFocus(Ref ref) override;
    bool scrollTo(Ref ref) override;
    Ref accessibleAtPoint(Ref ref, int x, int y) override;

    int selectedChildCount(Ref ref) override;
    Ref selectedChildAt(Ref ref, int index) override;

    TableInfo tableInfo(Ref ref) override;
    CellInfo cellInfo(Ref ref) override;
    Ref tableOf(Ref ref) override;
    std::string rowHeaderText(Ref ref) override;
    std::string columnHeaderText(Ref ref) override;

    std::vector<Ref> matchesFrom(Ref root, Ref from, const MatchRule& rule, SortOrder order,
                                 TreeTraversal traversal, int count, bool forwards) override;
    std::vector<Ref> matches(Ref root, const MatchRule& rule, SortOrder order,
                             int count) override;

    Ref application(Ref ref) override;
    AppInfo appInfo(Ref ref) override;
    Ref desktop() override;

    bool subscribe(const std::vector<std::string>& eventTypes, EventSink sink,
                   std::string* error) override;
    void unsubscribe() override;

    // How many AT-SPI calls this backend has made, and how many of them
    // failed. The cache-miss meter of §3.5 counts getter calls; this counts
    // what actually reached the bus, which is the number that matters for
    // T4.2 and for the latency budget.
    std::uint64_t callCount() const noexcept override { return calls_; }
    std::vector<std::pair<std::string, std::uint64_t>> callsByName() const override;
    std::uint64_t errorCount() const noexcept { return errors_; }
    void resetCounters() noexcept;

private:
    class Listener;
    friend class Listener;

    // Called by the listener for every AT-SPI event, on the main thread.
    // Declared with a void* rather than AtspiEvent* so that no AT-SPI type
    // reaches this header.
    void deliver(void* event);

    // `name` is the calling function's __func__, a string with static
    // storage, so the map keys by pointer-free string_view and never copies.
    void noteCall(std::string_view name) {
        ++calls_;
        ++perCall_[name];
    }
    // Counts the call and, when it took 20 ms or more, logs which one: the
    // ~200 ms from an arrow to speech in Nautilus is synchronous calls into
    // the application, and "slow event" alone did not say which (N128).
    struct CallTimer {
        CallTimer(AtspiBackend& backend, const char* name);
        ~CallTimer();
        CallTimer(const CallTimer&) = delete;
        CallTimer& operator=(const CallTimer&) = delete;
        const char* name;
        std::int64_t startedNs;
    };
    void noteError(const char* what, const char* detail);

    Options options_;
    bool initialized_ = false;
    // True when this backend turned org.a11y.Status IsEnabled on and must
    // turn it off again.
    bool restoreIsEnabled_ = false;
    void enableApplicationAccessibility();
    void restoreApplicationAccessibility();
    std::unique_ptr<Listener> listener_;
    EventSink sink_;
    std::vector<std::string> subscribed_;
    std::uint64_t calls_ = 0;
    std::unordered_map<std::string_view, std::uint64_t> perCall_;
    // AppInfo per application bus name; see appInfo() for why it may be kept.
    static constexpr std::size_t kMaxRememberedApplications = 256;
    std::unordered_map<std::string, AppInfo> appInfoByBus_;
    std::uint64_t errors_ = 0;
};

}  // namespace alus::a11y
