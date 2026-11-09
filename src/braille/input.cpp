// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/input.hpp.

#include "alus/braille/input.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>

namespace alus::braille {
namespace {

std::string dotsIdentifier(std::uint8_t dots, bool space) {
    std::string out = "bk:";
    bool first = true;
    for (int dot = 0; dot < 8; ++dot) {
        if ((dots & (1U << dot)) == 0) {
            continue;
        }
        if (!first) {
            out.push_back('+');
        }
        out += "dot";
        out.push_back(static_cast<char>('1' + dot));
        first = false;
    }
    if (space) {
        if (!first) {
            out.push_back('+');
        }
        out += "space";
    }
    return out;
}

std::string displayTextFor(std::uint8_t dots, bool space) {
    std::string out;
    if (space && dots != 0) {
        // Translators: reported when braille space is pressed with dots.
        out = _("space with dot");
    } else if (dots != 0) {
        // Translators: reported when braille dots are pressed.
        out = _("dot");
    } else if (space) {
        // Translators: reported when braille space is pressed.
        out = _("space");
    }
    if (dots != 0) {
        out += " ";
        out += formatDotNumbers(dots);
    }
    return out;
}

// The last codepoint of a UTF-8 string, and the string without it.
std::pair<std::string, std::string> splitLastCodepoint(const std::string& text) {
    if (text.empty()) {
        return {{}, {}};
    }
    const text::OffsetConverter converter{text};
    const std::size_t count = converter.codepointLength();
    const std::size_t lastByte = converter.toByte(count - 1);
    return {text.substr(0, lastByte), text.substr(lastByte)};
}

}  // namespace

// ---------------------------------------------------------------------------
// InputGesture
// ---------------------------------------------------------------------------

InputGesture::InputGesture(std::uint8_t dots, bool space) : dots_(dots), space_(space) {}

const std::string& InputGesture::genericDotsIdentifier() {
    static const std::string identifier = input::normalizeGestureIdentifier("bk:dots");
    return identifier;
}

const std::string& InputGesture::genericSpaceDotsIdentifier() {
    static const std::string identifier =
        input::normalizeGestureIdentifier("bk:space+dots");
    return identifier;
}

std::vector<std::string> InputGesture::identifiers() const {
    if (space_ && dots_ != 0) {
        return {dotsIdentifier(dots_, true), genericSpaceDotsIdentifier()};
    }
    if (dots_ == kDot7 || dots_ == kDot8 || dots_ == (kDot7 | kDot8)) {
        // The three reserved combinations are bindable on their own: erase,
        // enter and translate. Everything else is text.
        return {dotsIdentifier(dots_, false), genericDotsIdentifier()};
    }
    if (dots_ != 0 || space_) {
        return {genericDotsIdentifier()};
    }
    return {};
}

std::string InputGesture::displayName() const { return displayTextFor(dots_, space_); }

std::pair<std::string, std::string> InputGesture::describeIdentifier(
    std::string_view identifier) {
    // Translators: used when describing keys on a braille keyboard.
    const std::string source = _("braille keyboard");
    const std::string normalized = input::normalizeGestureIdentifier(identifier);
    if (normalized == genericSpaceDotsIdentifier()) {
        return {source, "space with any dots"};
    }
    if (normalized == genericDotsIdentifier()) {
        return {source, "any dots"};
    }
    const std::size_t colon = normalized.find(':');
    if (colon == std::string::npos) {
        return {source, {}};
    }
    std::uint8_t dots = 0;
    bool space = false;
    std::string_view parts = std::string_view{normalized}.substr(colon + 1);
    while (!parts.empty()) {
        const std::size_t plus = parts.find('+');
        const std::string_view part = parts.substr(0, plus);
        if (part == "space") {
            space = true;
        } else if (part.size() == 4 && part.rfind("dot", 0) == 0 && part[3] >= '1' &&
                   part[3] <= '8') {
            dots = static_cast<std::uint8_t>(dots | (1U << (part[3] - '1')));
        }
        if (plus == std::string_view::npos) {
            break;
        }
        parts = parts.substr(plus + 1);
    }
    return {source, displayTextFor(dots, space)};
}

// ---------------------------------------------------------------------------
// DisplayGesture
// ---------------------------------------------------------------------------

DisplayGesture::DisplayGesture(std::string source, std::string model, std::string id,
                               std::vector<int> cellIndexes)
    : source_(std::move(source)),
      model_(std::move(model)),
      id_(std::move(id)),
      cellIndexes_(std::move(cellIndexes)) {}

std::string DisplayGesture::idForCellCount(std::size_t count, const std::string& baseName) {
    if (count <= 1 || baseName.empty()) {
        return baseName;
    }
    std::string out = "multi";
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(baseName[0]))));
    out += baseName.substr(1);
    return out;
}

