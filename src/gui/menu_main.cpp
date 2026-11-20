// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `alus-menu` — the ALUS menu (alus+a), as its own process.
//
// NVDA's alus+n opens a menu: Preferences, Help, Donate, the configuration
// commands and Exit. ALUS's key used to open the settings window directly
// (src/gui/service.cpp said why); the user asked for the menu.
//
// ITS OWN PROCESS for the reason every window of ALUS is one (src/gui/main.cpp,
// phase-9.md N79): the screen reader cannot read a window in its own process.
//
// What it does itself: About, Donate, and opening the user guide and What's
// new in the browser -- all of it this process's own windows or a URI.
// What the DAEMON does: everything that changes the screen reader. The choice
// goes back as one line on stdout, exactly as alus-elements answers:
//
//   settings   open the settings window          gestures  the input gestures
//   commands   the commands quick reference      save      save the configuration
//   revert     back to the saved configuration   reset     back to factory defaults
//   exit       quit ALUS
//
// Nothing printed: the menu was closed with Escape, or the action was this
// process's own.

#include "alus/core/log.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/i18n/service.hpp"

#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>

#ifndef ALUS_VERSION
#define ALUS_VERSION "0"
#endif
#ifndef ALUS_DOC_DIR
#define ALUS_DOC_DIR ""
#endif

namespace {

// The donation addresses, as the author asked for them (2026.1.0): PayPal and
// Google Pay, both on the same address.
constexpr const char* kDonationAddress = "daremc86@gmail.com";
constexpr const char* kPayPalUrl =
    "https://www.paypal.com/donate/?business=daremc86%40gmail.com&no_recurring=0&currency_code=EUR";
constexpr const char* kProjectUrl = "https://github.com/DarkoMilosevic86/ALUS";

struct Menu {
    GtkWidget* window = nullptr;
    // The list that is the menu.
    GtkWidget* anchor = nullptr;
    // A dialog this process opened, which keeps it alive after the menu.
    int openDialogs = 0;
    // A URI being handed to the browser.
    int launching = 0;
    bool chosen = false;
    bool done = false;
    // Showing the Help submenu rather than the menu itself.
    bool inHelp = false;
    std::string language;
};

Menu& menu() {
    static Menu value;
    return value;
}

void finishIfIdle() {
    Menu& m = menu();
    if (m.openDialogs == 0 && m.launching == 0) {
        m.done = true;
    }
}

// An answer for the daemon, and the end of this process.
void answer(const char* action) {
    std::printf("%s\n", action);
    std::fflush(stdout);
    menu().chosen = true;
    menu().done = true;
}

// ---------------------------------------------------------------------------
// Help: the user guide and What's new, in the user's language
// ---------------------------------------------------------------------------

// The language directory of the help: "sr" (Cyrillic), "sr@latin", or "en".
std::string helpLanguage() {
    const std::string& language = menu().language.empty() ? alus::i18n::currentLanguage()
                                                          : menu().language;
    if (language.rfind("sr", 0) == 0) {
        return language.find("@latin") != std::string::npos ? "sr@latin" : "sr";
    }
    return "en";
}

// Where the HTML help is: the build tree's when this program runs from one
// (the same rule as core::findDataDirectory), else the installed one.
std::filesystem::path helpDirectory() {
    std::error_code code;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", code);
    if (!code) {
        const std::filesystem::path buildDir = self.parent_path().parent_path();
        if (std::filesystem::exists(buildDir / "meson-info", code) &&
            std::filesystem::exists(buildDir / "docs" / "help", code)) {
            return buildDir / "docs" / "help";
        }
    }
    return std::filesystem::path{ALUS_DOC_DIR};
}

void openHelp(const char* page) {
    std::filesystem::path file = helpDirectory() / helpLanguage() / page;
    std::error_code code;
    if (!std::filesystem::exists(file, code)) {
        file = helpDirectory() / "en" / page;
    }
    gchar* uri = g_filename_to_uri(file.c_str(), nullptr, nullptr);
    if (uri == nullptr) {
        finishIfIdle();
        return;
    }
    GtkUriLauncher* launcher = gtk_uri_launcher_new(uri);
    g_free(uri);
    ++menu().launching;
    gtk_uri_launcher_launch(
        launcher, GTK_WINDOW(menu().window), nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer) {
            GError* error = nullptr;
            if (gtk_uri_launcher_launch_finish(GTK_URI_LAUNCHER(source), result, &error) ==
                FALSE) {
                std::fprintf(stderr, "alus-menu: could not open the help: %s\n",
                             error != nullptr ? error->message : "unknown error");
                if (error != nullptr) {
                    g_error_free(error);
                }
            }
            g_object_unref(source);
            --menu().launching;
            finishIfIdle();
        },
        nullptr);
}

