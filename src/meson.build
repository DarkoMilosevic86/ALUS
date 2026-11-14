# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).

alus_core_sources = files(
  'config/config.cpp',
  'i18n/data_strings.cpp',
  'i18n/gettext.cpp',
  'i18n/service.cpp',
  'core/event_dispatcher.cpp',
  'core/event_limiter.cpp',
  'core/log.cpp',
  'core/loop.cpp',
  'core/paths.cpp',
  'core/cached.cpp',
  'core/queue.cpp',
  'core/task.cpp',
  'core/watchdog.cpp',
)

libalus_core = static_library('alus_core',
  alus_core_sources,
  cpp_args: [
    # core::findDataDirectory needs to know where the data was installed.
    '-DALUS_DATA_DIR="' + get_option('prefix') / get_option('datadir') / 'alus' + '"',
    # i18n::initialize needs to know where the gettext catalogues were
    # installed. Note this is $datadir/locale and NOT $datadir/alus/locale:
    # the latter is phase 3's per-language gestures.ini, which is ALUS's own
    # data, while this is the standard gettext tree every tool expects.
    '-DALUS_LOCALE_DIR="' + get_option('prefix') / get_option('localedir') + '"',
  ],
  include_directories: alus_inc,
  dependencies: [glib_dep, gobject_dep, gio_dep, thread_dep],
  install: false)

alus_core_dep = declare_dependency(
  link_with: libalus_core,
  include_directories: alus_inc,
  dependencies: [glib_dep, gobject_dep, gio_dep, thread_dep])

# ---------------------------------------------------------------------------
# Phase 2: speech, text processing, audio
# ---------------------------------------------------------------------------

alus_speech_sources = files(
  'audio/tones.cpp',
  'audio/wave_player.cpp',
  'speech/commands.cpp',
  'speech/dictionaries.cpp',
  'speech/manager.cpp',
  'speech/presenter.cpp',
  'speech/sentence_buffer.cpp',
  'speech/spelling.cpp',
  'speech/service.cpp',
  'speech/settings_ring.cpp',
  'speech/voice_catalog.cpp',
  'speech/ssip_synth.cpp',
  'speech/synth.cpp',
  'text/char_descriptions.cpp',
  'text/char_descriptions_data.cpp',
  'text/offsets.cpp',
  'text/regex.cpp',
  'text/segment.cpp',
  'text/symbols.cpp',
  'text/symbols_data.cpp',
  'text/unit.cpp',
  'ui/message.cpp',
)

alus_speech_args = [
  '-DALUS_DATA_DIR="' + get_option('prefix') / get_option('datadir') / 'alus' + '"',
]
alus_speech_deps = [alus_core_dep, speechd_dep]
if pulse_dep.found()
  alus_speech_args += '-DALUS_HAVE_PULSE'
  alus_speech_deps += pulse_dep
endif
if icu_dep.found()
  alus_speech_args += '-DALUS_HAVE_ICU'
  alus_speech_deps += icu_dep
endif

libalus_speech = static_library('alus_speech',
  alus_speech_sources,
  cpp_args: alus_speech_args,
  include_directories: alus_inc,
  dependencies: alus_speech_deps,
  install: false)

alus_speech_dep = declare_dependency(
  link_with: libalus_speech,
  include_directories: alus_inc,
  dependencies: alus_speech_deps)

# ---------------------------------------------------------------------------
# Phase 3: input and scripts
# ---------------------------------------------------------------------------

alus_input_sources = files(
  'input/a11y_manager_backend.cpp',
  'input/commands.cpp',
  'input/gesture.cpp',
  'input/gesture_map.cpp',
  'input/input_help.cpp',
  'input/key_names.cpp',
  'input/keymap.cpp',
  'input/manager.cpp',
  'input/service.cpp',
  'input/typed_chars.cpp',
  'script/catalog.cpp',
  'script/command_status.cpp',
  'script/dispatch.cpp',
  'script/script.cpp',
  'script/scriptable.cpp',
)

# The input library deliberately does NOT depend on the speech library: it
# reaches speech only through the hooks the daemon injects (GestureManager::Hooks).
# That is what lets the whole gesture pipeline be tested without a synthesizer.
libalus_input = static_library('alus_input',
  alus_input_sources,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, xkbcommon_dep],
  install: false)

