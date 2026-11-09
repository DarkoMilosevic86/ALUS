// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The gesture dialog's WIDGETS. What it decides is in gesture_model.cpp.
//
// The shape is NVDA's input gestures dialog, because the users this project is
// for already know it: a tree of categories and commands on the left, the keys
// bound to the selected command under it, and Add / Remove.
//
// THE ONE PART THAT IS NOT NVDA'S, and could not be: capturing the key the
// user wants to bind. NVDA puts the dialog into a mode where the next key
// press is captured instead of executed. On Wayland that is not available to
// an application -- ALUS only sees keys the compositor has already granted it
// a grab for, and a key with no grab never arrives (finding N1/N18). So the
// gesture is TYPED, as it is written in gestures.ini, and the entry is checked
// against the key tables as the user types: `kb:alus+shift+t` is accepted,
// `kb:alus+shiftt` is refused with the reason. A "press the key now" capture
// is possible only for keys ALUS already grabs, which is precisely the set the
// user does not need to bind.

#include "internal.hpp"

#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/gui/gesture_model.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/input/gesture.hpp"
#include "alus/input/key_names.hpp"
#include "alus/input/manager.hpp"
#include "alus/script/catalog.hpp"

#include <adwaita.h>

#include <memory>
#include <string>

namespace alus::gui {
namespace {

// The columns of the tree. GtkTreeView is deprecated in GTK 4.10, so this uses
// GtkColumnView over a GtkTreeListModel, which is the supported replacement
// and -- more to the point -- the one with working accessibility: a
// GtkColumnView row exposes its cells as AT-SPI table cells, which is what
// makes this dialog readable by ALUS itself.
//
// The model is a GtkStringList of opaque row ids plus a side table, rather
// than a GObject per command: 390 commands times a GObject subclass with
// properties is a lot of boilerplate for a list that never changes while the
// dialog is open.

struct DialogState {
    Context* context = nullptr;
    Model model;
    // The command the tree has selected, or null.
    const Command* selected = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* commandList = nullptr;
    GtkWidget* bindingList = nullptr;
    GtkWidget* addButton = nullptr;
    GtkWidget* removeButton = nullptr;
    GtkWidget* status = nullptr;
    // What the binding list currently shows, parallel to its rows.
    std::vector<Binding> shownBindings;
};

void setStatus(DialogState& state, const std::string& text) {
    if (state.status != nullptr) {
        gtk_label_set_text(GTK_LABEL(state.status), text.c_str());
    }
    if (!text.empty() && state.context->message) {
        // Said as well as shown: the user of this dialog is not reading it.
        state.context->message(text);
    }
}

void refreshModel(DialogState& state) {
    state.model = buildModel(*state.context->catalog, *state.context->gestures,
                             state.context->keyNames);
}

void fillBindings(DialogState& state) {
    state.shownBindings.clear();
    if (state.selected != nullptr) {
        state.shownBindings = state.selected->bindings;
    }

    GtkStringList* list = gtk_string_list_new(nullptr);
    for (const Binding& binding : state.shownBindings) {
        // "ALUS+t — built in". One string per row rather than two columns: a
        // braille display reads a row as one line anyway, and the layer is
        // what decides whether Remove shadows or deletes, so it has to be
        // visible.
        const std::string text =
            binding.display + "  —  " + layerLabel(binding.layer);
        gtk_string_list_append(list, text.c_str());
    }
    gtk_list_view_set_model(GTK_LIST_VIEW(state.bindingList),
                            GTK_SELECTION_MODEL(gtk_single_selection_new(
                                G_LIST_MODEL(list))));
    gtk_widget_set_sensitive(state.removeButton,
                             state.shownBindings.empty() ? FALSE : TRUE);
    gtk_widget_set_sensitive(state.addButton, state.selected != nullptr ? TRUE : FALSE);
}

// The "add a gesture" prompt. A dialog and not an inline row, because the
// text has to be explained: see the file comment on why the gesture is typed.
void promptForGesture(DialogState& state) {
    if (state.selected == nullptr) {
        return;
    }
    // AdwAlertDialog and not AdwMessageDialog: the latter is deprecated as of
    // libadwaita 1.6 and this project builds with -Werror.
    AdwDialog* dialog = ADW_DIALOG(adw_alert_dialog_new(
        // Translators: the title of the prompt that asks for a key to bind.
        _("Add a gesture"), nullptr));
    adw_alert_dialog_format_body(
        ADW_ALERT_DIALOG(dialog), "%s",
        // Translators: the prompt's explanation. It says the gesture is typed
        // rather than pressed, which is a Wayland limitation and not a choice;
        // %s is the command's description.
        i18n::substitute(_("Type the gesture to bind to “%s”, the way it is "
                           "written in gestures.ini, for example kb:alus+shift+t. "
                           "Wayland does not let an application capture a key it has "
                           "not already been granted, so the gesture cannot be "
                           "pressed here."),
                         state.selected->description)
            .c_str());

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "kb:alus+shift+t");
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), entry);

    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "cancel",
                                  // Translators: a button in the add-gesture prompt.
                                  _("Cancel"));
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "add",
                                  // Translators: a button in the add-gesture prompt.
                                  _("Add"));
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "add",
                                             ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "add");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");

    struct PromptData {
        DialogState* state;
        GtkWidget* entry;
    };
    auto* data = new PromptData{&state, entry};
    g_object_set_data_full(G_OBJECT(dialog), "alus-prompt", data,
                           [](gpointer p) { delete static_cast<PromptData*>(p); });

    g_signal_connect(
        dialog, "response",
        G_CALLBACK(+[](AdwAlertDialog* d, const char* response, gpointer) {
            auto* p = static_cast<PromptData*>(g_object_get_data(G_OBJECT(d), "alus-prompt"));
            if (p == nullptr || g_strcmp0(response, "add") != 0) {
                return;
            }
            DialogState& s = *p->state;
            const char* typed = gtk_editable_get_text(GTK_EDITABLE(p->entry));
            const std::string gesture = typed != nullptr ? typed : "";
            if (gesture.empty() || s.selected == nullptr) {
                return;
            }
            // Checked before it is written: a gesture no keyboard can produce
            // is a binding that silently never fires.
            const std::string normalized = input::normalizeGestureIdentifier(gesture);
            if (normalized.rfind("kb", 0) == 0 &&
                !input::parseKeyboardGesture(normalized).has_value()) {
                setStatus(s,
                          // Translators: shown when the typed gesture is not a
                          // gesture ALUS can recognise; %s is what was typed.
                          i18n::substitute(_("“%s” is not a gesture ALUS "
                                             "recognises"),
                                           gesture));
                return;
            }
            const WriteResult result =
                addBinding(*s.context->gestures, userGestureFile(*s.context->config),
                           normalized, s.selected->location, s.selected->script);
            if (!result.ok) {
                setStatus(s,
                          // Translators: shown when the gesture map could not be
                          // written; %s is the reason.
                          i18n::substitute(_("Could not save the gesture: %s"),
                                           result.error));
                return;
            }
            const std::string location = s.selected->location;
            const std::string script = s.selected->script;
            refreshModel(s);
            s.selected = s.model.find(location, script);
            fillBindings(s);
            setStatus(s,
                      // Translators: shown after a gesture has been bound; the
                      // first %s is the gesture and the second the command.
                      i18n::substitute(_("%s is now bound to “%s”"), gesture,
                                       s.selected != nullptr ? s.selected->description
                                                             : std::string{}));
        }),
        nullptr);

    adw_dialog_present(dialog, state.window);
}

