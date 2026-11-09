// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/ssip_synth.hpp"

#include "alus/core/log.hpp"

#include <libspeechd.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <format>
#include <utility>

namespace alus::speech {

namespace {

SPDConnection* conn(void* pointer) { return static_cast<SPDConnection*>(pointer); }

// libspeechd's callbacks are plain C function pointers with no user data, so
// the instance has to be reachable from a file-static slot. A screen reader has
// exactly one connection to speech-dispatcher, which is why this is a
// limitation on paper only — and it is enforced rather than assumed.
std::mutex g_activeMutex;
SsipSynth* g_active = nullptr;

void setActive(SsipSynth* synth) {
    const std::lock_guard lock{g_activeMutex};
    if (synth != nullptr && g_active != nullptr && g_active != synth) {
        ALUS_WARNING("a second SsipSynth was connected; notifications go to the newest one");
    }
    g_active = synth;
}

SsipSynth* active() {
    const std::lock_guard lock{g_activeMutex};
    return g_active;
}

void onIndexMarkCallback(std::size_t /*messageId*/, std::size_t /*clientId*/,
                         SPDNotificationType /*state*/, char* indexMark) {
    if (SsipSynth* synth = active()) {
        synth->onIndexMark(indexMark);
    }
    // libspeechd FREES the mark name itself after this returns (verified with
    // valgrind against libspeechd 0.12: freeing it here is a double free that
    // aborts the process on the reader thread). The callback must only read it.
}

void onEndCallback(std::size_t /*messageId*/, std::size_t /*clientId*/,
                   SPDNotificationType /*state*/) {
    if (SsipSynth* synth = active()) {
        synth->onEnd();
    }
}

void onCancelCallback(std::size_t /*messageId*/, std::size_t /*clientId*/,
                      SPDNotificationType /*state*/) {
    if (SsipSynth* synth = active()) {
        synth->onCancel();
    }
}

void onBeginCallback(std::size_t /*messageId*/, std::size_t /*clientId*/,
                     SPDNotificationType /*state*/) {
    if (SsipSynth* synth = active()) {
        synth->onBegin();
    }
}

// ALUS uses 0..100 for every parameter; SSIP uses -100..100.
int toSsip(int value) { return std::clamp(value, 0, 100) * 2 - 100; }

std::string escapeXml(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        switch (character) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out += character;
                break;
        }
    }
    return out;
}

// Builds the SSML for one utterance, splitting it into several SSIP messages
// where an index mark would otherwise land right after sentence-final
// punctuation (findings N15/N16).
class SsmlBuilder {
public:
    SsmlBuilder(int baseRate, int basePitch, int baseVolume)
        : baseRate_(baseRate), basePitch_(basePitch), baseVolume_(baseVolume) {}

    void addText(std::string_view text) {
        if (text.empty()) {
            return;
        }
        openIfNeeded();
        body_ += escapeXml(text);
        plainTail_.append(text);
    }

    void addMark(int index) {
        openIfNeeded();
        // Measure 3 from the header: a mark that would follow a sentence end
        // goes at the very end of its own message instead of inside one.
        body_ += std::format("<mark name=\"{}\"/>", index);
        marks_.push_back(index);
        if (SsipSynth::markWouldFollowSentenceEnd(plainTail_)) {
            endMessage();
        }
        plainTail_.clear();
    }

    void addBreak(int milliseconds) {
        if (milliseconds <= 0) {
            return;
        }
        openIfNeeded();
        body_ += std::format("<break time=\"{}ms\"/>", milliseconds);
        plainTail_.clear();
    }

    void addPhoneme(const std::string& ipa, const std::string& text) {
        openIfNeeded();
        if (ipa.empty()) {
            addText(text);
            return;
        }
        body_ += std::format("<phoneme ph=\"{}\">{}</phoneme>", escapeXml(ipa),
                             escapeXml(text));
        plainTail_.clear();
    }

    void setProsody(const std::shared_ptr<ProsodyCommand>& command, CommandType type) {
        int base = 0;
        switch (type) {
            case CommandType::Rate:
                base = baseRate_;
                break;
            case CommandType::Pitch:
                base = basePitch_;
                break;
            case CommandType::Volume:
                base = baseVolume_;
                break;
            default:
                return;
        }
        // NVDA's prosody commands are relative to the user's configured value
        // (§4.3), and SSML wants a percentage of the current voice, so the
        // relative change is what goes into the attribute.
        const double multiplier = command->multiplier(base);
        const int percent = static_cast<int>(multiplier * 100.0);
        switch (type) {
            case CommandType::Rate:
                ratePercent_ = command->isDefault() ? 100 : percent;
                break;
            case CommandType::Pitch:
                pitchPercent_ = command->isDefault() ? 100 : percent;
                break;
            case CommandType::Volume:
                volumePercent_ = command->isDefault() ? 100 : percent;
                break;
            default:
                return;
        }
        reopenProsody();
    }

