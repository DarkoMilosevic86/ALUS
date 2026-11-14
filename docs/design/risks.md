# ALUS — Risk register

> Design document 3 of 3.
>
> Every risk has a **cause** (a checked fact, not an assumption), a
> **consequence**, a **mitigation** and a **detector**: a command or a test
> that says whether the risk has materialised.

---

## Matrix

| # | Risk | Likelihood | Impact | Phase | Status |
|---|---|---|---|---|---|
| **R1** | Mutter/KWin allow KeyboardMonitor only for Orca | **High** | **Critical** | 0 | mitigated |
| **R2** | Compositors without KeyboardMonitor (sway, wlroots) | Medium | High | 0 / v1.1 | deliberately postponed |
| **R3** | Chromium does not fill the AT-SPI cache → slow browse mode | Medium | High | 0 / 6 | mitigated (`Collection`) |
| **R4** | speech-dispatcher does not keep the speech contract | Medium | High | 0 / 2 | **partly confirmed, mitigation implemented** |
| **R5** | AT-SPI event floods | **High** | **Critical** | 1 | mitigated in the architecture |
| **R6** | Offset encoding (UTF-8 / UTF-16 / codepoint) | **High** | High | 5 | **materialised (N113), fixed** |
| **R7** | `getTextWithFields` synthesised incorrectly | **High** | **Critical** | 6 | ported from NVDA |
| **R8** | Caret event reliability varies by toolkit | **High** | Medium | 5 | mitigated in the architecture |
| **R9** | Project scope (NVDA is 189k lines) | **High** | **Critical** | all | binding non-goals |
| **R10** | Coexistence with Orca | High | Low | 3 | simple mitigation |
| **R11** | Licence exposure | Low | High | before code | decided |
| **R12** | Lack of test coverage for accessibility | Medium | Medium | 4 | mock backend |
| **R13** | SSML support depends on the module | **High** | Medium | 2 | **mitigation implemented** |
| **R14** | An index mark in the middle of an utterance is silently lost | **High** | **Critical** | 2 | **mitigation implemented and tested** |
| **R15** | Latency measurement is unreliable for synthesisers with a long tail | Medium | Low | 0 / 2 | handled in the method |
| **R16** | `libspeechd` destroys its mutex while its thread still waits on it | Medium | Medium | 9 | report upstream |

R13–R15 were added after phase 0 (`spikes/RESULTS.md`), R16 after phase 2
([`phase-2.md`](../history/phase-2.md)).

---

## R1 — Mutter and KWin allow KeyboardMonitor only for Orca

**Cause (checked).** Mutter 48.7 exposes `org.freedesktop.a11y.KeyboardMonitor`
on `org.freedesktop.a11y.Manager` / `/org/freedesktop/a11y/Manager`. Access
control requires the caller to hold a D-Bus name from a hard-coded list. In
`libmutter-16.so.0.0.0` there is **exactly one such name**:

```bash
strings /usr/lib/x86_64-linux-gnu/libmutter-16.so.0.0.0 | grep KeyboardMonitor
# MetaDBusKeyboardMonitor              ← GObject types, not D-Bus names
# MetaDBusKeyboardMonitorProxy
# org.freedesktop.a11y.KeyboardMonitor ← the interface name
# MetaDBusKeyboardMonitorSkeleton
# org.gnome.Orca.KeyboardMonitor       ← the ONLY name on the allow list
```

**Consequence.** Without passing access control, ALUS on Wayland **sees no
key at all**. No command works. The project is dead.

**Mitigation (two layers).**

1. **Now:** ALUS requests the name `org.gnome.Orca.KeyboardMonitor` (the
   `[input] appId` setting) and passes access control. This is legitimate:
   the name identifies the role "this session's screen reader", not a claim
   that ALUS is Orca. The consequence is that **ALUS and Orca cannot run at
   the same time** (see R10), which is the case anyway. ALUS never says the
   other reader's name to the user.
   Mutter also has a debug flag `a11y-manager-without-access-control`
   (visible in the `libmutter` strings) as a way out for development.
2. **Permanently:** upstream merge requests that add an ALUS name to the
   Mutter and KWin allow lists. The GNOME merge cycle is months long, and the
   `appId` workaround bridges the gap until then.

**Detector:**
```bash
busctl --user list | grep KeyboardMonitor    # who holds the name
alus-cli input-info                          # ALUS's own diagnosis
```

---

## R2 — Compositors without KeyboardMonitor