void removeSelectedBinding(DialogState& state) {
    if (state.selected == nullptr || state.shownBindings.empty()) {
        return;
    }
    auto* selection =
        GTK_SINGLE_SELECTION(gtk_list_view_get_model(GTK_LIST_VIEW(state.bindingList)));
    const guint index =
        selection != nullptr ? gtk_single_selection_get_selected(selection) : 0;
    if (index >= state.shownBindings.size()) {
        return;
    }
    const Binding binding = state.shownBindings[index];
    const std::string location = state.selected->location;
    const std::string script = state.selected->script;

    const WriteResult result =
        removeBinding(*state.context->gestures, userGestureFile(*state.context->config),
                      binding.gesture, location, script);
    if (!result.ok) {
        setStatus(state, i18n::substitute(_("Could not save the gesture: %s"),
                                          result.error));
        return;
    }
    refreshModel(state);
    state.selected = state.model.find(location, script);
    fillBindings(state);
    setStatus(state,
              binding.layer == Layer::User
                  // Translators: shown after the user's own binding was deleted;
                  // %s is the gesture.
                  ? i18n::substitute(_("%s is no longer bound"), binding.display)
                  // Translators: shown after a built-in binding was shadowed
                  // rather than deleted, because the file it lives in is not
                  // the user's; %s is the gesture.
                  : i18n::substitute(_("%s is now unbound for you; the default is "
                                       "kept in place for everything else"),
                                     binding.display));
}

