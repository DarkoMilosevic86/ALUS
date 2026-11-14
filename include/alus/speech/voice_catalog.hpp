// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// A module's voice list, split into voices and variants when it has them.
//
// WHY (phase-10.md N126). speech-dispatcher's espeak-ng module reports 14805
// "voices": 140 languages times 105 variants, each as "Serbian+Adam" with
// language "sr" and variant "Adam". As one list -- in the settings window or
// in the alus+control+arrows ring -- that is unusable. NVDA's eSpeak driver
// offers "voice" (the language) and "variant" as two settings; this does the
// same, but from the DATA and not for one module by name: a voice whose id is
// another voice's id, a "+", and its own variant, is that voice plus a
// variant. A module that has none (rhvoice, mbrola_ng) keeps one flat list.
//
// The configuration keeps ONE key, `voice`, holding the full id
// ("Serbian+Adam"), which is what speech-dispatcher takes; the split exists
// only for choosing.
#pragma once

#include "alus/speech/synth.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace alus::speech {

class VoiceCatalog {
public:
    VoiceCatalog() = default;
    // `preferredLanguage` ("sr", "sr-RS", "sr_RS.UTF-8") puts the voices of
    // that language first; the rest are sorted by name. Empty keeps the
    // module's order.
    static VoiceCatalog build(const std::vector<VoiceInfo>& all,
                              std::string_view preferredLanguage = {});

    // The voices to choose from: the base voices when the module has
    // variants, every voice otherwise.
    const std::vector<VoiceInfo>& voices() const noexcept { return voices_; }
    // The variants, without the "no variant" entry (that one is the empty
    // string, and callers add it where they list them). Sorted by name.
    const std::vector<std::string>& variants() const noexcept { return variants_; }
    bool hasVariants() const noexcept { return !variants_.empty(); }

    // A full id into its base voice and variant. An id with no variant, or
    // one this catalog does not know as a variant, is all base.
    std::pair<std::string, std::string> split(std::string_view id) const;
    // The other way: base plus variant, as the module names it. An empty
    // variant is the base alone.
    std::string join(std::string_view base, std::string_view variant) const;

    // Positions, or -1.
    int indexOfVoice(std::string_view baseId) const;
    int indexOfVariant(std::string_view variant) const;

    // How a voice is shown: its name, and its language in brackets when the
    // name does not already say it ("Serbian (sr)", "Marija (hr)").
    static std::string label(const VoiceInfo& voice);

private:
    std::vector<VoiceInfo> voices_;
    std::vector<std::string> variants_;
};

}  // namespace alus::speech
