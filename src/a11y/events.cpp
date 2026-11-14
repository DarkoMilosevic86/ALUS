// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/events.hpp"
#include "alus/a11y/output_reason.hpp"

#include <array>
#include <string>
#include <utility>

namespace alus::a11y {
namespace {

struct EventRow {
    const char* atspi;
    std::string_view alus;
};

// §6.1's list, with what each one becomes. An empty ALUS name means
// "subscribed because the platform only delivers the whole family, but ALUS
// has nothing to do with this member".
constexpr std::array<EventRow, 38> kEvents{{
    // The focus. `object:state-changed:focused` with detail1 == 0 is handled
    // in alusEventName(), not here: losing the focus is decided by
    // api::setFocusObject, never by an event, or two applications disagreeing
    // about who has the focus would leave ALUS with none.
    {"object:state-changed:focused", events::kGainFocus},
    // The legacy focus event. Some toolkits still send only this one.
    {"focus:", events::kGainFocus},
    {"object:state-changed:selected", events::kStateChange},
    {"object:state-changed:checked", events::kStateChange},
    {"object:state-changed:pressed", events::kStateChange},
    {"object:state-changed:expanded", events::kStateChange},
    // Ignored (FAZA10.md N121). Nothing in ALUS reads it -- offscreen is read
    // from the states when an object is spoken -- and gnome-shell sends 1700 of
    // them in three seconds of typing into the overview's search. Kept, they
    // used up the per-application budget of the limiter and pushed out the one
    // `selected` that said which result Enter would start.
    {"object:state-changed:showing", ""},
    {"object:state-changed:busy", events::kStateChange},
    {"object:state-changed:active", events::kStateChange},
    {"object:state-changed:sensitive", events::kStateChange},
    {"object:state-changed:indeterminate", events::kStateChange},
    {"object:text-caret-moved", events::kCaret},
    {"object:text-changed:insert", events::kTextChange},
    {"object:text-changed:delete", events::kTextChange},
    {"object:text-selection-changed", events::kTextSelectionChange},
    {"object:text-attributes-changed", events::kAttributeChange},
    {"object:property-change:accessible-name", events::kNameChange},
    {"object:property-change:accessible-description", events::kDescriptionChange},
    {"object:property-change:accessible-value", events::kValueChange},
    {"object:children-changed:add", events::kChildrenAdded},
    {"object:children-changed:remove", events::kChildrenRemoved},
    {"object:active-descendant-changed", events::kActiveDescendantChange},
    {"object:selection-changed", events::kSelection},
    {"object:value-changed", events::kValueChange},
    {"object:attributes-changed", events::kAttributeChange},
    // Live regions.
    {"object:announcement", events::kLiveRegionChange},
    {"object:row-reordered", events::kReorder},
    {"object:column-reordered", events::kReorder},
    {"window:activate", events::kForeground},
    {"window:deactivate", events::kWindowDeactivate},
    {"window:create", events::kWindowCreate},
    {"window:destroy", events::kWindowDestroy},
    {"document:load-complete", events::kDocumentLoadComplete},
    {"document:reload", events::kDocumentReload},
    {"document:load-stopped", events::kDocumentLoadStopped},
    {"document:page-changed", events::kPageChange},
    {"document:attributes-changed", events::kAttributeChange},
    {"mouse:button", events::kMouseButton},
}};

}  // namespace

const std::vector<std::string>& subscribedEventTypes() {
    static const std::vector<std::string> types = [] {
        std::vector<std::string> result;
        result.reserve(kEvents.size());
        for (const EventRow& row : kEvents) {
            result.emplace_back(row.atspi);
        }
        return result;
    }();
    return types;
}

std::string_view alusEventName(std::string_view atspiType, int detail1) {
    for (const EventRow& row : kEvents) {
        if (atspiType != row.atspi) {
            continue;
        }
        if (row.alus == events::kGainFocus && atspiType != "focus:" && detail1 == 0) {
            // "no longer focused". See the comment on the table.
            return {};
        }
        return row.alus;
    }
    return {};
}

core::EventLimiterConfig limiterConfig(std::size_t maxEventsPerApplication,
                                       std::size_t maxFocusEvents) {
    core::EventLimiterConfig config;
    config.maxEventsPerSource = maxEventsPerApplication;
    config.maxFocusEvents = maxFocusEvents;
    config.focusName = std::string{events::kGainFocus};
    config.foregroundName = std::string{events::kForeground};
    // Empty on purpose; see the header.
    config.menuNames = {};
    config.cancellingPairs = {
        {std::string{events::kChildrenAdded}, std::string{events::kChildrenRemoved}},
        {std::string{events::kShow}, std::string{events::kHide}},
        {std::string{events::kWindowCreate}, std::string{events::kWindowDestroy}},
    };
    return config;
}

const char* outputReasonName(OutputReason reason) {
    switch (reason) {
        case OutputReason::Focus:
            return "focus";
        case OutputReason::FocusEntered:
            return "focusEntered";
        case OutputReason::Mouse:
            return "mouse";
        case OutputReason::Query:
            return "query";
        case OutputReason::Change:
            return "change";
        case OutputReason::Message:
            return "message";
        case OutputReason::SayAll:
            return "sayAll";
        case OutputReason::Caret:
            return "caret";
        case OutputReason::OnlyCache:
            return "onlyCache";
        case OutputReason::QuickNav:
            return "quickNav";
    }
    return "unknown";
}

}  // namespace alus::a11y
