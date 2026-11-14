# Developing ALUS

This document is for people who change ALUS: where things are, how to test
them, how to find out what the running screen reader is doing, and the rules
the code base follows. Read [the architecture](design/architecture.md) for
the why.

## Source layout

```
include/alus/<subsystem>/   public headers of each subsystem (internal to ALUS)
include/alus/alus-plugin.h  the add-on ABI, the only installed header
src/<subsystem>/            the implementation
src/daemon/                 alus-daemon: wires the subsystems together
src/cli/                    alus-cli: diagnostics and one-shot tools
src/gui/                    alus-settings: the settings window (its own process)
data/                       the command table, gesture maps, dictionaries, desktop files
po/                         gettext catalogues (sr, sr@latin) and the template
tests/<subsystem>/          Catch2 tests, one executable per subsystem
tools/                      generators and the screen reader swap script
reference/nvda/             the subset of upstream NVDA source the generators read
spikes/                     phase 0 measurement programs and their results
docs/                       this documentation
```

The subsystems, in dependency order:

| Subsystem | What it does | NVDA counterpart |
|---|---|---|
| `core` | main loop and pump, queue, coroutines, `Cached<T>`, extension points, watchdog, logging | `core.py`, `queueHandler.py`, `baseObject.py`, `watchdog.py` |
| `config` | `alus.ini`, profiles, restart in place | `config/` |
| `i18n` | gettext, plural forms, language choice | `languageHandler.py` |
| `speech` | speech manager, presentation, SSIP synthesizer, dictionaries | `speech/`, `synthDrivers/` |
| `audio` | tones and wave playback (libpulse-simple) | `tones.py`, `nvwave.py` |
| `text` | text ranges, offsets, segmentation, symbols, character descriptions | `textInfos/`, `textUtils/`, `characterProcessing.py` |
| `input` | keyboard monitor backend, gestures, gesture maps, typed character echo | `inputCore.py`, `keyboardHandler.py` |
| `script` | script registry, command status, input help | `scriptHandler.py` |
| `a11y` | the AT-SPI backend, objects, behaviours, events, the event limiter, roles and states | `NVDAObjects/`, `IAccessibleHandler/`, `eventHandler.py`, `api.py` |
| `nav` | review cursor, caret tracking, say all, browse mode, tree interceptors, tables | `review.py`, `editableText.py`, `browseMode.py`, `compoundDocuments.py` |
| `braille` | regions, buffer, liblouis, BrlAPI display | `braille/`, `louisHelper.py` |
| `app` | app modules, the plugin loader | `appModuleHandler.py`, `appModules/`, `globalPluginHandler.py` |
| `commands` | global commands that span subsystems | `globalCommands.py` |
| `ui` | `ui::message` | `ui.py` |
| `gui` | the settings window and gestures dialog | `gui/` |

Each subsystem is a static library with a `Service` class that the daemon
initialises and terminates in order, and that can be restarted in place when
the configuration changes.

## Building for development

```bash
meson setup build --buildtype=debug
meson compile -C build
```

[building.md](building.md) lists the dependencies. Compile commands for
editors and clangd are in `build/compile_commands.json`.

## Tests

```bash
meson test -C build                      # the default setup: everything but `live`
meson test -C build --suite nav          # one suite
meson test -C build nav --verbose        # one test executable, with its output
meson test -C build --print-errorlogs    # show the output of failures
./build/tests/test-nav "[sayall]"        # Catch2 directly: filter by tag
./build/tests/test-nav "the focus say-all moves through paragraphs is not announced"
```

| Test | Suite | Covers |
|---|---|---|
| `core` | core | loop, pump, queue, coroutines, cache, extension points, watchdog |
| `config` | core, config | configuration layers, profiles, restart in place |
| `daemon-smoke` | core, daemon | the assembled daemon starts and stops |
| `speech` | speech | speech manager, presenter, SSIP message building, dictionaries, symbols |
| `text` | text | text ranges, offset units, segmentation |
| `input`, `input-speech` | input | gesture normalisation, maps, grabs, the keyboard state machine |
| `a11y` | a11y | objects, behaviours, events, focus, the event limiter (against the mock backend) |
| `nav` | nav, text | review cursor, caret tracking, say all |
| `browsemode` | browsemode, nav | browse mode, quick navigation, `getTextWithFields`, tables |
| `braille` | braille | regions, buffer, translation |
| `app` | app | app modules, the plugin ABI (with real C plugins from `tests/app/plugins/`) |
| `gui`, `settings-smoke`, `settings-bad-page` | gui | settings model, gestures dialog model, the settings process (`settings-bad-page` is an expected failure) |
| `commands`, `command-behaviour` | commands | every command in the table resolves and behaves; `tests/coverage/` measures how many keys do something |
| `potfiles-up-to-date`, `command-reference-up-to-date` | external | `po/POTFILES.in` and `docs/commands.md` match the sources |
| `*-live` | live | speech, accessibility, text and braille against the **real** session; speaks out loud; run with `--suite live` |