**Cause.** The interface exists in Mutter since 48 and in KWin since 6.3.6.
**sway and the other wlroots compositors do not implement it.** There, a
client cannot see other clients' keys in any supported way.

**Consequence.** ALUS does not work on sway/Hyprland/river. A minority of
users, but a vocal one.

**Mitigation.** The `IInputBackend` abstraction exists since phase 3, so
adding a backend later is mechanical, not architectural. A third backend
(`EvdevBackend`) is **postponed to v1.1** with a documented cost:

- it reads `/dev/input/event*` directly; that requires the user to be in the
  `input` group or a udev rule;
- "consuming" keys requires `EVIOCGRAB`, which grabs **the whole device**, so
  ALUS has to re-inject every other key through `/dev/uinput`, which is
  root-only;
- that means a privileged helper process. The security and installation cost
  is real and must be the user's conscious decision, not a default.

An alternative to study before evdev: `libei` /
`org.gnome.Mutter.InputCapture`. It is meant for input leasing, not
accessibility, but may be a cleaner path.

**Detector:**
```bash
gdbus introspect --session --dest org.freedesktop.a11y.Manager \
  --object-path /org/freedesktop/a11y/Manager >/dev/null 2>&1 \
  && echo "KeyboardMonitor available" || echo "none — a fallback is needed"
```

---

## R3 — Chromium does not fill the AT-SPI cache

**Cause (checked).** `org.a11y.atspi.Cache.GetItems` per application:

| Application | Objects in the cache | Time |
|---|---|---|
| gnome-shell | 1837 | 31 ms |
| org.gnome.Terminal | 87 | 1.8 ms |
| gjs | 58 | 1.0 ms |
| **Google Chrome** | **4** | 0.4 ms |

Chromium builds its AT-SPI tree **lazily, on demand**, and does not fill the
cache. So a "read the whole tree in one call" strategy does not work for
browsers, and browsers are the most important case for browse mode.

**Consequence.** If quick navigation had to walk the tree object by object,
pressing `h` in a long article could take hundreds of milliseconds. That is
the difference between a usable and an unusable browse mode.

**Mitigation.** Do not walk the tree. `org.a11y.atspi.Collection.GetMatchesFrom`
is a **server-side** search: one D-Bus call per quick navigation jump, with
the search running inside the browser process. Spike S3 measured it before
phase 6 was designed (`spikes/RESULTS.md`).

**Detector:** `alus-cli browse-info` prints what quick navigation costs on
the running browser.

---

## R4 — speech-dispatcher does not keep the speech contract

**Cause.** NVDA's `SynthDriver` contract requires an `IndexCommand` and the
notifications `synthIndexReached` / `synthDoneSpeaking`. NVDA's eSpeak driver
uses `AUDIO_OUTPUT_SYNCHRONOUS` and **plays the audio itself**, which is why
caret synchronisation in say all is sample accurate.

Through speech-dispatcher, ALUS **does not own the audio stream**. So:
- cancel latency depends on the SSIP module and the audio output,
- the accuracy of index marks depends on the module; `espeak-ng` has
  `EspeakIndexing 1`, but RHVoice and generic modules may not,
- `volume` is not supported by every module.

A mitigating fact: SSIP **has** every notification needed:
```bash
strings /usr/lib/x86_64-linux-gnu/libspeechd.so.2 | grep "SET SELF NOTIFICATION"
# begin, end, cancel, pause, resume, index_marks
```
And NVDA uses **one callback per line** in say all anyway, not per word; line
granularity is quite enough.

**Consequence if it materialises.** Say all "creaks": the caret lags behind
the audio, a cancel is heard late. Users notice immediately, and it is the
main complaint about existing Linux screen readers.

**Mitigation (three layers).**

1. **An `ISynth` abstraction with two real implementations.** `SsipSynth` is
   the default (it meets the user requirement and works with every module the
   user already has). `EspeakDirectSynth` (direct `libespeak-ng` + ALUS's own
   `WavePlayer`) is planned for v1.1 for users who want maximum accuracy.
2. **ALUS holds the speech queue, not speech-dispatcher.** SSIP's priority
   queue is **not** used for sequencing: ALUS sends one utterance in flight
   at one SSIP priority, and `SpeechManager` implements NVDA's semantics
   (`NOW/NEXT/NORMAL`, index callbacks, cancelling stale announcements).
   Mixing two queues would give an unpredictable order.
