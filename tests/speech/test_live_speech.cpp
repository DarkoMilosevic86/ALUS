// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T2.11 — the latency check, against a real speech-dispatcher.
//
// `meson test -C build --suite live`
//
// WHAT THIS MEASURES, AND WHAT IT DELIBERATELY DOES NOT. The phase plan asks
// for "p95 speak → audio < 50 ms, the same test as S2, now in CI". Phase 0
// established (finding N14) that timing the actual audio requires capturing the
// output sink with pw-record; that harness is spikes/s2-speech.c and it stays
// there, because a screen reader must not grow an audio tap to test itself.
//
// So the budget is split, and both halves are honest:
//
//   * spikes/s2-speech.c owns "speak() → first audible sample": 8.71 ms p50,
//     11.50 ms p95 on the target system, against the 50 ms budget. Rerun it
//     when the audio stack changes.
//   * this test owns the SSIP path, which is what ALUS's own code can affect:
//     speak() → the first index mark, and speak() → the last one. If a change
//     in ALUS makes speech slow to start, this catches it; it cannot catch a
//     regression inside speech-dispatcher's audio output.
//
// Finding N10 is why `begin` is not used here: it arrives BEFORE the audio and
// is a protocol acknowledgement, not an audio event. The first index mark is
// the earliest signal that comes from the synthesis itself.
//
// DO NOT JUDGE THE BUDGET UNDER A SANITIZER. Under ThreadSanitizer the same
// measurement drifts into the hundreds of milliseconds, which says nothing
// about ALUS. The sanitizer builds are for the race in risk R16; the budget is
// measured in an ordinary build.
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/speech/manager.hpp"
#include "alus/speech/ssip_synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glib.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using alus::core::Loop;
using alus::core::LoopConfig;
using alus::speech::ISynth;
using alus::speech::Priority;
using alus::speech::SpeechManager;
using alus::speech::SpeechSequence;
using alus::speech::SsipSynth;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto position =
        static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1) + 0.5);
    return values[std::min(position, values.size() - 1)];
}

}  // namespace

TEST_CASE("T2.11 speech starts within the budget over SSIP", "[speech][live]") {
    auto synth = std::make_unique<SsipSynth>("alus-test", "latency");
    std::string error;
    if (!synth->initialize(&error)) {
        // A build machine with no speech-dispatcher is not a broken ALUS.
        // SKIP and not WARN + SUCCEED: see phase-7.md N65 -- a warning is its
        // own TAP test point and the plan line then disagrees with the
        // count, which meson reports as a failed test rather than a skip.
        SKIP("no speech-dispatcher (" + error + ")");
    }
    // The budgets belong to the MODULE, not to SSIP (finding N17): rhvoice
    // misses every one of them, and it is a perfectly reasonable thing for a
    // user to have configured. The regression guard therefore pins the module
    // phase 0 measured.
    if (!synth->timingIsGuaranteed()) {
        const std::string previous = synth->outputModule();
        if (!synth->setOutputModule("espeak-ng")) {
            synth->terminate();
            SKIP("the espeak-ng module is not available (module is " + previous + ")");
        }
    }
    std::printf("# module: %s, timing guaranteed: %s\n", synth->outputModule().c_str(),
                synth->timingIsGuaranteed() ? "yes" : "no");

    GMainContext* context = g_main_context_new();
    Loop loop{nullptr, LoopConfig{.pumpMaxDelay = 5ms, .context = context}};
    SpeechManager manager{loop};
    manager.setSynth(synth.get());

    std::vector<double> toFirstMark;
    std::vector<double> toLastMark;
    unsigned incomplete = 0;
    constexpr int kWarmUp = 3;
    constexpr int kIterations = 20;

    for (int iteration = 0; iteration < kWarmUp + kIterations; ++iteration) {
        bool done = false;
        bool haveFirst = false;
        double firstMark = 0.0;
        double lastMark = 0.0;
        Clock::time_point start;

        const auto handle = synth->indexReached.add(
            [&](ISynth*, int) {
                const double elapsed =
                    std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                if (!haveFirst) {
                    haveFirst = true;
                    firstMark = elapsed;
                }
                lastMark = elapsed;
            },
            "latency");

        SpeechSequence sequence;
        // A mark at the START of the utterance is the shape measured to be
        // safe (the N16 matrix), and it is the earliest signal from synthesis.
        sequence.emplace_back(alus::speech::callback([] {}, "start"));
        sequence.emplace_back(std::string{"Проба"});
        sequence.emplace_back(alus::speech::callback([&done] { done = true; }, "end"));

        start = Clock::now();
        manager.speak(std::move(sequence), Priority::Normal);

        const auto deadline = Clock::now() + 10s;
        while (!done && Clock::now() < deadline) {
            if (!loop.iterate(false)) {
                std::this_thread::sleep_for(1ms);
            }
        }
        if (!done) {
            ++incomplete;
        }
        manager.cancel();
        loop.iterateUntilIdle();
        if (iteration >= kWarmUp) {
            if (haveFirst) {
                toFirstMark.push_back(firstMark);
            }
            if (done) {
                toLastMark.push_back(lastMark);
            }
        }
        // Let the server settle, so one iteration's tail does not land in the
        // next one's measurement (finding N15 in reverse: risk R15).
        std::this_thread::sleep_for(50ms);
    }

    std::printf("# speak -> first mark: p50 %.2f p95 %.2f ms (n=%zu)\n",
                percentile(toFirstMark, 0.5), percentile(toFirstMark, 0.95),
                toFirstMark.size());
    std::printf("# speak -> last mark:  p50 %.2f p95 %.2f ms (n=%zu)\n",
                percentile(toLastMark, 0.5), percentile(toLastMark, 0.95),
                toLastMark.size());

    manager.setSynth(nullptr);
    synth->terminate();
    g_main_context_unref(context);

    REQUIRE_FALSE(toFirstMark.empty());
    // The budget from phase-plan.md, applied to what this test can see. It is
    // deliberately the same number: the SSIP path has to stay well inside it
    // for the audio-onset budget to hold at all.
    CHECK(percentile(toFirstMark, 0.95) < 50.0);
    // RISK R14: an utterance that never completes means a lost index mark.
    // Zero tolerance — this is the failure that silences the screen reader.
    CHECK(incomplete == 0);
    CHECK(manager.lostIndexCount() == 0);
}