How the tests work without a desktop:

- **`tests/a11y/mock_backend.hpp`** implements `alus::a11y::Backend` with a
  synthetic object tree. Tests build a tree, `emit()` events and `kill()`
  objects, exactly as an application would. It counts text offsets in
  codepoints, as AT-SPI does (finding N113).
- **`NullSynth`** (`include/alus/speech/synth.hpp`) records every utterance.
  Tests assert on the text the user would hear. With `setAutoComplete(false)`
  the test decides when each index mark is reached (`completeIndex`,
  `pendingIndexes`), so speech timing is deterministic.
- **`tests/support/loop_fixture.hpp`** runs the main loop for a test.

Memory checking and sanitizers:

```bash
meson test -C build --setup=valgrind --suite core --suite speech --suite a11y
meson setup build-asan -Db_sanitize=address,undefined && meson test -C build-asan
meson setup build-tsan -Db_sanitize=thread && meson test -C build-tsan
```

The `valgrind` setup turns off the PCRE2 JIT (`ALUS_REGEX_NO_JIT`) and tells
latency tests to report rather than assert their budgets
(`ALUS_UNDER_VALGRIND`).

### Writing a test for a bug

Most bugs in a screen reader are "it said the wrong thing" or "it said
nothing". The pattern that catches them:

1. Reproduce the application's behaviour in the mock: the tree, the order of
   events, which objects die. A debug log from the real session shows it.
2. Assert on `NullSynth::spokenText()`.
3. Check that the test **fails without the fix** before relying on it.

## Running and observing the daemon

```bash
./build/src/alus-daemon --log-level debug --log-file ~/alus-debug.log
tools/alus-switch.sh measure 300      # swap readers, debug log, swap back after 300 s
```

Useful log lines at `debug` level:

| Line | Meaning |
|---|---|
| `slow event: <event> for <object> (<role>) took N ms, M AT-SPI calls: states×12 parent×9 …` | an event that took long, how many platform calls it cost, and which |
| `slow AT-SPI call: <function> took N ms` | one platform call of 20 ms or more |
| `watchdog: …` | the core was stuck; keys were released and are reinstalled after recovery |
| `the speech-dispatcher module is X, whose latency ALUS has not measured` | timing is not guaranteed with this module |

`--log-level io` additionally logs every speech utterance and key event.

To see what a focus announcement costs without running the daemon:

```bash
./build/tests/measure-focus            # the current focus, all layers
./build/tests/measure-focus noapp      # the same without app modules (also nonav, nobraille)
```

It assembles the daemon's layers silently against the live accessibility bus
and replays a focus event for whatever has the focus, printing the AT-SPI
calls by name. It takes no keys, speaks nothing and moves no focus, so it is
safe while another screen reader runs. It is how N129 was found.

