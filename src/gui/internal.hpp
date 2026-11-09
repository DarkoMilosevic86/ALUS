// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// What the two GUI source files share. NOT installed and NOT in include/:
// nothing outside src/gui/ may depend on GTK's types, which is what keeps
// every other suite buildable and testable without a display.
#pragma once

#include "alus/gui/window.hpp"

#include <gtk/gtk.h>

namespace alus::gui {

// The gesture dialog (T9.2, T9.3). A GtkWindow, transient for `parent` when
// one is given. Its contents come from gui::buildModel, and the two buttons
// call gui::addBinding and gui::removeBinding -- so everything the dialog
// DECIDES is in gesture_model.cpp and everything it DRAWS is here.
GtkWidget* createGestureDialog(Context& context, GtkWindow* parent);

}  // namespace alus::gui