alus_input_dep = declare_dependency(
  link_with: libalus_input,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, xkbcommon_dep])

# ---------------------------------------------------------------------------
# The commands that need BOTH speech and the script layer.
#
# Its own library, and a small one, because of the two rules stated above it:
# the input library must not depend on speech (which is what lets the whole
# gesture pipeline be tested with no synthesizer), and the speech library must
# not depend on the script layer (phase 2 is below phase 3). A script that
# CHANGES A SPEECH SETTING or SAVES THE CONFIGURATION needs both halves, so it
# lives above both -- exactly as braille's commands do in phase 7.
#
# Nothing below it links it; the daemon, alus-cli and the command-coverage
# test do.
# ---------------------------------------------------------------------------

alus_commands_sources = files(
  'commands/system.cpp',
  'config/commands.cpp',
  'speech/global_commands.cpp',
)

libalus_commands = static_library('alus_commands',
  alus_commands_sources,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, alus_speech_dep, alus_input_dep],
  install: false)

alus_commands_dep = declare_dependency(
  link_with: libalus_commands,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, alus_speech_dep, alus_input_dep])

# ---------------------------------------------------------------------------
# Phase 4: the object model
# ---------------------------------------------------------------------------

alus_a11y_sources = files(
  'a11y/api.cpp',
  'text/fields.cpp',
  'text/offsets_range.cpp',
  'text/range.cpp',
  'a11y/atspi_backend.cpp',
  'a11y/commands.cpp',
  'a11y/behaviors.cpp',
  'a11y/events.cpp',
  'a11y/object.cpp',
  'a11y/object_speech.cpp',
  'a11y/overlay_registry.cpp',
  'a11y/platform_tables.cpp',
  'a11y/refine.cpp',
  'a11y/registry.cpp',
  'a11y/role_map.cpp',
  'a11y/role_tables.cpp',
  'a11y/service.cpp',
  'a11y/settings.cpp',
  'a11y/state_map.cpp',
  'a11y/text_range.cpp',
  'a11y/text_speech.cpp',
  'a11y/state_tables.cpp',
)

# The object model depends on the input library for script::ScriptableObject
# (an a11y::Object owns scripts, as an NVDAObject does) and on the speech
# library for the presentation of objects. It reaches speech ONLY through
# a11y::PresentationHooks, which is what lets every test in tests/a11y assert
# on what would be said with no synthesizer running.
libalus_a11y = static_library('alus_a11y',
  alus_a11y_sources,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, alus_input_dep, alus_speech_dep, atspi_dep],
  install: false)

alus_a11y_dep = declare_dependency(
  link_with: libalus_a11y,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, alus_input_dep, alus_speech_dep, atspi_dep])

# ---------------------------------------------------------------------------
# Phase 5: text navigation, review and say-all
#
# Its own library, because it is the layer ABOVE the object model: the review
# cursor, the caret, say-all and the text commands all need a11y::Object and
# the text range, and nothing in the object model needs them back. Keeping the
# dependency one-way is what lets tests/a11y stay free of the review cursor
# and tests/nav free of the platform (phase-plan.md, the rule for every phase).
alus_nav_sources = files(
  'nav/browse_mode.cpp',
  'nav/control_field.cpp',
  'nav/document_range.cpp',
  'nav/quick_nav.cpp',
  'nav/cursor_manager.cpp',
  'nav/tree_interceptor.cpp',
  'nav/editable_text.cpp',
  'nav/paragraph_nav.cpp',
  'nav/review.cpp',
  'nav/say_all.cpp',
  'nav/screen_explorer.cpp',
  'nav/screen_review.cpp',
  'nav/text_behaviors.cpp',
  'nav/text_commands.cpp',
  'nav/service.cpp',
)

libalus_nav = static_library('alus_nav',
  alus_nav_sources,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, alus_input_dep, alus_speech_dep, alus_a11y_dep],
  install: false)

alus_nav_dep = declare_dependency(
  link_with: libalus_nav,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, alus_input_dep, alus_speech_dep, alus_a11y_dep])