// ---------------------------------------------------------------------------
// About and Donate: small dialogs whose text is read as a dialog's message
// ---------------------------------------------------------------------------

// A dialog whose message is a read-only text the user can also walk with the
// arrows, and the given buttons. The window has the dialog role, so ALUS reads
// the message when it opens (NVDA's Dialog behaviour).
GtkWidget* messageDialog(const std::string& title, const std::string& message) {
    GtkWidget* dialog = GTK_WIDGET(
        g_object_new(GTK_TYPE_WINDOW, "accessible-role", GTK_ACCESSIBLE_ROLE_DIALOG, nullptr));
    gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 360);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), message.c_str(), -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   title.c_str(), -1);
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), view);
    gtk_box_append(GTK_BOX(box), scroller);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    gtk_widget_set_name(buttons, "buttons");
    gtk_box_append(GTK_BOX(box), buttons);
    g_object_set_data(G_OBJECT(dialog), "alus-buttons", buttons);
    g_object_set_data(G_OBJECT(dialog), "alus-view", view);

    // Escape closes, as in every dialog.
    GtkEventController* shortcuts = gtk_shortcut_controller_new();
    gtk_shortcut_controller_add_shortcut(
        GTK_SHORTCUT_CONTROLLER(shortcuts),
        gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Escape, static_cast<GdkModifierType>(0)),
                         gtk_named_action_new("window.close")));
    gtk_widget_add_controller(dialog, shortcuts);

    ++menu().openDialogs;
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) {
                         --menu().openDialogs;
                         finishIfIdle();
                     }),
                     nullptr);
    return dialog;
}

GtkWidget* addButton(GtkWidget* dialog, const char* mnemonicLabel, GCallback callback,
                     gpointer data) {
    auto* buttons = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(dialog), "alus-buttons"));
    GtkWidget* button = gtk_button_new_with_mnemonic(mnemonicLabel);
    g_signal_connect(button, "clicked", callback, data);
    gtk_box_append(GTK_BOX(buttons), button);
    return button;
}

void addCloseButton(GtkWidget* dialog) {
    // Translators: closes the About and Donate dialogs of the ALUS menu.
    GtkWidget* close = addButton(dialog, _("_Close"),
                                 G_CALLBACK(+[](GtkButton*, gpointer window) {
                                     gtk_window_destroy(GTK_WINDOW(window));
                                 }),
                                 dialog);
    gtk_window_set_default_widget(GTK_WINDOW(dialog), close);
}

void showAbout() {
    const std::string message = alus::i18n::substitute(
        // Translators: the text of the About dialog of the ALUS menu. %s is the
        // version, e.g. "2026.1.0 beta".
        _("ALUS (Access Linux Using Speech), version %s\n\n"
          "A screen reader for Linux: speech and braille for the GNOME desktop, the web, "
          "LibreOffice, mail and the terminal.\n\n"
          "Copyright (C) 2026 Darko Milošević.\n\n"
          "ALUS is free software, released under the GNU General Public License, version 2 "
          "or later. Its design follows the screen reader it was ported from, whose data and "
          "translations it builds on.\n\n"
          "Project page: https://github.com/DarkoMilosevic86/ALUS"),
        std::string{ALUS_VERSION} + " beta");
    // Translators: the title of the About dialog of the ALUS menu.
    GtkWidget* dialog = messageDialog(_("About ALUS"), message);
    addCloseButton(dialog);
    gtk_window_present(GTK_WINDOW(dialog));
}