std::vector<std::string> DisplayGesture::identifiers() const {
    std::vector<std::string> ids;
    // The cell numbers, 1 based, appended to the id: `route7` addresses the
    // seventh cell specifically, which is what a user binds to select a range.
    std::string cellSuffix;
    if (id_.find('+') == std::string::npos && !cellIndexes_.empty()) {
        for (const int index : cellIndexes_) {
            if (!cellSuffix.empty()) {
                cellSuffix.push_back('+');
            }
            cellSuffix += std::to_string(index + 1);
        }
    }
    // Model-specific identifiers come first, so a binding for one display
    // beats a binding for the driver.
    if (!model_.empty()) {
        if (!cellSuffix.empty()) {
            ids.push_back("br(" + source_ + "." + model_ + "):" + id_ + cellSuffix);
        }
        ids.push_back("br(" + source_ + "." + model_ + "):" + id_);
    }
    if (!cellSuffix.empty()) {
        ids.push_back("br(" + source_ + "):" + id_ + cellSuffix);
    }
    ids.push_back("br(" + source_ + "):" + id_);
    return ids;
}

std::string DisplayGesture::displayName() const {
    std::string name = source_;
    name += " ";
    name += id_;
    if (!cellIndexes_.empty()) {
        name += " ";
        for (const int index : cellIndexes_) {
            name += std::to_string(index + 1);
            name += " ";
        }
        name.pop_back();
    }
    return name;
}

// ---------------------------------------------------------------------------
// BrailleInput
// ---------------------------------------------------------------------------

BrailleInput::BrailleInput(Louis& louis, Hooks hooks)
    : louis_(&louis), hooks_(std::move(hooks)) {}

BrailleInput::~BrailleInput() = default;

void BrailleInput::applySettings(Settings settings) {
    const bool tableChanged = settings.inputTable != settings_.inputTable ||
                              settings.language != settings_.language;
    settings_ = std::move(settings);
    if (tableChanged) {
        // Half a word translated by the old table means nothing under the new
        // one.
        flushBuffer();
    }
}

std::vector<std::string> BrailleInput::inputTables() const {
    std::string table = settings_.inputTable;
    if (table.empty() || table == "auto") {
        table = louis_->registry().defaultTableForLanguage(settings_.language,
                                                            /*forInput=*/true);
    }
    return {table, "braille-patterns.cti"};
}

bool BrailleInput::isContracted() const {
    std::string table = settings_.inputTable;
    if (table.empty() || table == "auto") {
        table = louis_->registry().defaultTableForLanguage(settings_.language, true);
    }
    const Table* entry = louis_->registry().find(table);
    return entry != nullptr && entry->contracted;
}

bool BrailleInput::useContractedForCurrentFocus() const {
    const bool focusIsText = hooks_.isFocusTextObject ? hooks_.isFocusTextObject() : false;
    return isContracted() && focusIsText && modifiers_.empty();
}

void BrailleInput::setUntranslatedCursorPos(int position) {
    const int limit = static_cast<int>(bufferCells_.size() - untranslatedStart_);
    untranslatedCursorPos_ = std::clamp(position, 0, std::max(limit, 0));
}

