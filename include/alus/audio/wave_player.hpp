// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Audio output.
//
// Port of NVDA's `nvwave.WavePlayer`. See architecture.md §4.6. The one
// non-negotiable part of the interface is `feed(data, size, onDone)` WITH A
// PER-BLOCK CALLBACK: that callback is the mechanism by which a synthesizer
// that hands ALUS raw audio (EspeakDirectSynth, planned for v1.1 — see
// spikes/RESULTS.md S2b) can report indexes that are sample-accurate instead of
// 40 ms early (finding N13).
//
// Threading (§13): `feed()` is non-blocking and callable from any thread. One
// writer thread per player owns the blocking device writes. The done callbacks
// are marshalled onto the main loop when the player is given one, because they
// run user code — a BeepCommand's callback must not execute on the audio
// thread.
//
// The device is behind `ISink` for two reasons, both practical: tests need audio
// output they can assert on (MemorySink), and a machine with no sound server
// must still run (NullSink) rather than fail to start the screen reader.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace alus::core {
class Loop;
}

namespace alus::audio {

struct Format {
    unsigned channels = 2;
    unsigned sampleRate = 44100;
    unsigned bitsPerSample = 16;

    std::size_t bytesPerFrame() const noexcept {
        return static_cast<std::size_t>(channels) * (bitsPerSample / 8U);
    }
    std::chrono::microseconds durationOf(std::size_t bytes) const;
    bool operator==(const Format& other) const noexcept;
};

// NVDA: AudioPurpose. Speech can be ducked and interrupted; sounds cannot.
enum class Purpose { Speech, Sounds };

class ISink {
public:
    ISink() = default;
    virtual ~ISink() = default;

    ISink(const ISink&) = delete;
    ISink& operator=(const ISink&) = delete;

    virtual std::string_view name() const = 0;

    virtual bool open(const Format& format, std::string_view streamName,
                      std::string* error = nullptr) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Blocking. Returns false on a device error, after which the player closes
    // the sink and tries to reopen on the next block.
    virtual bool write(const std::uint8_t* data, std::size_t bytes) = 0;
    // Play out everything written.
    virtual void drain() = 0;
    // Discard everything written but not yet played.
    virtual void flush() = 0;
    // How long the audio already written will still take to play.
    virtual std::chrono::microseconds latency() = 0;
};

// Writes nowhere. The screen reader starts and speaks through speech-dispatcher
// even when ALUS itself cannot open a device.
class NullSink final : public ISink {
public:
    std::string_view name() const override { return "null"; }
    bool open(const Format& format, std::string_view streamName,
              std::string* error = nullptr) override;
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    bool write(const std::uint8_t* data, std::size_t bytes) override;
    void drain() override {}
    void flush() override {}
    std::chrono::microseconds latency() override { return std::chrono::microseconds{0}; }

    std::size_t bytesWritten() const;

private:
    mutable std::mutex mutex_;
    bool open_ = false;
    std::size_t written_ = 0;
};

// Keeps what was written, so a test can assert on real sample data.
class MemorySink final : public ISink {
public:
    std::string_view name() const override { return "memory"; }
    bool open(const Format& format, std::string_view streamName,
              std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override;
    bool write(const std::uint8_t* data, std::size_t bytes) override;
    void drain() override;
    void flush() override;
    std::chrono::microseconds latency() override { return std::chrono::microseconds{0}; }

    std::vector<std::uint8_t> data() const;
    std::size_t size() const;
    void clear();
    unsigned drains() const;
    unsigned flushes() const;
    const Format& format() const;

private:
    mutable std::mutex mutex_;
    bool open_ = false;
    Format format_{};
    std::vector<std::uint8_t> data_;
    unsigned drains_ = 0;
    unsigned flushes_ = 0;
};

// The PulseAudio sink (also the PipeWire sink, through pipewire-pulse). Returns
// null when ALUS was built without libpulse.
std::unique_ptr<ISink> makePulseSink();
// Pulse if available, otherwise null — never a null pointer.
std::unique_ptr<ISink> makeDefaultSink();

class WavePlayer {
public:
    using DoneCallback = std::function<void()>;

