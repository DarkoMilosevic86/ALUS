// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `alus-elements` — the browse mode elements list (alus+f7), as its own
// process.
//
// ITS OWN PROCESS for the reason `alus-settings` is (src/gui/main.cpp,
// docs/history/phase-9.md N79): a window in the screen reader's own process
// cannot be read, because ALUS's synchronous AT-SPI calls about it would be
// answered by the loop they block.
//
// The protocol is two files' worth of text, nothing more:
//
//   in   --items FILE: one element per line,
//        "kind<TAB>level<TAB>caret<TAB>label", in document order. The kinds
//        are link, heading, formField, button and landmark; caret is 1 on the
//        one element per kind the list opens on (the one at the browse mode
//        caret) and 0 elsewhere. The daemon writes the file; this process
//        deletes it once read.
//   out  stdout, one line when the user chooses: "activate<TAB>kind<TAB>index"
//        or "move<TAB>kind<TAB>index", the index counting that kind's lines in
//        the file. Nothing at all for Cancel, Escape or closing the window.
//
// The daemon keeps the objects; this process only ever sees their labels.
//
// The dialog is NVDA's: the element type as radio buttons, the list, a filter,
// and Activate (the default: Enter), Move to, and Cancel (Escape). The list
// is a tree, as NVDA's is: a heading's row carries its level, which is read
// when it changes ("level 2"), and is indented by it. Every row is a top
// level row, though; nothing folds.

#include "alus/core/log.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/i18n/service.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Element {
    std::string kind;
    int level = 0;
    std::string label;
    // Its index among the elements of its own kind: what the daemon is told.
    int index = 0;
    // The one of its kind the list opens on: the element at the caret.
    bool atCaret = false;
};

struct Kind {
    const char* id;
    const char* label;  // N_()-marked; translated when shown
};

// NVDA's order.
const std::array<Kind, 5> kKinds{{
    // Translators: an element type in the elements list (alus+f7).
    {"link", N_("Lin_ks")},
    // Translators: an element type in the elements list (alus+f7).
    {"heading", N_("_Headings")},
    // Translators: an element type in the elements list (alus+f7).
    {"formField", N_("_Form fields")},
    // Translators: an element type in the elements list (alus+f7).
    {"button", N_("_Buttons")},
    // Translators: an element type in the elements list (alus+f7).
    {"landmark", N_("Lan_dmarks")},
}};

struct Dialog {
    std::vector<Element> elements;
    std::string kind = "link";
    GtkWidget* window = nullptr;
    GtkWidget* list = nullptr;
    GtkWidget* filter = nullptr;
    // What each row of the list shows, by position in the list.
    std::vector<const Element*> shown;
    bool done = false;
};

std::string folded(const std::string& text) {
    gchar* value = g_utf8_casefold(text.c_str(), -1);
    std::string out = value != nullptr ? value : "";
    g_free(value);
    return out;
}

// The type's name without its mnemonic, for the list's accessible name.
std::string kindLabel(const std::string& id) {
    for (const Kind& kind : kKinds) {
        if (id == kind.id) {
            std::string label = _(kind.label);
            std::erase(label, '_');
            return label;
        }
    }
    return {};
}

void fillList(Dialog& dialog) {
    GtkListBox* list = GTK_LIST_BOX(dialog.list);
    gtk_list_box_remove_all(list);
    dialog.shown.clear();
    const std::string needle = folded(gtk_editable_get_text(GTK_EDITABLE(dialog.filter)));
    GtkListBoxRow* selected = nullptr;
    for (const Element& element : dialog.elements) {
        if (element.kind != dialog.kind) {
            continue;
        }
        if (!needle.empty() && folded(element.label).find(needle) == std::string::npos) {
            continue;
        }
        GtkWidget* label = gtk_label_new(element.label.c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        // Headings are indented by level, as NVDA's tree shows them.
        if (element.level > 1) {
            gtk_widget_set_margin_start(label, 16 * (element.level - 1));
        }
        // The row is named by hand: GTK does not name a list row from the
        // label inside it, and a nameless row is read as just "list". A tree
        // item, so that the heading level below is exported (the "level"
        // attribute) and read like any tree's; the role itself is silent on
        // focus, as a list item's is.
        GtkWidget* row = GTK_WIDGET(g_object_new(GTK_TYPE_LIST_BOX_ROW, "accessible-role",
                                                 GTK_ACCESSIBLE_ROLE_TREE_ITEM, nullptr));
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_accessible_update_property(GTK_ACCESSIBLE(row), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       element.label.c_str(), -1);
        if (element.kind == "heading" && element.level > 0) {
            gtk_accessible_update_property(GTK_ACCESSIBLE(row), GTK_ACCESSIBLE_PROPERTY_LEVEL,
                                           element.level, -1);
        }
        gtk_list_box_append(list, row);
        if (selected == nullptr && needle.empty() && element.atCaret) {
            selected = GTK_LIST_BOX_ROW(row);
        }
        dialog.shown.push_back(&element);
    }
    // The list is named after what it lists, so arriving in it says "Links".
    gtk_accessible_update_property(GTK_ACCESSIBLE(dialog.list), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   kindLabel(dialog.kind).c_str(), -1);
    // The element at the caret, as NVDA opens; while filtering, the first
    // match, which is what the typed text is looking for.
    if (selected == nullptr) {
        selected = gtk_list_box_get_row_at_index(list, 0);
    }
    if (selected != nullptr) {
        gtk_list_box_select_row(list, selected);
    }
}

// Prints the choice and closes. Nothing chosen (an empty list): nothing
// printed, and the window stays, as NVDA's does.
void choose(Dialog& dialog, const char* action) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(dialog.list));
    if (row == nullptr) {
        gtk_widget_error_bell(dialog.window);
        return;
    }
    const int position = gtk_list_box_row_get_index(row);
    if (position < 0 || position >= static_cast<int>(dialog.shown.size())) {
        return;
    }
    const Element& element = *dialog.shown[static_cast<std::size_t>(position)];
    std::printf("%s\t%s\t%d\n", action, element.kind.c_str(), element.index);
    std::fflush(stdout);
    gtk_window_close(GTK_WINDOW(dialog.window));
}