    void setLanguage(const std::string& language) {
        language_ = language;
        reopenVoice();
    }

    void setCharacterMode(bool state) {
        characterMode_ = state;
        reopenSayAs();
    }

    std::vector<SsipMessage> finish() {
        endMessage();
        return std::move(messages_);
    }

private:
    void openIfNeeded() {
        if (open_) {
            return;
        }
        open_ = true;
        body_ = "<speak>";
        if (!language_.empty()) {
            body_ += std::format("<voice xml:lang=\"{}\">", escapeXml(language_));
            voiceOpen_ = true;
        }
        if (ratePercent_ != 100 || pitchPercent_ != 100 || volumePercent_ != 100) {
            openProsodyElement();
        }
        if (characterMode_) {
            body_ += "<say-as interpret-as=\"characters\">";
            sayAsOpen_ = true;
        }
    }

    void openProsodyElement() {
        body_ += std::format("<prosody rate=\"{}%\" pitch=\"{}%\" volume=\"{}%\">",
                             ratePercent_, pitchPercent_, volumePercent_);
        prosodyOpen_ = true;
    }

    void reopenProsody() {
        if (!open_) {
            return;  // the state is applied when the message opens
        }
        closeSayAs();
        if (prosodyOpen_) {
            body_ += "</prosody>";
            prosodyOpen_ = false;
        }
        if (ratePercent_ != 100 || pitchPercent_ != 100 || volumePercent_ != 100) {
            openProsodyElement();
        }
        reopenSayAs();
    }

    void reopenVoice() {
        if (!open_) {
            return;
        }
        closeSayAs();
        if (prosodyOpen_) {
            body_ += "</prosody>";
            prosodyOpen_ = false;
        }
        if (voiceOpen_) {
            body_ += "</voice>";
            voiceOpen_ = false;
        }
        if (!language_.empty()) {
            body_ += std::format("<voice xml:lang=\"{}\">", escapeXml(language_));
            voiceOpen_ = true;
        }
        if (ratePercent_ != 100 || pitchPercent_ != 100 || volumePercent_ != 100) {
            openProsodyElement();
        }
        reopenSayAs();
    }

    void closeSayAs() {
        if (sayAsOpen_) {
            body_ += "</say-as>";
            sayAsOpen_ = false;
        }
    }

    void reopenSayAs() {
        closeSayAs();
        if (characterMode_ && open_) {
            body_ += "<say-as interpret-as=\"characters\">";
            sayAsOpen_ = true;
        }
    }

    void endMessage() {
        if (!open_) {
            return;
        }
        closeSayAs();
        if (prosodyOpen_) {
            body_ += "</prosody>";
            prosodyOpen_ = false;
        }
        if (voiceOpen_) {
            body_ += "</voice>";
            voiceOpen_ = false;
        }
        body_ += "</speak>";
        messages_.push_back(SsipMessage{std::move(body_), std::move(marks_)});
        body_.clear();
        marks_.clear();
        plainTail_.clear();
        open_ = false;
    }

    int baseRate_;
    int basePitch_;
    int baseVolume_;
    int ratePercent_ = 100;
    int pitchPercent_ = 100;
    int volumePercent_ = 100;
    std::string language_;
    bool characterMode_ = false;

    bool open_ = false;
    bool voiceOpen_ = false;
    bool prosodyOpen_ = false;
    bool sayAsOpen_ = false;
    std::string body_;
    // The text since the last mark or element, with no markup: what the
    // sentence-end test looks at.
    std::string plainTail_;
    std::vector<int> marks_;
    std::vector<SsipMessage> messages_;
};

}  // namespace

bool SsipSynth::markWouldFollowSentenceEnd(std::string_view text) {
    // The shape that loses a mark: "... some long clause. <mark/>". Trailing
    // whitespace and one closing quote or bracket are allowed between the
    // punctuation and the mark.
    std::size_t end = text.size();
    while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' ||
                       text[end - 1] == '\r')) {
        --end;
    }
    if (end == 0) {
        return false;
    }
    static constexpr std::string_view kClosers[] = {"\"", "'", ")", "”", "’"};
    for (const std::string_view closer : kClosers) {
        if (end >= closer.size() && text.substr(end - closer.size(), closer.size()) == closer) {
            end -= closer.size();
            break;
        }
    }
    if (end == 0) {
        return false;
    }
    const char last = text[end - 1];
    return last == '.' || last == '!' || last == '?';
}

