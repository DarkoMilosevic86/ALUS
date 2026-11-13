// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/gui/window.hpp.
//
// This file holds NO knowledge of what any setting means. It turns a
// gui::Row into a widget and a widget's change back into a configuration
// write, and that is all -- everything a test would want to assert about the
// settings window lives in model.cpp and gesture_model.cpp.

#include "alus/gui/window.hpp"

#include "internal.hpp"

#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/gui/gesture_model.hpp"
#include "alus/gui/model.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/input/manager.hpp"
#include "alus/script/catalog.hpp"

#include <adwaita.h>
#include <gtk/gtk.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

namespace alus::gui {
namespace {

// GTK initialisation is once per process and can fail. The result is cached
// because `isAvailable()` is called from a script (so, possibly on every key
// press of a shortcut) and gtk_init_check() is not free.
struct Availability {
    bool checked = false;
    bool available = false;
    std::string reason;
};

Availability& availability() {
    static Availability value;
    return value;
}

void check() {
    Availability& state = availability();
    if (state.checked) {
        return;
    }
    state.checked = true;

    // Asked BEFORE gtk_init_check, because gtk_init_check on a session with no
    // display prints to stderr and that lands in the user's terminal.
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* x11 = std::getenv("DISPLAY");
    if ((wayland == nullptr || *wayland == '\0') && (x11 == nullptr || *x11 == '\0')) {
        state.reason = "no display: neither WAYLAND_DISPLAY nor DISPLAY is set";
        ALUS_INFO("gui: {}", state.reason);
        return;
    }
    // gtk_init_check and not gtk_init: gtk_init calls exit() on failure, which
    // in this process would take the screen reader with it.
    if (gtk_init_check() == FALSE) {
        state.reason = "GTK could not open the display";
        ALUS_WARNING("gui: {}", state.reason);
        return;
    }
    adw_init();
    state.available = true;
    ALUS_INFO("gui: GTK {}.{}.{} and libadwaita are available", gtk_get_major_version(),
              gtk_get_minor_version(), gtk_get_micro_version());
}

// ---------------------------------------------------------------------------
// One row of the model, bound to one configuration key.
//
// The binding is held in a heap-allocated struct whose lifetime is tied to the
// widget with g_object_set_data_full, so a signal that fires after the window
// is destroyed cannot reach freed memory -- the usual way this goes wrong in a
// GTK program, and one valgrind would only find if the test closed the window.
// ---------------------------------------------------------------------------

struct RowBinding {
    Context* context = nullptr;
    const Row* row = nullptr;
    // The choices as SHOWN, which for a row with a `source` is not
    // `row->choices`: the voices are asked of speech-dispatcher when the
    // window opens. The index the combo row reports is an index into this.
    std::vector<Choice> choices;
};

void destroyBinding(gpointer data) { delete static_cast<RowBinding*>(data); }

RowBinding* bind(GtkWidget* widget, Context& context, const Row& row) {
    auto* binding = new RowBinding{&context, &row};
    g_object_set_data_full(G_OBJECT(widget), "alus-binding", binding, destroyBinding);
    return binding;
}

void noteChanged(const RowBinding& binding) {
    if (binding.context->settingChanged) {
        binding.context->settingChanged(binding.row->section);
    }
}

void onToggle(GObject* object, GParamSpec* /*spec*/, gpointer data) {
    const auto* binding = static_cast<RowBinding*>(data);
    const bool active = adw_switch_row_get_active(ADW_SWITCH_ROW(object)) != FALSE;
    binding->context->config->setBool(binding->row->section, binding->row->key, active);
    noteChanged(*binding);
}

void onChoice(GObject* object, GParamSpec* /*spec*/, gpointer data) {
    const auto* binding = static_cast<RowBinding*>(data);
    const guint selected = adw_combo_row_get_selected(ADW_COMBO_ROW(object));
    if (selected >= binding->choices.size()) {
        return;
    }
    // The CHOICE's value and never its label: the label is translated and the
    // configuration file must not be.
    binding->context->config->setString(binding->row->section, binding->row->key,
                                        binding->choices[selected].value);
    noteChanged(*binding);
}

void onInteger(GObject* object, GParamSpec* /*spec*/, gpointer data) {
    const auto* binding = static_cast<RowBinding*>(data);
    const double value = adw_spin_row_get_value(ADW_SPIN_ROW(object));
    binding->context->config->setInt(binding->row->section, binding->row->key,
                                     static_cast<std::int64_t>(value));
    noteChanged(*binding);
}

void onText(GtkEditable* editable, gpointer data) {
    const auto* binding = static_cast<RowBinding*>(data);
    const char* text = gtk_editable_get_text(editable);
    binding->context->config->setString(binding->row->section, binding->row->key,
                                        text != nullptr ? text : "");
    noteChanged(*binding);
}

GtkWidget* buildRow(Context& context, const Row& row, Window& window);

GtkWidget* buildActionRow(Context& context, const Row& row, Window& window) {
    GtkWidget* widget = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(widget), _(row.title));
    if (row.subtitle != nullptr) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(widget), _(row.subtitle));
    }
    // Translators: the label of the button on a settings row that opens
    // something else.
    GtkWidget* button = gtk_button_new_with_label(_("Open"));
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(widget), button);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(widget), button);

    struct ActionData {
        Context* context;
        Window* window;
        std::string action;
    };
    auto* data = new ActionData{&context, &window, row.action};
    g_object_set_data_full(G_OBJECT(button), "alus-action", data,
                           [](gpointer p) { delete static_cast<ActionData*>(p); });
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
                         auto* d = static_cast<ActionData*>(
                             g_object_get_data(G_OBJECT(b), "alus-action"));
                         if (d == nullptr) {
                             return;
                         }
                         if (d->action == "gestures") {
                             d->window->openGestures();
                         } else if (d->action == "config-dir") {
                             const std::string uri =
                                 "file://" + d->context->config->paths().configDir.string();
                             GError* error = nullptr;
                             // The portal, so this works in a sandbox too.
                             if (g_app_info_launch_default_for_uri(uri.c_str(), nullptr,
                                                                   &error) == FALSE) {
                                 ALUS_WARNING("gui: could not open {}: {}", uri,
                                              error != nullptr ? error->message : "?");
                                 if (error != nullptr) {
                                     g_error_free(error);
                                 }
                                 if (d->context->message) {
                                     // Translators: spoken when the file manager
                                     // could not be started.
                                     d->context->message(_("Could not open the folder"));
                                 }
                             }
                         } else if (d->action == "reset") {
                             if (d->context->resetAll) {
                                 d->context->resetAll();
                             }
                             if (d->context->message) {
                                 // Translators: spoken after a factory reset.
                                 d->context->message(_("Every setting has been reset"));
                             }
                         }
                     }),
                     nullptr);
    return widget;
}