void BrailleInput::input(std::uint8_t dots) {
    const std::size_t position =
        untranslatedStart_ + static_cast<std::size_t>(untranslatedCursorPos_);
    bufferCells_.insert(bufferCells_.begin() +
                            static_cast<std::ptrdiff_t>(
                                std::min(position, bufferCells_.size())),
                        dots);
    ++untranslatedCursorPos_;
    // A blank cell is a space, and a space ends a word.
    const bool endOfWord = dots == 0;

    // Uncontracted braille translates on every cell and sends what it
    // produces. Contracted braille waits for the end of the word, because a
    // later cell can change what the earlier ones mean ("tg" against "tgr").
    if (!useContractedForCurrentFocus() || endOfWord) {
        if (translateBuffer(endOfWord)) {
            if (!endOfWord) {
                cellsWithText_.insert(position);
            }
            return;
        }
        if (!bufferText_.empty() && !useContractedForCurrentFocus() && isContracted()) {
            // Translators: reported when braille input could not be
            // translated.
            if (hooks_.message) {
                hooks_.message(_("Unsupported input"));
            }
            flushBuffer();
            return;
        }
        // This cell produced nothing on its own: a number sign, a capital
        // sign. Show it to the user instead of swallowing it.
        reportUntranslated(position);
        return;
    }
    reportUntranslated(position);
}

bool BrailleInput::translateBuffer(bool endOfWord) {
    if (useContractedForCurrentFocus()) {
        // `bufferText_` was used by the per-cell guess; start clean.
        bufferText_.clear();
    }
    const std::size_t oldLength = bufferText_.size();
    const std::size_t position =
        std::min(untranslatedStart_ + static_cast<std::size_t>(untranslatedCursorPos_),
                 bufferCells_.size());
    TranslationMode mode = TranslationMode::None;
    const bool focusIsText = hooks_.isFocusTextObject ? hooks_.isFocusTextObject() : false;
    if ((!focusIsText || !modifiers_.empty()) && isContracted()) {
        // The cells are a command, not a word: tell liblouis the input may be
        // the start of a longer one so it does not force a contraction.
        mode |= TranslationMode::PartialTrans;
    }
    const std::vector<std::uint8_t> upToCursor{bufferCells_.begin(),
                                               bufferCells_.begin() +
                                                   static_cast<std::ptrdiff_t>(position)};
    bufferText_ = louis_->backTranslate(inputTables(), upToCursor, mode);

    std::string newText =
        bufferText_.size() > oldLength ? bufferText_.substr(oldLength) : std::string{};
    if (!newText.empty()) {
        if (modifiers_.empty() && focusIsText) {
            lastSent_ = std::chrono::steady_clock::now();
            hasSent_ = true;
        }
        untranslatedStart_ = position;
        untranslatedCursorPos_ = 0;
        if (!modifiers_.empty() || !focusIsText) {
            if (text::codepointCount(newText) > 1) {
                // Emulating several characters at once is not a thing.
                newText.clear();
            } else {
                emulateKey(newText);
            }
        } else {
            sendChars(newText);
        }
    }

    if (endOfWord || (!newText.empty() && (!focusIsText || !modifiers_.empty()))) {
        // Only one word needs buffering; drop everything before the cursor.
        bufferCells_.erase(bufferCells_.begin(),
                           bufferCells_.begin() +
                               static_cast<std::ptrdiff_t>(
                                   std::min(position, bufferCells_.size())));
        bufferText_.clear();
        cellsWithText_.clear();
        modifiers_.clear();
        untranslatedStart_ = 0;
        untranslatedCursorPos_ = 0;
    }
    if (!newText.empty() || endOfWord) {
        updateUntranslated();
        return true;
    }
    return false;
}

