// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The synthesizer settings ring: alus+control+arrows.
//
// WHAT IS BEING ASSERTED, and why it is worth a file of its own: this is the
// only way to change the voice or the speaking rate WITHOUT reading anything.
// Every other way -- the settings window's text entries, alus.ini, alus-cli
// --voice -- needs the user to already know the name of a voice the
// synthesizer has. A user whose synthesizer is too fast to follow, or speaking
// in the wrong language, cannot find that out. So the ring has to be right
// about three things, and each one is a test below:
//
//   * it must never offer a setting the driver cannot change, and never offer
//     a voice ring with nothing to step to,
//   * the steps are upstream's (5 normal, 20 large, clamped to 0-100), because
//     a user who knows NVDA counts presses,
//   * and every change must reach the configuration, or `alus+control+c`
//     would save a configuration the user cannot hear.
#include "alus/speech/settings_ring.hpp"

#include "alus/config/config.hpp"
#include "alus/speech/synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace alus;

namespace {

// A driver with as many voices as the test wants, and only the features the
// test wants. Written against ISynth directly rather than around NullSynth,
// which is `final` and offers exactly one voice -- and one voice is the case
// where the ring must do the LEAST, so it cannot be the only one tested.
//
// Every value is kept whether or not the driver "supports" it, which is
// ISynth's own contract for a parameter a driver cannot change (synth.hpp):
// the setting has to survive a driver change.
class FakeSynth final : public speech::ISynth {
public:
    explicit FakeSynth(std::vector<speech::VoiceInfo> voices = {}) : voices_(std::move(voices)) {
        if (!voices_.empty()) {
            voice_ = voices_.front().id;
        }
    }

    std::string_view name() const override { return "fake"; }
    bool initialize(std::string* /*error*/ = nullptr) override { return true; }
    void terminate() override {}

    std::set<speech::CommandType> supportedCommands() const override {
        return {speech::CommandType::Index};
    }

    bool supports(speech::SynthFeature feature) const override {
        return std::find(unsupported_.begin(), unsupported_.end(), feature) == unsupported_.end();
    }

    void speak(const speech::SpeechSequence& /*utterance*/) override {}
    void cancel() override {}
    void pause(bool /*switchOn*/) override {}

    std::vector<speech::VoiceInfo> availableVoices() const override { return voices_; }
    std::string voice() const override { return voice_; }

    bool setVoice(const std::string& id) override {
        const auto it = std::find_if(voices_.begin(), voices_.end(),
                                     [&id](const speech::VoiceInfo& v) { return v.id == id; });
        if (it == voices_.end()) {
            // A real driver refuses a voice it does not have, and the ring
            // must never ask for one.
            ++refusals_;
            return false;
        }
        voice_ = id;
        return true;
    }

    int rate() const override { return rate_; }
    void setRate(int value) override { rate_ = value; }
    int pitch() const override { return pitch_; }
    void setPitch(int value) override { pitch_ = value; }
    int volume() const override { return volume_; }
    void setVolume(int value) override { volume_ = value; }
    int inflection() const override { return inflection_; }
    void setInflection(int value) override { inflection_ = value; }

    void doesNotSupport(speech::SynthFeature feature) { unsupported_.push_back(feature); }
    std::size_t refusals() const { return refusals_; }
    // What a stale `voice=` in alus.ini leaves behind: a name no voice has.
    void forceVoice(std::string id) { voice_ = std::move(id); }

private:
    std::vector<speech::VoiceInfo> voices_;
    std::vector<speech::SynthFeature> unsupported_;
    std::string voice_;
    std::size_t refusals_ = 0;
    int rate_ = 50;
    int pitch_ = 50;
    int volume_ = 100;
    int inflection_ = 50;
};

std::vector<speech::VoiceInfo> threeVoices() {
    return {speech::VoiceInfo{"marija", "Marija", "sr", ""},
            speech::VoiceInfo{"alan", "Alan", "en", ""},
            speech::VoiceInfo{"klara", "Klara", "hr", ""}};
}

// A configuration that writes nowhere: the ring must put its values in, and
// no test may touch the developer's own alus.ini.
config::Manager scratchConfig() {
    return config::Manager{config::Paths::forDirectory(
        std::filesystem::temp_directory_path() / "alus-settings-ring-test")};
}

}  // namespace

