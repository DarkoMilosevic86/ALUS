// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The units a text range can be expanded to or moved by.
//
// Port of NVDA's `textInfos.UNIT_*` constants (01-ARHITEKTURA.md §7.1). They
// are an enum here rather than strings, for a reason that is not style: a
// misspelt unit in NVDA is a run-time LookupError in a screen reader, and
// every `unit` argument in this phase comes from a command table.
//
// Three of them are NOT platform units and ALUS synthesises them itself, as
// §7.2 says: `Story` (the whole text), `ReadingChunk` (what say-all reads at a
// time -- the user configures it) and `Offset` (exactly one offset, whatever
// an offset means to the implementation).
#pragma once

#include <optional>
#include <string_view>

namespace alus::text {

enum class TextUnit {
    Character,
    Word,
    Line,
    Sentence,
    Paragraph,
    Page,
    Table,
    Row,
    Column,
    Cell,
    Screen,
    // The whole text of the object.
    Story,
    // Resolved against the configuration (line, sentence or paragraph) by
    // TextRange::resolveReadingChunk().
    ReadingChunk,
    // One offset. For OffsetsTextRange that is one byte, which is why nothing
    // outside the offset layer should ask for it.
    Offset,
    ControlField,
    FormatField,
};

// NVDA's own spelling ("character", "readingChunk"), because it appears in
// configuration files and in the log.
std::string_view unitName(TextUnit unit);
std::optional<TextUnit> parseUnit(std::string_view name);

// What is spoken when a unit is reported to the user ("character", "word",
// "line", "paragraph"). NVDA has labels for exactly those four
// (`textInfos.unitLabels`); every other unit answers with an empty view, and
// a caller must not speak it.
std::string_view unitLabel(TextUnit unit);

// NVDA: `config.conf["speech"]["sayAllReadingUnit"]`.
enum class ReadingUnit { Line, Sentence, Paragraph };

std::string_view readingUnitName(ReadingUnit unit);
std::optional<ReadingUnit> parseReadingUnit(std::string_view name);
TextUnit readingUnitToTextUnit(ReadingUnit unit);

// NVDA: `documentNavigation.paragraphStyle` (§11.2). The three paragraph
// interpretations, and the reason `ParagraphNav` exists at all.
enum class ParagraphStyle {
    // Trust the application's own paragraph unit.
    Application,
    // Every line break starts a paragraph.
    SingleLineBreak,
    // Only a blank line starts a paragraph (the Markdown reading).
    MultiLineBreak,
};

std::string_view paragraphStyleName(ParagraphStyle style);
std::optional<ParagraphStyle> parseParagraphStyle(std::string_view name);

}  // namespace alus::text
