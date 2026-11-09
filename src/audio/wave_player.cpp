// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/audio/wave_player.hpp"

#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

#ifdef ALUS_HAVE_PULSE
#include <pulse/error.h>
#include <pulse/simple.h>
#endif

namespace alus::audio {

std::chrono::microseconds Format::durationOf(std::size_t bytes) const {
    const std::size_t frame = bytesPerFrame();
    if (frame == 0 || sampleRate == 0) {
        return std::chrono::microseconds{0};
    }
    const auto frames = static_cast<std::uint64_t>(bytes / frame);
    return std::chrono::microseconds{(frames * 1000000ULL) / sampleRate};
}

bool Format::operator==(const Format& other) const noexcept {
    return channels == other.channels && sampleRate == other.sampleRate &&
           bitsPerSample == other.bitsPerSample;
}

// ---------------------------------------------------------------------------
// Sinks
// ---------------------------------------------------------------------------

bool NullSink::open(const Format& format, std::string_view streamName, std::string* error) {
    (void)format;
    (void)streamName;
    (void)error;
    const std::lock_guard lock{mutex_};
    open_ = true;
    return true;
}

bool NullSink::write(const std::uint8_t* data, std::size_t bytes) {
    (void)data;
    const std::lock_guard lock{mutex_};
    written_ += bytes;
    return true;
}

std::size_t NullSink::bytesWritten() const {
    const std::lock_guard lock{mutex_};
    return written_;
}

bool MemorySink::open(const Format& format, std::string_view streamName, std::string* error) {
    (void)streamName;
    (void)error;
    const std::lock_guard lock{mutex_};
    format_ = format;
    open_ = true;
    return true;
}

void MemorySink::close() {
    const std::lock_guard lock{mutex_};
    open_ = false;
}

bool MemorySink::isOpen() const {
    const std::lock_guard lock{mutex_};
    return open_;
}

bool MemorySink::write(const std::uint8_t* data, std::size_t bytes) {
    const std::lock_guard lock{mutex_};
    data_.insert(data_.end(), data, data + bytes);
    return true;
}

void MemorySink::drain() {
    const std::lock_guard lock{mutex_};
    ++drains_;
}

void MemorySink::flush() {
    const std::lock_guard lock{mutex_};
    ++flushes_;
}

std::vector<std::uint8_t> MemorySink::data() const {
    const std::lock_guard lock{mutex_};
    return data_;
}

std::size_t MemorySink::size() const {
    const std::lock_guard lock{mutex_};
    return data_.size();
}

void MemorySink::clear() {
    const std::lock_guard lock{mutex_};
    data_.clear();
}

unsigned MemorySink::drains() const {
    const std::lock_guard lock{mutex_};
    return drains_;
}

unsigned MemorySink::flushes() const {
    const std::lock_guard lock{mutex_};
    return flushes_;
}

const Format& MemorySink::format() const { return format_; }

#ifdef ALUS_HAVE_PULSE

// libpulse-simple. It works on PipeWire through pipewire-pulse, which is what
// the target system runs (§4.6). A native PipeWire sink for lower latency is a
// later change behind this same interface.
class PulseSink final : public ISink {
public:
    ~PulseSink() override { close(); }

    std::string_view name() const override { return "pulse"; }

    bool open(const Format& format, std::string_view streamName, std::string* error) override {
        close();
        pa_sample_spec spec{};
        switch (format.bitsPerSample) {
            case 16:
                spec.format = PA_SAMPLE_S16NE;
                break;
            case 24:
                spec.format = PA_SAMPLE_S24NE;
                break;
            case 32:
                spec.format = PA_SAMPLE_S32NE;
                break;
            case 8:
                spec.format = PA_SAMPLE_U8;
                break;
            default:
                if (error != nullptr) {
                    *error = "unsupported sample size";
                }
                return false;
        }
        spec.rate = format.sampleRate;
        spec.channels = static_cast<std::uint8_t>(format.channels);

        // A screen reader is latency-bound, not throughput-bound: a small
        // buffer is the whole point. tlength is the one that decides how long
        // audio keeps playing after a stop (S2 N11).
        pa_buffer_attr attr{};
        const auto bytesFor = [&format](std::chrono::microseconds duration) {
            const auto frames = static_cast<std::uint64_t>(format.sampleRate) *
                                static_cast<std::uint64_t>(duration.count()) / 1000000ULL;
            return static_cast<std::uint32_t>(frames * format.bytesPerFrame());
        };
        attr.maxlength = bytesFor(std::chrono::milliseconds{200});
        attr.tlength = bytesFor(std::chrono::milliseconds{40});
        attr.prebuf = bytesFor(std::chrono::milliseconds{10});
        attr.minreq = bytesFor(std::chrono::milliseconds{10});
        attr.fragsize = static_cast<std::uint32_t>(-1);

        int errorCode = 0;
        const std::string name{streamName};
        stream_ = pa_simple_new(nullptr, "ALUS", PA_STREAM_PLAYBACK, nullptr, name.c_str(), &spec,
                                nullptr, &attr, &errorCode);
        if (stream_ == nullptr) {
            if (error != nullptr) {
                *error = pa_strerror(errorCode);
            }
            return false;
        }
        return true;
    }

