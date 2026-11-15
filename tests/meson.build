# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).

catch2_dep = dependency('catch2-with-main',
  version: '>=3.4',
  fallback: ['catch2', 'catch2_with_main_dep'])

core_test_sources = files(
  'core/test_log.cpp',
  'core/test_cached.cpp',
  'core/test_event_dispatcher.cpp',
  'core/test_event_limiter.cpp',
  'core/test_extension_points.cpp',
  'core/test_loop.cpp',
  'core/test_queue.cpp',
  'core/test_task.cpp',
  'core/test_watchdog.cpp',
)

test_core = executable('test-core',
  core_test_sources,
  dependencies: [alus_core_dep, catch2_dep],
  install: false)

test('core', test_core,
  suite: 'core',
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  timeout: 60)
config_test_sources = files(
  'config/test_config.cpp',
)

test_config = executable('test-config',
  config_test_sources,
  dependencies: [alus_core_dep, catch2_dep],
  install: false)

# Registered in the core suite as well, so the phase 1 exit criterion
# (meson test --suite core) covers T1.10.
test('config', test_config,
  suite: ['core', 'config'],
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  timeout: 60)

# Hermetic in both senses: no speech device and no keyboard. `--no-input` is
# what keeps this test from competing with the running screen reader for the
# compositor's KeyboardMonitor name (N8).
test('daemon-smoke', alus_daemon,
  suite: ['core', 'daemon'],
  args: ['--allow-multiple', '--no-speech', '--no-input', '--exit-after', '1',
         '--log-level', 'debug'],
  # Hermetic: never touch the developer's own ~/.config/alus.
  env: {'ALUS_CONFIG_DIR': meson.current_build_dir() / 'daemon-smoke-config'},
  timeout: 30)

# ---------------------------------------------------------------------------
# Phase 2: speech, text processing, audio
# ---------------------------------------------------------------------------

speech_test_sources = files(
  'speech/test_commands.cpp',
  'speech/test_dictionaries.cpp',
  'speech/test_manager.cpp',
  'speech/test_presenter.cpp',
  'speech/test_sentence_buffer.cpp',
  'speech/test_settings_ring.cpp',
  'speech/test_ssip_messages.cpp',
  'text/test_regex.cpp',
  'text/test_symbols.cpp',
  'audio/test_audio.cpp',
)

test_speech = executable('test-speech',
  speech_test_sources,
  cpp_args: ['-DALUS_TEST_DATA_DIR="' + meson.project_source_root() / 'data' + '"'],
  dependencies: [alus_core_dep, alus_speech_dep, catch2_dep],
  install: false)

test('speech', test_speech,
  suite: 'speech',
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  timeout: 120)

# The live latency check (T2.11). It needs a running speech-dispatcher and a
# sound device, so it is its own suite: `meson test -C build --suite live`.
# Without a server it reports itself as skipped rather than failing, because a
# build machine without audio is not a broken ALUS.
#
# `--allow-running-no-tests` is what MAKES that true, and every live test below
# carries it: Catch2 exits 4 when all of its test cases were skipped, and meson
# reads any non-zero exit as a failed test. Without the flag the graceful skip
# these files promise is a red suite on exactly the machines it was written for
# (docs/history/phase-7.md N65).
test_speech_live = executable('test-speech-live',
  files('speech/test_live_speech.cpp'),
  dependencies: [alus_core_dep, alus_speech_dep, catch2_dep],
  install: false)

test('speech-live', test_speech_live,
  suite: 'live',
  protocol: 'tap',
  args: ['--reporter', 'tap', '--allow-running-no-tests'],
  timeout: 300)

# ---------------------------------------------------------------------------
# Phase 5: the text layer
# ---------------------------------------------------------------------------

# The offset converter and the unit tables need nothing but the text library,
# so they are their own executable: a failure here is never a failure of the
# accessibility platform, and Â§7.3 says this is where silent bugs come from.
text_test_sources = files(
  'text/test_offsets.cpp',
  'text/test_segment.cpp',
)

test_text = executable('test-text',
  text_test_sources,
  cpp_args: ['-DALUS_TEST_DATA_DIR="' + meson.project_source_root() / 'data' + '"'],
  dependencies: [alus_core_dep, alus_speech_dep, catch2_dep],
  install: false)

