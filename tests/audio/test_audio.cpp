// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The audio path: WavePlayer, tones and WAVE files.
//
// The part that matters for later phases is `feed(data, size, onDone)` WITH ITS
// PER-BLOCK CALLBACK (01-ARHITEKTURA.md §4.6). That callback is how a
// synthesizer which hands ALUS raw audio reports sample-accurate positions, and
// it is the reason EspeakDirectSynth can exist in v1.1 (spikes/RESULTS.md S2b:
// a mark accurate to 0.000 ms, against a persistent 40 ms shift over SSIP).
#include "alus/audio/wave_player.hpp"

#include "../support/loop_fixture.hpp"
#include "alus/audio/tones.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using alus::audio::Format;
using alus::audio::MemorySink;
using alus::audio::NullSink;
using alus::audio::WaveData;
using alus::audio::WavePlayer;
using alus::testing::LoopFixture;
using alus::testing::QuietLog;
using namespace std::chrono_literals;

namespace {

constexpr Format kFormat{2, 44100, 16};

std::vector<std::uint8_t> block(std::size_t frames, std::int16_t value = 1000) {
    std::vector<std::uint8_t> data(frames * kFormat.bytesPerFrame());
    auto* samples = reinterpret_cast<std::int16_t*>(data.data());
    for (std::size_t i = 0; i < frames * kFormat.channels; ++i) {
        samples[i] = value;
    }
    return data;
}

}  // namespace

TEST_CASE("a format converts bytes to time", "[audio][player]") {
    CHECK(kFormat.bytesPerFrame() == 4);
    CHECK(kFormat.durationOf(44100 * 4) == 1s);
    CHECK(kFormat.durationOf(0) == 0us);
    // A format with nonsense in it must not divide by zero.
    const Format broken{0, 0, 16};
    CHECK(broken.durationOf(100) == 0us);
}

TEST_CASE("fed audio reaches the device", "[audio][player]") {
    auto sink = std::make_unique<MemorySink>();
    MemorySink* raw = sink.get();
    WavePlayer player{kFormat, std::move(sink)};

    const std::vector<std::uint8_t> data = block(441);
    player.feed(data);
    player.sync();
    CHECK(raw->size() == data.size());
    CHECK(raw->data() == data);
    CHECK(player.blocksPlayed() == 1);
    CHECK(player.queuedBytes() == 0);
}

TEST_CASE("the per-block callback runs when the block has played", "[audio][player]") {
    auto sink = std::make_unique<MemorySink>();
    WavePlayer player{kFormat, std::move(sink)};

    std::atomic<int> done{0};
    player.feed(block(441), [&done] { ++done; });
    player.feed(block(441), [&done] { ++done; });
    player.sync();
    // Without a loop the callbacks run on the writer thread, which is what a
    // synthesizer driver wants; the daemon passes the loop and gets them on the
    // main thread instead.
    CHECK(done.load() == 2);
}

TEST_CASE("the callback is marshalled onto the loop when there is one",
          "[audio][player]") {
    LoopFixture fixture;
    auto sink = std::make_unique<MemorySink>();
    WavePlayer player{kFormat, std::move(sink), &fixture.loop()};

    std::thread::id ranOn;
    bool ran = false;
    player.feed(block(441), [&] {
        ran = true;
        ranOn = std::this_thread::get_id();
    });
    player.sync();
    REQUIRE(fixture.pumpUntil([&] { return ran; }));
    // A BeepCommand's callback must not run on the audio thread.
    CHECK(ranOn == std::this_thread::get_id());
}

TEST_CASE("feeding nothing still completes the callback", "[audio][player]") {
    auto sink = std::make_unique<MemorySink>();
    WavePlayer player{kFormat, std::move(sink)};
    bool ran = false;
    player.feed(nullptr, 0, [&ran] { ran = true; });
    // A caller that chains on the callback must not hang on an empty block.
    CHECK(ran);
}

TEST_CASE("stop discards the queue and the pending callbacks", "[audio][player]") {
    auto sink = std::make_unique<MemorySink>();
    MemorySink* raw = sink.get();
    WavePlayer player{kFormat, std::move(sink)};

    player.pause(true);  // hold the writer so the queue builds up
    std::atomic<int> done{0};
    for (int i = 0; i < 5; ++i) {
        player.feed(block(441), [&done] { ++done; });
    }
    CHECK(player.queuedBytes() > 0);

    player.stop();
    CHECK(player.queuedBytes() == 0);
    CHECK(raw->flushes() >= 1);
    player.pause(false);
    player.sync();
    // NVDA drops the callbacks of discarded blocks, and so does this: they
    // describe audio that will never be heard.
    CHECK(done.load() == 0);
}

