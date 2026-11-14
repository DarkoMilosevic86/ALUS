// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Translation as a restartable subsystem.
//
// Rule 3 of phase-plan.md: every subsystem is restartable in process. That is not
// a formality here -- `[general] language` is a setting a user changes in the
// GUI, and a screen reader that has to be restarted to speak Serbian is a
// screen reader the user restarts while unable to hear what the desktop is
// doing. So the language is re-read and re-bound on resetConfiguration(), the
// same as the synthesizer and the braille table.
//
// It is registered FIRST, before the log and before speech, for one reason:
// every subsystem below it may speak during its own initialize(), and a
// message spoken before the catalogue is bound comes out in English.
#pragma once

#include "alus/config/config.hpp"

#include <string>

namespace alus::i18n {

class Service final : public config::Subsystem {
public:
    // `commandLineLanguage` wins over the configuration file, as --log-level
    // does over [general] logLevel.
    Service(config::Manager& manager, std::string commandLineLanguage = {});

    static void registerDefaults(config::Manager& manager);

    std::string_view name() const override { return "i18n"; }
    void initialize() override;
    void terminate() override {}

private:
    config::Manager& manager_;
    std::string commandLineLanguage_;
};

// What main() calls on its FIRST line, before the configuration exists: binds
// the domain from the environment alone, so that a failure between here and
// the configuration being loaded is still reported in the user's language.
// Service::initialize() then re-binds with [general] language.
void initializeFromEnvironment();

}  // namespace alus::i18n
