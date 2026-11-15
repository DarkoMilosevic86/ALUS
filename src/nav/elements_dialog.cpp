// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/nav/elements_dialog.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/gettext.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace alus::nav {

namespace {

std::string clean(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c == '\t' || c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return out;
}

// What the pipe watch owns: the answer so far and what to call with it.
struct Pending {
    std::string answer;
    std::function<void(const ElementsDialogChoice&)> done;
};

gboolean onOutput(GIOChannel* channel, GIOCondition condition, gpointer data) {
    auto* pending = static_cast<Pending*>(data);
    if ((condition & G_IO_IN) != 0) {
        gchar buffer[512];
        gsize got = 0;
        const GIOStatus status = g_io_channel_read_chars(channel, buffer, sizeof buffer, &got, nullptr);
        if (got > 0) {
            pending->answer.append(buffer, got);
        }
        if (status == G_IO_STATUS_NORMAL || status == G_IO_STATUS_AGAIN) {
            return TRUE;
        }
    } else if ((condition & (G_IO_HUP | G_IO_ERR)) == 0) {
        return TRUE;
    }
    // The child is gone: what it printed is the whole answer.
    const std::size_t end = pending->answer.find('\n');
    const ElementsDialogChoice choice =
        parseElementsDialogChoice(pending->answer.substr(0, end));
    if (!choice.action.empty() && pending->done) {
        pending->done(choice);
    } else {
        ALUS_DEBUG("elements list closed without a choice");
    }
    return FALSE;
}

}  // namespace

ElementsDialogChoice parseElementsDialogChoice(const std::string& line) {
    ElementsDialogChoice choice;
    const std::size_t first = line.find('\t');
    const std::size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
    if (second == std::string::npos) {
        return choice;
    }
    const std::string action = line.substr(0, first);
    if (action != "activate" && action != "move") {
        return choice;
    }
    char* endOfNumber = nullptr;
    const std::string number = line.substr(second + 1);
    const long index = std::strtol(number.c_str(), &endOfNumber, 10);
    if (number.empty() || endOfNumber == number.c_str() || index < 0) {
        return choice;
    }
    choice.action = action;
    choice.kind = line.substr(first + 1, second - first - 1);
    choice.index = static_cast<int>(index);
    return choice;
}

std::string formatElementsDialogEntries(const std::vector<ElementsDialogEntry>& entries) {
    std::string out;
    for (const ElementsDialogEntry& entry : entries) {
        out += clean(entry.kind);
        out += '\t';
        out += std::to_string(entry.level);
        out += '\t';
        out += entry.atCaret ? '1' : '0';
        out += '\t';
        out += clean(entry.label);
        out += '\n';
    }
    return out;
}

std::string elementsDialogProgram() {
    std::error_code code;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", code);
    if (!code) {
        const std::filesystem::path beside = self.parent_path() / "alus-elements";
        if (std::filesystem::exists(beside, code)) {
            return beside.string();
        }
        // The uninstalled daemon lives in build/src too, but the tests do not.
        const std::filesystem::path built = self.parent_path().parent_path() / "src" / "alus-elements";
        if (std::filesystem::exists(built, code)) {
            return built.string();
        }
    }
    if (gchar* found = g_find_program_in_path("alus-elements")) {
        std::string path = found;
        g_free(found);
        return path;
    }
    return {};
}

bool openElementsDialog(const std::vector<ElementsDialogEntry>& entries,
                        const std::string& firstKind,
                        std::function<void(const ElementsDialogChoice&)> done) {
    const std::string program = elementsDialogProgram();
    if (program.empty()) {
        ALUS_WARNING("elements list: alus-elements is not installed");
        return false;
    }
    gchar* path = nullptr;
    GError* error = nullptr;
    const gint fd = g_file_open_tmp("alus-elements-XXXXXX", &path, &error);
    if (fd < 0) {
        ALUS_WARNING("elements list: no temporary file: {}",
                     error != nullptr ? error->message : "unknown error");
        if (error != nullptr) {
            g_error_free(error);
        }
        return false;
    }
    const std::string text = formatElementsDialogEntries(entries);
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t now = ::write(fd, text.data() + written, text.size() - written);
        if (now <= 0) {
            break;
        }
        written += static_cast<std::size_t>(now);
    }
    ::close(fd);
    const std::string itemsPath = path;
    g_free(path);
    if (written < text.size()) {
        g_remove(itemsPath.c_str());
        return false;
    }

    const std::string language = i18n::currentLanguage();
    std::vector<const char*> argv{program.c_str(), "--items", itemsPath.c_str(), "--type",
                                  firstKind.c_str()};
    if (!language.empty()) {
        argv.push_back("--language");
        argv.push_back(language.c_str());
    }
    argv.push_back(nullptr);
    gint out = -1;
    // Asynchronous: the screen reader's loop never waits for a window
    // (src/gui/service.cpp). GLib reaps the child.
    const gboolean started = g_spawn_async_with_pipes(
        nullptr, const_cast<char**>(argv.data()), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr,
        nullptr, nullptr, &out, nullptr, &error);
    if (started == FALSE) {
        ALUS_WARNING("elements list: could not start {}: {}", program,
                     error != nullptr ? error->message : "unknown error");
        if (error != nullptr) {
            g_error_free(error);
        }
        g_remove(itemsPath.c_str());
        return false;
    }
    GIOChannel* channel = g_io_channel_unix_new(out);
    g_io_channel_set_close_on_unref(channel, TRUE);
    g_io_channel_set_encoding(channel, nullptr, nullptr);
    g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, nullptr);
    auto* pending = new Pending{{}, std::move(done)};
    g_io_add_watch_full(channel, G_PRIORITY_DEFAULT,
                        static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), onOutput,
                        pending, +[](gpointer data) { delete static_cast<Pending*>(data); });
    g_io_channel_unref(channel);
    ALUS_INFO("elements list: {} elements", entries.size());
    return true;
}

}  // namespace alus::nav