void showDonate() {
    const std::string message = alus::i18n::substitute(
        // Translators: the text of the Donate dialog of the ALUS menu. Both %s
        // are the author's donation address.
        _("ALUS is free, and it is made by one person. If it helps you, you can support its "
          "development with a donation.\n\n"
          "PayPal: %s\n"
          "Google Pay: %s\n\n"
          "Open PayPal opens the donation page in your browser. For Google Pay, copy the "
          "address and send the donation from the Google Pay app.\n\n"
          "Thank you."),
        kDonationAddress, kDonationAddress);
    // Translators: the title of the Donate dialog of the ALUS menu.
    GtkWidget* dialog = messageDialog(_("Donate"), message);
    // Translators: a button in the Donate dialog; opens PayPal's donation page.
    GtkWidget* paypal = addButton(
        dialog, _("Open _PayPal"),
        G_CALLBACK(+[](GtkButton*, gpointer window) {
            GtkUriLauncher* launcher = gtk_uri_launcher_new(kPayPalUrl);
            gtk_uri_launcher_launch(
                launcher, GTK_WINDOW(window), nullptr,
                +[](GObject* source, GAsyncResult* result, gpointer) {
                    gtk_uri_launcher_launch_finish(GTK_URI_LAUNCHER(source), result, nullptr);
                    g_object_unref(source);
                },
                nullptr);
        }),
        dialog);
    // Translators: a button in the Donate dialog; copies the donation address
    // for Google Pay.
    addButton(dialog, _("Copy the _address"),
              G_CALLBACK(+[](GtkButton* button, gpointer) {
                  gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(button)),
                                         kDonationAddress);
                  // Translators: said by the Donate dialog after the address
                  // was copied.
                  gtk_accessible_announce(GTK_ACCESSIBLE(button), _("Address copied"),
                                          GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
              }),
              nullptr);
    addCloseButton(dialog);
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_grab_focus(paypal);
}

// ---------------------------------------------------------------------------
// The menu
// ---------------------------------------------------------------------------

// WHY NOT GtkPopoverMenu. It was the first version, and measured over AT-SPI
// on GTK 4.18 every item was a "menu item" with NO NAME: the label is behind
// a labelled-by relation whose target is not in the accessible tree
// ("invalid"). Any GMenuModel menu reads like that. So the menu is built
// from widgets whose names ALUS sets: a list with the menu role, rows with
// the menu item role, each named explicitly.

struct Item {
    const char* action;     // what onAction does; "help" opens the submenu
    const char* label;      // N_()-marked, with "_" before the mnemonic letter
    bool submenu = false;
};

// Translators: items of the ALUS menu (alus+a). The letter after "_" is the
// key that chooses the item at once.
const Item kMainItems[] = {
    {"settings", N_("_Settings…")},
    {"gestures", N_("_Input gestures…")},
    {"help", N_("_Help"), true},
    {"donate", N_("_Donate…")},
    {"save", N_("Sa_ve configuration")},
    {"revert", N_("_Revert to saved configuration")},
    {"reset", N_("Reset configuration to _factory defaults")},
    {"exit", N_("E_xit")},
};

// Translators: items of the Help submenu of the ALUS menu.
const Item kHelpItems[] = {
    {"guide", N_("_User guide")},
    {"commands", N_("_Commands quick reference")},
    {"news", N_("What's _new")},
    {"about", N_("_About…")},
};

// The label without its "_", and the mnemonic letter (lower case), if any.
std::string plainLabel(const char* label, gunichar* mnemonic) {
    std::string out;
    *mnemonic = 0;
    for (const char* at = label; *at != '\0'; ++at) {
        if (*at == '_' && at[1] != '\0' && *mnemonic == 0) {
            *mnemonic = g_unichar_tolower(g_utf8_get_char(at + 1));
            continue;
        }
        out += *at;
    }
    return out;
}

void choose(const Item& item);

