// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/core/clipboard.hpp"

#include "alus/core/log.hpp"

#include <glib.h>
#include <unistd.h>

#include <filesystem>
#include <system_error>
#include <vector>

namespace alus::core {

namespace {

// What a pipe watch owns: the output so far and what to do with it.
struct Pending {
    std::string output;
    // --set answers as soon as "ok" arrives, without waiting for the child,
    // which stays to own the clipboard; --get answers when the child is done.
    bool answerOnOk = false;
    std::function<void(const std::string& output, bool ok)> done;
};

gboolean onReadable(GIOChannel* channel, GIOCondition condition, gpointer data) {
    auto* pending = static_cast<Pending*>(data);
    bool finished = (condition & (G_IO_HUP | G_IO_ERR)) != 0;
    if ((condition & G_IO_IN) != 0) {
        gchar buffer[4096];
        gsize got = 0;
        const GIOStatus status =
            g_io_channel_read_chars(channel, buffer, sizeof buffer, &got, nullptr);
        if (got > 0) {
            pending->output.append(buffer, got);
        }
        if (status == G_IO_STATUS_EOF || status == G_IO_STATUS_ERROR) {
            finished = true;
        }
    }
    if (pending->answerOnOk && pending->output.rfind("ok\n", 0) == 0) {
        pending->done(pending->output, true);
        return FALSE;
    }
    if (finished) {
        pending->done(pending->output, !pending->answerOnOk);
        return FALSE;
    }
    return TRUE;
}

// Spawns the helper with `mode`, writes `input` to its stdin (closed after),
// and watches its stdout.
bool spawnHelper(const char* mode, const std::string& input, bool answerOnOk,
                 std::function<void(const std::string&, bool)> done) {
    const std::string program = clipboardProgram();
    if (program.empty()) {
        ALUS_WARNING("clipboard: alus-clipboard is not installed");
        return false;
    }
    std::vector<const char*> argv{program.c_str(), mode, nullptr};
    gint in = -1;
    gint out = -1;
    GError* error = nullptr;
    const gboolean started = g_spawn_async_with_pipes(
        nullptr, const_cast<char**>(argv.data()), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr,
        nullptr, input.empty() ? nullptr : &in, &out, nullptr, &error);
    if (started == FALSE) {
        ALUS_WARNING("clipboard: could not start {}: {}", program,
                     error != nullptr ? error->message : "unknown error");
        if (error != nullptr) {
            g_error_free(error);
        }
        return false;
    }
    if (in >= 0) {
        // Small texts in practice (what was just spoken); the child reads all
        // of stdin before it does anything else.
        std::size_t written = 0;
        while (written < input.size()) {
            const ssize_t now = ::write(in, input.data() + written, input.size() - written);
            if (now <= 0) {
                break;
            }
            written += static_cast<std::size_t>(now);
        }
        ::close(in);
    }
    GIOChannel* channel = g_io_channel_unix_new(out);
    g_io_channel_set_close_on_unref(channel, TRUE);
    g_io_channel_set_encoding(channel, nullptr, nullptr);
    g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, nullptr);
    auto* pending = new Pending{{}, answerOnOk, std::move(done)};
    g_io_add_watch_full(channel, G_PRIORITY_DEFAULT,
                        static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), onReadable,
                        pending, +[](gpointer data) { delete static_cast<Pending*>(data); });
    g_io_channel_unref(channel);
    return true;
}

}  // namespace

std::string clipboardProgram() {
    std::error_code code;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", code);
    if (!code) {
        const std::filesystem::path beside = self.parent_path() / "alus-clipboard";
        if (std::filesystem::exists(beside, code)) {
            return beside.string();
        }
        const std::filesystem::path built =
            self.parent_path().parent_path() / "src" / "alus-clipboard";
        if (std::filesystem::exists(built, code)) {
            return built.string();
        }
    }
    if (gchar* found = g_find_program_in_path("alus-clipboard")) {
        std::string path = found;
        g_free(found);
        return path;
    }
    return {};
}

bool readClipboard(std::function<void(std::optional<std::string>)> done) {
    return spawnHelper("--get", {}, /*answerOnOk=*/false,
                       [done = std::move(done)](const std::string& output, bool) {
                           if (output.empty()) {
                               done(std::nullopt);
                           } else {
                               done(output);
                           }
                       });
}

bool writeClipboard(const std::string& text, std::function<void(bool)> done) {
    if (text.empty()) {
        return false;
    }
    return spawnHelper("--set", text, /*answerOnOk=*/true,
                       [done = std::move(done)](const std::string&, bool ok) { done(ok); });
}

}  // namespace alus::core
