# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).

# C is here for ONE reason: the add-on ABI (phase 8, architecture.md §12.3)
# is a C header, and the test plugins in tests/app/plugins/ are compiled as C
# against it and nothing else. That is what makes "an add-on needs only a C
# compiler and one header" a property of the build rather than a claim in a
# comment. No ALUS code is written in C.
project('alus', 'cpp', 'c',
  version: '2026.1.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>=1.3.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++20',
    'warning_level=3',
    'werror=true',
    'b_ndebug=if-release',
  ])

cxx = meson.get_compiler('cpp')

add_project_arguments(
  cxx.get_supported_arguments([
    '-Wshadow',
    '-Wnon-virtual-dtor',
    '-Wold-style-cast',
    '-Woverloaded-virtual',
    '-Wconversion',
    '-Wsign-conversion',
    '-Wdouble-promotion',
    '-Wno-missing-field-initializers',
  ]),
  language: 'cpp')

# include_type: 'system' turns -I into -isystem, so GLib's own old-style casts
# and other C idioms are not held to this project's warning level.
glib_dep     = dependency('glib-2.0',    version: '>=2.74', include_type: 'system')
gobject_dep  = dependency('gobject-2.0', version: '>=2.74', include_type: 'system')
gio_dep      = dependency('gio-2.0',     version: '>=2.74', include_type: 'system')
thread_dep   = dependency('threads')

# Speech and audio (phase 2). speech-dispatcher is the default synthesizer path,
# decided by measurement in spikes/RESULTS.md S2 (11.5 ms p95 to first audio).
speechd_dep  = dependency('speech-dispatcher', version: '>=0.11', include_type: 'system')
# libpulse-simple also covers PipeWire, through pipewire-pulse (architecture.md
# §4.6). Optional on purpose: without it ALUS still speaks through
# speech-dispatcher, it only loses its own tones and sounds, and that must not
# stop the screen reader from starting.
pulse_dep    = dependency('libpulse-simple', required: false, include_type: 'system')

# Phase 3: keysym names and the keysym <-> Unicode conversion. The canonical
# source for both on Linux, and the same library the compositor uses, so a grab
# ALUS builds for `nvda+š` names the keysym the compositor will compare against
# (docs/history/phase-3.md O2). Debian ships no .pc for it before libxkbcommon-dev, so
# this is a hard dependency, not an optional one: without it ALUS could only
# bind ASCII keys, and silently.
xkbcommon_dep = dependency('xkbcommon', version: '>=1.0', include_type: 'system')

# Phase 4: the object model. libatspi is the ONE accessibility API ALUS speaks
# (architecture.md §6.1). Its DEVICE API is deliberately not used -- that is
# what phase 3 found to be the cause of N19/N26 -- so this dependency covers
# the accessible tree and the event stream only.
atspi_dep = dependency('atspi-2', version: '>=2.46', include_type: 'system')

# Phase 9: the settings window and the gesture dialog. GTK4 + libadwaita, in
# the screen reader's own process and on its own main loop
# (architecture.md §1 names both; include/alus/gui/window.hpp says why
# in-process).
#
# `required: false` with a loud fallback, for the third time in this file and
# for the strongest reason yet: ALUS without its settings window is a screen
# reader that is configured through ~/.config/alus/alus.ini and `alus-cli`,
# which is exactly what vision.md §5 listed as the v1.0 position before
# this phase. ALUS without GTK is still a screen reader; ALUS that refuses to
# build on a machine with no libgtk-4-dev is not one at all.
gtk_dep = dependency('gtk4', version: '>=4.10', required: false, include_type: 'system')
adwaita_dep = dependency('libadwaita-1', version: '>=1.4', required: false,
                         include_type: 'system')
have_gui = gtk_dep.found() and adwaita_dep.found()
if not have_gui
  warning('no gtk4/libadwaita: ALUS will build without its settings window; ' +
          'configure it with ~/.config/alus/alus.ini and alus-cli')
endif

# Phase 5: word, sentence and grapheme segmentation. architecture.md §7.3
# says ICU and only ICU: it is the UAX#29 implementation the rest of the
# desktop agrees with, and it carries the dictionaries without which Japanese
# and Thai cannot be segmented at all (T5.3 names Japanese).
#
# `required: false` with a loud fallback, NOT because ICU is optional in the
# design, but because a screen reader that refuses to build on a machine
# without libicu-dev is worse than one that reads Serbian and English
# correctly and says so about Japanese. alus::text::Segmenter::icuAvailable()
# reports which answer the running build gives, and the text suite asserts it.
icu_dep = dependency('icu-uc', version: '>=70', required: false, include_type: 'system')