    // `loop` may be null, in which case done callbacks run on the writer
    // thread. The daemon always passes the loop.
    WavePlayer(Format format, std::unique_ptr<ISink> sink, core::Loop* loop = nullptr,
               Purpose purpose = Purpose::Speech, std::string streamName = "alus");
    ~WavePlayer();

    WavePlayer(const WavePlayer&) = delete;
    WavePlayer& operator=(const WavePlayer&) = delete;

    const Format& format() const noexcept { return format_; }
    Purpose purpose() const noexcept { return purpose_; }
    ISink& sink() noexcept { return *sink_; }

    // Queues audio and returns at once. `onDone` runs when the block has
    // finished playing, on the main thread if the player has a loop.
    void feed(const void* data, std::size_t bytes, DoneCallback onDone = {});
    void feed(const std::vector<std::uint8_t>& data, DoneCallback onDone = {});

    // Blocks until everything queued has been played. Never call it from the
    // main thread while speech is in flight: it is for shutdown and for tests.
    void sync();

    // Discards queued audio and whatever the device has not played yet. Pending
    // done callbacks are dropped, exactly as in NVDA.
    void stop();

    // Holds the writer thread. Audio already inside the device keeps playing
    // (pa_simple has no cork), which is at most one device buffer.
    void pause(bool switchOn);
    bool isPaused() const;

    // NVDA: idle(). Plays out and releases the device so it can sleep; the next
    // feed() reopens it.
    void idle();

    // Software volume, 0..100 per channel. pa_simple has no per-stream volume,
    // and a screen reader must not touch the user's global volume, so this is
    // applied to the samples.
    void setVolume(int left, int right);

    std::size_t queuedBytes() const;
    std::uint64_t blocksPlayed() const;

private:
    struct Block {
        std::vector<std::uint8_t> data;
        DoneCallback onDone;
    };

    void writerThread();
    void applyVolume(std::vector<std::uint8_t>& data) const;
    void postDone(DoneCallback callback, std::chrono::microseconds after);

    Format format_;
    std::unique_ptr<ISink> sink_;
    core::Loop* loop_;
    Purpose purpose_;
    std::string streamName_;

    mutable std::mutex mutex_;
    // Held around every device call. `feed()` never takes it, so queueing
    // audio is never blocked by a device write in progress; `stop()`, `sync()`
    // and `idle()` do, which bounds them by one block rather than by the
    // queue.
    mutable std::mutex sinkMutex_;
    std::condition_variable queueChanged_;
    std::condition_variable drained_;
    std::deque<Block> queue_;
    bool paused_ = false;
    bool stopping_ = false;
    bool writing_ = false;
    int volumeLeft_ = 100;
    int volumeRight_ = 100;
    std::size_t queuedBytes_ = 0;
    std::uint64_t blocksPlayed_ = 0;
    std::thread writer_;
};

// ---------------------------------------------------------------------------
// The process-wide players (NVDA: `tones.player` and `nvwave.playWaveFile`).
// ---------------------------------------------------------------------------

// `sink` null means makeDefaultSink(). Safe to call twice; the second call
// replaces the players.
void initialize(core::Loop* loop = nullptr, std::unique_ptr<ISink> speechSink = nullptr,
                std::unique_ptr<ISink> soundsSink = nullptr);
void terminate();
bool isInitialized();
// Null until initialize().
WavePlayer* soundsPlayer();
WavePlayer* speechPlayer();

// Decoded PCM from a RIFF/WAVE file.
struct WaveData {
    Format format;
    std::vector<std::uint8_t> samples;
};
// Reads a 8/16/24/32-bit PCM or IEEE-float WAVE file. Nullopt on error, with
// `error` filled; a missing sound file is an expected error (§3.8) and must not
// be reported as a failure to the user.
bool readWaveFile(const std::string& path, WaveData& out, std::string* error = nullptr);

// NVDA: nvwave.playWaveFile(asynchronous=True). Returns false if the file could
// not be read.
bool playWaveFile(const std::string& path, bool asynchronous = true);

}  // namespace alus::audio