void fill(bool help) {
    Menu& m = menu();
    GtkListBox* list = GTK_LIST_BOX(m.anchor);
    gtk_list_box_remove_all(list);
    m.inHelp = help;
    const Item* items = help ? kHelpItems : kMainItems;
    const std::size_t count = help ? std::size(kHelpItems) : std::size(kMainItems);
    for (std::size_t index = 0; index < count; ++index) {
        gunichar mnemonic = 0;
        std::string text = plainLabel(_(items[index].label), &mnemonic);
        GtkWidget* label = gtk_label_new_with_mnemonic(_(items[index].label));
        gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
        gtk_widget_set_margin_start(label, 12);
        gtk_widget_set_margin_end(label, 12);
        gtk_widget_set_margin_top(label, 4);
        gtk_widget_set_margin_bottom(label, 4);
        GtkWidget* row = GTK_WIDGET(g_object_new(GTK_TYPE_LIST_BOX_ROW, "accessible-role",
                                                 GTK_ACCESSIBLE_ROLE_MENU_ITEM, nullptr));
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_accessible_update_property(GTK_ACCESSIBLE(row), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       text.c_str(), -1);
        if (items[index].submenu) {
            gtk_accessible_update_property(GTK_ACCESSIBLE(row),
                                           GTK_ACCESSIBLE_PROPERTY_HAS_POPUP, TRUE, -1);
        }
        if (mnemonic != 0) {
            char key[8] = {0};
            g_unichar_to_utf8(mnemonic, key);
            // Translators: how the key that chooses a menu item is announced,
            // e.g. "S"; usually left as it is.
            gtk_accessible_update_property(GTK_ACCESSIBLE(row),
                                           GTK_ACCESSIBLE_PROPERTY_KEY_SHORTCUTS, key, -1);
        }
        g_object_set_data(G_OBJECT(row), "alus-item", const_cast<Item*>(&items[index]));
        g_object_set_data(G_OBJECT(row), "alus-mnemonic", GUINT_TO_POINTER(mnemonic));
        gtk_list_box_append(list, row);
    }
    // Translators: the name of the ALUS menu and of its Help submenu.
    gtk_accessible_update_property(GTK_ACCESSIBLE(m.anchor), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   help ? _("Help") : _("ALUS menu"), -1);
    if (GtkListBoxRow* first = gtk_list_box_get_row_at_index(list, 0)) {
        gtk_list_box_select_row(list, first);
        gtk_widget_grab_focus(GTK_WIDGET(first));
    }
}

void choose(const Item& item) {
    Menu& m = menu();
    if (item.submenu) {
        fill(/*help=*/true);
        return;
    }
    const std::string name = item.action;
    m.chosen = true;
    gtk_widget_set_visible(m.window, FALSE);
    if (name == "guide") {
        openHelp("user-guide.html");
    } else if (name == "news") {
        openHelp("whats-new.html");
    } else if (name == "about") {
        showAbout();
    } else if (name == "donate") {
        showDonate();
    } else {
        answer(name.c_str());
    }
}

