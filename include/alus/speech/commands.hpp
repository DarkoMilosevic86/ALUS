// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Speech commands: the whole hierarchy from NVDA's `speech/commands.py`.
//
// See 01-ARHITEKTURA.md §4.3 for the command → SSML mapping. The distinction
// that matters is the one NVDA draws:
//
//   Command          anything that can sit between strings in a sequence
//   isSynthCommand() reaches the synthesizer at all
//   isParamCommand() changes a parameter for SUBSEQUENT speech, so it has to be
//                    re-emitted when an interrupted queue resumes (§4.1,
//                    ParamChangeTracker) — otherwise the voice stays stuck at
//                    the wrong rate
//   isCallback()     never reaches the synthesizer; the manager turns it into an
//                    IndexCommand and runs it ON THE MAIN THREAD when that
//                    index is reached
//
// A sequence item is either text or a command, exactly as in NVDA
// (`speech/types.py`: `SequenceItemT = Union[SpeechCommand, str]`).
//
// Commands are held by shared_ptr because the manager tracks some of them by
// IDENTITY, not by value: a CancellableSpeechCommand is looked up in a map while
// the utterance containing it is being spoken, and a CallbackCommand is owned by
// whoever queued the speech.
#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace alus::config {
class Manager;
}

namespace alus::speech {

enum class CommandType {
    // → the synthesizer
    Index,
    CharacterMode,
    LangChange,
    Break,
    Pitch,
    Rate,
    Volume,
    Phoneme,
    // handled by ALUS, never passed on
    EndUtterance,
    SuppressUnicodeNormalization,
    Callback,
    Beep,
    WaveFile,
    ProfileTrigger,
    Cancellable,
};

const char* commandTypeName(CommandType type);

class Command {
public:
    Command() = default;
    virtual ~Command() = default;

    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;

    virtual CommandType type() const = 0;
    virtual std::string repr() const = 0;

    // NVDA: SynthCommand. Only these are allowed to reach an ISynth.
    virtual bool isSynthCommand() const { return false; }
    // NVDA: SynthParamCommand.
    virtual bool isParamCommand() const { return false; }
    // NVDA: SynthParamCommand.isDefault — "this returns the parameter to the
    // value the user configured", which is what lets ParamChangeTracker forget
    // about it instead of replaying it.
    virtual bool isDefault() const { return false; }
    // NVDA: BaseCallbackCommand.
    virtual bool isCallback() const { return false; }

    // Runs on the MAIN THREAD when the speech reaches this command. Must return
    // quickly: it blocks the production of further speech (NVDA says the same).
    virtual void run() {}
};

using CommandPtr = std::shared_ptr<Command>;

// A sequence item: text, or a command.
class SpeechItem {
public:
    SpeechItem(std::string text) : value_(std::move(text)) {}        // NOLINT(*-explicit-*)
    SpeechItem(const char* text) : value_(std::string{text}) {}      // NOLINT(*-explicit-*)
    SpeechItem(CommandPtr command) : value_(std::move(command)) {}   // NOLINT(*-explicit-*)
    // A shared_ptr to a concrete command would otherwise need two user-defined
    // conversions (to CommandPtr, then to SpeechItem), which C++ does not do
    // implicitly — and `sequence.push_back(PitchCommand::byOffset(30))` has to
    // read naturally.
    template <class T, class = std::enable_if_t<std::is_base_of_v<Command, T>>>
    SpeechItem(std::shared_ptr<T> command)                           // NOLINT(*-explicit-*)
        : value_(CommandPtr{std::move(command)}) {}

    bool isText() const noexcept { return std::holds_alternative<std::string>(value_); }
    bool isCommand() const noexcept { return !isText(); }

    const std::string& text() const { return std::get<std::string>(value_); }
    std::string& text() { return std::get<std::string>(value_); }

    // Null for a text item.
    const CommandPtr& command() const;

    // The command's type, or nullopt-like behaviour for text: callers that care
    // test isCommand() first. `is()` is the convenient form.
    bool is(CommandType type) const;