// GtkColumnView over a flat GtkStringList of "category / description" labels,
// with a side vector giving the Command for each row. A GtkTreeListModel would
// nest categories, which reads better with a mouse and worse with a braille
// display: a flat list where every row says its own category is one line per
// command with no state to track.
GtkWidget* buildCommandList(DialogState& state) {
    GtkStringList* list = gtk_string_list_new(nullptr);
    for (const Command& command : state.model.commands) {
        const std::string label = command.category + "  ›  " + command.description;
        gtk_string_list_append(list, label.c_str());
    }
    auto* selection = gtk_single_selection_new(G_LIST_MODEL(list));
    gtk_single_selection_set_autoselect(selection, FALSE);
    gtk_single_selection_set_can_unselect(selection, TRUE);
    gtk_single_selection_set_selected(selection, GTK_INVALID_LIST_POSITION);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup",
                     G_CALLBACK(+[](GtkListItemFactory*, GtkListItem* item, gpointer) {
                         GtkWidget* label = gtk_label_new(nullptr);
                         gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
                         gtk_label_set_wrap(GTK_LABEL(label), TRUE);
                         gtk_list_item_set_child(item, label);
                     }),
                     nullptr);
    g_signal_connect(factory, "bind",
                     G_CALLBACK(+[](GtkListItemFactory*, GtkListItem* item, gpointer) {
                         auto* string = GTK_STRING_OBJECT(gtk_list_item_get_item(item));
                         gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(item)),
                                            gtk_string_object_get_string(string));
                     }),
                     nullptr);

    GtkWidget* view = gtk_list_view_new(GTK_SELECTION_MODEL(selection), factory);
    state.commandList = view;

    g_signal_connect(selection, "notify::selected",
                     G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer data) {
                         auto* s = static_cast<DialogState*>(data);
                         const guint index =
                             gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(object));
                         s->selected = index < s->model.commands.size()
                                           ? &s->model.commands[index]
                                           : nullptr;
                         fillBindings(*s);
                     }),
                     &state);
    return view;
}

GtkWidget* buildBindingList(DialogState& state) {
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup",
                     G_CALLBACK(+[](GtkListItemFactory*, GtkListItem* item, gpointer) {
                         GtkWidget* label = gtk_label_new(nullptr);
                         gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
                         gtk_list_item_set_child(item, label);
                     }),
                     nullptr);
    g_signal_connect(factory, "bind",
                     G_CALLBACK(+[](GtkListItemFactory*, GtkListItem* item, gpointer) {
                         auto* string = GTK_STRING_OBJECT(gtk_list_item_get_item(item));
                         gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(item)),
                                            gtk_string_object_get_string(string));
                     }),
                     nullptr);
    GtkWidget* view = gtk_list_view_new(nullptr, factory);
    state.bindingList = view;
    return view;
}

}  // namespace