3. **Measurement is a test**, not a note: `speak→sound p95 < 50 ms`,
   `cancel→silence p95 < 30 ms`.

**Detector:** spike S2, then the same measurement as `alus-cli bench-speech`
and `meson test -C build --suite live`.

> **Status after phases 0 and 2: partly confirmed, mitigation known.**
> speech-dispatcher keeps three of the four parts of the contract: first
> sound 11.5 ms p95, cancel 29.0 ms p95, index marks accurate to 46 ms p95.
> The fourth (`end`) it does not keep, it is ~294 ms late, but the mitigation
> exists and is implemented: chaining goes only through `IndexCommand`
> ([`phase-2.md`](../history/phase-2.md)).

---

## R5 — AT-SPI event floods

**Cause.** NVDA has `OrderedWinEventLimiter` with two-stage filtering
(deduplication by moving in time, SHOW cancels HIDE, foreground deletes
focus, **at most 10 events per thread per cycle**, keep the last 4 focus
events, then emit **only one** focus/foreground). It is not an optimisation;
it is why NVDA stays responsive.

**AT-SPI floods in the same way:** Chromium emits thousands of
`ChildrenChanged` while loading a page; LibreOffice emits a `StateChanged`
per cell while scrolling a sheet; a GTK list view emits `ChildrenChanged` per
row.

**Consequence.** Without a limiter, ALUS pumps thousands of events per
cycle, speech lags by seconds, and the user experience is "the screen reader
choked", exactly the complaint about existing solutions.

**Mitigation.** `alus::a11y::EventLimiter` is **part of phase 1**, not an
optimisation for later. The rules are in
[`architecture.md`](architecture.md) §3.4.

**Detector:** tests T4.3–T4.5 (`meson test -C build --suite a11y`). In the
running daemon, every event that takes more than a few tens of milliseconds
is logged at debug level as `slow event: … took N ms, M AT-SPI calls`.

---

## R6 — Offset encoding

**Cause.** Three systems are involved:
- AT-SPI2 offsets, assumed to be **UTF-8 bytes** (measurement later showed
  they are **codepoints**, see the status note),
- NVDA assumes **UTF-16 code units** (`WCHAR_ENCODING`),
- internal logic works in **codepoints / graphemes**.

**Consequence.** Silent errors. Every emoji, CJK string and combining
diacritic is shifted by a few positions. Nothing crashes; the wrong character
is read, the selection is off, the braille display shows shifted text.
**It shows only in bug reports, months later.**

For Serbian this is directly relevant: Cyrillic is 2 bytes per character in
UTF-8 and 1 UTF-16 code unit.

**Mitigation.** An `OffsetConverter` layer as in NVDA
(`textUtils/__init__.py`), done once, in phase 5, with tests.

**Detector:** test T5.1, a byte↔codepoint round trip for emoji (U+1F600), CJK,
a combining diacritic and Cyrillic; T5.4, a decomposed "é" is recognised as
**one** character for spelling.

> **Status: materialised and fixed** ([`phase-10.md`](../history/phase-10.md)
> N113). The tests passed because the mock backend counted bytes, just like
> the code. A live measurement in Chrome showed AT-SPI counts codepoints; the
> mock was corrected to count as the platform does, and the text layer now
> has an explicit `offsetUnit()`.

---

## R7 — `getTextWithFields` synthesised incorrectly

**Cause.** AT-SPI gives flat text + attributes per offset, but **does not
give** NVDA's nested `controlStart` / `controlEnd` command stream. NVDA gets
it for free, from a virtual buffer (which ALUS does not build, see
[`architecture.md`](architecture.md) §9.1) or from a `CompoundDocument`.
ALUS has to **synthesise** it from ancestor chains.

**Consequence.** **All** document reading depends on this stream:
- `commonFieldCount` continuity: without a stable `uniqueID`, "list with 5
  items" is repeated on **every line** of the list,
- `isBlock`: splitting utterances in say all; without it say all merges
  paragraphs,
- the `table-*` keys: table navigation and headers,
- container enter/leave announcements.

If this is wrong, browse mode is unusable even though it "works".

**Mitigation.** The solution **already exists in NVDA, in a platform-neutral
form**, and is ported literally:

```
reference/nvda/source/compoundDocuments.py:164-211
    _getControlFieldForObject(obj, ignoreEditableText=True)
    → synthesises a ControlField from role, states, name, description, level, table coordinates

reference/nvda/source/compoundDocuments.py:331-368
    TreeCompoundTextInfo.getTextWithFields(formatConfig)
    → emits controlStart/controlEnd/formatChange by DIFFING ancestor chains
```

