// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The event vocabulary: AT-SPI's names in, NVDA's names out.
//
// §6.1 fixes the subscription list -- the minimal set a real screen reader
// uses, verified live against Orca's own 38 subscriptions. §3.3 fixes what the
// core sees: event names it does not interpret. This header is the one place
// that knows both, so that the core stays free of AT-SPI and the object model
// stays free of ad-hoc string comparisons.
//
// The ALUS names are NVDA's own event names (`event_gainFocus`,
// `event_nameChange`), because the whole dispatch chain, every app module and
// every gesture map section is written against them.
#pragma once

#include "alus/core/event_limiter.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace alus::a11y {

// ---------------------------------------------------------------------------
// The ALUS event names. Spelled out so that a typo is a link error, not a
// silently dead event handler.
// ---------------------------------------------------------------------------
namespace events {

inline constexpr std::string_view kGainFocus = "gainFocus";
inline constexpr std::string_view kLoseFocus = "loseFocus";
inline constexpr std::string_view kFocusEntered = "focusEntered";
inline constexpr std::string_view kFocusExited = "focusExited";
inline constexpr std::string_view kForeground = "foreground";
inline constexpr std::string_view kNameChange = "nameChange";
inline constexpr std::string_view kDescriptionChange = "descriptionChange";
inline constexpr std::string_view kValueChange = "valueChange";
inline constexpr std::string_view kStateChange = "stateChange";
inline constexpr std::string_view kAttributeChange = "attributeChange";
inline constexpr std::string_view kChildrenAdded = "childrenAdded";
inline constexpr std::string_view kChildrenRemoved = "childrenRemoved";
inline constexpr std::string_view kActiveDescendantChange = "activeDescendantChange";
inline constexpr std::string_view kSelection = "selection";
inline constexpr std::string_view kCaret = "caret";
inline constexpr std::string_view kTextChange = "textChange";
inline constexpr std::string_view kTextSelectionChange = "textSelectionChange";
inline constexpr std::string_view kLiveRegionChange = "liveRegionChange";
inline constexpr std::string_view kShow = "show";
inline constexpr std::string_view kHide = "hide";
inline constexpr std::string_view kAlert = "alert";
inline constexpr std::string_view kWindowCreate = "windowCreate";
inline constexpr std::string_view kWindowDestroy = "windowDestroy";
inline constexpr std::string_view kWindowDeactivate = "windowDeactivate";
inline constexpr std::string_view kDocumentLoadComplete = "documentLoadComplete";
inline constexpr std::string_view kDocumentReload = "documentReload";
inline constexpr std::string_view kDocumentLoadStopped = "documentLoadStopped";
inline constexpr std::string_view kPageChange = "pageChange";
inline constexpr std::string_view kReorder = "reorder";
inline constexpr std::string_view kMouseButton = "mouseButton";
inline constexpr std::string_view kBecomeNavigatorObject = "becomeNavigatorObject";

}  // namespace events

// The AT-SPI event types to subscribe to: §6.1's list, which is what Orca
// subscribes to and no more. Order is not significant.
const std::vector<std::string>& subscribedEventTypes();

// AT-SPI type -> ALUS name. Empty when ALUS has no use for the event, which is
// the normal answer for half of what a busy application sends (the state
// changes nobody announces, `object:state-changed:focused` with detail1 == 0 --
// losing focus is decided by api::setFocusObject, never by an event).
std::string_view alusEventName(std::string_view atspiType, int detail1);

// The limiter configured for AT-SPI (§3.4). Phase 1 left the names
// configurable precisely so that this function, and not the core, knows them.
//
// One deviation from NVDA is recorded here rather than in a comment far away:
// the MENU events have no AT-SPI counterpart. On Windows NVDA throws away all
// but the last of EVENT_SYSTEM_MENUSTART and friends; on AT-SPI a menu
// announces itself through ordinary focus events on its items, so the menu
// list is empty and the focus rules do that work instead.
core::EventLimiterConfig limiterConfig(std::size_t maxEventsPerApplication = 10,
                                       std::size_t maxFocusEvents = 4);

// What the payload of a dispatched event carries, beyond the object it is
// about. `core::Event::payload` points at one of these.
struct EventPayload {
    // The AT-SPI type, kept for the log and for the few handlers that need the
    // detail (which state changed, which text was inserted).
    std::string atspiType;
    int detail1 = 0;
    int detail2 = 0;
    std::string text;
    // The accessible from `any_data`, as an already-created object id; 0 when
    // there was none.
    std::uintptr_t relatedObject = 0;
};

}  // namespace alus::a11y
