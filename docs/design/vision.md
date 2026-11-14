# ALUS — Vision and scope

> Design document 1 of 3. Read it first. It defines **what** ALUS is, who it
> is for and, just as important, **what it is not**.
>
> | Document | Contents |
> |---|---|
> | [`vision.md`](vision.md) | this document: goals, non-goals, licence, glossary |
> | [`architecture.md`](architecture.md) | modules, threading, the NVDA → ALUS → Linux API mapping |
> | [`risks.md`](risks.md) | the risk register and its mitigations |
>
> The phase plan with its executable exit criteria is in
> [`../history/phase-plan.md`](../history/phase-plan.md) (Serbian).

---

## 1. What ALUS is

**ALUS** = **A**ccess **L**inux **U**sing **S**peech.

The name states the primary modality: speech is the default output and is
always there. Braille is a hard requirement (H7 below), but it is the second
output channel, not the first. That is why speech was phase 2 and braille
phase 7, and why `speech-dispatcher` is part of the project's identity rather
than an implementation detail.

ALUS is a screen reader for Linux, written in C++20, that:

1. **Uses the NVDA command set.** The same keys, the same script names, the
   same desktop and laptop layouts, the same `gestures.ini` format. A user
   coming from Windows has nothing new to learn.
2. **Speaks through speech-dispatcher.** The standard Linux speech layer. It
   works with espeak-ng, RHVoice, Piper and every other SSIP module the user
   already has.
3. **Runs on every desktop.** GNOME on Wayland first, then KDE Plasma and X11,
   through the same abstract input layer.
4. **Responds quickly.** The budget is **under 50 ms from key press to first
   sound**. This is a measured, tested requirement, not an aspiration.

## 2. Who it is for

Primarily blind and partially sighted Linux users who come from NVDA and find
Orca too slow, too unpredictable, or bound to the wrong keys. Secondarily,
developers who want to write app modules and add-ons without having to know
the whole core.

## 3. Why not Orca

Orca is the only serious screen reader for Linux, and it works. But:

- **The commands are different.** Orca has its own key (Insert/Caps Lock), its
  own flat review model and its own commands. Moving from NVDA means learning
  everything again, and that is the main practical reason people stay on
  Windows.
- **Architecture, not language, is the cause of slowness.** Measured on the
  target system: an AT-SPI property round trip takes **0.053–0.064 ms**, and
  `Cache.GetItems` returns **1837 objects in 31 ms** (about 17 µs per object).
  IPC is not the bottleneck. Slowness comes from missing per-cycle caching,
  missing event rate limiting and synchronous querying.
- NVDA spent 20 years solving these problems and the solutions are
  **documented in its code** (`core.CorePump`, `baseObject.AutoPropertyObject`,
  `OrderedWinEventLimiter`, `SpeechManager`). ALUS does not invent an
  architecture; it carries over a proven one.

ALUS is not a criticism of Orca and does not aim to replace it for its
existing users. It aims at NVDA users on Linux.

## 4. Hard requirements

| # | Requirement | How it is checked |
|---|---|---|
| H1 | The NVDA command set, desktop and laptop layouts | the generated command table is compared against the NVDA user guide |
| H2 | The ALUS key is Insert, numpad Insert and/or Caps Lock, by bit mask | `alus+t` works; a double Insert passes a real Insert through |
| H3 | `gestures.ini` in the same format, in three layers (user → locale → braille driver) | a user's `gestures.ini` from NVDA loads unchanged |
| H4 | speech-dispatcher as the default speech output | works with the user's existing SSIP configuration |
| H5 | Key press → first sound under 50 ms; speech cancelled in under 30 ms | a measuring test in CI |
| H6 | GNOME Wayland, KDE Wayland, X11 | one build, no conditional compilation in the core |
| H7 | Braille through BrlAPI, translation through liblouis | text on the display, cursor routing moves the caret |
| H8 | Localisation, Serbian included | the `sr` translation, the `sr-Cyrl` braille table, Serbian key remaps |

## 5. Non-goals for v1.0 — **this list is binding**

NVDA has about 189,000 lines of source code. ALUS v1.0 has to be dramatically
smaller or it never ships. The following are **deliberately left out** of
v1.0:

| Left out | Why | When |
|---|---|---|
| **Screen review through a display model** | NVDA hooks GDI calls inside other processes. On Wayland you cannot even read another client's surface. | Replaced by *flat object review* (v1.0): spatially sorted content objects. See [`architecture.md`](architecture.md) §10. |
| **OCR / content recognition** | Needs a screencast portal and a tesseract pipeline | v1.1, as the *last* fallback after AT-SPI and flat review |
| **Magnifier** (21 commands) | A separate problem; GNOME already has a magnifier | never, or as a separate project |
| **Screen curtain** | No clear Wayland equivalent | v1.2 |
| **Touch screen support** (`ts:` gestures) | Few users on Linux | v1.2 |
| **Remote access** (8 commands) | A large, self-contained subsystem | v1.2 |
| **Java Access Bridge** | AT-SPI covers Java through `java-atk-wrapper` | not needed |
| **Add-on store** | A stable add-on API has to come first | v1.1 |
| **Math (MathML interaction)** | Needs MathCAT or an equivalent | v1.1 |
| **Vision enhancement providers** | Visual features are not the focus | never |
| ~~**A settings GUI**~~ | ~~v1.0 has an ini configuration and a CLI~~ | **Done in phase 9**: `alus-settings` |

Everything left out of v1.0 must stay **architecturally possible**. That is
why `ISynth`, the input backend and the review modes were abstractions from
the start, not afterwards.

## 6. Licence

**ALUS is released under the GNU GPL version 2 or later
(GPL-2.0-or-later).**

Rationale: NVDA is GPL-2.0-or-later. ALUS **does not copy NVDA code**; the C++
implementation is new. But ALUS **does take derived works**:

- the command tables and script names (`globalCommands.py`,
  `browseMode.addQuickNav`),
- `symbols.dic` and `characterDescriptions.dic` for each language,
- the command descriptions from the user guide,
- the structure of the `gestures.ini` format,
- the translations of shared messages (`po/`).

GPL-2.0-or-later removes any legal doubt and is compatible with every
dependency: `libatspi` (LGPL-2.1+), `liblouis` (LGPL-3.0+ / GPL-3.0+ for the
tables), `libbrlapi` (LGPL-2.1+), `glib` (LGPL-2.1+), `libspeechd`
(LGPL-2.1+), GTK 4 (LGPL-2.1+).

Every source file carries an SPDX header:
`SPDX-License-Identifier: GPL-2.0-or-later`.

## 7. Glossary — the same terms as NVDA

Code, documentation and user help must speak **the same language as NVDA**;
otherwise users and documentation cannot be translated into each other.

| Term | Meaning |
|---|---|
| **caret** (system cursor) | the cursor the application moves with the arrow keys; ALUS follows it, it does not own it |
| **review cursor** | an independent cursor the user moves with the numpad; it does not move the caret |
| **navigator object** | the object the review cursor is on; the basis of object navigation |
| **review mode** | `object` / `document` / `screen`: what the review cursor walks over |
| **script** | a function bound to an input gesture; synonym: command |
| **gesture** | one input event: `kb:alus+t`, `br(alva):t1`, `bk:dot7` |
| **ALUS key** | Insert, numpad Insert and/or Caps Lock; not a real modifier. In a gesture it is written `alus` (`kb:alus+t`). A map imported from the other reader may spell it differently, and that spelling is accepted when reading ([`phase-3.md`](../history/phase-3.md) O14) |
| **browse mode** | reading a document as flat text, with single-letter quick navigation keys |
| **focus mode** | keys go to the application; automatic in edit fields |
| **pass-through** | `alus+space`; switching between browse and focus mode |
| **tree interceptor** | an interceptor that takes events and scripts for a whole subtree of objects |
| **overlay class** | a class woven dynamically into an object's inheritance chain |
| **app module** | customisation specific to one application |
| **say all** | continuous reading from the caret or review cursor to the end |
| **tether** | whether the braille display follows the focus or the review cursor |
| **input help** | `alus+1`; every key says what it does without doing it |

## 8. Success criteria for v1.0

ALUS v1.0 is done when an NVDA user can, without reading any documentation:

1. log in and navigate GNOME Shell and Nautilus,
2. read and edit text in GNOME Text Editor and LibreOffice Writer,
3. read a web page in Firefox or Chromium in browse mode, using `h`, `k`, `t`,
   `f`, `1`–`9` and `alus+f7`,
4. work in a terminal (gnome-terminal / Konsole) with new output announced,
5. use a braille display if they have one,
6. do all of this **faster than Orca**, measurably.
