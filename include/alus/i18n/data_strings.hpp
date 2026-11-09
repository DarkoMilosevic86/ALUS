// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The translatable strings that live in GENERATED DATA, as msgids.
//
// `src/i18n/data_strings.cpp` is written by tools/extract-commands.py and
// holds one N_() mark per distinct script category, script description and
// key label. That is the only way those strings reach a translator: xgettext
// reads source files, and §5.4 keeps the command table in `data/scripts.csv`
// precisely so it is NOT hand-written code.
//
// Nothing reads these on the speaking path -- the catalogue loader translates
// each row as it loads it. They exist so that tests/gui/i18n_test.cpp can
// assert that the generated .cpp and the generated .csv/.ini still hold the
// same set of strings. Two generated copies of one truth is acceptable; two
// copies that drift apart silently is not.
#pragma once

#include <span>

namespace alus::i18n {

std::span<const char* const> scriptCategoryStrings();
std::span<const char* const> scriptDescriptionStrings();
std::span<const char* const> keyLabelStrings();

}  // namespace alus::i18n