Alongside it, the concept of `virtualBuffers._normalizeControlField`
(`:393-448`): **one** place that turns attributes into canonical keys
(`table-id`, `table-rowheadertext`, `isHidden`, `alwaysReportName`,
`uniqueID`). `uniqueID` = the AT-SPI `(bus name, object path)`, **more
stable** than NVDA's `(docHandle, ID)`.

**Detector:** tests T6.1–T6.4 (`--suite browsemode`). In particular T6.3:
moving through 100 lines must not repeat a container announcement.

---

## R8 — Caret event reliability varies

**Cause.** NVDA's `editableText._hasCaretMoved` has **three strategies with a
time budget**, because Windows applications lie about caret events. On Linux
**GTK, Qt, Electron, Java and LibreOffice lie, each differently.** Some send
`TextCaretMoved` before the change, some after, some not at all for
programmatic moves.

**Consequence.** "I press an arrow and it does not read the new line", or
"it reads the old line". The most irritating bug a screen reader can have.

**Mitigation.**
- The three strategies are **carried over**, not simplified.
- `caretMovementDetectionUsesEvents` stays an override per behaviour.
- The timeouts are **tuned per toolkit**, and that is expected work.

**Detector:** test T5.5, three separate tests (GTK 4, Qt 6, LibreOffice),
with a timeout setting allowed to differ per toolkit.

---

## R9 — Project scope

**Cause (checked).** NVDA's source code:

```
source/ total              188,739 lines of Python
source/NVDAObjects/         28,040
source/gui/                 19,523
source/appModules/           9,838   (79 modules)
source/UIAHandler/           7,875
source/speech/               5,721
source/globalCommands.py     5,714   (~300 scripts)
source/braille/              5,285
source/config/               4,111
source/browseMode.py         3,012
source/virtualBuffers/       2,738
```

**Consequence.** A project that tries to be NVDA never ships. This is the
most likely way for ALUS to fail: not technically, but by exhaustion.

**Mitigation.**
1. **The non-goals in [`vision.md`](vision.md) §5 are binding.** Left out:
   display model screen review, OCR, the magnifier, the screen curtain,
   touch, remote access, the Java Access Bridge, the add-on store, math,
   vision providers. That is about 60% of NVDA's scope.
2. **The deletions that save the most:** `UIAHandler` + `IAccessibleHandler`
   + `JABHandler` + `displayModel` + `virtualBuffers` + `NVDAHelper` +
   `comInterfaces` ≈ **45,000 lines**, replaced by one `alus::a11y` layer
   over `libatspi`.
3. **Braille drivers:** NVDA has about 40 drivers for individual displays.
   ALUS has **one** (`brlapi`); brltty already supports all of those
   displays and users have it configured.
4. **App modules:** NVDA has 79. ALUS v1.0 has **five**. Browsers need **no**
   app module (architecture §9.4).
5. **If a phase grows, scope is cut, not the deadline.** This is a rule, not
   a recommendation.

**Detector:** at the end of each phase, `cloc src/` compared with the budget.
The target size of ALUS v1.0 was **35,000–45,000 lines of C++20**.

---

## R10 — Coexistence with Orca

**Cause.** Orca and ALUS compete for: the D-Bus name
`org.gnome.Orca.KeyboardMonitor` (R1), the gsettings key
`org.gnome.desktop.a11y.applications screen-reader-enabled`, and autostart.

**Consequence.** If both run, keys go to one and speech to the other, or
neither gets the keys. Confusing and hard to diagnose.

**Mitigation.**
1. At startup ALUS checks whether the KeyboardMonitor name already has an
   owner, and if so **says** why it gets no keys, instead of starting silent.
2. ALUS installs its autostart entry but **does not enable it**; enabling it
   is the user's explicit choice (`alus-cli autostart --enable`). It never
   disables the other reader silently.
3. `tools/alus-switch.sh` swaps the two readers safely, speaks every outcome
   through `spd-say`, and rolls back to the reader that worked if a start
   fails.

**Detector:**
```bash
alus-cli autostart                 # what starts with the session, who holds the keys
tools/alus-switch.sh status
gsettings get org.gnome.desktop.a11y.applications screen-reader-enabled
```

---

## R11 — Licence exposure