std::vector<SsipMessage> SsipSynth::buildMessages(const SpeechSequence& utterance, int baseRate,
                                                  int basePitch, int baseVolume) {
    SsmlBuilder builder{baseRate, basePitch, baseVolume};
    for (const SpeechItem& item : utterance) {
        if (item.isText()) {
            builder.addText(item.text());
            continue;
        }
        const CommandPtr& command = item.command();
        if (command == nullptr) {
            continue;
        }
        switch (command->type()) {
            case CommandType::Index:
                builder.addMark(as<IndexCommand>(command)->index());
                break;
            case CommandType::Break:
                builder.addBreak(as<BreakCommand>(command)->milliseconds());
                break;
            case CommandType::Phoneme: {
                const auto phonemeCommand = as<PhonemeCommand>(command);
                builder.addPhoneme(phonemeCommand->ipa(), phonemeCommand->text());
                break;
            }
            case CommandType::LangChange:
                builder.setLanguage(as<LangChangeCommand>(command)->language());
                break;
            case CommandType::CharacterMode:
                builder.setCharacterMode(as<CharacterModeCommand>(command)->state());
                break;
            case CommandType::Rate:
            case CommandType::Pitch:
            case CommandType::Volume: {
                const auto prosody = std::static_pointer_cast<ProsodyCommand>(command);
                builder.setProsody(prosody, command->type());
                break;
            }
            default:
                // Everything else is handled inside ALUS and never reaches a
                // driver (§4.3).
                break;
        }
    }
    return builder.finish();
}

SsipSynth::SsipSynth(std::string clientName, std::string connectionName)
    : clientName_(std::move(clientName)), connectionName_(std::move(connectionName)) {}

SsipSynth::~SsipSynth() { terminate(); }

bool SsipSynth::initialize(std::string* error) {
    terminate();

    char* openError = nullptr;
    SPDConnection* connection =
        spd_open2(clientName_.c_str(), connectionName_.c_str(), nullptr, SPD_MODE_THREADED,
                  nullptr, 1 /* autospawn */, &openError);
    if (connection == nullptr) {
        const std::string message =
            openError != nullptr ? openError : "cannot connect to speech-dispatcher";
        if (openError != nullptr) {
            std::free(openError);
        }
        if (error != nullptr) {
            *error = message;
        }
        ALUS_ERROR("speech-dispatcher: {}", message);
        return false;
    }
    if (openError != nullptr) {
        std::free(openError);
    }

    connection_ = connection;
    setActive(this);

    connection->callback_im = &onIndexMarkCallback;
    connection->callback_end = &onEndCallback;
    connection->callback_cancel = &onCancelCallback;
    connection->callback_begin = &onBeginCallback;

    // SSML mode, so marks, breaks and prosody can be expressed at all.
    if (spd_set_data_mode(connection, SPD_DATA_SSML) < 0) {
        if (error != nullptr) {
            *error = "the server refused SSML mode";
        }
        terminate();
        return false;
    }
    // Index marks are the only notification ALUS depends on; the others are
    // reported because they are cheap and useful in the log, and `end` serves
    // as the lost-mark watchdog in the manager.
    if (spd_set_notification_on(connection, SPD_INDEX_MARKS) < 0) {
        if (error != nullptr) {
            *error = "the server refused index mark notifications";
        }
        terminate();
        return false;
    }
    spd_set_notification_on(connection, SPD_END);
    spd_set_notification_on(connection, SPD_CANCEL);
    spd_set_notification_on(connection, SPD_BEGIN);

    if (!preferredModule_.empty()) {
        // Before the liveness check and before the timing judgement below, so
        // that both describe the module the user actually gets.
        if (spd_set_output_module(connection, preferredModule_.c_str()) < 0) {
            ALUS_WARNING("speech-dispatcher refused the output module {}", preferredModule_);
        }
    }
    char* module = spd_get_output_module(connection);
    if (module != nullptr) {
        outputModule_ = module;
        std::free(module);
    }

    // THE LIVENESS CHECK. A connection that cannot list its voices is not a
    // working connection, however successfully it opened.
    SPDVoice** voices = spd_list_synthesis_voices(connection);
    if (voices == nullptr) {
        if (error != nullptr) {
            *error = "the server returned no voices";
        }
        ALUS_ERROR("speech-dispatcher answered but has no voices; refusing to use it");
        terminate();
        return false;
    }
    std::size_t count = 0;
    while (voices[count] != nullptr) {
        ++count;
    }
    free_spd_voices(voices);

    applyAllParameters();
    ALUS_INFO("speech-dispatcher connected: module {}, {} voices",
              outputModule_.empty() ? "unknown" : outputModule_, count);
    if (!timingIsGuaranteed()) {
        // Finding N17: the budgets are a property of the module.
        ALUS_WARNING(
            "the speech-dispatcher module is {}, whose latency ALUS has not measured; "
            "responsiveness is not guaranteed",
            outputModule_.empty() ? "unknown" : outputModule_);
    }
    return true;
}

