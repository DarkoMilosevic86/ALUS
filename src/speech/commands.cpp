// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/commands.hpp"

#include "alus/audio/tones.hpp"
#include "alus/audio/wave_player.hpp"
#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/speech/priority.hpp"

#include <cmath>
#include <format>

namespace alus::speech {

const char* priorityName(Priority priority) {
    switch (priority) {
        case Priority::Normal:
            return "normal";
        case Priority::Next:
            return "next";
        case Priority::Now:
            return "now";
    }
    return "?";
}

const char* commandTypeName(CommandType type) {
    switch (type) {
        case CommandType::Index:
            return "index";
        case CommandType::CharacterMode:
            return "characterMode";
        case CommandType::LangChange:
            return "langChange";
        case CommandType::Break:
            return "break";
        case CommandType::Pitch:
            return "pitch";
        case CommandType::Rate:
            return "rate";
        case CommandType::Volume:
            return "volume";
        case CommandType::Phoneme:
            return "phoneme";
        case CommandType::EndUtterance:
            return "endUtterance";
        case CommandType::SuppressUnicodeNormalization:
            return "suppressUnicodeNormalization";
        case CommandType::Callback:
            return "callback";
        case CommandType::Beep:
            return "beep";
        case CommandType::WaveFile:
            return "waveFile";
        case CommandType::ProfileTrigger:
            return "profileTrigger";
        case CommandType::Cancellable:
            return "cancellable";
    }
    return "?";
}

namespace {
const CommandPtr kNullCommand{};
}  // namespace

const CommandPtr& SpeechItem::command() const {
    if (isText()) {
        return kNullCommand;
    }
    return std::get<CommandPtr>(value_);
}

bool SpeechItem::is(CommandType type) const {
    const CommandPtr& cmd = command();
    return cmd != nullptr && cmd->type() == type;
}

std::string SpeechItem::repr() const {
    if (isText()) {
        return std::format("\"{}\"", text());
    }
    const CommandPtr& cmd = command();
    return cmd == nullptr ? std::string{"null"} : cmd->repr();
}

std::string repr(const SpeechSequence& sequence) {
    std::string out = "[";
    bool first = true;
    for (const SpeechItem& item : sequence) {
        if (!first) {
            out += ", ";
        }
        first = false;
        out += item.repr();
    }
    out += "]";
    return out;
}

std::string IndexCommand::repr() const { return std::format("IndexCommand({})", index_); }

std::string CharacterModeCommand::repr() const {
    return std::format("CharacterModeCommand({})", state_);
}

std::string LangChangeCommand::repr() const {
    return std::format("LangChangeCommand({})", language_.empty() ? "default" : language_);
}

std::string BreakCommand::repr() const {
    return std::format("BreakCommand(time={})", milliseconds_);
}

std::string SuppressUnicodeNormalizationCommand::repr() const {
    return std::format("SuppressUnicodeNormalizationCommand({})", state_);
}

std::string PhonemeCommand::repr() const {
    return text_.empty() ? std::format("PhonemeCommand({})", ipa_)
                         : std::format("PhonemeCommand({}, text={})", ipa_, text_);
}

std::string CallbackCommand::repr() const {
    return std::format("CallbackCommand(name={})", name_);
}

void CallbackCommand::run() {
    if (callback_ != nullptr) {
        callback_();
    }
}

std::string BeepCommand::repr() const {
    return std::format("BeepCommand({}, {}, left={}, right={})", hz_, milliseconds_, left_, right_);
}

void BeepCommand::run() { audio::beep(hz_, milliseconds_, left_, right_); }

std::string WaveFileCommand::repr() const {
    return std::format("WaveFileCommand({})", fileName_);
}

void WaveFileCommand::run() { audio::playWaveFile(fileName_); }

std::string ProfileTriggerCommand::repr() const {
    return std::format("ProfileTriggerCommand({}, enter={})",
                       trigger_ == nullptr ? "null" : trigger_->spec(), enter_);
}

std::string CancellableSpeechCommand::repr() const {
    const std::string info = devInfo();
    return std::format("CancellableSpeech({}{}{})", cancelled_ ? "cancelled" : "still valid",
                       info.empty() ? "" : ", ", info);
}

bool CancellableSpeechCommand::isCancelled() {
    if (cancelled_) {
        return true;
    }
    bool valid = false;
    try {
        valid = checkIfValid();
    } catch (...) {
        // A validity check that throws is an expired object nine times out of
        // ten. Treat it as cancelled rather than letting it break the queue.
        core::Log::instance().exception("cancellable speech validity check failed");
        valid = false;
    }
    if (!valid) {
        cancelled_ = true;
    }
    return cancelled_;
}

// ---------------------------------------------------------------------------
// Prosody
// ---------------------------------------------------------------------------

ProsodyCommand::ProsodyCommand(int offset, double multiplier)
    : offset_(offset), multiplier_(multiplier) {
    if (offset != 0 && multiplier != 1.0) {
        throw std::invalid_argument("prosody command: offset and multiplier both specified");
    }
}

int ProsodyCommand::offset(int defaultValue) const {
    if (offset_ != 0) {
        return offset_;
    }
    if (multiplier_ == 1.0) {
        return 0;
    }
    const double scaled = static_cast<double>(defaultValue) * multiplier_;
    return static_cast<int>(scaled) - defaultValue;
}

double ProsodyCommand::multiplier(int defaultValue) const {
    if (multiplier_ != 1.0) {
        return multiplier_;
    }
    if (offset_ == 0) {
        return 1.0;
    }
    if (defaultValue == 0) {
        // Dividing by the configured value is NVDA's formula; a configured zero
        // has no meaningful multiplier, so report "unchanged" instead of
        // dividing by zero.
        return 1.0;
    }
    return static_cast<double>(defaultValue + offset_) / static_cast<double>(defaultValue);
}

int ProsodyCommand::newValue(int defaultValue) const {
    if (offset_ != 0) {
        return defaultValue + offset_;
    }
    if (multiplier_ != 1.0) {
        return static_cast<int>(static_cast<double>(defaultValue) * multiplier_);
    }
    return defaultValue;
}

std::string ProsodyCommand::repr() const {
    if (offset_ != 0) {
        return std::format("{}Command(offset={})", settingName(), offset_);
    }
    if (multiplier_ != 1.0) {
        return std::format("{}Command(multiplier={})", settingName(), multiplier_);
    }
    return std::format("{}Command()", settingName());
}

std::shared_ptr<PitchCommand> PitchCommand::byOffset(int offset) {
    return std::make_shared<PitchCommand>(offset, 1.0);
}
std::shared_ptr<PitchCommand> PitchCommand::byMultiplier(double multiplier) {
    return std::make_shared<PitchCommand>(0, multiplier);
}
std::shared_ptr<PitchCommand> PitchCommand::toDefault() {
    return std::make_shared<PitchCommand>(0, 1.0);
}

std::shared_ptr<RateCommand> RateCommand::byOffset(int offset) {
    return std::make_shared<RateCommand>(offset, 1.0);
}
std::shared_ptr<RateCommand> RateCommand::byMultiplier(double multiplier) {
    return std::make_shared<RateCommand>(0, multiplier);
}
std::shared_ptr<RateCommand> RateCommand::toDefault() {
    return std::make_shared<RateCommand>(0, 1.0);
}

std::shared_ptr<VolumeCommand> VolumeCommand::byOffset(int offset) {
    return std::make_shared<VolumeCommand>(offset, 1.0);
}
std::shared_ptr<VolumeCommand> VolumeCommand::byMultiplier(double multiplier) {
    return std::make_shared<VolumeCommand>(0, multiplier);
}
std::shared_ptr<VolumeCommand> VolumeCommand::toDefault() {
    return std::make_shared<VolumeCommand>(0, 1.0);
}

// ---------------------------------------------------------------------------
// Profile triggers
// ---------------------------------------------------------------------------

ProfileTrigger::ProfileTrigger(config::Manager& manager, std::string spec)
    : manager_(manager), spec_(std::move(spec)) {}

bool ProfileTrigger::hasProfile() const {
    return manager_.profileForTrigger(spec_).has_value();
}

void ProfileTrigger::enter() { manager_.triggerEnter(spec_); }

void ProfileTrigger::exit() { manager_.triggerExit(spec_); }

// ---------------------------------------------------------------------------
// Shorthands
// ---------------------------------------------------------------------------

CommandPtr index(int value) { return std::make_shared<IndexCommand>(value); }
CommandPtr characterMode(bool state) { return std::make_shared<CharacterModeCommand>(state); }
CommandPtr langChange(std::string language) {
    return std::make_shared<LangChangeCommand>(std::move(language));
}
CommandPtr breakFor(int milliseconds) { return std::make_shared<BreakCommand>(milliseconds); }
CommandPtr endUtterance() { return std::make_shared<EndUtteranceCommand>(); }
CommandPtr suppressUnicodeNormalization(bool state) {
    return std::make_shared<SuppressUnicodeNormalizationCommand>(state);
}
CommandPtr callback(std::function<void()> function, std::string name) {
    return std::make_shared<CallbackCommand>(std::move(function), std::move(name));
}
CommandPtr beep(double hz, int milliseconds, int left, int right) {
    return std::make_shared<BeepCommand>(hz, milliseconds, left, right);
}
CommandPtr waveFile(std::string fileName) {
    return std::make_shared<WaveFileCommand>(std::move(fileName));
}
CommandPtr phoneme(std::string ipa, std::string text) {
    return std::make_shared<PhonemeCommand>(std::move(ipa), std::move(text));
}

}  // namespace alus::speech
