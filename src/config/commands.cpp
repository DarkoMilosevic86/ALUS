// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/config/commands.hpp"

#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/ui/message.hpp"

#include <gio/gio.h>

#include <utility>

namespace alus::config {

namespace {
// The repeat count of the script running now, 0 when there is no dispatcher.
int repeatsOf(const std::function<const script::Dispatcher*()>& dispatcher) {
    const script::Dispatcher* current = dispatcher ? dispatcher() : nullptr;
    return current != nullptr ? current->lastScriptRepeatCount() : 0;
}
}  // namespace


Commands::Commands(Context context, const script::Catalog* catalog)
    // One class name, NVDA's: it is what the generated gesture map addresses.
    : script::ScriptableObject({"globalCommands.GlobalCommands"}, catalog),
      context_(std::move(context)) {
    ALUS_SCRIPT(saveConfiguration);
    ALUS_SCRIPT(revertConfiguration);
    ALUS_SCRIPT(toggleKeyboardLayout);
    ALUS_SCRIPT(cycleSpeechAutomaticLanguageSwitching);
    ALUS_SCRIPT(speech_cycleUnicodeNormalization);
    ALUS_SCRIPT(toggleReportTableHeaders);
    ALUS_SCRIPT(toggleReportFontAttributes);
    ALUS_SCRIPT(toggleReportLinkType);
    ALUS_SCRIPT(toggleConfigProfileTriggers);
    ALUS_SCRIPT(reportActiveConfigurationProfile);
    ALUS_SCRIPT(openUserConfigurationDirectory);
    ALUS_SCRIPT(restart);
    bindFormattingToggles();
}

void Commands::say(const std::string& text) const {
    if (context_.message) {
        context_.message(text);
        return;
    }
    ui::message(text);
}

int Commands::repeatCount() const {
    return repeatsOf(context_.dispatcher);
}

void Commands::saveConfiguration(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        // Translators: spoken when the configuration cannot be saved because
        // the command was run somewhere that has none, e.g. a diagnostic tool.
        say(_("cannot save the configuration from here"));
        return;
    }
    std::string error;
    if (!context_.manager->save(&error)) {
        ALUS_ERROR("could not save the configuration: {}", error);
        // Translators: spoken when writing the configuration file failed --
        // a read-only home directory, a full disk. NOT silent: a user who
        // believes a setting was kept and finds it gone after a restart has
        // no way to know which of the two went wrong.
        say(_("Could not save configuration"));
        return;
    }
    ALUS_INFO("configuration saved to {}", context_.manager->paths().configDir.string());
    // Translators: spoken when the configuration has been written to disk.
    say(_("Configuration saved"));
}

void Commands::revertConfiguration(input::Gesture& /*gesture*/) {
    if (!context_.reset) {
        // Translators: spoken when the configuration cannot be reloaded
        // because the command was run outside the screen reader.
        say(_("cannot revert the configuration from here"));
        return;
    }
    // Upstream's counts exactly: one press goes back to what is on disk, THREE
    // go back to the factory defaults. Two presses do nothing at all, which is
    // not an oversight -- it is the gap that keeps a double press from
    // wiping a configuration the user only wanted reloaded.
    const int count = repeatCount();
    if (count == 0) {
        ALUS_INFO("reverting to the saved configuration");
        context_.reset(/*factoryDefaults=*/false);
        // Translators: spoken when the configuration has been reloaded from
        // disk, undoing every change made since it was last saved.
        say(_("Configuration applied"));
        return;
    }
    if (count == 2) {
        ALUS_INFO("reverting to the factory defaults");
        context_.reset(/*factoryDefaults=*/true);
        // Translators: spoken when every setting has been returned to its
        // factory value.
        say(_("Configuration restored to factory defaults"));
    }
}