GtkWidget* buildRow(Context& context, const Row& row, Window& window) {
    if (row.kind == RowKind::Action) {
        return buildActionRow(context, row, window);
    }

    GtkWidget* widget = nullptr;
    switch (row.kind) {
        case RowKind::Toggle: {
            widget = adw_switch_row_new();
            adw_switch_row_set_active(
                ADW_SWITCH_ROW(widget),
                context.config->getBool(row.section, row.key) ? TRUE : FALSE);
            RowBinding* binding = bind(widget, context, row);
            g_signal_connect(widget, "notify::active", G_CALLBACK(onToggle), binding);
            break;
        }
        case RowKind::Choice: {
            widget = adw_combo_row_new();
            const std::string current = context.config->getString(row.section, row.key);
            std::vector<Choice> choices = resolveChoices(row, context.speech, current);
            // GtkStringList of the labels as the user sees them: a msgid
            // translated, or a runtime name (a voice) shown as it is. The
            // values stay in the binding and are looked up by index.
            std::vector<std::string> owned;
            owned.reserve(choices.size());
            for (const Choice& choice : choices) {
                owned.push_back(!choice.text.empty() ? choice.text
                                : choice.label != nullptr ? std::string{_(choice.label)}
                                                          : choice.value);
            }
            std::vector<const char*> labels;
            labels.reserve(owned.size() + 1);
            for (const std::string& label : owned) {
                labels.push_back(label.c_str());
            }
            labels.push_back(nullptr);
            GtkStringList* list = gtk_string_list_new(labels.data());
            adw_combo_row_set_model(ADW_COMBO_ROW(widget), G_LIST_MODEL(list));

            guint selected = 0;
            for (guint i = 0; i < choices.size(); ++i) {
                if (choices[i].value == current) {
                    selected = i;
                    break;
                }
            }
            adw_combo_row_set_selected(ADW_COMBO_ROW(widget), selected);
            RowBinding* binding = bind(widget, context, row);
            binding->choices = std::move(choices);
            g_signal_connect(widget, "notify::selected", G_CALLBACK(onChoice), binding);
            break;
        }
        case RowKind::Integer: {
            GtkAdjustment* adjustment = gtk_adjustment_new(
                static_cast<double>(context.config->getInt(row.section, row.key)),
                static_cast<double>(row.minimum), static_cast<double>(row.maximum),
                static_cast<double>(row.step), static_cast<double>(row.step) * 10.0, 0.0);
            widget = adw_spin_row_new(adjustment, static_cast<double>(row.step), 0);
            RowBinding* binding = bind(widget, context, row);
            g_signal_connect(widget, "notify::value", G_CALLBACK(onInteger), binding);
            break;
        }
        case RowKind::Text: {
            widget = adw_entry_row_new();
            gtk_editable_set_text(
                GTK_EDITABLE(widget),
                context.config->getString(row.section, row.key).c_str());
            RowBinding* binding = bind(widget, context, row);
            g_signal_connect(widget, "changed", G_CALLBACK(onText), binding);
            break;
        }
        case RowKind::Action:
            break;
    }
    if (widget == nullptr) {
        return nullptr;
    }
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(widget), _(row.title));
    if (row.subtitle != nullptr) {
        // AdwActionRow and its descendants all take a subtitle; AdwComboRow
        // and AdwSwitchRow are descendants, AdwEntryRow is not.
        if (ADW_IS_ACTION_ROW(widget)) {
            adw_action_row_set_subtitle(ADW_ACTION_ROW(widget), _(row.subtitle));
        }
    }
    return widget;
}

}  // namespace

