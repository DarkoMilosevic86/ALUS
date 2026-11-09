// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// GENERATED from data/characters/en.dic -- the compiled-in floor under the
// character descriptions, for the same reason src/text/symbols_data.cpp exists:
// an installation with no data directory must still be able to tell the user
// that "b" is "bravo" and "d" is "delta".

#include "alus/text/char_descriptions.hpp"

namespace alus::text {

std::string_view builtinEnglishCharacterDescriptions() {
    static constexpr std::string_view kData =
    "# ALUS character descriptions (en).\n"
    "# SPDX-License-Identifier: GPL-2.0-or-later\n"
    "#\n"
    "# Format: character<TAB>description[<TAB>further description...]\n"
    "# Taken from NVDA source/locale/en/characterDescriptions.dic, which is the\n"
    "# same data a user of NVDA already knows by ear.\n"
    "#English characterDescriptions.dic\n"
    "#A part of NonVisual Desktop Access (NVDA)\n"
    "#URL: http://www.nvda-project.org/\n"
    "#Copyright (c) 2010-2011 World Light Information Limited and Hong Kong Blind Union.\n"
    "#This file is covered by the GNU General Public License.\n"
    "a\tAlfa\n"
    "b\tBravo\n"
    "c\tCharlie\n"
    "d\tDelta\n"
    "e\tEcho\n"
    "f\tFoxtrot\n"
    "g\tGolf\n"
    "h\tHotel\n"
    "i\tIndia\n"
    "j\tJuliet\n"
    "k\tKilo\n"
    "l\tLima\n"
    "m\tMike\n"
    "n\tNovember\n"
    "o\tOscar\n"
    "p\tPapa\n"
    "q\tQuebec\n"
    "r\tRomeo\n"
    "s\tSierra\n"
    "t\tTango\n"
    "u\tUniform\n"
    "v\tVictor\n"
    "w\tWhiskey\n"
    "x\tXray\n"
    "y\tYankee\n"
    "z\tZulu\n";
    return kData;
}

}  // namespace alus::text