# ---------------------------------------------------------------------------
# Phase 7: braille
#
# Its own library, above the text layer and beside it: braille needs the object
# model (an object's properties), the text layer (the review cursor, the browse
# mode caret, the document formatting settings) and speech (speak-on-routing),
# and nothing in any of them needs braille back. The dependency stays one-way
# the same way phase 5's did, which is what lets tests/braille run the whole
# layer with no display, no desktop and no synthesizer.
# ---------------------------------------------------------------------------

alus_braille_sources = files(
  'braille/buffer.cpp',
  'braille/commands.cpp',
  'braille/display.cpp',
  'braille/formatting.cpp',
  'braille/handler.cpp',
  'braille/input.cpp',
  'braille/labels.cpp',
  'braille/louis.cpp',
  'braille/properties.cpp',
  'braille/region.cpp',
  'braille/service.cpp',
  'braille/settings.cpp',
  'braille/text_region.cpp',
)

alus_braille_args = [
  # Where liblouis keeps its tables, as its own .pc file reports it. The
  # run-time lookup can still override it with $ALUS_LOUIS_TABLES_DIR, which is
  # what the add-on resolver test uses.
  '-DALUS_LOUIS_TABLES_DIR="' + louis_tables_dir + '"',
]
alus_braille_deps = [alus_core_dep, alus_speech_dep, alus_input_dep, alus_a11y_dep,
                     alus_nav_dep, louis_dep]
if brlapi_found
  alus_braille_args += '-DALUS_HAVE_BRLAPI'
  alus_braille_sources += files('braille/brlapi_display.cpp')
  alus_braille_deps += brlapi_deps
endif

libalus_braille = static_library('alus_braille',
  alus_braille_sources,
  cpp_args: alus_braille_args,
  include_directories: alus_inc,
  dependencies: alus_braille_deps,
  install: false)

alus_braille_dep = declare_dependency(
  link_with: libalus_braille,
  include_directories: alus_inc,
  dependencies: alus_braille_deps)

# ---------------------------------------------------------------------------
# Phase 8: app modules and extensibility
#
# The topmost layer, and the only one that may reach every layer below it: an
# app module adjusts what the object model says (phase 4), what the text layer
# intercepts (phases 5 and 6) and -- through a plugin's braille table source --
# what braille translates (phase 7). Nothing below it knows it exists, which is
# what keeps every seam it uses optional and every phase 4-7 test free of it
# (app/service.hpp lists the seams).
#
# It does NOT link liblouis: a plugin's braille table directory reaches phase 7
# through a function in app::Services, so the dependency stays one-way here too.
# ---------------------------------------------------------------------------

alus_app_sources = files(
  'app/commands.cpp',
  'app/key.cpp',
  'app/module.cpp',
  'app/plugin.cpp',
  'app/registry.cpp',
  'app/service.cpp',
  'app/modules/builtin.cpp',
  'app/modules/electron.cpp',
  'app/modules/libreoffice.cpp',
  'app/modules/nautilus.cpp',
  'app/modules/gnome_shell.cpp',
  'app/modules/terminal.cpp',
)

libalus_app = static_library('alus_app',
  alus_app_sources,
  cpp_args: [
    '-DALUS_VERSION="' + meson.project_version() + '"',
    '-DALUS_DATA_DIR="' + get_option('prefix') / get_option('datadir') / 'alus' + '"',
  ],
  include_directories: alus_inc,
  dependencies: [alus_core_dep, alus_speech_dep, alus_input_dep, alus_a11y_dep,
                 alus_nav_dep, dl_dep],
  install: false)

alus_app_dep = declare_dependency(
  link_with: libalus_app,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, alus_speech_dep, alus_input_dep, alus_a11y_dep,
                 alus_nav_dep, dl_dep])

# ---------------------------------------------------------------------------
# Phase 9: the settings window and the gesture dialog
#
# Its own library, ABOVE everything else and depended on by nothing: the GUI
# reads the configuration, the gesture maps and the command catalogue, and
# none of them knows it exists. That is what lets every other suite build and
# run with no display and no GTK at all.
#
# The MODEL (model.cpp, gesture_model.cpp) is compiled whether or not GTK is
# installed, because it has no GTK in it and tests/gui asserts on it. Only the
# WIDGETS (window.cpp, gesture_dialog.cpp) are conditional. That split is the
# whole reason T9.1-T9.3 are runnable tests rather than a person clicking.
# ---------------------------------------------------------------------------