void build() {
    Menu& m = menu();
    m.window = gtk_window_new();
    // Translators: the title of the ALUS menu's window.
    gtk_window_set_title(GTK_WINDOW(m.window), _("ALUS menu"));
    gtk_window_set_default_size(GTK_WINDOW(m.window), 360, 300);
    gtk_window_set_resizable(GTK_WINDOW(m.window), FALSE);

    m.anchor = GTK_WIDGET(
        g_object_new(GTK_TYPE_LIST_BOX, "accessible-role", GTK_ACCESSIBLE_ROLE_MENU, nullptr));
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(m.anchor), GTK_SELECTION_BROWSE);
    gtk_window_set_child(GTK_WINDOW(m.window), m.anchor);
    g_signal_connect(m.anchor, "row-activated",
                     G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer) {
                         if (const auto* item = static_cast<const Item*>(
                                 g_object_get_data(G_OBJECT(row), "alus-item"))) {
                             choose(*item);
                         }
                     }),
                     nullptr);

    // The keys of a menu: Escape and Left leave the submenu (or the menu),
    // Right opens it, a mnemonic letter chooses at once.
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed",
                     G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType state,
                                    gpointer) -> gboolean {
                         Menu& self = menu();
                         if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != 0) {
                             return FALSE;
                         }
                         if (keyval == GDK_KEY_Escape || keyval == GDK_KEY_Left) {
                             if (self.inHelp) {
                                 fill(/*help=*/false);
                                 // Back on Help, where the user left the menu.
                                 if (GtkListBoxRow* help = gtk_list_box_get_row_at_index(
                                         GTK_LIST_BOX(self.anchor), 2)) {
                                     gtk_list_box_select_row(GTK_LIST_BOX(self.anchor), help);
                                     gtk_widget_grab_focus(GTK_WIDGET(help));
                                 }
                                 return TRUE;
                             }
                             if (keyval == GDK_KEY_Escape) {
                                 self.done = true;
                                 return TRUE;
                             }
                             return FALSE;
                         }
                         GtkListBoxRow* selected =
                             gtk_list_box_get_selected_row(GTK_LIST_BOX(self.anchor));
                         if (keyval == GDK_KEY_Right && selected != nullptr) {
                             const auto* item = static_cast<const Item*>(
                                 g_object_get_data(G_OBJECT(selected), "alus-item"));
                             if (item != nullptr && item->submenu) {
                                 choose(*item);
                                 return TRUE;
                             }
                             return FALSE;
                         }
                         const gunichar typed = g_unichar_tolower(gdk_keyval_to_unicode(keyval));
                         if (typed == 0 || !g_unichar_isalnum(typed)) {
                             return FALSE;
                         }
                         for (int index = 0;; ++index) {
                             GtkListBoxRow* row =
                                 gtk_list_box_get_row_at_index(GTK_LIST_BOX(self.anchor), index);
                             if (row == nullptr) {
                                 break;
                             }
                             if (GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "alus-mnemonic")) ==
                                 typed) {
                                 if (const auto* item = static_cast<const Item*>(
                                         g_object_get_data(G_OBJECT(row), "alus-item"))) {
                                     choose(*item);
                                 }
                                 return TRUE;
                             }
                         }
                         return FALSE;
                     }),
                     nullptr);
    gtk_widget_add_controller(m.window, keys);

    g_signal_connect(m.window, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer) {
                         if (menu().openDialogs == 0 && menu().launching == 0) {
                             menu().done = true;
                         }
                         return FALSE;
                     }),
                     nullptr);

    fill(/*help=*/false);
    gtk_window_present(GTK_WINDOW(m.window));
}

}  // namespace

int main(int argc, char* argv[]) {
    alus::core::Log::instance().setLevel(alus::core::LogLevel::Warning);
    alus::i18n::initializeFromEnvironment();

    gchar* language = nullptr;
    const GOptionEntry entries[] = {
        {"language", 0, 0, G_OPTION_ARG_STRING, &language, "Language code", "CODE"},
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
    };
    GOptionContext* context = g_option_context_new("- the ALUS menu");
    g_option_context_add_main_entries(context, entries, nullptr);
    GError* error = nullptr;
    const gboolean parsed = g_option_context_parse(context, &argc, &argv, &error);
    g_option_context_free(context);
    if (parsed == FALSE) {
        std::fprintf(stderr, "alus-menu: %s\n",
                     error != nullptr ? error->message : "bad arguments");
        if (error != nullptr) {
            g_error_free(error);
        }
        return 2;
    }
    menu().language = language != nullptr ? language : "";
    g_free(language);

    alus::i18n::Options i18nOptions;
    i18nOptions.language = menu().language;
    alus::i18n::initialize(i18nOptions);

    if (!gtk_init_check()) {
        std::fprintf(stderr, "alus-menu: no display\n");
        return 1;
    }
    build();
    while (!menu().done) {
        g_main_context_iteration(nullptr, TRUE);
    }
    // The menu window itself goes as soon as the menu is done; a dialog it
    // opened has already been waited for above.
    if (menu().window != nullptr) {
        gtk_window_destroy(GTK_WINDOW(menu().window));
    }
    return 0;
}