// ---------------------------------------------------------------------------
// N156: commands that change one setting (upstream's globalCommands). Each
// writes the configuration, makes it take effect through `apply` -- the same
// path as the settings window's OK -- and says the new state in upstream's
// words, so its Serbian catalogue translates them.
// ---------------------------------------------------------------------------

namespace {

struct FormattingToggle {
    const char* script;
    const char* key;  // in [documentFormatting]
    const char* on;   // N_()-marked
    const char* off;
};

// Translators: the document formatting switches (no key by default; they
// can be given one in the Input gestures dialog). Each is spoken after the
// change.
constexpr FormattingToggle kFormattingToggles[] = {
    {"toggleReportAlignment", "reportAlignment", N_("report alignment on"),
     N_("report alignment off")},
    {"toggleReportArticles", "reportArticles", N_("report articles on"), N_("report articles off")},
    {"toggleReportBlockQuotes", "reportBlockQuotes", N_("report block quotes on"),
     N_("report block quotes off")},
    {"toggleReportCellBorders", "reportCellBorders", N_("report cell borders on"),
     N_("report cell borders off")},
    {"toggleReportClickable", "reportClickable", N_("report if clickable on"),
     N_("report if clickable off")},
    {"toggleReportColor", "reportColor", N_("report colors on"), N_("report colors off")},
    {"toggleReportComments", "reportComments", N_("report comments on"), N_("report comments off")},
    {"toggleReportEmphasis", "reportEmphasis", N_("report emphasis on"), N_("report emphasis off")},
    {"toggleReportFigures", "reportFigures", N_("report figures and captions on"),
     N_("report figures and captions off")},
    {"toggleReportFontName", "reportFontName", N_("report font name on"),
     N_("report font name off")},
    {"toggleReportFontSize", "reportFontSize", N_("report font size on"),
     N_("report font size off")},
    {"toggleReportFrames", "reportFrames", N_("report frames on"), N_("report frames off")},
    {"toggleReportGraphics", "reportGraphics", N_("report graphics on"), N_("report graphics off")},
    {"toggleReportGroupings", "reportGroupings", N_("report groupings on"),
     N_("report groupings off")},
    {"toggleReportHeadings", "reportHeadings", N_("report headings on"), N_("report headings off")},
    {"toggleReportHighlightedText", "reportHighlight", N_("report highlighted on"),
     N_("report highlighted off")},
    {"toggleReportLandmarks", "reportLandmarks", N_("report landmarks and regions on"),
     N_("report landmarks and regions off")},
    {"toggleReportLineNumber", "reportLineNumber", N_("report line numbers on"),
     N_("report line numbers off")},
    {"toggleReportLineSpacing", "reportLineSpacing", N_("report line spacing on"),
     N_("report line spacing off")},
    {"toggleReportLinks", "reportLinks", N_("report links on"), N_("report links off")},
    {"toggleReportLists", "reportLists", N_("report lists on"), N_("report lists off")},
    {"toggleReportPage", "reportPage", N_("report pages on"), N_("report pages off")},
    {"toggleReportParagraphIndentation", "reportParagraphIndentation",
     N_("report paragraph indentation on"), N_("report paragraph indentation off")},
    {"toggleReportRevisions", "reportRevisions", N_("report revisions on"),
     N_("report revisions off")},
    {"toggleReportSpellingErrors", "reportSpellingErrors", N_("report spelling errors on"),
     N_("report spelling errors off")},
    {"toggleReportSpellingErrorsInBraille", "reportSpellingErrorsInBraille",
     N_("Report errors in braille on"), N_("Report errors in braille off")},
    {"toggleReportStyle", "reportStyle", N_("report style on"), N_("report style off")},
    {"toggleReportSuperscriptsAndSubscripts", "reportSuperscriptsAndSubscripts",
     N_("report superscripts and subscripts on"), N_("report superscripts and subscripts off")},
    {"toggleReportTableCellCoords", "reportTableCellCoords",
     N_("report table cell coordinates on"), N_("report table cell coordinates off")},
    {"toggleReportTables", "reportTables", N_("report tables on"), N_("report tables off")},
    {"toggleignoreBlankLinesForReportLineIndentation", "ignoreBlankLinesForRLI",
     N_("Ignore blank lines for line indentation reporting on"),
     N_("Ignore blank lines for line indentation reporting off")},
};

constexpr const char* kFormatting = "documentFormatting";

}  // namespace