void SsipSynth::terminate() {
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        connection = std::exchange(connection_, nullptr);
    }
    if (connection == nullptr) {
        return;
    }
    {
        const std::lock_guard lock{g_activeMutex};
        if (g_active == this) {
            g_active = nullptr;
        }
    }
    // Stop first: closing a connection with speech in flight leaves the server
    // talking to nobody.
    spd_cancel(conn(connection));
    spd_close(conn(connection));
}

bool SsipSynth::isConnected() const {
    const std::lock_guard lock{mutex_};
    return connection_ != nullptr;
}

std::set<CommandType> SsipSynth::supportedCommands() const {
    // Index is mandatory (§4.5). Break depends on the module (risk R13), and is
    // reported as supported only when the module honours it.
    std::set<CommandType> commands{CommandType::Index, CommandType::CharacterMode,
                                   CommandType::LangChange, CommandType::Pitch,
                                   CommandType::Rate,       CommandType::Volume,
                                   CommandType::Phoneme};
    if (supports(SynthFeature::Breaks)) {
        commands.insert(CommandType::Break);
    }
    return commands;
}

bool SsipSynth::supports(SynthFeature feature) const {
    switch (feature) {
        case SynthFeature::Rate:
        case SynthFeature::Pitch:
        case SynthFeature::Volume:
        case SynthFeature::Voice:
        case SynthFeature::Language:
            return true;
        case SynthFeature::Inflection:
            return true;  // SSIP pitch range
        case SynthFeature::Breaks:
            // Finding N17: rhvoice ignores <break time=...>. Anything ALUS has
            // not measured is assumed not to honour it.
            return outputModule_ == "espeak-ng" || outputModule_ == "espeak";
    }
    return false;
}

bool SsipSynth::timingIsGuaranteed() const {
    return outputModule_ == "espeak-ng" || outputModule_ == "espeak";
}

void SsipSynth::speak(const SpeechSequence& utterance) {
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        connection = connection_;
    }
    if (connection == nullptr) {
        return;
    }
    const std::vector<SsipMessage> messages =
        buildMessages(utterance, rate_, pitch_, volume_);
    for (const SsipMessage& message : messages) {
        ALUS_IO("ssip say: {}", message.ssml);
        // One SSIP priority for everything: ALUS owns the queue, the server
        // does not (risk R4). The messages of one utterance are sent back to
        // back so the server chains them without a round trip through ALUS.
        if (spd_say(conn(connection), SPD_MESSAGE, message.ssml.c_str()) < 0) {
            ALUS_DEBUG_WARNING("speech-dispatcher refused a message");
        }
    }
}

void SsipSynth::cancel() {
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        connection = connection_;
    }
    if (connection == nullptr) {
        return;
    }
    // spd_cancel, not spd_stop: everything ALUS queued goes away, including
    // messages the server has not started. Finding N11: the audio stops before
    // the acknowledgement arrives, so there is nothing to wait for here.
    spd_cancel(conn(connection));
}

void SsipSynth::pause(bool switchOn) {
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        connection = connection_;
    }
    if (connection == nullptr) {
        return;
    }
    if (switchOn) {
        spd_pause(conn(connection));
    } else {
        spd_resume(conn(connection));
    }
}

std::vector<VoiceInfo> SsipSynth::availableVoices() const {
    std::vector<VoiceInfo> out;
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        connection = connection_;
    }
    if (connection == nullptr) {
        return out;
    }
    SPDVoice** voices = spd_list_synthesis_voices(conn(connection));
    if (voices == nullptr) {
        return out;
    }
    for (std::size_t i = 0; voices[i] != nullptr; ++i) {
        VoiceInfo info;
        info.id = voices[i]->name != nullptr ? voices[i]->name : "";
        info.name = info.id;
        info.language = voices[i]->language != nullptr ? voices[i]->language : "";
        info.variant = voices[i]->variant != nullptr ? voices[i]->variant : "";
        out.push_back(std::move(info));
    }
    free_spd_voices(voices);
    return out;
}