std::string BrailleInput::translateForReport(std::size_t position) {
    const std::string previous = bufferText_;
    const std::size_t count = std::min(position + 1, bufferCells_.size());
    const std::vector<std::uint8_t> cells{bufferCells_.begin(),
                                          bufferCells_.begin() +
                                              static_cast<std::ptrdiff_t>(count)};
    bufferText_ = louis_->backTranslate(inputTables(), cells, TranslationMode::PartialTrans);
    return previous;
}

bool BrailleInput::reportContractedCell(std::size_t position) {
    const std::string previous = translateForReport(position);
    if (bufferText_.compare(0, previous.size(), previous) != 0) {
        // This cell changed the text in front of it, so no useful guess can
        // be made -- "alw" becoming "always" is not a per-cell story.
        return false;
    }
    const std::string added = bufferText_.substr(previous.size());
    if (added.empty()) {
        return false;
    }
    if (hooks_.message) {
        // Spoken as separate characters: a rough idea is what helps here.
        std::string spaced;
        const text::OffsetConverter converter{added};
        for (std::size_t index = 0; index < converter.codepointLength(); ++index) {
            const std::size_t from = converter.toByte(index);
            const std::size_t to = converter.toByte(index + 1);
            if (!spaced.empty()) {
                spaced.push_back(' ');
            }
            spaced += added.substr(from, to - from);
        }
        hooks_.message(spaced);
    }
    return true;
}

void BrailleInput::reportUntranslated(std::size_t position) {
    const bool protectedField =
        hooks_.isTypingProtected ? hooks_.isTypingProtected() : false;
    if (protectedField) {
        if (hooks_.message) {
            hooks_.message("*");
        }
    } else if (!isContracted() || !reportContractedCell(position)) {
        if (hooks_.speakDots && position < bufferCells_.size()) {
            // Translators: reported when braille dots are typed; "dot 1 2 5".
            // Translators: the dots of one untranslated braille cell; %s is
            // the list of dot numbers, e.g. "dot 1 3 5".
            hooks_.speakDots(
                i18n::substitute(_("dot %s"), formatDotNumbers(bufferCells_[position])));
        }
    }
    if (isContracted() && protectedField) {
        // Even without speaking it, the state the contracted guess needs has
        // to be kept up to date.
        translateForReport(position);
    }
    updateUntranslated();
    if (hooks_.updateDisplay) {
        hooks_.updateDisplay();
    }
}

void BrailleInput::updateUntranslated() {
    untranslated_.clear();
    const bool protectedField =
        hooks_.isTypingProtected ? hooks_.isTypingProtected() : false;
    for (std::size_t index = untranslatedStart_; index < bufferCells_.size(); ++index) {
        // A password field shows every cell as all eight dots, exactly as
        // speech says "star" rather than the character.
        untranslated_ += unicodeBrailleFor(protectedField ? 0xFF : bufferCells_[index]);
    }
}

void BrailleInput::eraseLastCell() {
    const std::ptrdiff_t index = static_cast<std::ptrdiff_t>(untranslatedStart_) +
                                 untranslatedCursorPos_ - 1;
    if (index < 0) {
        // Erasing before the start of the buffer is an ordinary backspace.
        lastSent_ = std::chrono::steady_clock::now();
        hasSent_ = true;
        emulateKey("backspace", /*withModifiers=*/false);
        return;
    }
    const std::size_t position = static_cast<std::size_t>(index);
    if (position >= bufferCells_.size()) {
        return;
    }
    const std::uint8_t cell = bufferCells_[position];
    bufferCells_.erase(bufferCells_.begin() + index);
    if (cellsWithText_.count(position) != 0) {
        // This cell produced a real character, so a real backspace is needed.
        lastSent_ = std::chrono::steady_clock::now();
        hasSent_ = true;
        emulateKey("backspace", /*withModifiers=*/false);
        const auto [withoutLast, last] = splitLastCodepoint(bufferText_);
        bufferText_ = withoutLast;
        cellsWithText_.erase(position);
        if (untranslatedStart_ > 0) {
            --untranslatedStart_;
        }
        untranslatedCursorPos_ = 0;
        // Erasing a character can leave untranslated cells behind: erasing the
        // digit 1 leaves the number sign.
        for (std::ptrdiff_t previous = index - 1; previous >= 0; --previous) {
            if (cellsWithText_.count(static_cast<std::size_t>(previous)) != 0) {
                break;
            }
            untranslatedStart_ = static_cast<std::size_t>(previous);
            ++untranslatedCursorPos_;
        }
        if (untranslatedCursorPos_ > 0) {
            updateUntranslated();
            if (hooks_.updateDisplay) {
                hooks_.updateDisplay();
            }
        }
        return;
    }
    if (isContracted()) {
        translateForReport(position);
    }
    if (hooks_.speakDots) {
        hooks_.speakDots("dot " + formatDotNumbers(cell));
    }
    if (untranslatedCursorPos_ > 0) {
        --untranslatedCursorPos_;
    }
    updateUntranslated();
    if (hooks_.updateDisplay) {
        hooks_.updateDisplay();
    }
}

