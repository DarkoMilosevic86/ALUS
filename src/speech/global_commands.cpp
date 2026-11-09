// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/global_commands.hpp"

#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/speech/settings_ring.hpp"
#include "alus/speech/synth.hpp"
#include "alus/text/symbols.hpp"
#include "alus/ui/message.hpp"

#include <array>
#include <utility>

namespace alus::speech {
namespace {

// NVDA: SpeechMode.displayString. The SPOKEN name of a mode; speechModeName()
// is the configuration spelling and is deliberately not translated.
// The msgctxt is upstream's ("speechModes"), and it is not decoration: the
// bare msgid "off" exists several times over in the catalogue with different
// translations, and the context is what picks the one a speech mode needs.
// It is also the key tools/import-nvda-po.py matches on, so getting it wrong
// costs the string its Serbian translation and says nothing at build time.
std::string speechModeLabel(SpeechMode mode) {
    switch (mode) {
        // Translators: one of the speech modes (alus+s); ALUS says nothing.
        case SpeechMode::Off:
            return C_("speechModes", "off");
        // Translators: one of the speech modes (alus+s); ALUS beeps instead
        // of speaking.
        case SpeechMode::Beeps:
            return C_("speechModes", "beeps");
        // Translators: one of the speech modes (alus+s); the normal one.
        case SpeechMode::Talk:
            return C_("speechModes", "talk");
        // Translators: one of the speech modes (alus+s); ALUS speaks only
        // what was explicitly asked for.
        case SpeechMode::OnDemand:
            return C_("speechModes", "on-demand");
    }
    return {};
}

// NVDA's SPEECH_SYMBOL_LEVEL_LABELS, for the four levels alus+p cycles. The
// msgctxt is upstream's, for the reason given above speechModeLabel.
// `char` is not among them, upstream or here: it is reachable in the settings
// window, and a user who landed on it by accident would hear every space and
// every letter of every word spelled out with no obvious way back.
std::string symbolLevelLabel(text::SymbolLevel level) {
    switch (level) {
        // Translators: one of the punctuation and symbol levels (alus+p):
        // no symbols are spoken.
        case text::SymbolLevel::None:
            return C_("symbolLevel", "none");
        // Translators: one of the punctuation and symbol levels (alus+p).
        case text::SymbolLevel::Some:
            return C_("symbolLevel", "some");
        // Translators: one of the punctuation and symbol levels (alus+p).
        case text::SymbolLevel::Most:
            return C_("symbolLevel", "most");
        // Translators: one of the punctuation and symbol levels (alus+p):
        // every symbol is spoken.
        case text::SymbolLevel::All:
            return C_("symbolLevel", "all");
        // Translators: one of the punctuation and symbol levels: every
        // character, as when spelling.
        case text::SymbolLevel::Char:
            return C_("symbolLevel", "character");
        case text::SymbolLevel::Unchanged:
            break;
    }
    return {};
}

// The cycle alus+s walks. Upstream sorts the enumeration so that the modes
// after the current one come first; with no exclusions that is simply the next
// one, which is what this is.
constexpr std::array<SpeechMode, 4> kModeCycle{SpeechMode::Off, SpeechMode::Beeps,
                                               SpeechMode::Talk, SpeechMode::OnDemand};

SpeechMode nextMode(SpeechMode mode) {
    for (std::size_t i = 0; i < kModeCycle.size(); ++i) {
        if (kModeCycle[i] == mode) {
            return kModeCycle[(i + 1) % kModeCycle.size()];
        }
    }
    return SpeechMode::Talk;
}

}  // namespace

GlobalCommands::GlobalCommands(Context context, const script::Catalog* catalog)
    // One class name, NVDA's: it is what the generated gesture map addresses.
    : script::ScriptableObject({"globalCommands.GlobalCommands"}, catalog),
      context_(std::move(context)) {
    ALUS_SCRIPT(speechMode);
    ALUS_SCRIPT(cycleSpeechSymbolLevel);
    ALUS_SCRIPT(repeatLastSpokenInformation);
    ALUS_SCRIPT(nextSynthSetting);
    ALUS_SCRIPT(previousSynthSetting);
    ALUS_SCRIPT(increaseSynthSetting);
    ALUS_SCRIPT(decreaseSynthSetting);
    ALUS_SCRIPT(increaseLargeSynthSetting);
    ALUS_SCRIPT(decreaseLargeSynthSetting);
}

void GlobalCommands::say(const std::string& text) const {
    if (text.empty()) {
        return;
    }
    if (context_.message) {
        context_.message(text);
        return;
    }
    ui::message(text);
}

void GlobalCommands::sayNoSettings() const {
    // Translators: spoken when the synthesizer settings ring
    // (alus+control+arrows) has nothing in it, because the synthesizer
    // reports no setting that can be stepped.
    say(_("No settings"));
}

void GlobalCommands::speechMode(input::Gesture& /*gesture*/) {
    Presenter* presenter = context_.presenter ? context_.presenter() : nullptr;
    if (presenter == nullptr) {
        // Translators: spoken when a command that needs the speech pipeline is
        // run where there is none, e.g. under `alus-daemon --no-speech`.
        say(_("speech is not available"));
        return;
    }
    const SpeechMode mode = nextMode(presenter->mode());
    // The announcement has to be heard even when the mode being LEFT is `off`,
    // so speech is switched on for it first and the new mode takes effect
    // after -- upstream's order, and the reason a user can find their way out
    // of `off` at all.
    presenter->setMode(SpeechMode::Talk);
    presenter->cancelSpeech();
    // Translators: announced when the speech mode is changed; {mode} is the
    // new mode ("off", "beeps", "talk", "on-demand").
    //
    // The brace is upstream's, and it is copied exactly: the msgid is the key
    // into upstream's Serbian catalogue, so a pattern rewritten to "%s" would
    // be an untranslated string (i18n::substituteNamed says more).
    say(i18n::substituteNamed(_("Speech mode {mode}"), "mode", speechModeLabel(mode)));
    presenter->setMode(mode);
    if (context_.config != nullptr) {
        context_.config->setString("speech", "speechMode", speechModeName(mode));
    }
    ALUS_INFO("speech mode: {}", speechModeName(mode));
}

void GlobalCommands::cycleSpeechSymbolLevel(input::Gesture& /*gesture*/) {
    Presenter* presenter = context_.presenter ? context_.presenter() : nullptr;
    if (presenter == nullptr) {
        say(_("speech is not available"));
        return;
    }
    Presenter::Settings settings = presenter->settings();
    settings.symbolLevel = text::nextSymbolLevel(settings.symbolLevel);
    presenter->setSettings(settings);
    if (context_.config != nullptr) {
        context_.config->setString("speech", "symbolLevel",
                                   text::symbolLevelName(settings.symbolLevel));
    }
    // Translators: reported when the punctuation and symbol level is cycled;
    // %s is the new level ("none", "some", "most", "all").
    say(i18n::substitute(_("Symbol level %s"), symbolLevelLabel(settings.symbolLevel)));
}

void GlobalCommands::repeatLastSpokenInformation(input::Gesture& /*gesture*/) {
    Presenter* presenter = context_.presenter ? context_.presenter() : nullptr;
    if (presenter == nullptr) {
        say(_("speech is not available"));
        return;
    }
    const std::optional<Presenter::LastSpeech>& last = presenter->lastSpeech();
    if (!last.has_value() || last->sequence.empty()) {
        // Upstream returns silently here. ALUS does not: a key that answers
        // nothing is indistinguishable from a key that did not arrive, which
        // is the defect FAZA9.md N99 is about. Nothing having been said yet is
        // a fact, and a short one.
        // Translators: reported when `alus+x` is pressed before ALUS has said
        // anything there is to repeat.
        say(_("Nothing to repeat"));
        return;
    }

    const int repeats =
        context_.dispatcher != nullptr ? context_.dispatcher->lastScriptRepeatCount() : 0;
    if (repeats == 0) {
        // Through the pipeline again, at the level it was spoken at, which is
        // upstream's `speech.speak(lastSpeechSeq, symbolLevel=symbolLevel)`.
        // The sequence is COPIED first: speaking it stores it again, and
        // handing the pipeline a reference to the very thing it is about to
        // overwrite is how that becomes a use-after-move.
        SpeechSequence sequence = last->sequence;
        const std::optional<text::SymbolLevel> level = last->symbolLevel;
        presenter->speak(std::move(sequence), level);
        return;
    }

    // THE SECOND PRESS SPELLS IT, and this is ALUS's own behaviour rather
    // than upstream's. Upstream opens a browseable message window -- a
    // scrollable dialog with a copy button -- and ALUS has no such window
    // (its only window is the settings one, in another process). Spelling is
    // what every other "press it again" command in ALUS does, and what a user
    // wants the second press for is almost always one word they could not
    // make out.
    presenter->speakSpelling(utteranceText(last->sequence));
}

void GlobalCommands::nextSynthSetting(input::Gesture& /*gesture*/) {
    SettingsRing* ring = context_.ring ? context_.ring() : nullptr;
    if (ring == nullptr || !ring->next().has_value()) {
        sayNoSettings();
        return;
    }
    say(ring->currentName() + " " + ring->currentValue());
}

void GlobalCommands::previousSynthSetting(input::Gesture& /*gesture*/) {
    SettingsRing* ring = context_.ring ? context_.ring() : nullptr;
    if (ring == nullptr || !ring->previous().has_value()) {
        sayNoSettings();
        return;
    }
    say(ring->currentName() + " " + ring->currentValue());
}

void GlobalCommands::adjust(const std::function<bool(SettingsRing&, std::string*)>& change) {
    SettingsRing* ring = context_.ring ? context_.ring() : nullptr;
    if (ring == nullptr || ring->empty()) {
        sayNoSettings();
        return;
    }
    // The NAME is read before the change and the VALUE after it, which is
    // upstream's order and not an accident: changing the voice changes which
    // voice the name would be read in, and the user is told what they are
    // adjusting before they are told where it landed.
    const std::string name = ring->currentName();
    std::string value;
    if (!change(*ring, &value)) {
        sayNoSettings();
        return;
    }
    say(name + " " + value);
}

void GlobalCommands::increaseSynthSetting(input::Gesture& /*gesture*/) {
    adjust([](SettingsRing& ring, std::string* value) {
        const std::optional<std::string> result = ring.increase();
        if (!result.has_value()) {
            return false;
        }
        *value = *result;
        return true;
    });
}

void GlobalCommands::decreaseSynthSetting(input::Gesture& /*gesture*/) {
    adjust([](SettingsRing& ring, std::string* value) {
        const std::optional<std::string> result = ring.decrease();
        if (!result.has_value()) {
            return false;
        }
        *value = *result;
        return true;
    });
}

void GlobalCommands::increaseLargeSynthSetting(input::Gesture& /*gesture*/) {
    adjust([](SettingsRing& ring, std::string* value) {
        const std::optional<std::string> result = ring.increaseLarge();
        if (!result.has_value()) {
            return false;
        }
        *value = *result;
        return true;
    });
}

void GlobalCommands::decreaseLargeSynthSetting(input::Gesture& /*gesture*/) {
    adjust([](SettingsRing& ring, std::string* value) {
        const std::optional<std::string> result = ring.decreaseLarge();
        if (!result.has_value()) {
            return false;
        }
        *value = *result;
        return true;
    });
}

}  // namespace alus::speech