test('text', test_text,
  suite: 'text',
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  timeout: 120)

# ---------------------------------------------------------------------------
# Phase 3: input and scripts
# ---------------------------------------------------------------------------

input_test_sources = files(
  'input/test_catalog.cpp',
  'input/test_dispatch.cpp',
  'input/test_gesture.cpp',
  'input/test_gesture_map.cpp',
  'input/test_keymap.cpp',
  'input/test_manager.cpp',
)

test_input = executable('test-input',
  input_test_sources,
  cpp_args: ['-DALUS_TEST_DATA_DIR="' + meson.project_source_root() / 'data' + '"'],
  dependencies: [alus_core_dep, alus_input_dep, catch2_dep],
  install: false)

test('input', test_input,
  suite: 'input',
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  timeout: 120)

# The one phase 3 test that links the speech library too: T3.9 asks whether a
# key press really stops speech, and only the real pipeline can answer that.
test_input_speech = executable('test-input-speech',
  files('input/test_input_speech.cpp'),
  dependencies: [alus_core_dep, alus_input_dep, alus_speech_dep, catch2_dep],
  install: false)

test('input-speech', test_input_speech,
  suite: 'input',
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  timeout: 60)

# ---------------------------------------------------------------------------
# Phase 4: the object model
# ---------------------------------------------------------------------------

# The mock accessibility platform (tests/a11y/mock_backend.hpp) implements
# alus::a11y::Backend, so the whole object model is tested with no session bus,
# no at-spi2-registryd and no desktop -- and with every platform call counted,
# which is what makes T4.2 a measurement.
a11y_test_sources = files(
  'a11y/test_api.cpp',
  'a11y/test_behaviors.cpp',
  'a11y/test_commands.cpp',
  'a11y/test_events.cpp',
  'a11y/test_mapping.cpp',
  'a11y/test_object.cpp',
  'a11y/test_object_speech.cpp',
)

test_a11y = executable('test-a11y',
  a11y_test_sources,
  cpp_args: ['-DALUS_TEST_DATA_DIR="' + meson.project_source_root() / 'data' + '"'],
  include_directories: include_directories('.'),
  dependencies: [alus_core_dep, alus_a11y_dep, catch2_dep],
  install: false)

test('a11y', test_a11y,
  suite: 'a11y',
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  timeout: 120)

# ---------------------------------------------------------------------------
# Phase 5: the text layer, the review cursor and say all
#
# The same mock platform as phase 4, plus a REAL speech pipeline with a
# NullSynth at the bottom (tests/nav/support.hpp says why): spelling, say-all
# and the caret all depend on what the speech layer does with a sequence, not
# only on the sequence.
# ---------------------------------------------------------------------------

nav_test_sources = files(
  'nav/test_text_range.cpp',
  'nav/test_review.cpp',
  'nav/test_caret.cpp',
  'nav/test_say_all.cpp',
  'nav/test_live_text.cpp',
  'nav/test_text_speech.cpp',
  'nav/test_paragraphs.cpp',
  'nav/test_commands.cpp',
)

test_nav = executable('test-nav',
  nav_test_sources,
  cpp_args: ['-DALUS_TEST_DATA_DIR="' + meson.project_source_root() / 'data' + '"'],
  include_directories: include_directories('.'),
  dependencies: [alus_core_dep, alus_a11y_dep, alus_nav_dep, catch2_dep],
  install: false)

test('nav', test_nav,
  suite: ['nav', 'text'],
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  timeout: 180)

# ---------------------------------------------------------------------------
# Phase 6: browse mode
#
# Its own executable and its own suite, because the exit criterion is one
# command (`meson test -C build --suite browsemode`) and because a failure here
# is never a failure of the text layer below it. The mock platform answers
# `Collection` for real (tests/a11y/mock_backend.hpp), so the 22 match rules of
# §9.2 are measured and not merely compiled.
# ---------------------------------------------------------------------------

browsemode_test_sources = files(
  'nav/test_browse_mode.cpp',
)