std::string SsipSynth::voice() const {
    const std::lock_guard lock{mutex_};
    return voice_;
}

bool SsipSynth::setVoice(const std::string& id) {
    void* connection = nullptr;
    std::string previous;
    {
        const std::lock_guard lock{mutex_};
        connection = connection_;
        previous = voice_;
        voice_ = id;
    }
    if (connection == nullptr || id.empty()) {
        return false;
    }
    if (spd_set_synthesis_voice(conn(connection), id.c_str()) >= 0) {
        return true;
    }
    // THE NAME IS PUT BACK. It was a bug that it was not: a `voice=` the
    // server does not know (a voice uninstalled since the configuration was
    // written, or the `voice=\s` -- one space -- that the settings window's
    // empty text entry used to write) left `voice()` reporting a voice that
    // does not exist. Everything downstream then believed it: the voice
    // dictionary was looked up under that name, the settings ring reported it,
    // and a reconnection tried to set it again. The only thing that did NOT
    // happen was anybody being told.
    {
        const std::lock_guard lock{mutex_};
        voice_ = previous;
    }
    ALUS_WARNING("speech-dispatcher refused the voice '{}'; keeping '{}'", id, previous);
    return false;
}

void SsipSynth::setRate(int value) {
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        rate_ = std::clamp(value, 0, 100);
        connection = connection_;
    }
    if (connection != nullptr) {
        spd_set_voice_rate(conn(connection), toSsip(rate_));
    }
}

void SsipSynth::setPitch(int value) {
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        pitch_ = std::clamp(value, 0, 100);
        connection = connection_;
    }
    if (connection != nullptr) {
        spd_set_voice_pitch(conn(connection), toSsip(pitch_));
    }
}

void SsipSynth::setVolume(int value) {
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        volume_ = std::clamp(value, 0, 100);
        connection = connection_;
    }
    if (connection != nullptr) {
        spd_set_volume(conn(connection), toSsip(volume_));
    }
}

void SsipSynth::setInflection(int value) {
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        inflection_ = std::clamp(value, 0, 100);
        connection = connection_;
    }
    if (connection != nullptr) {
        spd_set_voice_pitch_range(conn(connection), toSsip(inflection_));
    }
}

bool SsipSynth::setOutputModule(const std::string& module) {
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        connection = connection_;
    }
    if (connection == nullptr) {
        return false;
    }
    if (spd_set_output_module(conn(connection), module.c_str()) < 0) {
        return false;
    }
    outputModule_ = module;
    return true;
}

std::vector<std::string> SsipSynth::availableModules() const {
    std::vector<std::string> out;
    void* connection = nullptr;
    {
        const std::lock_guard lock{mutex_};
        connection = connection_;
    }
    if (connection == nullptr) {
        return out;
    }
    char** modules = spd_list_modules(conn(connection));
    if (modules == nullptr) {
        return out;
    }
    for (std::size_t i = 0; modules[i] != nullptr; ++i) {
        out.emplace_back(modules[i]);
        std::free(modules[i]);
    }
    std::free(modules);
    return out;
}

void SsipSynth::applyAllParameters() {
    setRate(rate_);
    setPitch(pitch_);
    setVolume(volume_);
    setInflection(inflection_);
    if (!voice_.empty()) {
        setVoice(voice_);
    }
}

// ---------------------------------------------------------------------------
// Notifications. ALL OF THESE RUN ON libspeechd's READER THREAD.
// ---------------------------------------------------------------------------

void SsipSynth::onIndexMark(const char* mark) {
    if (mark == nullptr) {
        return;
    }
    const std::string_view name{mark};
    int index = 0;
    const auto result = std::from_chars(name.data(), name.data() + name.size(), index);
    if (result.ec != std::errc{}) {
        // speech-dispatcher sends its own marks too (for instance "__spd_end").
        ALUS_DEBUG("ignoring the non-numeric index mark {}", std::string{name});
        return;
    }
    ALUS_IO("ssip index mark {}", index);
    indexReached.notify(this, index);
}

void SsipSynth::onEnd() {
    // NOT an audio event: finding N12 measured it ~294 ms after the last
    // audible sample. The manager treats it as a latch and a watchdog.
    ALUS_IO("ssip end");
    doneSpeaking.notify(this);
}

void SsipSynth::onCancel() { ALUS_IO("ssip cancel acknowledged"); }

void SsipSynth::onBegin() {
    // Finding N10: this arrives ~7.5 ms BEFORE the first sample. It is a
    // protocol acknowledgement, so nothing may be synchronised to it.
    ALUS_IO("ssip begin");
}

}  // namespace alus::speech