TEST_CASE("the ring holds only what the driver can change", "[speech][ring]") {
    SECTION("no driver at all means an empty ring, not a crash") {
        speech::SettingsRing ring;
        CHECK(ring.empty());
        CHECK(ring.size() == 0);
        CHECK_FALSE(ring.current().has_value());
        CHECK(ring.currentName().empty());
        CHECK(ring.currentValue().empty());
        // Every mutator answers "nothing happened" rather than pretending.
        CHECK_FALSE(ring.next().has_value());
        CHECK_FALSE(ring.previous().has_value());
        CHECK_FALSE(ring.increase().has_value());
        CHECK_FALSE(ring.decrease().has_value());
        CHECK_FALSE(ring.increaseLarge().has_value());
        CHECK_FALSE(ring.decreaseLarge().has_value());
        CHECK_FALSE(ring.first().has_value());
        CHECK_FALSE(ring.last().has_value());
    }

    SECTION("one voice is not a ring entry: there would be nothing to step to") {
        FakeSynth synth{std::vector<speech::VoiceInfo>{
            speech::VoiceInfo{"only", "The only voice", "sr", ""}}};
        speech::SettingsRing ring{&synth};
        REQUIRE_FALSE(ring.empty());
        CHECK(ring.size() == 4);  // rate, pitch, inflection, volume
        for (std::size_t i = 0; i < ring.size(); ++i) {
            CHECK(ring.current() != speech::SettingsRing::Setting::Voice);
            ring.next();
        }
    }

    SECTION("a driver that cannot change a parameter does not offer it") {
        FakeSynth synth{threeVoices()};
        synth.doesNotSupport(speech::SynthFeature::Inflection);
        synth.doesNotSupport(speech::SynthFeature::Pitch);
        speech::SettingsRing ring{&synth};
        CHECK(ring.size() == 3);  // voice, rate, volume
        std::vector<speech::SettingsRing::Setting> seen;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            seen.push_back(*ring.current());
            ring.next();
        }
        CHECK(std::find(seen.begin(), seen.end(), speech::SettingsRing::Setting::Pitch) ==
              seen.end());
        CHECK(std::find(seen.begin(), seen.end(), speech::SettingsRing::Setting::Inflection) ==
              seen.end());
    }

    SECTION("the ring starts on the rate, as upstream does") {
        FakeSynth synth{threeVoices()};
        speech::SettingsRing ring{&synth};
        REQUIRE(ring.current().has_value());
        CHECK(*ring.current() == speech::SettingsRing::Setting::Rate);
    }

    SECTION("with no rate it starts on the first entry there is") {
        FakeSynth synth{threeVoices()};
        synth.doesNotSupport(speech::SynthFeature::Rate);
        speech::SettingsRing ring{&synth};
        REQUIRE(ring.current().has_value());
        CHECK(*ring.current() == speech::SettingsRing::Setting::Voice);
    }
}

TEST_CASE("the ring moves in both directions and wraps", "[speech][ring]") {
    FakeSynth synth{threeVoices()};
    speech::SettingsRing ring{&synth};
    REQUIRE(ring.size() == 5);

    // Voice, Rate, Pitch, Inflection, Volume -- the ring starts on Rate.
    const std::vector<speech::SettingsRing::Setting> forwards{
        speech::SettingsRing::Setting::Pitch, speech::SettingsRing::Setting::Inflection,
        speech::SettingsRing::Setting::Volume, speech::SettingsRing::Setting::Voice,
        speech::SettingsRing::Setting::Rate};
    for (const speech::SettingsRing::Setting expected : forwards) {
        const std::optional<std::string> name = ring.next();
        REQUIRE(name.has_value());
        CHECK(*ring.current() == expected);
        // The name is what the user hears, so it must never be empty.
        CHECK_FALSE(name->empty());
        CHECK(*name == ring.currentName());
    }

    // ...and back again, through the same five.
    for (std::size_t i = 0; i < forwards.size(); ++i) {
        REQUIRE(ring.previous().has_value());
    }
    CHECK(*ring.current() == speech::SettingsRing::Setting::Rate);
}

TEST_CASE("the steps are upstream's, and they clamp", "[speech][ring]") {
    FakeSynth synth{threeVoices()};
    config::Manager config = scratchConfig();
    speech::SettingsRing ring{&synth, &config};
    REQUIRE(*ring.current() == speech::SettingsRing::Setting::Rate);
    synth.setRate(50);

    SECTION("one press is five, one large press is twenty") {
        CHECK(ring.increase() == "55");
        CHECK(synth.rate() == 55);
        CHECK(ring.decrease() == "50");
        CHECK(ring.increaseLarge() == "70");
        CHECK(synth.rate() == 70);
        CHECK(ring.decreaseLarge() == "50");
    }

    SECTION("nothing leaves 0-100, however many times it is pressed") {
        for (int i = 0; i < 40; ++i) {
            ring.increaseLarge();
        }
        CHECK(synth.rate() == 100);
        CHECK(ring.currentValue() == "100");
        for (int i = 0; i < 40; ++i) {
            ring.decreaseLarge();
        }
        CHECK(synth.rate() == 0);
        CHECK(ring.currentValue() == "0");
    }

    SECTION("first and last go straight to the ends") {
        CHECK(ring.first() == "0");
        CHECK(ring.last() == "100");
    }

    SECTION("volume, pitch and inflection are the same arithmetic") {
        synth.setVolume(100);
        synth.setPitch(50);
        synth.setInflection(50);
        while (*ring.current() != speech::SettingsRing::Setting::Volume) {
            ring.next();
        }
        CHECK(ring.decrease() == "95");
        CHECK(synth.volume() == 95);
        while (*ring.current() != speech::SettingsRing::Setting::Pitch) {
            ring.next();
        }
        CHECK(ring.increase() == "55");
        CHECK(synth.pitch() == 55);
        while (*ring.current() != speech::SettingsRing::Setting::Inflection) {
            ring.next();
        }
        CHECK(ring.increase() == "55");
        CHECK(synth.inflection() == 55);
    }
}