**Cause.** NVDA is GPL-2.0-or-later. ALUS does not copy code, but takes
**derived works**: the command tables, script names, `symbols.dic` and
`characterDescriptions.dic` per language, command descriptions, the
structure of the `gestures.ini` format, and translations. In addition,
`tools/extract-commands.py` **imports NVDA modules** to generate ALUS data.

**Consequence.** An unclear licence blocks packaging in distributions and
turns contributors away.

**Mitigation (decided before the first line of code).** **ALUS is
GPL-2.0-or-later.** It is compatible with every dependency: `libatspi`
(LGPL-2.1+), `liblouis` (LGPL-3.0+; tables GPL-3.0+), `libbrlapi`
(LGPL-2.1+), `glib` (LGPL-2.1+), `libspeechd` (LGPL-2.1+), GTK 4
(LGPL-2.1+). Every file carries `SPDX-License-Identifier: GPL-2.0-or-later`.
`reference/nvda/` keeps the upstream commit (`UPSTREAM_COMMIT`) and licence
(`copying.txt`) of the subset the generators read.

**Detector:** `reuse lint`; a check that every `.cpp`/`.hpp` has an SPDX
header.

---

## R12 — Lack of test coverage for accessibility

**Cause.** Testing a screen reader needs a live session with applications.
CI runners have no display server and no accessibility bus.

**Consequence.** Phases 4–8 would have no automated tests, regressions would
be found by hand, and the project would depend on one tester.

**Mitigation (as implemented).**
1. **A mock platform.** `alus::a11y::Backend` is an interface, and
   `tests/a11y/mock_backend.hpp` implements it with a synthetic object tree
   (`Accessible`, `Component`, `Text`, `Collection`, `Table`), so the whole
   `alus::a11y`, `alus::text`, `alus::nav` and browse mode stack is tested
   without a screen.
2. **`NullSynth` as the test output.** Every utterance is recorded; tests
   check the **text** the user would hear, not audio, and drive index marks
   by hand so timing is deterministic.
3. **The `live` suite** runs against the real session (speech-dispatcher, the
   accessibility bus) when asked for explicitly.

**Detector:** `meson test -C build` runs on a machine without a display.

---

## R13 — SSML support depends on the module

**Added after phase 0** (finding N17). **Likelihood high, impact medium.**

**Cause.** `SsipSynth` builds utterances with SSML (`<mark>`, `<break>`,
`<prosody>`, `<voice>`). What a module honours is not part of the SSIP
protocol: `espeak-ng` honours `<break time=...>`, `rhvoice` **ignores** it.

**Consequence.** Behaviour changes with the module the user picks, and the
user does not connect it with the module: to them, "ALUS does not pause".

**Mitigation (implemented in phase 2).** `ISynth::supports(SynthFeature::Breaks)`
answers per module, and `supportedCommands()` leaves out `BreakCommand` when
the module does not honour breaks, so the upper layers do not use it.
`timingIsGuaranteed()` is `false` for every module ALUS has not measured, and
that is **said in the log at startup**.

**Detector:** `alus-cli bench-speech --module X`.

---

## R14 — An index mark in the middle of an utterance is silently lost

**Added after phase 0** (findings N15/N16). **Likelihood high, impact
critical.** The worst new risk phase 0 found.

**Cause.** espeak-ng loses a `<mark>` after a sentence boundary when the
clause before it is long enough. No error, no event; the mark simply does not
exist. The SSIP path inherits that.

**Consequence.** A `SpeechManager` that tracks progress through index marks
blocks waiting for an index that never comes: **speech stops and does not
resume**, and the log does not say why. For a screen reader that is total
failure.

**Mitigation (implemented and tested in phase 2).**

1. **Marks only at the end of an SSIP message.** `SsipSynth` splits the
   utterance into separate messages where a mark would fall right after final
   punctuation, the only shape proven to survive.
2. **`SpeechManager` has deadlines.** The `end` notification (which is late,
   N12) is the watchdog: if the server says it has finished and the marks
   have not arrived, after a short grace period (`[speech] lostIndexGraceMs`,
   300 ms) the manager goes on with a `DEBUG_WARNING`. There is also an
   absolute deadline (`[speech] stallTimeoutMs`, 10 s) for a backend that
   says nothing. `SpeechManager::lostIndexCount()` is a counter for
   diagnostics.
3. Report the bug upstream to espeak-ng.

**Detector:** `spikes/s2-markloss.c` per module; `tests/speech/test_manager.cpp`
tag `[r14]`; `tests/speech/test_ssip_messages.cpp`; `--suite live` fails if
any utterance does not reach its end.

