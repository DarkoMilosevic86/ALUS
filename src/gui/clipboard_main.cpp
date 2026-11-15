// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `alus-clipboard` — the system clipboard for a screen reader with no window
// (phase-10.md N145).
//
// WHY A PROCESS OF ITS OWN, AND WHY X11. A Wayland compositor hands the
// clipboard only to the client with the keyboard focus, and ALUS has no window
// and never has the focus: read through Wayland, the clipboard is always
// empty (measured). GNOME's compositor bridges the clipboard between Wayland
// and Xwayland, and an X11 client may read and own the CLIPBOARD selection
// without a window being focused (measured: set by one X11 client, read by
// another). So this program forces GDK's X11 backend and does one thing.
//
//   alus-clipboard --get   prints the clipboard's text on stdout and exits 0;
//                          prints nothing and exits 3 when there is no text.
//   alus-clipboard --set   reads the text from stdin and takes the clipboard.
//                          It prints "ok" once it owns it, and then STAYS: an
//                          X11 selection lives as long as its owner, so it
//                          exits only when somebody else takes the clipboard,
//                          or after an hour.
//
// Exit 2 for every failure (no Xwayland, no display, bad arguments).

#include <glib.h>
#include <gtk/gtk.h>

#include <cstdio>
#include <iostream>
#include <iterator>
#include <string>

namespace {

constexpr guint kGetTimeoutSeconds = 5;
constexpr guint kOwnershipSeconds = 3600;

struct State {
    bool done = false;
    int status = 2;
};

int getText() {
    GdkDisplay* display = gdk_display_get_default();
    if (display == nullptr) {
        std::fprintf(stderr, "alus-clipboard: no X11 display\n");
        return 2;
    }
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    State state;
    gdk_clipboard_read_text_async(
        clipboard, nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer data) {
            auto* self = static_cast<State*>(data);
            GError* error = nullptr;
            char* text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, &error);
            if (text != nullptr && *text != '\0') {
                std::fputs(text, stdout);
                std::fflush(stdout);
                self->status = 0;
            } else {
                self->status = 3;  // empty, or not text
            }
            g_free(text);
            if (error != nullptr) {
                g_error_free(error);
            }
            self->done = true;
        },
        &state);
    const gint64 deadline = g_get_monotonic_time() + kGetTimeoutSeconds * G_USEC_PER_SEC;
    while (!state.done && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(nullptr, TRUE);
    }
    return state.done ? state.status : 2;
}

int setText() {
    const std::string text{std::istreambuf_iterator<char>(std::cin),
                           std::istreambuf_iterator<char>()};
    if (text.empty()) {
        return 2;
    }
    GdkDisplay* display = gdk_display_get_default();
    if (display == nullptr) {
        std::fprintf(stderr, "alus-clipboard: no X11 display\n");
        return 2;
    }
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    gdk_clipboard_set_text(clipboard, text.c_str());
    if (gdk_clipboard_is_local(clipboard) == FALSE) {
        return 2;
    }
    std::puts("ok");
    std::fflush(stdout);
    // Held until someone else copies something: that is the moment the
    // selection stops being ours, and the process has nothing left to do.
    State state;
    g_signal_connect(clipboard, "changed", G_CALLBACK(+[](GdkClipboard* board, gpointer data) {
                         if (gdk_clipboard_is_local(board) == FALSE) {
                             static_cast<State*>(data)->done = true;
                         }
                     }),
                     &state);
    const gint64 deadline = g_get_monotonic_time() + kOwnershipSeconds * G_USEC_PER_SEC;
    while (!state.done && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(nullptr, TRUE);
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode != "--get" && mode != "--set") {
        std::fprintf(stderr, "usage: alus-clipboard --get | --set\n");
        return 2;
    }
    // Before GTK starts: the whole point is the X11 backend (see above).
    g_setenv("GDK_BACKEND", "x11", TRUE);
    if (!gtk_init_check()) {
        std::fprintf(stderr, "alus-clipboard: cannot open the X11 display (is Xwayland running?)\n");
        return 2;
    }
    return mode == "--get" ? getText() : setText();
}