TEST_CASE("pause holds the writer and resume lets it go", "[audio][player]") {
    auto sink = std::make_unique<MemorySink>();
    MemorySink* raw = sink.get();
    WavePlayer player{kFormat, std::move(sink)};

    player.pause(true);
    CHECK(player.isPaused());
    player.feed(block(441));
    std::this_thread::sleep_for(20ms);
    CHECK(raw->size() == 0);

    player.pause(false);
    player.sync();
    CHECK(raw->size() > 0);
}

TEST_CASE("the software volume scales the samples", "[audio][player]") {
    auto sink = std::make_unique<MemorySink>();
    MemorySink* raw = sink.get();
    WavePlayer player{kFormat, std::move(sink)};

    // pa_simple has no per-stream volume, and a screen reader must never touch
    // the user's global volume, so this is applied to the samples.
    player.setVolume(50, 0);
    player.feed(block(2, 1000));
    player.sync();

    const std::vector<std::uint8_t> written = raw->data();
    REQUIRE(written.size() == 2 * kFormat.bytesPerFrame());
    const auto* samples = reinterpret_cast<const std::int16_t*>(written.data());
    CHECK(samples[0] == 500);  // left at 50%
    CHECK(samples[1] == 0);    // right muted
}

TEST_CASE("a device that cannot be opened does not hang the player",
          "[audio][player]") {
    QuietLog quiet;
    // A sink that always fails to open: the machine has no sound server.
    class FailingSink final : public alus::audio::ISink {
    public:
        std::string_view name() const override { return "failing"; }
        bool open(const Format&, std::string_view, std::string* error) override {
            if (error != nullptr) {
                *error = "no device";
            }
            return false;
        }
        void close() override {}
        bool isOpen() const override { return false; }
        bool write(const std::uint8_t*, std::size_t) override { return false; }
        void drain() override {}
        void flush() override {}
        std::chrono::microseconds latency() override { return 0us; }
    };

    WavePlayer player{kFormat, std::make_unique<FailingSink>()};
    player.feed(block(441));
    player.sync();
    // The audio is dropped and the screen reader carries on: speech goes
    // through speech-dispatcher either way.
    CHECK(player.queuedBytes() == 0);
}

TEST_CASE("a tone has the requested length and fades at both ends", "[audio][tones]") {
    const std::vector<std::uint8_t> tone = alus::audio::generateBeep(kFormat, 440.0, 100);
    const std::size_t frames = tone.size() / kFormat.bytesPerFrame();
    // 100 ms at 44100 Hz.
    CHECK(frames == 4410);

    const auto* samples = reinterpret_cast<const std::int16_t*>(tone.data());
    // The ramp exists so a progress bar does not click on every tick.
    CHECK(std::abs(samples[0]) < 1000);
    CHECK(std::abs(samples[(frames - 1) * kFormat.channels]) < 1000);
    // And the middle is a real signal.
    std::int16_t peak = 0;
    for (std::size_t i = frames / 4; i < 3 * frames / 4; ++i) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(samples[i * 2])));
    }
    CHECK(peak > 10000);
}

TEST_CASE("the stereo balance of a tone is honoured", "[audio][tones]") {
    const std::vector<std::uint8_t> tone = alus::audio::generateBeep(kFormat, 440.0, 50, 100, 0);
    const auto* samples = reinterpret_cast<const std::int16_t*>(tone.data());
    const std::size_t frames = tone.size() / kFormat.bytesPerFrame();
    std::int16_t rightPeak = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        rightPeak = std::max(rightPeak, static_cast<std::int16_t>(std::abs(samples[i * 2 + 1])));
    }
    CHECK(rightPeak == 0);
}

TEST_CASE("nonsense tone parameters produce nothing", "[audio][tones]") {
    CHECK(alus::audio::generateBeep(kFormat, 440.0, 0).empty());
    CHECK(alus::audio::generateBeep(kFormat, 0.0, 100).empty());
    CHECK(alus::audio::generateBeep(Format{2, 44100, 8}, 440.0, 100).empty());
}