GtkWidget* createGestureDialog(Context& context, GtkWindow* parent) {
    if (context.catalog == nullptr || context.gestures == nullptr ||
        context.config == nullptr) {
        ALUS_WARNING("gui: the gesture dialog needs the catalogue and the gesture maps");
        return nullptr;
    }

    auto state = std::make_unique<DialogState>();
    state->context = &context;
    refreshModel(*state);

    GtkWidget* window = adw_window_new();
    state->window = window;
    // Translators: the title of the gesture dialog.
    gtk_window_set_title(GTK_WINDOW(window), _("Gestures"));
    gtk_window_set_default_size(GTK_WINDOW(window), 760, 620);
    if (parent != nullptr) {
        gtk_window_set_transient_for(GTK_WINDOW(window), parent);
    }

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* header = adw_header_bar_new();
    gtk_box_append(GTK_BOX(box), header);

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(content, 12);
    gtk_widget_set_margin_bottom(content, 12);
    gtk_widget_set_margin_start(content, 12);
    gtk_widget_set_margin_end(content, 12);
    gtk_widget_set_vexpand(content, TRUE);

    // Translators: the heading above the list of every ALUS command.
    GtkWidget* commandsLabel = gtk_label_new(_("Commands"));
    gtk_label_set_xalign(GTK_LABEL(commandsLabel), 0.0F);
    gtk_box_append(GTK_BOX(content), commandsLabel);

    GtkWidget* commandScroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(commandScroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(commandScroll),
                                  buildCommandList(*state));
    gtk_box_append(GTK_BOX(content), commandScroll);

    // Translators: the heading above the keys bound to the selected command.
    GtkWidget* bindingsLabel = gtk_label_new(_("Gestures for this command"));
    gtk_label_set_xalign(GTK_LABEL(bindingsLabel), 0.0F);
    gtk_box_append(GTK_BOX(content), bindingsLabel);

    GtkWidget* bindingScroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(bindingScroll, -1, 140);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(bindingScroll),
                                  buildBindingList(*state));
    gtk_box_append(GTK_BOX(content), bindingScroll);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    // Translators: the button that adds a gesture to the selected command.
    state->addButton = gtk_button_new_with_label(_("Add"));
    // Translators: the button that removes the selected gesture.
    state->removeButton = gtk_button_new_with_label(_("Remove"));
    gtk_widget_set_sensitive(state->addButton, FALSE);
    gtk_widget_set_sensitive(state->removeButton, FALSE);
    gtk_box_append(GTK_BOX(buttons), state->addButton);
    gtk_box_append(GTK_BOX(buttons), state->removeButton);
    gtk_box_append(GTK_BOX(content), buttons);

    state->status = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(state->status), 0.0F);
    gtk_label_set_wrap(GTK_LABEL(state->status), TRUE);
    gtk_box_append(GTK_BOX(content), state->status);

    gtk_box_append(GTK_BOX(box), content);
    adw_window_set_content(ADW_WINDOW(window), box);

    g_signal_connect(state->addButton, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer data) {
                         promptForGesture(*static_cast<DialogState*>(data));
                     }),
                     state.get());
    g_signal_connect(state->removeButton, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer data) {
                         removeSelectedBinding(*static_cast<DialogState*>(data));
                     }),
                     state.get());

    // The state outlives every signal because it is owned by the window and
    // freed when the window is destroyed, after GTK has disconnected.
    g_object_set_data_full(G_OBJECT(window), "alus-gesture-state", state.release(),
                           [](gpointer p) { delete static_cast<DialogState*>(p); });

    g_signal_connect(window, "close-request", G_CALLBACK(+[](GtkWindow* w, gpointer) {
                         gtk_widget_set_visible(GTK_WIDGET(w), FALSE);
                         return TRUE;
                     }),
                     nullptr);
    return window;
}

}  // namespace alus::gui