    std::string repr() const;

private:
    std::variant<std::string, CommandPtr> value_;
};

using SpeechSequence = std::vector<SpeechItem>;

std::string repr(const SpeechSequence& sequence);

// Downcast helper: `as<IndexCommand>(item)` is null unless the item is a command
// of that exact kind.
template <class T>
std::shared_ptr<T> as(const SpeechItem& item) {
    if (!item.isCommand()) {
        return nullptr;
    }
    const CommandPtr& command = item.command();
    if (command == nullptr || command->type() != T::kType) {
        return nullptr;
    }
    return std::static_pointer_cast<T>(command);
}

template <class T>
std::shared_ptr<T> as(const CommandPtr& command) {
    if (command == nullptr || command->type() != T::kType) {
        return nullptr;
    }
    return std::static_pointer_cast<T>(command);
}

// ---------------------------------------------------------------------------
// Synth commands
// ---------------------------------------------------------------------------

// Marks a point in the speech. When the synthesizer reaches it, it notifies
// ALUS. Callers do NOT create these: the SpeechManager owns all indexing and
// creates them from callback commands and utterance ends (NVDA says the same).
class IndexCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::Index;

    explicit IndexCommand(int index) : index_(index) {}

    CommandType type() const override { return kType; }
    std::string repr() const override;
    bool isSynthCommand() const override { return true; }

    int index() const noexcept { return index_; }

private:
    int index_;
};

class CharacterModeCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::CharacterMode;

    explicit CharacterModeCommand(bool state) : state_(state) {}

    CommandType type() const override { return kType; }
    std::string repr() const override;
    bool isSynthCommand() const override { return true; }
    bool isParamCommand() const override { return true; }
    bool isDefault() const override { return !state_; }

    bool state() const noexcept { return state_; }

private:
    bool state_;
};

// An empty language means "back to the ALUS locale".
class LangChangeCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::LangChange;

    explicit LangChangeCommand(std::string language = {}) : language_(std::move(language)) {}

    CommandType type() const override { return kType; }
    std::string repr() const override;
    bool isSynthCommand() const override { return true; }
    bool isParamCommand() const override { return true; }
    bool isDefault() const override { return language_.empty(); }

    const std::string& language() const noexcept { return language_; }

private:
    std::string language_;
};

class BreakCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::Break;

    explicit BreakCommand(int milliseconds = 0) : milliseconds_(milliseconds) {}

    CommandType type() const override { return kType; }
    std::string repr() const override;
    bool isSynthCommand() const override { return true; }

    int milliseconds() const noexcept { return milliseconds_; }

private:
    int milliseconds_;
};

// Pitch/rate/volume. NVDA's semantics (commands.py:249-344), which have to carry
// over exactly because they are user-visible: the change is expressed as an
// OFFSET XOR a MULTIPLIER, and both are relative to THE VALUE THE USER
// CONFIGURED, not to an absolute scale. "Raise the pitch on capitals" is
// PitchCommand::byOffset(30) — plus 30 on top of whatever the user set.
//
// ALUS differs from NVDA in one respect, deliberately: NVDA reaches into the
// global configuration to find the default value, which makes the command
// untestable in isolation. Here the default is passed in by the caller that
// knows it — the synth driver.
class ProsodyCommand : public Command {
public:
    ProsodyCommand(int offset, double multiplier);

    std::string repr() const override;
    bool isSynthCommand() const override { return true; }
    bool isParamCommand() const override { return true; }
    bool isDefault() const override { return offset_ == 0 && multiplier_ == 1.0; }

    // The amount by which to change `defaultValue`.
    int offset(int defaultValue) const;
    // The number by which to multiply `defaultValue`.
    double multiplier(int defaultValue) const;
    // The resulting absolute value. NOT clamped: the synth driver clamps to its
    // own range, because the range is the driver's business.
    int newValue(int defaultValue) const;

    int rawOffset() const noexcept { return offset_; }
    double rawMultiplier() const noexcept { return multiplier_; }

protected:
    virtual const char* settingName() const = 0;

private:
    int offset_;
    double multiplier_;
};

class PitchCommand final : public ProsodyCommand {
public:
    static constexpr CommandType kType = CommandType::Pitch;