`alus-cli` has read-only diagnostics for every layer (`input-info`,
`a11y-info`, `a11y-tree`, `text-info`, `browse-info`, `browse-walk`,
`braille-info`, `app-info`, `gui-info`, `autostart`); see the
[user guide](user-guide.md#diagnostics). `alus-cli a11y-tree 4 nautilus`
shows the tree of one application as ALUS reads it, which is the first thing
to look at when an application is silent.

## Generated files

Some files in the repository are generated. Edit the generator or its input,
never the output; the `external` test suite fails when an output is stale.

| Output | Generator | Input |
|---|---|---|
| `data/scripts.csv`, `data/gestures-builtin.ini`, `data/key-labels.ini`, `data/locale/*/gestures.ini`, `src/i18n/data_strings.cpp` | `tools/extract-commands.py` | the NVDA source (`reference/nvda/` or a full checkout) and `data/gestures-alus.ini` |
| `src/a11y/role_tables.cpp`, `src/a11y/state_tables.cpp` | `tools/extract-controltypes.py` | `reference/nvda/source/controlTypes/` |
| `docs/commands.md`, `docs/commands.csv` | `tools/generate-command-reference.py` | `data/scripts.csv`, `data/commands-status.ini` |
| `po/POTFILES.in` | `tools/update-potfiles.py` | the sources that use `_()` |
| `po/alus.pot` | `meson compile -C build alus-pot` | `po/POTFILES.in` |
| `po/sr.po`, `po/sr@latin.po` (shared messages) | `tools/import-nvda-po.py` | NVDA's Serbian catalogue |

```bash
python3 tools/extract-commands.py --source ./reference/nvda --out data/ \
        --strings-out src/i18n/data_strings.cpp
python3 tools/extract-controltypes.py --source ./reference/nvda --out .
python3 tools/generate-command-reference.py
python3 tools/update-potfiles.py
meson compile -C build alus-pot
meson compile -C build alus-update-po
```

Files that are **not** generated and are maintained by hand:

- `data/commands-status.ini`: which commands ALUS does not have yet
  (`[announce]`: the key says so) and which keys are deliberately not grabbed
  because they belong to the application (`[platform]`).
- `data/gestures-alus.ini`: the keys ALUS binds differently from upstream;
  the extractor applies them.

## Adding things

**A command.** Implement the script in the subsystem that owns it, as a
method of that subsystem's scriptable object (for example
`speech::GlobalCommands` in `src/speech/global_commands.cpp`), under the name
it has in `data/scripts.csv`, so the existing key binding and
description apply. If the command was listed in `[announce]` in
`data/commands-status.ini`, remove it from there, then regenerate the command
reference.

**An app module.** Add a file to `src/app/modules/`, register it in
`src/app/modules/builtin.cpp` with the executable it matches, and add tests
in `tests/app/`. Look at a real application's tree with `alus-cli a11y-tree`
first; the app modules in phase 8 were all rewritten after measuring real
applications ([phase-8.md](history/phase-8.md)).

**A setting.** Add a `Row` to `src/gui/model.cpp` (section, key, label,
help), give it a default where the subsystem reads it, and read it in that
subsystem's `applyConfiguration`. Document it in
[configuration.md](configuration.md).

**A translatable message.** Wrap it in `_()` (or `N_()`, `C_()`, `P_()` for
plurals, see `include/alus/i18n/i18n.hpp`). Build whole sentences with
placeholders; never assemble a sentence from fragments, and let gettext
choose plural forms (Serbian has three).

## Translations

The catalogues are in `po/`. To add a language, add its code to `po/LINGUAS`,
then:

```bash
meson compile -C build alus-pot          # refresh the template
msginit -i po/alus.pot -l <lang> -o po/<lang>.po
# translate po/<lang>.po
meson compile -C build                   # builds the .mo files
./build/src/alus-daemon --language <lang>
```

To update after the sources change: `meson compile -C build alus-pot`, then
`meson compile -C build alus-update-po`. Role and state names, and many
messages, are shared with NVDA; `tools/import-nvda-po.py` imports NVDA's
translations by message id, which gives a new language a large head start.

`po/overrides.po` holds translations ALUS deliberately words differently
from the imported ones.

## Conventions

- **Language.** Code, comments, commit messages and this documentation are
  in English. The phase reports in `docs/history/` are in Serbian.
- **Every source file** starts with the SPDX header
  (`SPDX-License-Identifier: GPL-2.0-or-later`) and the copyright line.
- **The build is warning free** (`werror`). Do not silence a warning with a
  cast without saying why.
- **Comments explain why**, especially when the obvious approach was tried
  and measured to be wrong. Cite the finding (`N113`) or the section of the
  architecture (`architecture.md §7.3`) when there is one.
- **Measured facts beat the plan.** When the platform behaves differently
  from the design, follow the platform and write the deviation down.
- **Silence is the worst failure.** A screen reader that starts, says it
  started and then answers no key, or a command that does nothing when
  pressed, is worse than one that says what is wrong. Fail out loud.
- **ALUS never says the other screen reader's product name** to the user. The
  key is called `alus`; the other reader's spelling is accepted only when
  reading imported gesture maps.
- **Everything that touches state runs on the main thread.** Callbacks from
  other threads go through `core::Loop::queueFunction`.
- **A check must not share its source with what it checks**: a test that
  reads the same generated table as the code proves nothing about the table.

## History

How ALUS got here, phase by phase, with every finding that changed the code
(N1–N128), is in [docs/history](history/README.md). When a comment says
"N79" or "phase-9.md O7", that is where to look.