bool isAvailable() {
    check();
    return availability().available;
}

std::string unavailableReason() {
    check();
    return availability().reason;
}

// ---------------------------------------------------------------------------

struct Window::Impl {
    Context context;
    // The Window this Impl belongs to, so a row's button can call back into
    // openGestures(). A raw pointer and not a reference because Impl is
    // default-constructed and filled afterwards.
    Window* owner = nullptr;
    // An AdwPreferencesDialog, NOT an AdwPreferencesWindow: the window
    // variants of both this and the alert dialog are deprecated as of
    // libadwaita 1.6 and this project builds with -Werror. Presented with a
    // null parent, an AdwDialog becomes a standalone window, which is what a
    // screen reader's settings need -- there is no application window to be
    // a sheet inside of.
    AdwDialog* dialog = nullptr;
    GtkWidget* gestureDialog = nullptr;
    std::string page;

    // REBUILT on every open rather than kept and re-presented. An AdwDialog
    // that was presented as a window is destroyed when it closes, so keeping
    // the pointer would leave a dangling one the second time the user pressed
    // alus+control+g -- and the cost is a few hundred widgets, once, when a
    // human asked for a window.
    void build();
};

void Window::Impl::build() {
    dialog = ADW_DIALOG(adw_preferences_dialog_new());
    // Translators: the title of ALUS's settings window. "ALUS" is the product
    // name and is not translated.
    adw_dialog_set_title(dialog, _("ALUS Settings"));
    adw_dialog_set_content_width(dialog, 720);
    adw_dialog_set_content_height(dialog, 640);
    // Search over every page's rows, which is the only practical way to find
    // one of ~90 settings by name.
    adw_preferences_dialog_set_search_enabled(ADW_PREFERENCES_DIALOG(dialog), TRUE);

    for (const Page& page_ : pages()) {
        GtkWidget* gtkPage = adw_preferences_page_new();
        adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(gtkPage), _(page_.title));
        adw_preferences_page_set_name(ADW_PREFERENCES_PAGE(gtkPage), page_.id.c_str());
        if (page_.iconName != nullptr) {
            adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(gtkPage),
                                               page_.iconName);
        }
        for (const Group& group : page_.groups) {
            GtkWidget* gtkGroup = adw_preferences_group_new();
            if (group.title != nullptr) {
                adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(gtkGroup),
                                                _(group.title));
            }
            if (group.description != nullptr) {
                adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(gtkGroup),
                                                      _(group.description));
            }
            for (const Row& row : group.rows) {
                if (GtkWidget* widget = buildRow(context, row, *owner)) {  // NOLINT
                    adw_preferences_group_add(ADW_PREFERENCES_GROUP(gtkGroup), widget);
                }
            }
            adw_preferences_page_add(ADW_PREFERENCES_PAGE(gtkPage),
                                     ADW_PREFERENCES_GROUP(gtkGroup));
        }
        adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog),
                                   ADW_PREFERENCES_PAGE(gtkPage));
    }

    // GTK destroys the dialog when it closes, so the pointer has to be
    // dropped with it -- otherwise the next open presents freed memory, which
    // is a crash in the screen reader and not in the settings window.
    g_signal_connect(dialog, "closed", G_CALLBACK(+[](AdwDialog*, gpointer data) {
                         auto* impl = static_cast<Impl*>(data);
                         impl->dialog = nullptr;
                         impl->page.clear();
                     }),
                     this);
}