TEST_CASE("a beep can be refused by a handler", "[audio][tones]") {
    LoopFixture fixture;
    auto speech = std::make_unique<NullSink>();
    auto sounds = std::make_unique<MemorySink>();
    MemorySink* raw = sounds.get();
    alus::audio::initialize(&fixture.loop(), std::move(speech), std::move(sounds));

    bool allow = false;
    const auto handle = alus::audio::decideBeep().add(
        [&](double, int, int, int, bool) { return allow; }, "test");
    alus::audio::beep(440.0, 20);
    alus::audio::soundsPlayer()->sync();
    CHECK(raw->size() == 0);

    allow = true;
    alus::audio::beep(440.0, 20);
    alus::audio::soundsPlayer()->sync();
    CHECK(raw->size() > 0);
    alus::audio::terminate();
}

TEST_CASE("a beep replaces the previous one instead of queueing", "[audio][tones]") {
    LoopFixture fixture;
    auto sounds = std::make_unique<MemorySink>();
    alus::audio::initialize(&fixture.loop(), std::make_unique<NullSink>(), std::move(sounds));
    // Progress bar ticks must not pile up into a chord.
    alus::audio::beep(440.0, 500);
    alus::audio::beep(880.0, 20);
    alus::audio::soundsPlayer()->sync();
    CHECK(alus::audio::soundsPlayer()->queuedBytes() == 0);
    alus::audio::terminate();
}

TEST_CASE("a WAVE file is read and played", "[audio][wave]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "alus-test-sound.wav";
    std::filesystem::remove(path);

    // A 16-bit stereo PCM file written by hand, so the parser is tested
    // against the format rather than against another copy of itself.
    const std::vector<std::uint8_t> samples = block(100, 2000);
    const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size());
    const std::uint32_t riffSize = 36 + dataSize;
    std::vector<std::uint8_t> file;
    const auto put32 = [&file](std::uint32_t value) {
        file.push_back(static_cast<std::uint8_t>(value & 0xFF));
        file.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        file.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        file.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    };
    const auto put16 = [&file](std::uint16_t value) {
        file.push_back(static_cast<std::uint8_t>(value & 0xFF));
        file.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    };
    const auto puts = [&file](const char* text) {
        for (const char* p = text; *p != '\0'; ++p) {
            file.push_back(static_cast<std::uint8_t>(*p));
        }
    };
    puts("RIFF");
    put32(riffSize);
    puts("WAVE");
    puts("fmt ");
    put32(16);
    put16(1);      // PCM
    put16(2);      // channels
    put32(44100);  // sample rate
    put32(44100 * 4);
    put16(4);   // block align
    put16(16);  // bits
    puts("data");
    put32(dataSize);
    file.insert(file.end(), samples.begin(), samples.end());

    std::FILE* out = std::fopen(path.c_str(), "wb");
    REQUIRE(out != nullptr);
    std::fwrite(file.data(), 1, file.size(), out);
    std::fclose(out);

    WaveData wave;
    std::string error;
    REQUIRE(alus::audio::readWaveFile(path.string(), wave, &error));
    CHECK(wave.format.channels == 2);
    CHECK(wave.format.sampleRate == 44100);
    CHECK(wave.format.bitsPerSample == 16);
    CHECK(wave.samples == samples);

    LoopFixture fixture;
    auto sounds = std::make_unique<MemorySink>();
    MemorySink* raw = sounds.get();
    alus::audio::initialize(&fixture.loop(), std::make_unique<NullSink>(), std::move(sounds));
    CHECK(alus::audio::playWaveFile(path.string()));
    alus::audio::soundsPlayer()->sync();
    CHECK(raw->size() == samples.size());
    alus::audio::terminate();
    std::filesystem::remove(path);
}

TEST_CASE("a missing or broken sound file is an expected error", "[audio][wave]") {
    QuietLog quiet;
    WaveData wave;
    std::string error;
    CHECK_FALSE(alus::audio::readWaveFile("/nonexistent/sound.wav", wave, &error));
    CHECK_FALSE(error.empty());

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "alus-test-notawave.wav";
    std::FILE* out = std::fopen(path.c_str(), "wb");
    REQUIRE(out != nullptr);
    std::fputs("this is not a RIFF file at all, not even nearly, no", out);
    std::fclose(out);
    CHECK_FALSE(alus::audio::readWaveFile(path.string(), wave, &error));

    // §3.8: an expected error must not make the screen reader chirp, so
    // playWaveFile reports false instead of throwing or beeping.
    LoopFixture fixture;
    alus::audio::initialize(&fixture.loop(), std::make_unique<NullSink>(),
                            std::make_unique<NullSink>());
    CHECK_FALSE(alus::audio::playWaveFile(path.string()));
    alus::audio::terminate();
    std::filesystem::remove(path);
}