    void close() override {
        if (stream_ != nullptr) {
            pa_simple_free(stream_);
            stream_ = nullptr;
        }
    }

    bool isOpen() const override { return stream_ != nullptr; }

    bool write(const std::uint8_t* data, std::size_t bytes) override {
        if (stream_ == nullptr) {
            return false;
        }
        int errorCode = 0;
        if (pa_simple_write(stream_, data, bytes, &errorCode) < 0) {
            ALUS_WARNING("pulse write failed: {}", pa_strerror(errorCode));
            return false;
        }
        return true;
    }

    void drain() override {
        if (stream_ != nullptr) {
            int errorCode = 0;
            pa_simple_drain(stream_, &errorCode);
        }
    }

    void flush() override {
        if (stream_ != nullptr) {
            int errorCode = 0;
            pa_simple_flush(stream_, &errorCode);
        }
    }

    std::chrono::microseconds latency() override {
        if (stream_ == nullptr) {
            return std::chrono::microseconds{0};
        }
        int errorCode = 0;
        const pa_usec_t usec = pa_simple_get_latency(stream_, &errorCode);
        if (errorCode != 0) {
            return std::chrono::microseconds{0};
        }
        return std::chrono::microseconds{static_cast<std::int64_t>(usec)};
    }

private:
    pa_simple* stream_ = nullptr;
};

std::unique_ptr<ISink> makePulseSink() { return std::make_unique<PulseSink>(); }

#else

std::unique_ptr<ISink> makePulseSink() { return nullptr; }

#endif  // ALUS_HAVE_PULSE

std::unique_ptr<ISink> makeDefaultSink() {
    if (auto pulse = makePulseSink()) {
        return pulse;
    }
    // An expected degradation, not a failure (§3.8): speech goes through
    // speech-dispatcher either way, only ALUS's own tones are lost. Said once,
    // at a level that does not play an error sound.
    static bool reported = false;
    if (!reported) {
        reported = true;
        ALUS_DEBUG_WARNING("built without libpulse; ALUS tones and sounds are silent");
    }
    return std::make_unique<NullSink>();
}

// ---------------------------------------------------------------------------
// WavePlayer
// ---------------------------------------------------------------------------

WavePlayer::WavePlayer(Format format, std::unique_ptr<ISink> sink, core::Loop* loop,
                       Purpose purpose, std::string streamName)
    : format_(format),
      sink_(std::move(sink)),
      loop_(loop),
      purpose_(purpose),
      streamName_(std::move(streamName)) {
    if (sink_ == nullptr) {
        sink_ = std::make_unique<NullSink>();
    }
    writer_ = std::thread{[this] { writerThread(); }};
}

WavePlayer::~WavePlayer() {
    {
        const std::lock_guard lock{mutex_};
        stopping_ = true;
        queue_.clear();
        queuedBytes_ = 0;
    }
    queueChanged_.notify_all();
    if (writer_.joinable()) {
        writer_.join();
    }
    const std::lock_guard sinkLock{sinkMutex_};
    sink_->close();
}

void WavePlayer::feed(const void* data, std::size_t bytes, DoneCallback onDone) {
    if (bytes == 0) {
        if (onDone) {
            postDone(std::move(onDone), std::chrono::microseconds{0});
        }
        return;
    }
    Block block;
    const auto* begin = static_cast<const std::uint8_t*>(data);
    block.data.assign(begin, begin + bytes);
    block.onDone = std::move(onDone);
    {
        const std::lock_guard lock{mutex_};
        if (stopping_) {
            return;
        }
        queuedBytes_ += bytes;
        queue_.push_back(std::move(block));
    }
    queueChanged_.notify_one();
}

void WavePlayer::feed(const std::vector<std::uint8_t>& data, DoneCallback onDone) {
    feed(data.data(), data.size(), std::move(onDone));
}

void WavePlayer::sync() {
    std::unique_lock lock{mutex_};
    drained_.wait(lock, [this] { return (queue_.empty() && !writing_) || stopping_; });
    lock.unlock();
    const std::lock_guard sinkLock{sinkMutex_};
    sink_->drain();
}

void WavePlayer::stop() {
    std::deque<Block> dropped;
    {
        const std::lock_guard lock{mutex_};
        dropped.swap(queue_);
        queuedBytes_ = 0;
    }
    // The writer may be inside a blocking device write; waiting for it to
    // return bounds the stop by one block, not by the whole queue.
    {
        std::unique_lock lock{mutex_};
        drained_.wait(lock, [this] { return !writing_ || stopping_; });
    }
    const std::lock_guard sinkLock{sinkMutex_};
    sink_->flush();
    dropped.clear();  // pending done callbacks are dropped, as in NVDA
}

void WavePlayer::pause(bool switchOn) {
    {
        const std::lock_guard lock{mutex_};
        paused_ = switchOn;
    }
    queueChanged_.notify_all();
}

bool WavePlayer::isPaused() const {
    const std::lock_guard lock{mutex_};
    return paused_;
}

void WavePlayer::idle() {
    sync();
    const std::lock_guard sinkLock{sinkMutex_};
    sink_->close();
}

void WavePlayer::setVolume(int left, int right) {
    const std::lock_guard lock{mutex_};
    volumeLeft_ = std::clamp(left, 0, 100);
    volumeRight_ = std::clamp(right, 0, 100);
}

std::size_t WavePlayer::queuedBytes() const {
    const std::lock_guard lock{mutex_};
    return queuedBytes_;
}

std::uint64_t WavePlayer::blocksPlayed() const {
    const std::lock_guard lock{mutex_};
    return blocksPlayed_;
}

void WavePlayer::applyVolume(std::vector<std::uint8_t>& data) const {
    int left = 100;
    int right = 100;
    {
        const std::lock_guard lock{mutex_};
        left = volumeLeft_;
        right = volumeRight_;
    }
    if (left == 100 && right == 100) {
        return;
    }
    if (format_.bitsPerSample != 16) {
        return;  // only the format ALUS generates is scaled in software
    }
    auto* samples = reinterpret_cast<std::int16_t*>(data.data());
    const std::size_t count = data.size() / sizeof(std::int16_t);
    for (std::size_t i = 0; i < count; ++i) {
        const int gain = (format_.channels == 2 && (i % 2) == 1) ? right : left;
        samples[i] = static_cast<std::int16_t>((static_cast<int>(samples[i]) * gain) / 100);
    }
}

void WavePlayer::postDone(DoneCallback callback, std::chrono::microseconds after) {
    if (!callback) {
        return;
    }
    if (loop_ == nullptr) {
        callback();
        return;
    }
    const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(after);
    loop_->callLater(delay, std::move(callback), "audio-block-done");
}

void WavePlayer::writerThread() {
    core::Log::setThreadName("audio-out");
    for (;;) {
        Block block;
        {
            std::unique_lock lock{mutex_};
            queueChanged_.wait(lock, [this] { return stopping_ || (!queue_.empty() && !paused_); });
            if (stopping_) {
                return;
            }
            block = std::move(queue_.front());
            queue_.pop_front();
            queuedBytes_ -= std::min(queuedBytes_, block.data.size());
            writing_ = true;
        }

        std::unique_lock sinkLock{sinkMutex_};
        if (!sink_->isOpen()) {
            std::string error;
            if (!sink_->open(format_, streamName_, &error)) {
                ALUS_DEBUG_WARNING("could not open the audio device ({}); dropping {} bytes",
                                   error, block.data.size());
                sinkLock.unlock();
                const std::lock_guard lock{mutex_};
                writing_ = false;
                drained_.notify_all();
                continue;
            }
        }

        applyVolume(block.data);
        const bool ok = sink_->write(block.data.data(), block.data.size());
        if (!ok) {
            sink_->close();
        }
        // The block has been accepted by the device; it finishes playing once
        // the queue inside the device has drained. That is the moment the
        // caller's callback is about, so it is delayed by exactly that much.
        const std::chrono::microseconds remaining = ok ? sink_->latency()
                                                       : std::chrono::microseconds{0};
        sinkLock.unlock();
        postDone(std::move(block.onDone), remaining);
        {
            const std::lock_guard lock{mutex_};
            writing_ = false;
            ++blocksPlayed_;
        }
        drained_.notify_all();
    }
}

// ---------------------------------------------------------------------------
// Process-wide players
// ---------------------------------------------------------------------------

namespace {

struct Global {
    std::unique_ptr<WavePlayer> sounds;
    std::unique_ptr<WavePlayer> speech;
    std::unique_ptr<WavePlayer> files;
    Format filesFormat{};
    core::Loop* loop = nullptr;
    bool usingMemorySinks = false;
};

Global& global() {
    static Global instance;
    return instance;
}

constexpr Format kSoundsFormat{2, 44100, 16};

}  // namespace

void initialize(core::Loop* loop, std::unique_ptr<ISink> speechSink,
                std::unique_ptr<ISink> soundsSink) {
    Global& g = global();
    g.loop = loop;
    const bool memorySounds = soundsSink != nullptr;
    g.sounds = std::make_unique<WavePlayer>(
        kSoundsFormat, soundsSink != nullptr ? std::move(soundsSink) : makeDefaultSink(), loop,
        Purpose::Sounds, "alus-sounds");
    g.speech = std::make_unique<WavePlayer>(
        kSoundsFormat, speechSink != nullptr ? std::move(speechSink) : makeDefaultSink(), loop,
        Purpose::Speech, "alus-speech");
    g.files.reset();
    g.usingMemorySinks = memorySounds;
}

void terminate() {
    Global& g = global();
    g.files.reset();
    g.speech.reset();
    g.sounds.reset();
    g.loop = nullptr;
}

bool isInitialized() { return global().sounds != nullptr; }

WavePlayer* soundsPlayer() { return global().sounds.get(); }
WavePlayer* speechPlayer() { return global().speech.get(); }

// ---------------------------------------------------------------------------
// WAVE files
// ---------------------------------------------------------------------------

namespace {

std::uint32_t readU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t readU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

bool fail(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

}  // namespace

bool readWaveFile(const std::string& path, WaveData& out, std::string* error) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return fail(error, "cannot open the file");
    }
    std::vector<std::uint8_t> bytes;
    std::uint8_t buffer[16384];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        bytes.insert(bytes.end(), buffer, buffer + read);
        if (bytes.size() > (64U << 20)) {
            break;  // a 64 MiB sound effect is a mistake, not a sound effect
        }
    }
    std::fclose(file);

    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return fail(error, "not a RIFF/WAVE file");
    }

    bool haveFormat = false;
    std::size_t offset = 12;
    std::uint16_t formatTag = 0;
    while (offset + 8 <= bytes.size()) {
        const std::uint8_t* chunk = bytes.data() + offset;
        const std::uint32_t size = readU32(chunk + 4);
        const std::size_t body = offset + 8;
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16 && body + 16 <= bytes.size()) {
            const std::uint8_t* fmt = bytes.data() + body;
            formatTag = readU16(fmt);
            out.format.channels = readU16(fmt + 2);
            out.format.sampleRate = readU32(fmt + 4);
            out.format.bitsPerSample = readU16(fmt + 14);
            haveFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            const std::size_t available = bytes.size() - body;
            const std::size_t length = std::min(static_cast<std::size_t>(size), available);
            out.samples.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body),
                               bytes.begin() + static_cast<std::ptrdiff_t>(body + length));
        }
        offset = body + size + (size % 2);  // chunks are word aligned
    }

    if (!haveFormat) {
        return fail(error, "no fmt chunk");
    }
    if (out.samples.empty()) {
        return fail(error, "no data chunk");
    }
    // 1 = PCM, 0xFFFE = extensible (whose sub-format we do not inspect; the
    // files ALUS ships are plain PCM).
    if (formatTag != 1 && formatTag != 0xFFFE) {
        return fail(error, "not PCM");
    }
    if (out.format.channels == 0 || out.format.sampleRate == 0 ||
        (out.format.bitsPerSample != 8 && out.format.bitsPerSample != 16 &&
         out.format.bitsPerSample != 24 && out.format.bitsPerSample != 32)) {
        return fail(error, "unsupported PCM format");
    }
    return true;
}

