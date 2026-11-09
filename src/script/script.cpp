// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/script/script.hpp"

#include <string_view>

namespace alus::script {

SayAllCursor sayAllCursorFromName(std::string_view name) {
    if (name == "caret") {
        return SayAllCursor::Caret;
    }
    if (name == "review") {
        return SayAllCursor::Review;
    }
    return SayAllCursor::None;
}

std::string_view sayAllCursorName(SayAllCursor cursor) {
    switch (cursor) {
        case SayAllCursor::Caret:
            return "caret";
        case SayAllCursor::Review:
            return "review";
        case SayAllCursor::None:
            break;
    }
    return "";
}

}  // namespace alus::script