# Phase 7: braille. liblouis is the braille translator the rest of the Linux
# desktop (and NVDA) uses, so a user's own table files and grade-2 tables work
# unchanged (architecture.md §14). Hard dependency: a braille display with no
# translator shows nothing, and there is no partial version of that.
louis_dep = dependency('liblouis', version: '>=3.0', include_type: 'system')

# The braille display itself goes through brltty's BrlAPI rather than through
# ~40 device drivers of ALUS's own (§14). Debian 13 ships libbrlapi-dev with
# the headers and the shared library but NO pkg-config file, so the dependency
# is assembled by hand -- the plan's `pkg-config --modversion libbrlapi` does
# not work on the target system (docs/history/phase-7.md O1).
#
# `required: false` with a loud fallback, for the same reason ICU is: without
# BrlAPI, ALUS still translates braille and still drives the braille viewer and
# the tests, it only cannot talk to a physical display, and that must not stop
# the screen reader from building.
brlapi_found = false
brlapi_deps = []
if cxx.has_header('brlapi.h')
  brlapi_lib = cxx.find_library('brlapi', required: false)
  if brlapi_lib.found()
    brlapi_found = true
    brlapi_deps = [brlapi_lib]
  endif
endif
if not brlapi_found
  warning('no libbrlapi: ALUS will translate braille but cannot drive a display')
endif

# Where liblouis keeps its tables. Asked of the library's own .pc file, so a
# self-compiled liblouis in /usr/local is found without configuration; the
# run-time lookup in alus::braille::TableRegistry can still override it with
# $ALUS_LOUIS_TABLES_DIR.
louis_tables_dir = louis_dep.get_variable(pkgconfig: 'tablesdir',
  default_value: get_option('prefix') / get_option('datadir') / 'liblouis' / 'tables')

# The add-on ABI (phase 8) is dlopen(), so libdl -- which on glibc 2.34 and
# later is inside libc itself. Not `required`, because finding nothing there is
# the normal answer on a current system and not an error.
dl_dep = cxx.find_library('dl', required: false)

alus_inc = include_directories('include')

subdir('src')
subdir('po')
subdir('tests')

# The add-on ABI header, and the ONLY header ALUS installs.
#
# §12.3's promise is that an add-on needs one header and a C compiler; a header
# that is not installed makes that promise unkeepable. Everything else in
# include/alus/ is internal: it is C++20, it changes with every phase, and
# installing it would turn every internal decision into a compatibility
# obligation.
install_headers('include/alus/alus-plugin.h', subdir: 'alus')

# The symbol dictionaries. The data directory is resolved at run time by
# alus::speech::Service::resolveDataDirectory(): $ALUS_DATA_DIR first (so an
# uninstalled build can run against the source tree), then this location, then
# `data/` beside the working directory. A broken installation still speaks
# punctuation, because a minimal English set is compiled in
# (src/text/symbols_data.cpp).
install_subdir('data/symbols',
  install_dir: get_option('datadir') / 'alus',
  strip_directory: false)
# The character descriptions, which `numpad2` three times reads out ("a, Avala").
#
# NOT INSTALLED UNTIL PHASE 9 (docs/history/phase-9.md N86), and the gap was invisible:
# alus::text::CharacterDescriptions looks for <datadir>/alus/characters/<lang>.dic
# and falls back to a compiled-in ENGLISH set, so an installed ALUS answered in
# English for every language and never said why. Found by reading the list of
# files `meson install` actually puts down.
install_subdir('data/characters',
  install_dir: get_option('datadir') / 'alus',
  strip_directory: false)
# Phase 3's generated command table (tools/extract-nvda-commands.py). These are
# DATA, not code, on purpose: §5.4 makes the extractor the source of truth for
# ~300 commands, their descriptions and their key assignments.
# commands-status.ini is NOT generated: it is what makes an unimplemented
# command SAY so instead of being silent, so an install that left it out would
# reintroduce the defect it exists to prevent (see
# include/alus/script/command_status.hpp).
#
# The swap from the other screen reader to ALUS and back, which every beta
# user on GNOME needs: Orca is the session's reader and only one reader can
# hold the keyboard. Installed as `alus-switch` beside alus-daemon; its own
# lookup finds the daemon on $PATH there (2026.1.0 release check).
install_data('tools/alus-switch.sh',
  install_dir: get_option('bindir'),
  rename: 'alus-switch',
  install_mode: 'rwxr-xr-x')