void BrailleInput::enter() {
    translateBuffer(/*endOfWord=*/true);
    emulateKey("enter", /*withModifiers=*/false);
}

void BrailleInput::translate() { translateBuffer(/*endOfWord=*/true); }

void BrailleInput::toggleModifiers(const std::vector<std::string>& modifiers) {
    if (!bufferText_.empty()) {
        translateBuffer(/*endOfWord=*/true);
    }
    for (const std::string& modifier : modifiers) {
        if (modifiers_.erase(modifier) != 0) {
            if (hooks_.message) {
                // Translators: reported when a braille input modifier is
                // released; %s is the modifier, e.g. "control released".
                hooks_.message(i18n::substitute(_("%s released"), modifier));
            }
        } else {
            modifiers_.insert(modifier);
            if (hooks_.message) {
                hooks_.message(modifier);
            }
        }
    }
}

void BrailleInput::flushBuffer() {
    bufferCells_.clear();
    bufferText_.clear();
    cellsWithText_.clear();
    modifiers_.clear();
    untranslated_.clear();
    untranslatedStart_ = 0;
    untranslatedCursorPos_ = 0;
}

void BrailleInput::handleGainFocus() { flushBuffer(); }

void BrailleInput::handleCaretMove() {
    if (bufferCells_.empty()) {
        return;
    }
    if (hasSent_) {
        // Our own text entry moved the caret. Within a short window that is
        // what this event is, and clearing the buffer because of our own
        // typing would make every second character start a new word.
        const auto elapsed = std::chrono::steady_clock::now() - lastSent_;
        if (elapsed <= std::chrono::milliseconds{300}) {
            return;
        }
        hasSent_ = false;
    }
    // The user moved away mid-word: nothing in the buffer means anything now.
    flushBuffer();
}

void BrailleInput::emulateKey(const std::string& key, bool withModifiers) {
    std::string gesture = key;
    if (withModifiers && !modifiers_.empty()) {
        gesture.clear();
        for (const std::string& modifier : modifiers_) {
            gesture += modifier;
            gesture.push_back('+');
        }
        gesture += key;
    }
    if (hooks_.emulateKey && hooks_.emulateKey(gesture)) {
        return;
    }
    // The platform could not emulate it. Fall back to sending the characters,
    // which on Wayland will also fail -- and then it is said out loud, once.
    sendChars(key);
}

void BrailleInput::sendChars(const std::string& chars) {
    if (chars.empty()) {
        return;
    }
    if (hooks_.typeText && hooks_.typeText(chars)) {
        return;
    }
    if (!reportedNoTyping_) {
        reportedNoTyping_ = true;
        ALUS_WARNING(
            "braille input cannot type on this session (N18): no keystroke injection; "
            "leave braille.handleBrailleInput off and let brltty type");
        if (hooks_.message) {
            // Translators: said once when braille typing is impossible on
            // this platform.
            hooks_.message(_("Braille typing is not available in this session"));
        }
    }
}

}  // namespace alus::braille