void Commands::changed(const std::string& section) {
    if (context_.apply) {
        context_.apply(section);
    }
}

void Commands::bindFormattingToggles() {
    for (const FormattingToggle& toggle : kFormattingToggles) {
        bindScript(toggle.script, [this, &toggle](input::Gesture&) {
            if (context_.manager == nullptr) {
                return;
            }
            // The effective value, defaults included: the first press turns
            // OFF what is on by default, as the settings window shows it.
            const bool now = !context_.manager->getBool(kFormatting, toggle.key, false);
            context_.manager->setBool(kFormatting, toggle.key, now);
            changed(kFormatting);
            say(i18n::translate(now ? toggle.on : toggle.off));
        });
    }
}

void Commands::toggleReportLinkType(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    if (!context_.manager->getBool(kFormatting, "reportLinks", true)) {
        // Translators: the link type (visited or not) is part of announcing
        // links, so it cannot be switched while links are not announced.
        say(_("The report links setting must be enabled to toggle report link type"));
        return;
    }
    const bool now = !context_.manager->getBool(kFormatting, "reportLinkType", true);
    context_.manager->setBool(kFormatting, "reportLinkType", now);
    changed(kFormatting);
    // Translators: whether a link is announced as visited.
    say(now ? _("Report link type on") : _("Report link type off"));
}

void Commands::toggleReportTableHeaders(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    // Upstream's cycle and numbers: off, rows and columns, rows, columns.
    const std::int64_t now = (context_.manager->getInt(kFormatting, "reportTableHeaders", 1) + 1) % 4;
    context_.manager->setInt(kFormatting, "reportTableHeaders", now);
    changed(kFormatting);
    const char* const modes[] = {N_("Off"), N_("Rows and columns"), N_("Rows only"),
                                 N_("Columns only")};
    // Translators: reported when the table header mode changes; {mode} is
    // the new mode, e.g. "Rows and columns".
    say(i18n::substituteNamed(_("Report table headers {mode}"), "mode",
                              i18n::translate(modes[now])));
}

void Commands::toggleReportFontAttributes(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    // Upstream's four states over ALUS's two switches: off, speech, braille,
    // speech and braille.
    const bool speech = context_.manager->getBool(kFormatting, "reportFontAttributes", false);
    const bool braille =
        context_.manager->getBool(kFormatting, "reportFontAttributesInBraille", false);
    const int state = (speech ? 1 : 0) + (braille ? 2 : 0);
    const int next = (state + 1) % 4;
    context_.manager->setBool(kFormatting, "reportFontAttributes", (next & 1) != 0);
    context_.manager->setBool(kFormatting, "reportFontAttributesInBraille", (next & 2) != 0);
    changed(kFormatting);
    switch (next) {
        case 0:
            // Translators: A state in which font attributes are not reported.
            say(_("Do not report font attributes"));
            break;
        case 1:
            // Translators: A state in which font attributes are only spoken.
            say(_("Speak font attributes"));
            break;
        case 2:
            // Translators: A state in which font attributes are only brailled.
            say(_("Braille font attributes"));
            break;
        default:
            // Translators: A state in which font attributes are both spoken
            // and brailled.
            say(_("Speak and braille font attributes"));
    }
}

void Commands::toggleKeyboardLayout(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    const bool laptop =
        context_.manager->getString("keyboard", "keyboardLayout", "desktop") != "laptop";
    context_.manager->setString("keyboard", "keyboardLayout", laptop ? "laptop" : "desktop");
    changed("keyboard");
    // Translators: reported when the keyboard layout changes; {layout} is
    // "desktop" or "laptop".
    say(i18n::substituteNamed(_("{layout} layout"), "layout",
                              laptop ? _("laptop") : _("desktop")));
}