    PitchCommand(int offset, double multiplier) : ProsodyCommand(offset, multiplier) {}
    static std::shared_ptr<PitchCommand> byOffset(int offset);
    static std::shared_ptr<PitchCommand> byMultiplier(double multiplier);
    static std::shared_ptr<PitchCommand> toDefault();

    CommandType type() const override { return kType; }

protected:
    const char* settingName() const override { return "pitch"; }
};

class RateCommand final : public ProsodyCommand {
public:
    static constexpr CommandType kType = CommandType::Rate;

    RateCommand(int offset, double multiplier) : ProsodyCommand(offset, multiplier) {}
    static std::shared_ptr<RateCommand> byOffset(int offset);
    static std::shared_ptr<RateCommand> byMultiplier(double multiplier);
    static std::shared_ptr<RateCommand> toDefault();

    CommandType type() const override { return kType; }

protected:
    const char* settingName() const override { return "rate"; }
};

class VolumeCommand final : public ProsodyCommand {
public:
    static constexpr CommandType kType = CommandType::Volume;

    VolumeCommand(int offset, double multiplier) : ProsodyCommand(offset, multiplier) {}
    static std::shared_ptr<VolumeCommand> byOffset(int offset);
    static std::shared_ptr<VolumeCommand> byMultiplier(double multiplier);
    static std::shared_ptr<VolumeCommand> toDefault();

    CommandType type() const override { return kType; }

protected:
    const char* settingName() const override { return "volume"; }
};

// IPA. `text` is spoken instead when the synthesizer cannot do the phonemes;
// an empty `text` means "skip this command entirely".
class PhonemeCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::Phoneme;

    explicit PhonemeCommand(std::string ipa, std::string text = {})
        : ipa_(std::move(ipa)), text_(std::move(text)) {}

    CommandType type() const override { return kType; }
    std::string repr() const override;
    bool isSynthCommand() const override { return true; }

    const std::string& ipa() const noexcept { return ipa_; }
    const std::string& text() const noexcept { return text_; }

private:
    std::string ipa_;
    std::string text_;
};

// ---------------------------------------------------------------------------
// Commands handled inside ALUS
// ---------------------------------------------------------------------------

// Ends the utterance here. Anything after it becomes a separate utterance.
class EndUtteranceCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::EndUtterance;

    CommandType type() const override { return kType; }
    std::string repr() const override { return "EndUtteranceCommand()"; }
};

class SuppressUnicodeNormalizationCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::SuppressUnicodeNormalization;

    explicit SuppressUnicodeNormalizationCommand(bool state = true) : state_(state) {}

    CommandType type() const override { return kType; }
    std::string repr() const override;

    bool state() const noexcept { return state_; }

private:
    bool state_;
};

// Runs a function when the speech reaches this point, on the main thread.
class CallbackCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::Callback;

    explicit CallbackCommand(std::function<void()> callback, std::string name = {})
        : callback_(std::move(callback)), name_(std::move(name)) {}

    CommandType type() const override { return kType; }
    std::string repr() const override;
    bool isCallback() const override { return true; }
    void run() override;

    const std::string& name() const noexcept { return name_; }

private:
    std::function<void()> callback_;
    std::string name_;
};

// A beep inside a speech sequence. Does NOT go through the synthesizer (§4.3):
// when its index is reached, `run()` plays it through alus::audio.
class BeepCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::Beep;

    BeepCommand(double hz, int milliseconds, int left = 50, int right = 50)
        : hz_(hz), milliseconds_(milliseconds), left_(left), right_(right) {}

    CommandType type() const override { return kType; }
    std::string repr() const override;
    bool isCallback() const override { return true; }
    void run() override;

    double hz() const noexcept { return hz_; }
    int milliseconds() const noexcept { return milliseconds_; }
    int left() const noexcept { return left_; }
    int right() const noexcept { return right_; }

private:
    double hz_;
    int milliseconds_;
    int left_;
    int right_;
};

class WaveFileCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::WaveFile;

    explicit WaveFileCommand(std::string fileName) : fileName_(std::move(fileName)) {}

    CommandType type() const override { return kType; }
    std::string repr() const override;
    bool isCallback() const override { return true; }
    void run() override;

    const std::string& fileName() const noexcept { return fileName_; }

