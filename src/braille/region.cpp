// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/region.hpp.

#include "alus/braille/region.hpp"

#include "alus/braille/properties.hpp"
#include "alus/core/log.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>

namespace alus::braille {

Region::Region(RegionHost& host) : host_(&host) {}
Region::~Region() = default;

void Region::update() {
    const Settings& settings = host_->brailleSettings();
    rawLength_ = static_cast<int>(text::codepointCount(rawText));

    TranslationMode mode = TranslationMode::None;
    if (settings.expandAtCursor && cursorPos.has_value()) {
        mode |= TranslationMode::CompBrlAtCursor;
    }

    std::string textToTranslate = rawText;
    std::optional<int> cursor = cursorPos;
    // NVDA normalises here too, and for the same reason: a decomposed "é" is
    // two codepoints and liblouis would translate the combining mark on its
    // own. The offsets of the normalised text differ from the original's,
    // which is why this is OFF by default -- NVDA's own default -- and why
    // turning it on only changes what is SHOWN, never what is routed to: the
    // position maps below are rebuilt from the text actually translated.
    if (settings.unicodeNormalization) {
        textToTranslate = text::normalizeNfc(rawText);
    }

    const Translation translation =
        host_->louis().translate(host_->outputTables(), textToTranslate, rawTextTypeforms,
                                 cursor, mode);
    brailleCells = translation.cells;
    brailleToRawPos = translation.brailleToRawPos;
    rawToBraillePos = translation.rawToBraillePos;
    brailleCursorPos = cursorPos.has_value() ? translation.cursorPos : std::nullopt;

    if (!translation.ok) {
        // §3.8: an expected failure must be noticeable without being fatal.
        // A broken table must not blank the display, so the text is shown as
        // its own codepoints -- wrong braille, readable shape, and the
        // warning is already in the log from Louis::translate.
        brailleCells.clear();
        brailleToRawPos.clear();
        rawToBraillePos.clear();
        const text::OffsetConverter converter{rawText};
        for (std::size_t index = 0; index < converter.codepointLength(); ++index) {
            const std::uint32_t codepoint =
                text::codepointAt(rawText, converter.toByte(index));
            brailleCells.push_back(static_cast<std::uint8_t>(codepoint & 0xFF));
            brailleToRawPos.push_back(static_cast<int>(index));
            rawToBraillePos.push_back(static_cast<int>(index));
        }
        brailleCursorPos = std::nullopt;
    }

    // Keep the maps at the sizes every caller assumes, whatever liblouis
    // returned: a map shorter than the text is an out-of-range index in the
    // buffer's word wrap, three call levels away from here.
    rawToBraillePos.resize(static_cast<std::size_t>(std::max(rawLength_, 0)),
                           static_cast<int>(brailleCells.size()));
    brailleToRawPos.resize(brailleCells.size(), std::max(rawLength_ - 1, 0));

    brailleSelectionStart.reset();
    brailleSelectionEnd.reset();
    if (selectionStart.has_value() && selectionEnd.has_value() && settings.showSelection) {
        const int start = *selectionStart;
        const int end = *selectionEnd;
        if (start >= 0 && start < static_cast<int>(rawToBraillePos.size())) {
            const int from = rawToBraillePos[static_cast<std::size_t>(start)];
            const int to = end >= rawLength_
                               ? static_cast<int>(brailleCells.size())
                               : (end >= 0 && end < static_cast<int>(rawToBraillePos.size())
                                      ? rawToBraillePos[static_cast<std::size_t>(end)]
                                      : static_cast<int>(brailleCells.size()));
            brailleSelectionStart = from;
            brailleSelectionEnd = to;
            for (int position = from; position < to; ++position) {
                if (position >= 0 && position < static_cast<int>(brailleCells.size())) {
                    brailleCells[static_cast<std::size_t>(position)] |= kSelectionShape;
                }
            }
        }
    }
}

std::string Region::describe() const { return "region \"" + rawText + "\""; }

// ---------------------------------------------------------------------------
// TextRegion
// ---------------------------------------------------------------------------

TextRegion::TextRegion(RegionHost& host, std::string text) : Region(host) {
    rawText = std::move(text);
}

std::string TextRegion::describe() const { return "text \"" + rawText + "\""; }

// ---------------------------------------------------------------------------
// ObjectRegion
// ---------------------------------------------------------------------------

ObjectRegion::ObjectRegion(RegionHost& host, a11y::ObjectPtr object, std::string appendText)
    : Region(host), object_(std::move(object)), appendText_(std::move(appendText)) {}

bool ObjectRegion::isAlive() const { return object_ != nullptr && object_->isAlive(); }

void ObjectRegion::update() {
    if (object_ != nullptr && object_->isAlive()) {
        const PropertyValues values =
            propertiesForObject(*object_, host().objectSettings(), host().brailleSettings(),
                                hasUsefulText(*object_));
        rawText = getPropertiesBraille(values) + appendText_;
    } else {
        rawText = appendText_;
    }
    Region::update();
}

void ObjectRegion::routeTo(int /*braillePos*/) {
    if (object_ == nullptr || !object_->isAlive()) {
        return;
    }
    if (review_ && host().routingShouldMoveSystemCaret() && object_->isFocusable() &&
        !object_->hasFocus()) {
        // Tethered to review, the user asked for routing to move the caret:
        // bring the focus here first, so that activating the control acts
        // where the user is reading rather than where the focus happened to
        // be left.
        object_->setFocus();
    }
    if (object_->actionCount() > 0) {
        object_->doAction(0);
    }
}

std::string ObjectRegion::describe() const {
    return std::string{review_ ? "review object " : "object "} + "\"" + rawText + "\"";
}

ReviewObjectRegion::ReviewObjectRegion(RegionHost& host, a11y::ObjectPtr object,
                                       std::string appendText)
    : ObjectRegion(host, std::move(object), std::move(appendText)) {
    review_ = true;
}

}  // namespace alus::braille