# The MODEL. No GTK in it, and that is load-bearing in three ways:
#   * tests/gui asserts on it with no display (T9.1-T9.3, T9.5),
#   * the daemon links it to answer "which page does this shortcut open" and
#     to start the settings program, with no GTK anywhere near the screen
#     reader,
#   * and because the daemon does not link GTK, it is not registered on the
#     accessibility bus as a GTK application at all -- which it has no business
#     being, and which produced a `GetItems ... /org/a11y/atspi/cache` warning
#     for every tree walk while it was (phase-9.md N80).
alus_gui_sources = files(
  'gui/gesture_model.cpp',
  'gui/model.cpp',
  'gui/service.cpp',
)

libalus_gui = static_library('alus_gui',
  alus_gui_sources,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, alus_speech_dep, alus_input_dep, alus_a11y_dep,
                 alus_nav_dep],
  install: false)

alus_gui_dep = declare_dependency(
  link_with: libalus_gui,
  include_directories: alus_inc,
  dependencies: [alus_core_dep, alus_speech_dep, alus_input_dep, alus_a11y_dep,
                 alus_nav_dep])

# The WIDGETS. Linked by `alus-settings` and by nothing else, so GTK is present
# in exactly one of ALUS's executables -- the one that has a window.
if have_gui
  libalus_gui_widgets = static_library('alus_gui_widgets',
    files('gui/gesture_dialog.cpp', 'gui/window.cpp'),
    cpp_args: ['-DALUS_HAVE_GUI'],
    include_directories: alus_inc,
    dependencies: [alus_gui_dep, gtk_dep, adwaita_dep],
    install: false)

  alus_gui_widgets_dep = declare_dependency(
    link_with: libalus_gui_widgets,
    include_directories: alus_inc,
    dependencies: [alus_gui_dep, gtk_dep, adwaita_dep],
    compile_args: ['-DALUS_HAVE_GUI'])
endif

alus_daemon = executable('alus-daemon',
  files('daemon/main.cpp'),
  cpp_args: ['-DALUS_VERSION="' + meson.project_version() + '"'],
  dependencies: [alus_core_dep, alus_speech_dep, alus_input_dep, alus_a11y_dep,
                 alus_nav_dep, alus_braille_dep, alus_app_dep, alus_gui_dep,
                 alus_commands_dep],
  install: true)

# `alus-settings`: the settings window, in its own process.
#
# ITS OWN PROCESS AND NOT THE DAEMON'S, which was measured rather than
# preferred: a window in the screen reader's own process cannot be read at
# all, because ALUS's synchronous AT-SPI calls about it are answered from the
# very main loop they are blocking (docs/history/phase-9.md N79, and the file comment
# in src/gui/main.cpp). Only built when GTK is: without it there is no window
# to put in a process.
if have_gui
  alus_settings = executable('alus-settings',
    files('gui/main.cpp'),
    cpp_args: ['-DALUS_VERSION="' + meson.project_version() + '"'],
    dependencies: [alus_core_dep, alus_speech_dep, alus_input_dep, alus_a11y_dep,
                   alus_nav_dep, alus_braille_dep, alus_gui_widgets_dep],
    install: true)
endif

alus_cli = executable('alus-cli',
  files('cli/main.cpp'),
  cpp_args: [
    '-DALUS_VERSION="' + meson.project_version() + '"',
    # `alus-cli autostart --enable` copies the installed .desktop file, so it
    # has to know where `meson install` put it.
    '-DALUS_DATADIR="' + get_option('prefix') / get_option('datadir') + '"',
  ],
  dependencies: [alus_core_dep, alus_speech_dep, alus_input_dep, alus_a11y_dep,
                 alus_nav_dep, alus_braille_dep, alus_app_dep, alus_gui_dep,
                 alus_commands_dep],
  install: true)