private:
    std::string fileName_;
};

// A configuration profile trigger, bound to the configuration manager.
// `hasProfile()` is what NVDA consults before honouring the command: a trigger
// with no profile mapped to it is silently ignored.
class ProfileTrigger {
public:
    ProfileTrigger(config::Manager& manager, std::string spec);
    ~ProfileTrigger() = default;

    ProfileTrigger(const ProfileTrigger&) = delete;
    ProfileTrigger& operator=(const ProfileTrigger&) = delete;

    const std::string& spec() const noexcept { return spec_; }
    bool hasProfile() const;
    void enter();
    void exit();

private:
    config::Manager& manager_;
    std::string spec_;
};

// Applies (or stops applying) a profile trigger to subsequent speech. The
// manager latches on these: the synthesizer must FINISH the current utterance
// before the switch, or an utterance would start in voice A and end in voice B
// (§4.1).
class ProfileTriggerCommand final : public Command {
public:
    static constexpr CommandType kType = CommandType::ProfileTrigger;

    ProfileTriggerCommand(std::shared_ptr<ProfileTrigger> trigger, bool enter)
        : trigger_(std::move(trigger)), enter_(enter) {}

    CommandType type() const override { return kType; }
    std::string repr() const override;

    const std::shared_ptr<ProfileTrigger>& trigger() const noexcept { return trigger_; }
    bool isEnter() const noexcept { return enter_; }

private:
    std::shared_ptr<ProfileTrigger> trigger_;
    bool enter_;
};

// Lets the utterance containing it be thrown away before it is spoken.
//
// This is the mechanism behind the single most visible behaviour in §4.1: move
// quickly through a list and you hear the CURRENT item, not five stale ones.
// The utterance carries a command that is asked, immediately before being handed
// to the synthesizer, whether it is still relevant.
class CancellableSpeechCommand : public Command {
public:
    static constexpr CommandType kType = CommandType::Cancellable;

    CommandType type() const override { return kType; }
    std::string repr() const override;

    // Caches: once cancelled, always cancelled.
    bool isCancelled();
    void cancelUtterance() { cancelled_ = true; }

    // Set by the manager when the utterance is handed to the synthesizer.
    int utteranceIndex() const noexcept { return utteranceIndex_; }
    void setUtteranceIndex(int index) noexcept { utteranceIndex_ = index; }

protected:
    // "Is this speech still worth saying?" Implemented by the object model in
    // phase 4 (is it still the focus / an ancestor of it / the foreground).
    virtual bool checkIfValid() = 0;
    // Developer info for the log; may be empty.
    virtual std::string devInfo() const { return {}; }

private:
    bool cancelled_ = false;
    int utteranceIndex_ = 0;
};

// A CancellableSpeechCommand built from a predicate, for small callers and for
// tests.
class FunctionCancellableCommand final : public CancellableSpeechCommand {
public:
    explicit FunctionCancellableCommand(std::function<bool()> isValid, std::string name = {})
        : isValid_(std::move(isValid)), name_(std::move(name)) {}

protected:
    bool checkIfValid() override { return isValid_ == nullptr || isValid_(); }
    std::string devInfo() const override { return name_; }

private:
    std::function<bool()> isValid_;
    std::string name_;
};

// ---------------------------------------------------------------------------
// Construction shorthands. `std::make_shared<IndexCommand>(1)` reads badly
// inside a sequence literal; `index(1)` does not.
// ---------------------------------------------------------------------------

CommandPtr index(int value);
CommandPtr characterMode(bool state);
CommandPtr langChange(std::string language);
CommandPtr breakFor(int milliseconds);
CommandPtr endUtterance();
CommandPtr suppressUnicodeNormalization(bool state = true);
CommandPtr callback(std::function<void()> function, std::string name = {});
CommandPtr beep(double hz, int milliseconds, int left = 50, int right = 50);
CommandPtr waveFile(std::string fileName);
CommandPtr phoneme(std::string ipa, std::string text = {});

}  // namespace alus::speech