TEST_CASE("the voice entry steps through the driver's own voices", "[speech][ring]") {
    FakeSynth synth{threeVoices()};
    config::Manager config = scratchConfig();
    speech::SettingsRing ring{&synth, &config};
    while (*ring.current() != speech::SettingsRing::Setting::Voice) {
        ring.next();
    }

    SECTION("the NAME is spoken, not the identifier the driver takes") {
        CHECK(ring.currentValue() == "Marija");
        CHECK(ring.increase() == "Alan");
        CHECK(synth.voice() == "alan");
        CHECK(ring.increase() == "Klara");
        CHECK(synth.voice() == "klara");
    }

    SECTION("the ends hold: the last voice does not wrap to the first") {
        // Deliberate, and upstream's behaviour: a numeric setting clamps, and
        // the voice IS a numeric setting here. A voice list that wrapped would
        // make "am I at the end?" unanswerable without counting.
        ring.last();
        CHECK(ring.currentValue() == "Klara");
        CHECK(ring.increase() == "Klara");
        ring.first();
        CHECK(ring.currentValue() == "Marija");
        CHECK(ring.decrease() == "Marija");
    }

    SECTION("the ring never asks the driver for a voice it does not have") {
        for (int i = 0; i < 20; ++i) {
            ring.increase();
        }
        for (int i = 0; i < 20; ++i) {
            ring.decrease();
        }
        CHECK(synth.refusals() == 0);
    }

    SECTION("a configured voice the driver dropped reads as the first one") {
        // Not an invented case: this is `voice=\s` in alus.ini, and a voice
        // uninstalled between two runs. The arrows have to keep working.
        synth.forceVoice(" ");  // what the configuration left behind
        speech::SettingsRing fresh{&synth, &config};
        while (*fresh.current() != speech::SettingsRing::Setting::Voice) {
            fresh.next();
        }
        CHECK(fresh.increase() == "Alan");
    }
}

TEST_CASE("every change the ring makes reaches the configuration", "[speech][ring]") {
    FakeSynth synth{threeVoices()};
    config::Manager config = scratchConfig();
    speech::SettingsRing ring{&synth, &config};
    synth.setRate(50);

    SECTION("a number is written under its own key") {
        ring.increase();
        CHECK(config.getInt("speech", "rate", -1) == 55);
        while (*ring.current() != speech::SettingsRing::Setting::Volume) {
            ring.next();
        }
        synth.setVolume(80);
        ring.decrease();
        CHECK(config.getInt("speech", "volume", -1) == 75);
    }

    SECTION("the voice is written as the IDENTIFIER, which is what reload takes") {
        while (*ring.current() != speech::SettingsRing::Setting::Voice) {
            ring.next();
        }
        ring.increase();
        CHECK(config.getString("speech", "voice") == "alan");
    }

    SECTION("with no configuration the ring still works, it just forgets") {
        speech::SettingsRing unsaved{&synth};
        CHECK(unsaved.increase().has_value());
    }
}

TEST_CASE("changing the driver keeps the selected setting", "[speech][ring]") {
    FakeSynth first{threeVoices()};
    speech::SettingsRing ring{&first};
    while (*ring.current() != speech::SettingsRing::Setting::Volume) {
        ring.next();
    }

    SECTION("the same setting is selected on the new driver") {
        FakeSynth second{threeVoices()};
        ring.setSynth(&second);
        REQUIRE(ring.current().has_value());
        CHECK(*ring.current() == speech::SettingsRing::Setting::Volume);
        CHECK(ring.synth() == &second);
    }

    SECTION("a driver without that setting falls back to the rate") {
        FakeSynth second{threeVoices()};
        second.doesNotSupport(speech::SynthFeature::Volume);
        ring.setSynth(&second);
        REQUIRE(ring.current().has_value());
        CHECK(*ring.current() == speech::SettingsRing::Setting::Rate);
    }

    SECTION("no driver empties the ring instead of pointing at the old one") {
        ring.setSynth(nullptr);
        CHECK(ring.empty());
        CHECK_FALSE(ring.increase().has_value());
    }
}