bool readItems(const std::string& path, Dialog& dialog) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t first = line.find('\t');
        const std::size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
        const std::size_t third = second == std::string::npos ? second : line.find('\t', second + 1);
        if (third == std::string::npos) {
            continue;
        }
        Element element;
        element.kind = line.substr(0, first);
        element.level = std::atoi(line.substr(first + 1, second - first - 1).c_str());
        element.atCaret = line.substr(second + 1, third - second - 1) == "1";
        element.label = line.substr(third + 1);
        int index = 0;
        for (const Element& before : dialog.elements) {
            if (before.kind == element.kind) {
                ++index;
            }
        }
        element.index = index;
        dialog.elements.push_back(std::move(element));
    }
    return true;
}

GtkWidget* labelled(GtkWidget* label, GtkWidget* widget) {
    gtk_accessible_update_relation(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_RELATION_LABELLED_BY,
                                   label, nullptr, -1);
    return widget;
}

void build(Dialog& dialog) {
    dialog.window = gtk_window_new();
    // Translators: the title of the browse mode elements list (alus+f7).
    gtk_window_set_title(GTK_WINDOW(dialog.window), _("Elements List"));
    gtk_window_set_default_size(GTK_WINDOW(dialog.window), 560, 520);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_window_set_child(GTK_WINDOW(dialog.window), box);

    // The element type.
    // Translators: the label of the element type radio buttons in the
    // elements list.
    GtkWidget* typeLabel = gtk_label_new(_("Type:"));
    gtk_label_set_xalign(GTK_LABEL(typeLabel), 0.0F);
    gtk_box_append(GTK_BOX(box), typeLabel);
    GtkWidget* types = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_accessible_update_property(GTK_ACCESSIBLE(types), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   _("Type:"), -1);
    GtkWidget* group = nullptr;
    for (const Kind& kind : kKinds) {
        GtkWidget* radio = gtk_check_button_new_with_mnemonic(_(kind.label));
        // Without this the accessible name keeps the mnemonic's underscore.
        gtk_accessible_update_property(GTK_ACCESSIBLE(radio), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       kindLabel(kind.id).c_str(), -1);
        if (group != nullptr) {
            gtk_check_button_set_group(GTK_CHECK_BUTTON(radio), GTK_CHECK_BUTTON(group));
        } else {
            group = radio;
        }
        g_object_set_data(G_OBJECT(radio), "alus-kind", const_cast<char*>(kind.id));
        if (dialog.kind == kind.id) {
            gtk_check_button_set_active(GTK_CHECK_BUTTON(radio), TRUE);
        }
        g_signal_connect(radio, "toggled", G_CALLBACK(+[](GtkCheckButton* button, gpointer data) {
                             if (gtk_check_button_get_active(button) == FALSE) {
                                 return;
                             }
                             auto* self = static_cast<Dialog*>(data);
                             self->kind = static_cast<const char*>(
                                 g_object_get_data(G_OBJECT(button), "alus-kind"));
                             fillList(*self);
                         }),
                         &dialog);
        gtk_box_append(GTK_BOX(types), radio);
    }
    gtk_box_append(GTK_BOX(box), types);

    // The list.
    // A tree, for the heading levels; see fillList.
    dialog.list = GTK_WIDGET(
        g_object_new(GTK_TYPE_LIST_BOX, "accessible-role", GTK_ACCESSIBLE_ROLE_TREE, nullptr));
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(dialog.list), GTK_SELECTION_BROWSE);
    // Tab leaves the list for the next control; the arrows move in it.
    gtk_list_box_set_tab_behavior(GTK_LIST_BOX(dialog.list), GTK_LIST_TAB_ITEM);
    // Enter on an element activates it.
    g_signal_connect(dialog.list, "row-activated",
                     G_CALLBACK(+[](GtkListBox*, GtkListBoxRow*, gpointer data) {
                         choose(*static_cast<Dialog*>(data), "activate");
                     }),
                     &dialog);
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), dialog.list);
    gtk_box_append(GTK_BOX(box), scroller);

    // The filter.
    // Translators: the label of the filter field in the elements list.
    GtkWidget* filterLabel = gtk_label_new_with_mnemonic(_("Filter b_y:"));
    gtk_label_set_xalign(GTK_LABEL(filterLabel), 0.0F);
    dialog.filter = labelled(filterLabel, gtk_entry_new());
    gtk_label_set_mnemonic_widget(GTK_LABEL(filterLabel), dialog.filter);
    g_signal_connect(dialog.filter, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
                         fillList(*static_cast<Dialog*>(data));
                     }),
                     &dialog);
    // Enter in the filter is the default button, Activate.
    g_signal_connect(dialog.filter, "activate", G_CALLBACK(+[](GtkEntry*, gpointer data) {
                         choose(*static_cast<Dialog*>(data), "activate");
                     }),
                     &dialog);
    gtk_box_append(GTK_BOX(box), filterLabel);
    gtk_box_append(GTK_BOX(box), dialog.filter);

    // The buttons.
    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    // Translators: a button in the elements list: activates the element (a
    // link is followed, a button pressed). Enter does the same.
    GtkWidget* activate = gtk_button_new_with_mnemonic(_("_Activate"));
    // Translators: a button in the elements list: moves the browse mode caret
    // to the element and closes the list.
    GtkWidget* move = gtk_button_new_with_mnemonic(_("_Move to"));
    // Translators: a button in the elements list: closes it; Escape does the
    // same.
    GtkWidget* cancel = gtk_button_new_with_mnemonic(_("_Cancel"));
    g_signal_connect(activate, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         choose(*static_cast<Dialog*>(data), "activate");
                     }),
                     &dialog);
    g_signal_connect(move, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         choose(*static_cast<Dialog*>(data), "move");
                     }),
                     &dialog);
    g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         gtk_window_close(GTK_WINDOW(static_cast<Dialog*>(data)->window));
                     }),
                     &dialog);
    gtk_box_append(GTK_BOX(buttons), activate);
    gtk_box_append(GTK_BOX(buttons), move);
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(box), buttons);
    gtk_window_set_default_widget(GTK_WINDOW(dialog.window), activate);

    // Escape closes, choosing nothing.
    GtkEventController* shortcuts = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(shortcuts), GTK_SHORTCUT_SCOPE_GLOBAL);
    gtk_shortcut_controller_add_shortcut(
        GTK_SHORTCUT_CONTROLLER(shortcuts),
        gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Escape, static_cast<GdkModifierType>(0)),
                         gtk_named_action_new("window.close")));
    gtk_widget_add_controller(dialog.window, shortcuts);

    g_signal_connect(dialog.window, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer data) {
                         static_cast<Dialog*>(data)->done = true;
                         return FALSE;
                     }),
                     &dialog);

    fillList(dialog);
    gtk_window_present(GTK_WINDOW(dialog.window));
    // The list has the focus, on the element at the caret, as in NVDA.
    if (GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(dialog.list))) {
        gtk_widget_grab_focus(GTK_WIDGET(row));
    } else {
        gtk_widget_grab_focus(dialog.filter);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    alus::core::Log::instance().setLevel(alus::core::LogLevel::Warning);
    alus::i18n::initializeFromEnvironment();

    gchar* items = nullptr;
    gchar* kind = nullptr;
    gchar* language = nullptr;
    const GOptionEntry entries[] = {
        {"items", 0, 0, G_OPTION_ARG_FILENAME, &items, "The elements, one per line", "FILE"},
        {"type", 0, 0, G_OPTION_ARG_STRING, &kind, "The element type shown first", "KIND"},
        {"language", 0, 0, G_OPTION_ARG_STRING, &language, "Language code", "CODE"},
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
    };
    GOptionContext* context = g_option_context_new("- ALUS elements list");
    g_option_context_add_main_entries(context, entries, nullptr);
    GError* error = nullptr;
    const gboolean parsed = g_option_context_parse(context, &argc, &argv, &error);
    g_option_context_free(context);
    const std::string itemsPath = items != nullptr ? items : "";
    const std::string firstKind = kind != nullptr ? kind : "link";
    const std::string languageCode = language != nullptr ? language : "";
    g_free(items);
    g_free(kind);
    g_free(language);
    if (parsed == FALSE || itemsPath.empty()) {
        std::fprintf(stderr, "alus-elements: %s\n",
                     error != nullptr ? error->message : "--items is required");
        if (error != nullptr) {
            g_error_free(error);
        }
        return 2;
    }

    alus::i18n::Options i18nOptions;
    i18nOptions.language = languageCode;
    alus::i18n::initialize(i18nOptions);

    Dialog dialog;
    dialog.kind = firstKind;
    const bool read = readItems(itemsPath, dialog);
    g_remove(itemsPath.c_str());
    if (!read) {
        std::fprintf(stderr, "alus-elements: cannot read %s\n", itemsPath.c_str());
        return 1;
    }

    gtk_init();
    build(dialog);
    while (!dialog.done) {
        g_main_context_iteration(nullptr, TRUE);
    }
    return 0;
}
