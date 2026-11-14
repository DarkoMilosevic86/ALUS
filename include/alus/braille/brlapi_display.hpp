// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The braille display, through brltty's BrlAPI.
//
// architecture.md §14's one large simplification: NVDA ships ~40 drivers for
// individual braille displays; ALUS ships none, and talks to brltty instead.
// brltty already has the drivers, Linux users already have it configured, and
// the ~6000 lines of device protocol NVDA carries are 6000 lines ALUS does not
// have to get right for hardware nobody here owns.
//
// WHAT IS GIVEN UP, honestly: a display-specific feature brltty does not model
// (ALVA's split point, Handy Tech's touch cells) is not reachable. The gesture
// identifiers keep the `br(...)` shape anyway, so a user's `gestures.ini`
// written for NVDA still addresses the keys brltty does report.
//
// THE KEY NAMES ARE BRLTTY'S OWN. `brlapi_describeKeyCode` gives a command
// name -- "fwinlt", "lnup", "route" -- and the gesture identifier is
// `br(brlapi):fwinlt`. That is deliberately brltty's vocabulary rather than
// NVDA's per-driver one: the names are what a brltty user has in their own
// configuration, and `brltty -l` lists them.
//
// THREADING. `display()` is called from the writer's worker thread, and
// `readKeys()` from the pump. BrlAPI's single-session functions are not
// re-entrant, so both take the same mutex -- which is also what makes the
// write-while-keys-are-waiting workaround below safe.
#pragma once

#include "alus/braille/display.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace alus::braille {

class BrlapiDisplay final : public IDisplay {
public:
    // `host` is brltty's own spelling (":0", "machine:0"). Empty means the
    // default, which honours $BRLAPI_HOST.
    explicit BrlapiDisplay(std::string host = {});
    ~BrlapiDisplay() override;

    std::string_view name() const override { return "brlapi"; }
    bool initialize(std::string* error) override;
    void terminate() override;

    DisplayDimensions dimensions() const override;
    bool display(const std::vector<std::uint8_t>& cells) override;
    bool isThreadSafe() const override { return true; }
    std::vector<DisplayKey> readKeys() override;
    void setBrailleInputEnabled(bool enabled) override;
    std::string model() const override { return driverName_; }

private:
    bool readKeysLocked(std::vector<DisplayKey>& into);

    std::string host_;
    std::string driverName_;
    mutable std::mutex mutex_;
    DisplayDimensions dimensions_;
    // Keys drained by the write workaround below, waiting for the pump to ask
    // for them. Without this they would be read and thrown away, and a routing
    // key pressed at the same moment as a caret movement would do nothing.
    std::vector<DisplayKey> pendingKeys_;
    bool connected_ = false;
    bool ttyMode_ = false;
    bool brailleInput_ = false;
};

}  // namespace alus::braille
