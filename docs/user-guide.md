# ALUS user guide

This guide is for people who use ALUS. If you know NVDA, you already know
most of ALUS: the commands, their keys and the vocabulary (focus, review
cursor, navigator object, browse mode, focus mode, say all) are the same.
This guide covers what is specific to ALUS and to Linux.

- [Starting and stopping](#starting-and-stopping)
- [The ALUS key and keyboard layouts](#the-alus-key-and-keyboard-layouts)
- [The most important commands](#the-most-important-commands)
- [Settings](#settings)
- [Language](#language)
- [Speech](#speech)
- [Braille](#braille)
- [Browse mode and focus mode](#browse-mode-and-focus-mode)
- [Applications](#applications)
- [Changing keys](#changing-keys)
- [Diagnostics](#diagnostics)
- [Troubleshooting](#troubleshooting)

## Starting and stopping

**Only one screen reader can hold the keyboard.** The compositor (Mutter on
GNOME, KWin on KDE) delivers keys to exactly one screen reader. If another
screen reader is running, ALUS speaks but gets no keys.

Ways to start ALUS:

| How | When |
|---|---|
| `alus-cli autostart --enable` | ALUS starts every time you log in ([installing.md](installing.md#starting-alus-with-the-session)) |
| `tools/alus-switch.sh alus` | swap from the other screen reader to ALUS now; every step is spoken, and a failed start brings the other reader back |
| `alus-daemon` (installed) or `./build/src/alus-daemon` (from the source tree root) | start it directly, when no other reader runs |

To stop ALUS press **ALUS+Q**. If you started it with `alus-switch.sh`,
`tools/alus-switch.sh other` gives the keyboard back to the other reader.

When ALUS starts it says "ALUS started". If the compositor refuses it the
keyboard, ALUS **says so** instead of staying silent; see
[Troubleshooting](#troubleshooting).

Useful `alus-daemon` options:

```
--language=CODE      speak this language (en, sr, sr@latin), overriding the setting
--log-level=LEVEL    debug, io, debugwarning, info, warning, error, critical, off
--log-file=PATH      write the log to a file
--config-dir=DIR     use another configuration directory (default ~/.config/alus)
--no-braille         run without braille
--no-plugins         load no add-ons
--version            print the version
```

`alus-daemon --help` lists all of them.

## The ALUS key and keyboard layouts

The **ALUS key** is the modifier for ALUS commands, written `ALUS` in this
guide and `alus` in gesture files. By default both **Insert** and **numpad
Insert** are ALUS keys; **Caps Lock** can be added in the Keyboard settings.

- Press the ALUS key **twice quickly** to send a real Insert (or toggle Caps
  Lock) to the application.
- ALUS has a **desktop** layout (uses the numpad) and a **laptop** layout
  (uses the letter keys instead). Choose it in Settings → Keyboard → Layout.
- The numpad review commands need **Num Lock off**.

## The most important commands

Desktop layout; the [command reference](commands.md) lists every command
with both layouts.

| Keys | Command |
|---|---|
| ALUS+Q | quit ALUS |
| ALUS+1 | input help on/off: every key says what it does instead of doing it |
| Control | stop speech |
| Shift | pause / resume speech |
| ALUS+Control+G | open the settings window |
| ALUS+A | open the settings window (laptop layout: ALUS+N) |
| ALUS+Control+V | voice settings |
| ALUS+Down Arrow | say all from the caret (laptop: ALUS+A) |
| ALUS+Up Arrow | read the current line (laptop: ALUS+L) |
| ALUS+Tab | report the focus |
| ALUS+T | report the window title |
| ALUS+B | read the whole foreground window |
| ALUS+End | read the status bar (laptop: ALUS+Shift+End) |
| ALUS+F12 | time; press twice for the date |
| ALUS+Shift+F | report the formatting at the caret |
| ALUS+S | speech mode: talk, beeps, off, on demand |
| ALUS+P | punctuation and symbol level |
| ALUS+2 | speak typed characters on/off |
| ALUS+Control+Left/Right Arrow | choose a voice setting (rate, pitch, volume, voice…) |
| ALUS+Control+Up/Down Arrow | change the chosen voice setting |
| ALUS+Space | switch between browse mode and focus mode |
| ALUS+F7 | elements list (links, headings, landmarks…) in browse mode |
| ALUS+V | screen layout on or off in browse mode (the choice is kept) |
| ALUS+Shift+S | sleep mode for the current application |
| Numpad 7 / 8 / 9 | review cursor: previous / current / next line |
| ALUS+Numpad 5 | report the navigator object |
| ALUS+Numpad 8 / 2 / 4 / 6 | navigator: parent / first child / previous / next object |
| Numpad Plus | say all from the review cursor |

Pressing a reporting command twice usually spells what it said; three times
spells it with character descriptions ("a, Avala").

Commands that ALUS does not have yet are still bound to their keys and
**say** that they are not in this version, so a key never silently does
nothing. The reference marks them.

## Settings

Open the settings with **ALUS+Control+G** (or **ALUS+A**), from the
application menu ("ALUS Settings"), or with `alus-settings`. The window is a
separate program, so it is read like any other application.

| Page | What it sets |
|---|---|
| General | language, log level |
| Speech | synthesizer, output module, voice, variant, rate, pitch, volume, inflection, symbol level, automatic language switching |
| Braille | display, translation tables, cursor, tethering, word wrap, messages |
| Keyboard | layout, which keys are ALUS keys, typed character and word echo, what interrupts speech |
| Object presentation | descriptions, shortcuts, position information, tooltips, notifications |
| Review cursor | what the review cursor follows |
| Document formatting | which formatting, structure and table information is announced while reading |
| Gestures | the gestures dialog: every command, its keys, add or remove a key |

Arrow keys change a choice and the new value is spoken. **OK** saves and
closes, **Cancel** or **Escape** discards every change. Some choices, such as
the voice, can be tried before saving.

Everything in the window is stored in `~/.config/alus/alus.ini`; see
[configuration.md](configuration.md) to edit it by hand.

## Language

ALUS's language is independent of the desktop's: you can have an English
desktop and a Serbian screen reader. Choose it in Settings → General →
Language:

| Choice | Messages, roles and states |
|---|---|
| Same as the session | the desktop's language |
| English | English |
| Serbian (Cyrillic) | `sr` |
| Serbian (Latin) | `sr@latin` |

The change takes effect when you press OK; ALUS does not need to be
restarted. `alus-daemon --language=sr` overrides the setting for one run.

The voice's language is separate: ALUS switches the voice language
automatically when the text says what language it is in (Settings → Speech →
Follow the language of the text).

## Speech

ALUS speaks through speech-dispatcher. Settings → Speech chooses the
**output module** (espeak-ng, RHVoice, …), then a **voice** of that module,
and for eSpeak a **variant**. Rate, pitch, volume and inflection go from 0 to
100. You can also change them without the window: **ALUS+Control+Left/Right
Arrow** picks a setting and **ALUS+Control+Up/Down Arrow** changes it.

`alus-cli list-modules` and `alus-cli list-voices` show what
speech-dispatcher offers.

espeak-ng is the module ALUS has measured. With other modules ALUS works, but
logs that their timing is not guaranteed; RHVoice, for example, ignores
pauses and responds more slowly.

**Symbol level** (ALUS+P) decides how much punctuation is spoken: none,
some, most, all. Symbols and their names come from `symbols.dic` for the
language; your own additions go in `~/.config/alus/symbols-<language>.dic`.

**Say all** (ALUS+Down Arrow) reads from the caret to the end and moves the
caret as it reads. Press **Control** to stop: the caret stays where reading
stopped.

## Braille

ALUS drives braille displays through **brltty**: install and configure
brltty for your display, then choose "brltty" in Settings → Braille →
Display. Translation is done by liblouis; choose the output and input tables
on the same page (Serbian Cyrillic tables are included in liblouis).

`alus-cli braille-info` checks liblouis, the tables and the connection to
brltty.

## Browse mode and focus mode

In web pages and read-only documents ALUS uses **browse mode**: the document
is read like text, with the arrow keys, and single letters jump through it:

| Key | Next | Key | Next |
|---|---|---|---|
| H | heading | K | link |
| 1–9 | heading at that level | F | form field |
| T | table | L | list |
| I | list item | B | button |
| E | edit field | X | check box |
| R | radio button | C | combo box |
| G | graphic | D | landmark |
| Q | block quote | S | separator |

Shift with the letter goes to the previous one.

**ALUS+F7** opens the elements list: the page's links, headings, form fields,
buttons or landmarks, chosen with the **Type** radio buttons, and narrowed by
typing in **Filter by**. **Activate** (or Enter on an element) follows the link
or presses the button; **Move to** closes the list and puts the browse mode
caret on the element; **Cancel** or Escape closes it without doing anything.
The list opens on the type you used last, on the element where you are on
the page: the link or heading the caret is on, or else the last one before it.
Headings are read with their level when it changes ("level 2"), as in a tree.

**ALUS+V** switches the **screen layout**. On (the default), a line of a page
is what the screen shows on one line: a label beside its field, two buttons
side by side, a link inside a sentence. Off, every link and every field is a
line of its own. ALUS says "Use screen layout on" or "off" and saves the
choice at once, so it is the same after a restart; the same switch is in the
settings window under Review cursor, Document navigation.

A quick navigation key (`h`, `k`, `f`, `b`…) reads the element it lands on,
not the whole line around it: `b` on the second of two buttons says only that
button. A table, a list, a landmark or an edit field reads its first line.

In an edit field ALUS switches to **focus mode** automatically, so typing
goes to the application; **ALUS+Space** switches between the two modes by
hand. Editable documents (a LibreOffice Writer document, a text editor) are
always in focus mode: the caret belongs to the application and ALUS follows
it.

Table navigation: **Control+Alt+Arrows** move by cell.

## Applications

ALUS has app modules for:

- **GNOME Shell**: the top bar, the overview, notifications.
- **Nautilus** (Files): the file list follows the selection; entering a
  folder and jumping to a file by typing its first letter are spoken.
- **LibreOffice**: Writer documents (paragraphs are read as text, without
  "paragraph" each time), Calc cells.
- **Terminals** (GNOME Terminal, MATE Terminal, Xfce Terminal, Konsole):
  new output is announced.
- **Electron** applications.

Browsers (Firefox, Chromium, Chrome) need no app module.

`alus-cli app-info` shows which module every running application gets.

## Changing keys

Any command can be bound to another key, or unbound:

- in the **gestures dialog**: Settings → Gestures. Find the command, add a
  key by pressing it, or remove one;
- by hand, in `~/.config/alus/gestures.ini`. The format is NVDA's, so a
  `gestures.ini` you used with NVDA works unchanged
  ([configuration.md](configuration.md#gesturesini)).

`alus-cli gesture 'kb:alus+t'` tells you which command a key runs.

## Diagnostics

`alus-cli` answers "why does it not work here" without changing anything.
All of these are safe to run while ALUS or another screen reader is running.

| Command | Answers |
|---|---|
| `alus-cli input-info` | can ALUS get the keyboard, and if not, why |
| `alus-cli gesture <id>` | which command a key runs, e.g. `kb:alus+1` |
| `alus-cli a11y-info` | does the object model work, what it sees |
| `alus-cli a11y-tree [depth] [app]` | the accessible tree, as ALUS reads it |
| `alus-cli text-info` | what the text layer sees and what it costs |
| `alus-cli browse-info` | does browse mode work, what quick navigation costs |
| `alus-cli braille-info` | liblouis, the tables, brltty |
| `alus-cli app-info` | the app module of every running application, installed add-ons |
| `alus-cli gui-info` | can the settings window open, which translations are installed |
| `alus-cli autostart` | what starts with the session, who holds the keyboard |
| `alus-cli speak "text"` | speak a text through ALUS's speech path |
| `alus-cli list-voices`, `list-modules` | what speech-dispatcher offers |
| `alus-cli bench-speech` | measure speech latency |

## Troubleshooting

**ALUS speaks but no key works.** Another screen reader holds the keyboard.
Run `alus-cli input-info`: it says who. Stop the other reader, or use
`tools/alus-switch.sh alus`.

**ALUS says nothing at all.** Check speech-dispatcher: `spd-say hello`. If
that is silent too, the problem is speech-dispatcher or the audio, not ALUS.
`alus-cli speak hello` tests ALUS's own speech path.

**An application is silent.** Its accessibility support may be off. On GNOME
the screen reader switch (`org.gnome.desktop.a11y.applications
screen-reader-enabled`) tells applications to turn it on; some applications
read it only at startup, so restart the application. `alus-cli a11y-tree 3
<application>` shows what ALUS can see in it.

**ALUS stops responding for a moment.** When an application blocks an
accessibility call, ALUS's watchdog notices within half a second, lets the
keys through to the applications so the keyboard keeps working, and takes
them back when it recovers.

**Something is wrong or slow.** Record a debug log while you reproduce it:

```bash
alus-daemon --log-level debug --log-file ~/alus-debug.log
# or, swapping readers automatically and back after 5 minutes:
tools/alus-switch.sh measure 300
```

Slow operations are logged with their cost (`slow event: … took N ms, M
AT-SPI calls`), which is usually enough to find the cause.