test_browsemode = executable('test-browsemode',
  browsemode_test_sources,
  cpp_args: ['-DALUS_TEST_DATA_DIR="' + meson.project_source_root() / 'data' + '"'],
  include_directories: include_directories('.'),
  dependencies: [alus_core_dep, alus_a11y_dep, alus_nav_dep, catch2_dep],
  install: false)

test('browsemode', test_browsemode,
  suite: ['browsemode', 'nav'],
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  # alus+v saves the configuration (N142): never the developer's own.
  env: {'ALUS_CONFIG_DIR': meson.current_build_dir() / 'browsemode-test-config'},
  timeout: 180)

# ---------------------------------------------------------------------------
# Phase 7: braille
#
# Its own executable and its own suite, because the exit criterion is one
# command (`meson test -C build --suite braille`) and because a failure here is
# never a failure of the layers below. It links the REAL liblouis: four of the
# eight criteria are statements about what that library does, and a fake
# translator would assert on ALUS's assumptions about it instead
# (tests/braille/support.hpp says why).
# ---------------------------------------------------------------------------

braille_test_sources = files(
  'braille/test_louis.cpp',
  'braille/test_buffer.cpp',
  'braille/test_regions.cpp',
  'braille/test_handler.cpp',
  'braille/test_input.cpp',
)

test_braille = executable('test-braille',
  braille_test_sources,
  cpp_args: ['-DALUS_TEST_DATA_DIR="' + meson.project_source_root() / 'data' + '"'],
  include_directories: include_directories('.'),
  dependencies: [alus_core_dep, alus_a11y_dep, alus_nav_dep, alus_braille_dep,
                 catch2_dep],
  install: false)

test('braille', test_braille,
  suite: 'braille',
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  timeout: 180)

# The phase 7 live check: BrlAPI against a real brltty. Its own suite for the
# same reason every other live check is -- it needs hardware (or brltty in its
# virtual mode), and a build machine with no braille display is not a broken
# ALUS. It is the automatic half of what phase-7.md leaves to the manual test:
# the display size, the frame and the key names, which no mock can answer.
test_braille_live = executable('test-braille-live',
  files('braille/test_live_braille.cpp'),
  include_directories: include_directories('.'),
  dependencies: [alus_core_dep, alus_braille_dep, catch2_dep],
  install: false)

test('braille-live', test_braille_live,
  suite: 'live',
  protocol: 'tap',
  args: ['--reporter', 'tap', '--allow-running-no-tests'],
  timeout: 120)

# The phase 5 live check: the text layer against the real toolkits. Its own
# suite for the same reason the phase 4 one is -- it needs a desktop, and a
# build machine without one is not a broken ALUS.
test_nav_live = executable('test-nav-live',
  files('nav/test_live_text_layer.cpp'),
  include_directories: include_directories('.'),
  dependencies: [alus_core_dep, alus_a11y_dep, alus_nav_dep, catch2_dep],
  install: false)

test('nav-live', test_nav_live,
  suite: 'live',
  protocol: 'tap',
  args: ['--reporter', 'tap', '--allow-running-no-tests'],
  timeout: 180)

# The live check: the real libatspi against the session's own accessibility
# bus. It reads the desktop tree and nothing else -- it never takes the
# keyboard and never activates a window. Without a bus it reports itself as
# skipped rather than failing, because a build machine with no desktop is not
# a broken ALUS.
test_a11y_live = executable('test-a11y-live',
  files('a11y/test_live_a11y.cpp'),
  include_directories: include_directories('.'),
  dependencies: [alus_core_dep, alus_a11y_dep, catch2_dep],
  install: false)

test('a11y-live', test_a11y_live,
  suite: 'live',
  protocol: 'tap',
  args: ['--reporter', 'tap', '--allow-running-no-tests'],
  timeout: 120)

# ---------------------------------------------------------------------------
# Phase 8: app modules and extensibility
#
# Its own executable and its own suite, because the exit criterion is one
# command (`meson test -C build --suite app`) and because a failure here is
# never a failure of the layers below it -- which is the claim app/service.hpp
# makes about every seam this layer uses.
#
# The test plugins are REAL shared modules, compiled as C against
# `alus/alus-plugin.h` and nothing else (tests/app/plugins/good.c says why).
# That makes the add-on ABI a compiled artefact of the build rather than a
# promise in a header: if the header ever stops being self-contained, or stops
# being C, the build fails here.
# ---------------------------------------------------------------------------

