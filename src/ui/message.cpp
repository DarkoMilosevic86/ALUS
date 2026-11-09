// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/ui/message.hpp"

#include "alus/core/log.hpp"
#include "alus/speech/presenter.hpp"

namespace alus::ui {

namespace {
speech::Presenter* g_presenter = nullptr;
}  // namespace

core::Action<const std::string&, speech::Priority>& messageShown() {
    static core::Action<const std::string&, speech::Priority> action;
    return action;
}

void initialize(speech::Presenter* presenter) { g_presenter = presenter; }

void terminate() { g_presenter = nullptr; }

bool isInitialized() { return g_presenter != nullptr; }

void message(const std::string& text, speech::Priority priority) {
    ALUS_DEBUG("ui message: {}", text);
    messageShown().notify(text, priority);
    if (g_presenter == nullptr) {
        return;
    }
    g_presenter->speakMessage(text, priority);
}

}  // namespace alus::ui
