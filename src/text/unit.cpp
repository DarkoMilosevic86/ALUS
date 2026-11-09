// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The unit names, in NVDA's own spelling. See include/alus/text/unit.hpp.

#include "alus/text/unit.hpp"

#include <array>
#include <utility>

namespace alus::text {
namespace {

constexpr std::array<std::pair<TextUnit, std::string_view>, 16> kUnitNames{{
    {TextUnit::Character, "character"},
    {TextUnit::Word, "word"},
    {TextUnit::Line, "line"},
    {TextUnit::Sentence, "sentence"},
    {TextUnit::Paragraph, "paragraph"},
    {TextUnit::Page, "page"},
    {TextUnit::Table, "table"},
    {TextUnit::Row, "row"},
    {TextUnit::Column, "column"},
    {TextUnit::Cell, "cell"},
    {TextUnit::Screen, "screen"},
    {TextUnit::Story, "story"},
    {TextUnit::ReadingChunk, "readingChunk"},
    {TextUnit::Offset, "offset"},
    {TextUnit::ControlField, "controlField"},
    {TextUnit::FormatField, "formatField"},
}};

}  // namespace

std::string_view unitName(TextUnit unit) {
    for (const auto& [value, name] : kUnitNames) {
        if (value == unit) {
            return name;
        }
    }
    return "unknown";
}

std::optional<TextUnit> parseUnit(std::string_view name) {
    for (const auto& [value, unitText] : kUnitNames) {
        if (unitText == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string_view unitLabel(TextUnit unit) {
    // NVDA's `textInfos.unitLabels` has exactly these four. A unit without a
    // label is one the user is never told the name of, and speaking "story"
    // would be a bug, not a translation gap.
    switch (unit) {
        case TextUnit::Character:
            return "character";
        case TextUnit::Word:
            return "word";
        case TextUnit::Line:
            return "line";
        case TextUnit::Paragraph:
            return "paragraph";
        default:
            return {};
    }
}

std::string_view readingUnitName(ReadingUnit unit) {
    switch (unit) {
        case ReadingUnit::Line:
            return "line";
        case ReadingUnit::Sentence:
            return "sentence";
        case ReadingUnit::Paragraph:
            return "paragraph";
    }
    return "line";
}

std::optional<ReadingUnit> parseReadingUnit(std::string_view name) {
    if (name == "line") {
        return ReadingUnit::Line;
    }
    if (name == "sentence") {
        return ReadingUnit::Sentence;
    }
    if (name == "paragraph") {
        return ReadingUnit::Paragraph;
    }
    return std::nullopt;
}

TextUnit readingUnitToTextUnit(ReadingUnit unit) {
    switch (unit) {
        case ReadingUnit::Line:
            return TextUnit::Line;
        case ReadingUnit::Sentence:
            return TextUnit::Sentence;
        case ReadingUnit::Paragraph:
            return TextUnit::Paragraph;
    }
    return TextUnit::Line;
}

std::string_view paragraphStyleName(ParagraphStyle style) {
    switch (style) {
        case ParagraphStyle::Application:
            return "application";
        case ParagraphStyle::SingleLineBreak:
            return "singleLineBreak";
        case ParagraphStyle::MultiLineBreak:
            return "multiLineBreak";
    }
    return "application";
}

std::optional<ParagraphStyle> parseParagraphStyle(std::string_view name) {
    if (name == "application") {
        return ParagraphStyle::Application;
    }
    if (name == "singleLineBreak") {
        return ParagraphStyle::SingleLineBreak;
    }
    if (name == "multiLineBreak") {
        return ParagraphStyle::MultiLineBreak;
    }
    return std::nullopt;
}

}  // namespace alus::text