test_plugin_args = ['-I' + meson.project_source_root() / 'include']

test_plugins = []
foreach plugin : ['good', 'badabi', 'toonew', 'refuses']
  test_plugins += shared_module(plugin,
    files('app/plugins/' + plugin + '.c'),
    c_args: test_plugin_args,
    # The tests find them by name in one directory, so no `lib` prefix and no
    # version suffix: `good.so`, exactly as an add-on is installed.
    name_prefix: '',
    install: false)
endforeach

app_test_sources = files(
  'app/test_lookup.cpp',
  'app/test_mechanisms.cpp',
  'app/test_modules.cpp',
  'app/test_plugins.cpp',
  'app/test_profile.cpp',
)

test_app = executable('test-app',
  app_test_sources,
  cpp_args: [
    '-DALUS_TEST_DATA_DIR="' + meson.project_source_root() / 'data' + '"',
    '-DALUS_TEST_PLUGIN_DIR="' + meson.current_build_dir() + '"',
    '-DALUS_VERSION="' + meson.project_version() + '"',
  ],
  include_directories: [include_directories('.'), include_directories('a11y')],
  dependencies: [alus_core_dep, alus_a11y_dep, alus_nav_dep, alus_app_dep, catch2_dep],
  install: false)

test('app', test_app,
  suite: 'app',
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  # The plugins have to exist before the test runs; meson does not know that
  # from the sources, because the test loads them by path at run time.
  depends: test_plugins,
  timeout: 180)

# ---------------------------------------------------------------------------
# Phase 9: the GUI and localization
#
# NO DISPLAY IS NEEDED, and that is deliberate: the settings window is split
# into a model (pure C++, asserted here) and widgets (a loop over the model,
# which holds no knowledge worth asserting). See include/alus/gui/model.hpp.
# The widget layer is exercised live (phase-9.md), which needs a
# session and is therefore not a CI test.
#
# ALUS_SOURCE_ROOT: these tests read generated DATA -- po/POTFILES.in,
# po/sr.po, data/scripts.csv, docs/commands.md -- because what they check is
# whether two generated copies of one truth still agree. A test that read the
# build directory instead would pass on a stale build.
# ---------------------------------------------------------------------------

gui_test_sources = files(
  'gui/test_gestures.cpp',
  'gui/test_i18n.cpp',
  'gui/test_model.cpp',
  'gui/test_reference.cpp',
)

test_gui = executable('test-gui',
  gui_test_sources,
  cpp_args: ['-DALUS_SOURCE_ROOT="' + meson.project_source_root() + '"'],
  dependencies: [alus_gui_dep, alus_braille_dep, catch2_dep],
  install: false)

test('gui', test_gui,
  suite: 'gui',
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  # Hermetic: its own configuration directory, never the developer's own, and
  # the catalogues it reads come from the source tree.
  env: {'ALUS_CONFIG_DIR': meson.current_build_dir() / 'gui-test-config'},
  timeout: 120)

# The two generators phase 9 adds, as tests. Both produce files that are
# CHECKED IN, and a stale checked-in generated file fails nothing by itself:
# po/POTFILES.in going stale means a string no translator ever sees, and
# docs/commands.md going stale means a reference that lies about which key runs
# what. So each generator's own --check is a test.
python = find_program('python3', required: true)

# The `external` suite and NOT `gui`: these run python3, and whatever Memcheck
# has to say about CPython is not ALUS's to answer. Both generators' --check is
# also done in C++ over the same files (test_i18n.cpp for POTFILES.in,
# test_reference.cpp for docs/commands.md), so `--suite gui` loses nothing.
test('potfiles-up-to-date', python,
  args: [meson.project_source_root() / 'tools' / 'update-potfiles.py', '--check'],
  suite: 'external',
  workdir: meson.project_source_root(),
  timeout: 60)