# data/gestures-alus.ini is deliberately NOT here. It is an input to
# tools/extract-commands.py, not something the daemon reads: its effect is
# already inside the generated gestures-builtin.ini and scripts.csv
# (docs/history/phase-9.md N105). Installing it would suggest otherwise.
install_data(
  ['data/scripts.csv', 'data/gestures-builtin.ini', 'data/key-labels.ini',
   'data/commands-status.ini'],
  install_dir: get_option('datadir') / 'alus')
install_subdir('data/locale',
  install_dir: get_option('datadir') / 'alus',
  strip_directory: false)

# ---------------------------------------------------------------------------
# Phase 9: starting with the session.
#
# @bindir@ is substituted rather than hard-coded, so an install into a prefix
# or a --destdir stage produces entries that point at the binaries THAT
# install put down. A .desktop file with the wrong Exec path is a screen
# reader that silently does not start, which is the failure mode this whole
# project treats as the worst one.
#
# NOTHING here is ENABLED by installing it. The autostart entry goes into
# $datadir/applications, not into the user's ~/.config/autostart, and
# `alus-cli autostart --enable` is what copies it -- because enabling ALUS's
# autostart on a machine where another screen reader already starts means one
# of the two gets no keyboard at all (N8), and choosing which is the user's
# call, not the installer's (risks.md R12).
# ---------------------------------------------------------------------------
desktop_data = configuration_data()
desktop_data.set('bindir', get_option('prefix') / get_option('bindir'))

foreach entry : ['alus.desktop', 'alus-settings.desktop']
  configure_file(
    input: 'data/desktop' / (entry + '.in'),
    output: entry,
    configuration: desktop_data,
    install: true,
    install_dir: get_option('datadir') / 'applications')
endforeach

# The systemd user unit, for a session with no XDG autostart at all (sway, a
# login shell). Into $prefix/lib/systemd/user rather than asking systemd's own
# pkg-config for its directory: that one is /usr/lib/systemd/user and is not
# writable from a --prefix build, which would break `meson install` into a
# stage directory -- and the stage install is what the phase 9 exit criterion
# runs.
configure_file(
  input: 'data/desktop/alus.service.in',
  output: 'alus.service',
  configuration: desktop_data,
  install: true,
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'user')
# `meson test` runs everything except the suites that are not ALUS's to judge:
#   live   — needs a running speech-dispatcher and speaks out loud; the
#            latency budget (T2.11) is meaningless under a sanitizer, so it is
#            asked for explicitly: meson test -C build --suite live
#   catch2 — the test framework's own self-test, from the subproject
add_test_setup('quick',
  exclude_suites: ['live', 'catch2'],
  is_default: true)

# meson test -C build --setup=valgrind --suite core
# The suppression file covers GLib's own never-joined worker threads; see its
# header for why they are not ALUS leaks.
# ALUS_REGEX_NO_JIT: see the comment on jitEnabled() in src/text/regex.cpp —
# PCRE2's JIT-generated code cannot be suppressed by frame, so it is turned off
# while Memcheck runs and left on in production.
add_test_setup('valgrind',
  # `external` is phase 9's addition: those tests run OTHER PEOPLE'S programs
  # (msgfmt, python3) against ALUS's generated files, so what Memcheck finds in
  # them is a leak in GNU gettext or in CPython. Suppressing by frame is not an
  # option -- the frames are in another project's library -- and suppressing by
  # object file would hide real findings the day ALUS links gettext itself.
  exclude_suites: ['live', 'catch2', 'external'],
  env: {
    'ALUS_REGEX_NO_JIT': '1',
    # Latency budgets are tests and not notes (phase-plan.md rule 2), but under
    # Memcheck every instruction costs 20 to 50 times more, so T5.8's 50 ms
    # measures 90 ms for reasons that have nothing to do with the code. The
    # tests that assert a BUDGET report the number instead when they see this,
    # and keep asserting it in every other run. Same reasoning the `live`
    # suite's comment above gives for T2.11.
    'ALUS_UNDER_VALGRIND': '1',
  },
  exe_wrapper: [
    'valgrind',
    '--leak-check=full',
    '--track-origins=yes',
    '--error-exitcode=9',
    '--suppressions=' + meson.project_source_root() / 'tests' / 'valgrind.supp',
  ],
  timeout_multiplier: 20)