void Commands::cycleSpeechAutomaticLanguageSwitching(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    Manager& config = *context_.manager;
    // Upstream's cycle: language -> language and dialect -> off -> language.
    if (config.getBool("speech", "autoLanguageSwitching", true)) {
        if (config.getBool("speech", "autoDialectSwitching", false)) {
            config.setBool("speech", "autoLanguageSwitching", false);
            config.setBool("speech", "autoDialectSwitching", false);
            changed("speech");
            // Translators: A message reported when executing the cycle
            // automatic language switching mode command.
            say(_("Automatic language switching off"));
            return;
        }
        config.setBool("speech", "autoDialectSwitching", true);
        changed("speech");
        // Translators: A message reported when executing the cycle automatic
        // language switching mode command.
        say(_("Automatic language and dialect switching on"));
        return;
    }
    config.setBool("speech", "autoLanguageSwitching", true);
    config.setBool("speech", "autoDialectSwitching", false);
    changed("speech");
    // Translators: A message reported when executing the cycle automatic
    // language switching mode command.
    say(_("Automatic language switching on"));
}

void Commands::speech_cycleUnicodeNormalization(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    // ALUS's setting is on or off (upstream also has "default", which is off).
    const bool now = !context_.manager->getBool("speech", "unicodeNormalization", false);
    context_.manager->setBool("speech", "unicodeNormalization", now);
    changed("speech");
    // Translators: Used when reporting speech Unicode normalization state;
    // {state} is "enabled" or "disabled".
    say(i18n::substituteNamed(_("Speech Unicode normalization {state}"), "state",
                              now ? _("enabled") : _("disabled")));
}

void Commands::toggleConfigProfileTriggers(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    const bool enable = !context_.manager->triggersEnabled();
    context_.manager->setTriggersEnabled(enable);
    say(enable
            // Translators: The message announced when re-enabling all
            // configuration profile triggers.
            ? _("Configuration profile triggers enabled")
            // Translators: The message announced when temporarily disabling
            // all configuration profile triggers.
            : _("Configuration profile triggers disabled"));
}

void Commands::reportActiveConfigurationProfile(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    const std::vector<std::string> active = context_.manager->activeProfiles();
    if (active.empty()) {
        // Translators: Message announced when the command to report the
        // current configuration profile and the default configuration
        // profile is active.
        say(_("normal configuration profile active"));
        return;
    }
    // Translators: Message announced when the command to report the current
    // configuration profile is active; {profileName} is its name.
    say(i18n::substituteNamed(_("{profileName} configuration profile active"), "profileName",
                              active.back()));
}

void Commands::openUserConfigurationDirectory(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    const std::string directory = context_.manager->paths().configDir.string();
    gchar* uri = g_filename_to_uri(directory.c_str(), nullptr, nullptr);
    GError* error = nullptr;
    const bool opened =
        uri != nullptr && g_app_info_launch_default_for_uri(uri, nullptr, &error) != FALSE;
    if (!opened) {
        ALUS_WARNING("could not open {}: {}", directory,
                     error != nullptr ? error->message : "no handler");
        // Translators: spoken when the file manager could not be started on
        // the configuration folder; %s is the folder.
        say(i18n::substitute(_("Could not open %s"), directory));
    }
    if (error != nullptr) {
        g_error_free(error);
    }
    g_free(uri);
}

void Commands::restart(input::Gesture& /*gesture*/) {
    if (!context_.restart) {
        // Translators: spoken when ALUS cannot restart itself from where the
        // command was run.
        say(_("cannot restart from here"));
        return;
    }
    // Translators: spoken just before ALUS restarts itself.
    say(_("restarting"));
    context_.restart();
}

}  // namespace alus::config