test('command-reference-up-to-date', python,
  args: [meson.project_source_root() / 'tools' / 'generate-command-reference.py',
         '--check'],
  suite: 'external',
  workdir: meson.project_source_root(),
  timeout: 60)

# `alus-settings` starts, parses its arguments and exits. Hermetic and with no
# display: --version returns before the window is built, which is the only part
# of it a machine with no session can reach.
#
# It is here because the settings window is a SEPARATE EXECUTABLE as of phase
# 9 (docs/history/phase-9.md N79) and nothing else in the suite would notice if it
# stopped linking.
if have_gui
  test('settings-smoke', alus_settings,
    suite: 'gui',
    args: ['--version'],
    timeout: 30)

  # And that it refuses a page that does not exist rather than opening the
  # wrong one -- the check that caught a renamed page in the shortcut table.
  test('settings-bad-page', alus_settings,
    suite: 'gui',
    args: ['--page', 'thereIsNoSuchPage'],
    should_fail: true,
    timeout: 30)
endif

# ---------------------------------------------------------------------------
# The commands that change speech and the configuration, RUN rather than
# looked up. tests/coverage asks whether a key finds a command; this asks what
# the command then does -- and both are needed, because a key that finds a
# command that says nothing is the defect phase-6.md O10 is about.
#
# Its own executable because it is the only test that links the commands
# library, and its own place in the `commands` suite next to the coverage
# test, for the same reason: a failure here is not a failure of any one layer.
test_commands = executable('test-commands',
  files('commands/test_speech_commands.cpp', 'commands/test_system_commands.cpp'),
  cpp_args: ['-DALUS_TEST_DATA_DIR="' + meson.project_source_root() / 'data' + '"'],
  include_directories: [include_directories('.')],
  dependencies: [alus_core_dep, alus_speech_dep, alus_input_dep, alus_commands_dep,
                 catch2_dep],
  install: false)

test('command-behaviour', test_commands,
  suite: ['commands', 'speech'],
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  # Hermetic: its own configuration directory, never the developer's own.
  env: {'ALUS_CONFIG_DIR': meson.current_build_dir() / 'command-behaviour-config'},
  timeout: 120)

# ---------------------------------------------------------------------------
# Command coverage: does every key ALUS ships actually run something?
#
# Its own executable because it is the only test that links EVERY layer at
# once -- that is the point of it: it assembles the daemon's own stack and
# asks the ten-step lookup about every shipped key assignment. Its own suite
# (`meson test -C build --suite commands`) because a failure here is not a
# failure of any one layer; it is a command that was documented and never
# implemented (docs/history/phase-9.md N99).
#
# ALUS_SOURCE_ROOT as well as the data directory: the second test case reads
# src/ to check that the inventory this file assembles has not been left
# behind by a new scriptable class.
# ---------------------------------------------------------------------------
test_coverage = executable('test-coverage',
  files('coverage/test_command_coverage.cpp'),
  cpp_args: [
    '-DALUS_TEST_DATA_DIR="' + meson.project_source_root() / 'data' + '"',
    '-DALUS_SOURCE_ROOT="' + meson.project_source_root() + '"',
  ],
  include_directories: [include_directories('.'), include_directories('a11y')],
  dependencies: [alus_core_dep, alus_a11y_dep, alus_nav_dep, alus_braille_dep,
                 alus_app_dep, alus_gui_dep, alus_input_dep, alus_commands_dep,
                 catch2_dep],
  install: false)

test('commands', test_coverage,
  suite: ['commands', 'core'],
  protocol: 'tap',
  args: ['--reporter', 'tap'],
  # Hermetic: its own configuration directory, never the developer's own.
  env: {'ALUS_CONFIG_DIR': meson.current_build_dir() / 'coverage-test-config'},
  timeout: 180)

# measure-focus: what one focus announcement costs on the live desktop, call
# by call (N129). A developer tool, not a test: it needs the session's
# accessibility bus, and what it prints is a measurement, not a verdict.
# See docs/development.md.
measure_focus = executable('measure-focus', files('measure_focus.cpp'),
  dependencies: [alus_core_dep, alus_a11y_dep, alus_nav_dep, alus_braille_dep,
                 alus_app_dep],
  install: false)