bool playWaveFile(const std::string& path, bool asynchronous) {
    Global& g = global();
    if (g.sounds == nullptr) {
        return false;
    }
    WaveData wave;
    std::string error;
    if (!readWaveFile(path, wave, &error)) {
        // Expected error (§3.8): a missing or broken sound must not make the
        // screen reader chirp.
        ALUS_DEBUG_WARNING("cannot play {}: {}", path, error);
        return false;
    }

    WavePlayer* player = nullptr;
    if (wave.format == g.sounds->format()) {
        player = g.sounds.get();
    } else {
        if (g.files == nullptr || !(g.filesFormat == wave.format)) {
            // One player per format, like NVDA. Sounds in one format are the
            // normal case, so this is built at most once.
            std::unique_ptr<ISink> sink =
                g.usingMemorySinks ? std::unique_ptr<ISink>{std::make_unique<NullSink>()}
                                   : makeDefaultSink();
            g.files = std::make_unique<WavePlayer>(wave.format, std::move(sink), g.loop,
                                                   Purpose::Sounds, "alus-sounds");
            g.filesFormat = wave.format;
        }
        player = g.files.get();
    }

    player->feed(wave.samples);
    if (!asynchronous) {
        player->sync();
    }
    return true;
}

}  // namespace alus::audio
