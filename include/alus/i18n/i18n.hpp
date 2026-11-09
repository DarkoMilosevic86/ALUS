// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The translation macros. **INCLUDE THIS FROM .cpp FILES ONLY.**
//
// A public header that defines `_` injects a one-character macro into every
// translation unit that includes any part of ALUS, including the add-on ABI
// header that §12.3 promises is self-contained C. GNOME solved this the same
// way: `gi18n.h` and `gi18n-lib.h` are separate from `glib.h` and are included
// by sources, never by installed headers. ALUS follows that, and the rule is
// enforced by a test (tests/gui/i18n_test.cpp, "no public header defines _").
//
// WHICH MACRO TO USE:
//
//   _("button")                 translate now. The common case.
//   N_("button")               do NOT translate now -- only mark the string so
//                              that xgettext collects it. For a string stored
//                              in a `constexpr` table (role labels, state
//                              labels, script descriptions) that cannot call a
//                              function at initialisation time. The POINT OF
//                              USE then calls _() on it, which is why
//                              roleLabel() returns the translated pointer and
//                              roleLabelUntranslated() the msgid.
//   C_("role", "edit")         translate with a disambiguating context.
//   NC_("role", "edit")        mark with context, translate later.
//   P_("%s item", "%s items", n)  plural. Serbian has three forms; see
//                              i18n/gettext.hpp.
//
// xgettext is told about all five in po/meson.build, and the keyword list is
// the single place that has to agree with this file.
#pragma once

#include "alus/i18n/gettext.hpp"

#define _(String) (::alus::i18n::translate(String))
#define N_(String) (String)
#define C_(Context, String) (::alus::i18n::translateContext(Context, String))
#define NC_(Context, String) (String)
#define P_(Singular, Plural, Count) \
    (::alus::i18n::translatePlural(Singular, Plural, static_cast<unsigned long>(Count)))