Window::Window(Context context) : impl_(std::make_unique<Impl>()) {
    impl_->context = std::move(context);
    impl_->owner = this;
}

bool Window::openGestures() {
    if (!isAvailable()) {
        return false;
    }
    if (impl_->gestureDialog == nullptr) {
        // No transient parent: the settings dialog is itself an AdwDialog, so
        // there is no GtkWindow to be transient for, and the gesture dialog is
        // a window of its own that the user may well leave open.
        impl_->gestureDialog = createGestureDialog(impl_->context, nullptr);
        if (impl_->gestureDialog == nullptr) {
            return false;
        }
    }
    gtk_window_present(GTK_WINDOW(impl_->gestureDialog));
    return true;
}

Window::~Window() {
    // Closed and not destroyed: a row's signal handler holds a pointer into
    // this Impl, and closing runs GTK's own teardown, which disconnects them
    // first. Destroying the widget out from under a pending signal is how a
    // GTK program crashes on exit.
    if (impl_->gestureDialog != nullptr) {
        gtk_window_destroy(GTK_WINDOW(impl_->gestureDialog));
        impl_->gestureDialog = nullptr;
    }
    if (impl_->dialog != nullptr) {
        adw_dialog_close(impl_->dialog);
        impl_->dialog = nullptr;
    }
}

bool Window::open(std::string_view pageId) {
    if (!isAvailable()) {
        return false;
    }
    if (findPage(pageId) == nullptr) {
        ALUS_WARNING("gui: no such settings page: {}", pageId);
        return false;
    }
    if (impl_->dialog == nullptr) {
        const auto started = std::chrono::steady_clock::now();
        impl_->build();
        ALUS_INFO("gui: built the settings window in {} ms",
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - started)
                      .count());
    }
    impl_->page = std::string{pageId};
    adw_preferences_dialog_set_visible_page_name(ADW_PREFERENCES_DIALOG(impl_->dialog),
                                                 impl_->page.c_str());
    // A null parent is what makes this a standalone window: ALUS has no
    // application window for a sheet to live inside.
    const auto presenting = std::chrono::steady_clock::now();
    adw_dialog_present(impl_->dialog, nullptr);
    ALUS_INFO("gui: presented the settings window in {} ms",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - presenting)
                  .count());
    return true;
}

void Window::close() {
    if (impl_->dialog != nullptr) {
        // The "closed" handler clears the pointer and the page.
        adw_dialog_close(impl_->dialog);
    }
}

bool Window::isOpen() const { return impl_->dialog != nullptr; }

bool Window::isGesturesOpen() const {
    return impl_->gestureDialog != nullptr &&
           gtk_widget_get_visible(impl_->gestureDialog) != FALSE;
}

std::string Window::currentPage() const { return isOpen() ? impl_->page : std::string{}; }

}  // namespace alus::gui