---

## R15 — Latency measurement is unreliable for synthesisers with a long tail

**Added after phase 0** (finding N17). **Likelihood medium, impact low.**

**Cause.** Waiting for silence before an iteration times out with modules
that have a long tail, so the onset detector catches leftover sound from the
previous iteration (a negative `min` with `rhvoice`).

**Mitigation.** When measuring a new module, extend the silence wait and
discard negative samples. `--suite live` pauses and cancels speech between
iterations.

**Detector:** a negative or impossibly small `min` in the report means the
measurement is invalid, not that the module is fast.

---

## R16 — `libspeechd` destroys its mutex while its thread still waits

**Added after phase 2** (finding N25). **Likelihood medium, impact low to
medium** (the shutdown path).

**Cause.** `spd_close()` calls `pthread_mutex_destroy` on a mutex on which the
same connection's reader thread is still in `pthread_cond_wait`.
ThreadSanitizer reports it as a data race, and by the standard it is undefined
behaviour. Both participants are inside `libspeechd`; ALUS only calls
`spd_close`, and there is no other API for closing.

```
WARNING: ThreadSanitizer: data race
  Write of size 1 by main thread: pthread_mutex_destroy ← spd_close
  Previous atomic read by thread T1: pthread_cond_wait ← libspeechd reader
SUMMARY: ... in spd_close
```

**Consequence.** At best nothing; at worst a crash at shutdown or on a
`resetConfiguration()` that changes the synthesiser. A crash at shutdown is
almost invisible to the user; a crash on a voice change is not.

**Mitigation.** For now `SsipSynth::terminate()` calls `spd_cancel()` first
and only then `spd_close()`, so the reader thread has less to do at the
moment of closing, and ALUS never holds two connections. If the crash shows
up in practice: report it upstream and consider reusing the connection on
`resetConfiguration()` instead of closing it.

**Detector:** a ThreadSanitizer build running the `live` speech test, the only
place it shows, because it needs a real server.

---

## Risks **resolved** by research

Listed so they are not researched again:

| Assumed risk | Finding |
|---|---|
| "AT-SPI is too slow for a screen reader" | **False.** A property round trip is 0.053–0.064 ms; `Cache.GetItems` returns 1837 objects in 31 ms (~17 µs/object). The architecture decides, not IPC. |
| "We need a virtual buffer like NVDA's" | **No.** `Collection.GetMatchesFrom` is a server-side search; the browser already maintains flat `Text` with U+FFFC and `Hypertext`. Architecture §9.1 |
| "Wayland does not allow global shortcuts; a hack is needed" | **Solved and standardised.** `org.freedesktop.a11y.KeyboardMonitor` in Mutter 48+ and KWin 6.3.6+. Only the allow list remains (R1). |
| "The ALUS key (Insert/Caps Lock) has to be implemented by hand" | **No.** A virtual modifier (15–31) that the compositor enforces. |
| "X11 and Wayland have to be supported separately" | **Not in the core.** One `IInputBackend` interface, the backend chosen at run time. |
| "Numpad versus navigation keys are a problem as on Windows" | **They are not.** With Num Lock off, the keysyms are named (`XK_KP_Home` ≠ `XK_Home`). Cleaner than NVDA's `(vkCode, extended)` pair. |
| "There are no Serbian braille tables" | **There are.** `liblouis` 3.33: `sr-Cyrl.ctb`, `sr-g1.ctb`, `sr-cyrletters.cti`, `sr-latletters.cti`. |
| "A driver has to be written per braille display" | **No.** `brlapi` (brltty 6.7) covers every display; NVDA's ~40 drivers are not ported. |
| "Firefox and Chrome need an app module" | **No.** NVDA has none on Windows either; support is in the object layer. On Linux one overlay for `toolkit_name ∈ {gecko, Chromium}`. |
| "Terminals need a display model as in NVDA" | **No.** VTE and Konsole expose a real `AtspiText` with `TextChanged` events; the `Terminal` behaviour works directly. |
| "AT-SPI does not cover everything NVDA needs" | **It does.** Checked interface by interface: `Action::get_key_binding` → `keyboardShortcut`, `Component::scroll_to` → `scrollIntoView`, `TableCell::get_row_header_cells` → headers, `Hyperlink::get_uri` → `alus+k`, `FLOWS_TO/FROM` → `alus+numpad3/9`, `DETAILS` → `alus+d`. See architecture §6.2. |
