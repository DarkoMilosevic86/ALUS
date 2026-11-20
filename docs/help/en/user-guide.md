# ALUS User Guide

Version 2026.1.0 beta

## Contents

- [Introduction](#introduction)
- [System requirements](#system-requirements)
- [Installing ALUS](#installing-alus)
- [Starting and stopping ALUS](#starting-and-stopping-alus)
- [Getting started](#getting-started)
- [The ALUS menu](#the-alus-menu)
- [Reading with ALUS](#reading-with-alus)
- [Object navigation and the review cursor](#object-navigation-and-the-review-cursor)
- [Web pages and mail: browse mode](#web-pages-and-mail-browse-mode)
- [Typing and editing: focus mode](#typing-and-editing-focus-mode)
- [Applications](#applications)
- [Speech](#speech)
- [Braille](#braille)
- [Settings](#settings)
- [Changing keys](#changing-keys)
- [Language](#language)
- [Troubleshooting](#troubleshooting)
- [Getting help and supporting ALUS](#getting-help-and-supporting-alus)

## Introduction

ALUS (Access Linux Using Speech) is a screen reader for Linux. It reads what
is on the screen with synthetic speech and on a braille display, so that
blind and partially sighted people can use the computer with the keyboard.

ALUS works with the GNOME desktop on Wayland, web browsers (Firefox, Chrome,
Chromium), mail (Thunderbird), LibreOffice, the file manager, terminals and
most GTK, Qt and Electron applications.

If you have used a screen reader on Windows, much of ALUS will be familiar:
the commands, their keys and the concepts -- focus, review cursor, navigator
object, browse mode, focus mode, say all -- follow the conventions blind users
already know. This guide also explains what is particular to Linux.

ALUS is free software, released under the GNU General Public License,
version 2 or later.

## System requirements

- A Linux distribution with the GNOME desktop on Wayland (developed and
  tested on Debian 13 with GNOME 48).
- speech-dispatcher with at least one speech synthesizer: espeak-ng is
  recommended; RHVoice and others also work.
- The accessibility bus (at-spi2-core), which every GNOME installation has.
- For braille: a braille display supported by brltty, and liblouis.

## Installing ALUS

ALUS is built from source and installed with Meson. See `docs/building.md`
and `docs/installing.md` in the source for the list of packages to install
first. In short:

```
meson setup build --prefix=/usr/local --buildtype=release
ninja -C build
sudo meson install -C build
```

Installing changes nothing in your session: ALUS does not start by itself,
and the desktop's own screen reader (Orca) keeps working until you choose
otherwise.

## Starting and stopping ALUS

Only one screen reader can use the keyboard at a time. To try ALUS while
Orca is running, open a terminal and type:

```
alus-switch alus nolog
```

The script stops Orca, starts ALUS and says each step aloud. `nolog` means
nothing is written to a log file. To give the keyboard back to Orca:

```
alus-switch other
```

Important: do **not** turn Orca off with `Alt+Super+S` before you start
ALUS. That shortcut also turns off the desktop's screen reader switch, and
the activities overview (the `Super` key) then announces nothing. The script
stops Orca itself and leaves the switch alone.

To start ALUS every time you log in: `alus-cli autostart --enable`. To stop
that: `alus-cli autostart --disable`.

To quit ALUS, press `ALUS+Q`, or choose Exit in the ALUS menu. ALUS says
"exiting" before it stops. If you started ALUS with `alus-switch`, use
`alus-switch other` instead, so that Orca comes back.

## Getting started

### The ALUS key

ALUS commands are pressed together with the **ALUS key**. By default both
`Insert` and the numpad `Insert` are ALUS keys; `Caps Lock` can be added in
Settings → Keyboard. In this guide the ALUS key is written `ALUS`, so
`ALUS+T` means: hold Insert and press T.

Press the ALUS key twice quickly to send a real Insert to the application.

### Keyboard layouts

ALUS has a **desktop** layout, which uses the numeric keypad, and a
**laptop** layout, which uses the letter keys instead. Choose it in
Settings → Keyboard → Layout. The numpad commands need Num Lock off.

### The first commands to learn

| Keys | What it does |
|---|---|
| `ALUS+A` (laptop: `ALUS+N`) | open the ALUS menu |
| `ALUS+1` | input help: every key says what it does instead of doing it; press again to leave |
| `Control` | stop speech |
| `Shift` | pause speech; press again to resume |
| `ALUS+Tab` | say what has the focus |
| `ALUS+T` | say the window title |
| `ALUS+Up Arrow` (laptop: `ALUS+L`) | read the current line |
| `ALUS+Down Arrow` (laptop: `ALUS+A`) | say all: read from here to the end |
| `ALUS+F12` | the time; press twice for the date |
| `ALUS+Q` | quit ALUS |

Pressing a reporting command twice spells what it said; three times spells
it with character descriptions ("a, Avala").

Commands that are not available on Linux still answer when pressed: they say
why, so a key is never silent.

## The ALUS menu

Press `ALUS+A` (laptop: `ALUS+N`) to open the ALUS menu. Move with the Up
and Down arrows and press Enter to choose. Each item also has a letter that
chooses it at once. `Escape` closes the menu.

| Item | What it does |
|---|---|
| Settings… | opens the settings window |
| Input gestures… | shows every command and its keys, and lets you change them |
| Help | opens the submenu: User guide, Commands quick reference, What's new, About |
| Donate… | how to support ALUS with a donation |
| Save configuration | writes the current settings to disk |
| Revert to saved configuration | undoes the changes made since the last save |
| Reset configuration to factory defaults | returns every setting to its original value |
| Exit | quits ALUS |

In the Help submenu, press the Right arrow or Enter to open it, and the Left
arrow or Escape to go back. The user guide and What's new open in your web
browser. The **Commands quick reference** lists every command with the keys
that run it right now, including the keys you assigned yourself.

## Reading with ALUS

| Keys | What it reads |
|---|---|
| `ALUS+Up Arrow` (laptop: `ALUS+L`) | the current line |
| `ALUS+Down Arrow` (laptop: `ALUS+A`) | say all, from the caret to the end |
| `ALUS+B` | the whole foreground window |
| `ALUS+End` (laptop: `ALUS+Shift+End`) | the status bar |
| `ALUS+Shift+F` | the formatting at the caret |
| `ALUS+C` | the text on the clipboard; twice: spell it |
| `ALUS+Control+X` | copies what ALUS said last to the clipboard |
| `ALUS+S` | speech mode: talk, beeps, off, on demand |
| `ALUS+P` | how much punctuation is spoken: none, some, most, all |
| `ALUS+2` / `ALUS+3` | speak typed characters / words on or off |

During say all, the caret follows the reading. Press `Control` to stop: the
caret stays where the reading stopped.

## Object navigation and the review cursor

Object navigation lets you explore everything on the screen, not only what
has the focus. The object you are on is the **navigator object**.

| Keys (desktop) | Keys (laptop) | What it does |
|---|---|---|
| `ALUS+Numpad 5` | `ALUS+Shift+O` | report the navigator object |
| `ALUS+Numpad 8` | `ALUS+Shift+Up Arrow` | go to the object that contains it |
| `ALUS+Numpad 2` | `ALUS+Shift+Down Arrow` | go to its first child |
| `ALUS+Numpad 4` / `6` | `ALUS+Shift+Left` / `Right Arrow` | previous / next object |
| `ALUS+Numpad Minus` | `ALUS+Backspace` | back to the focus |
| `Numpad Slash` | `ALUS+[` | left click on the navigator object |
| `Numpad Star` | `ALUS+]` | right click (context menu) on the navigator object |

The **review cursor** reads the text of the navigator object without moving
the caret:

| Keys (desktop) | Keys (laptop) | What it does |
|---|---|---|
| `Numpad 7` / `8` / `9` | `ALUS+Up Arrow` / `ALUS+Shift+.` / `ALUS+Down Arrow` | previous / current / next line |
| `Numpad 4` / `5` / `6` | | previous / current / next word |
| `Numpad 1` / `2` / `3` | | previous / current / next character |
| `Numpad Plus` | `ALUS+Shift+A` | say all from the review cursor |

On Wayland, ALUS cannot move the mouse pointer or press a mouse button. The
click keys therefore act directly on the navigator object: left click presses
a button, follows a link or toggles a check box; right click opens the context
menu when the application allows it, and otherwise ALUS suggests
`Shift+F10` or the menu key.

## Web pages and mail: browse mode

On web pages and in mail messages ALUS uses **browse mode**: you read the
document with the arrow keys like a text, and single letters jump to the
next element of a kind.

| Key | Next | Key | Next |
|---|---|---|---|
| `H` | heading | `K` | link |
| `1` to `6` | heading of that level | `F` | form field |
| `T` | table | `L` | list |
| `I` | list item | `B` | button |
| `E` | edit field | `X` | check box |
| `R` | radio button | `C` | combo box |
| `G` | graphic | `D` | landmark |
| `Q` | block quote | `S` | separator |

Add `Shift` to go to the previous one. A jump reads only the element it lands
on; a table, a list or a landmark reads its first line. `ALUS+Shift+Space`
turns the single letter keys off and on, for pages that have their own
shortcuts.

### Elements list

`ALUS+F7` opens the elements list: the links, headings, form fields, buttons
or landmarks of the page. Choose the type with the radio buttons, narrow the
list by typing in the filter field, and press Enter to activate the element,
or Move to to place the cursor on it. Escape closes the list. The list opens
on the element where you are on the page, and headings are shown as a tree
with their levels.

### Tables

`Control+Alt` with the arrow keys moves from cell to cell. ALUS reports the
row and column, and the headers when they change.

### Screen layout

`ALUS+V` switches the screen layout on or off. On (the default), a line is
what the screen shows on one line: a label beside its field, two buttons side
by side, links inside a sentence. Off, every link and every field is a line
of its own. Very long lines are divided at about 100 characters. The choice is
saved at once and kept after a restart.

### Other browse mode keys

| Keys | What it does |
|---|---|
| `Enter` | activate the element at the caret (follow a link, press a button) |
| `ALUS+F5` | refresh the document |
| `ALUS+F2` | pass the next key to the page (for example, `K` to play or pause YouTube) |
| `,` / `Shift+,` | move past the end / to the start of the current container |

## Typing and editing: focus mode

When you enter an edit field, ALUS switches to **focus mode** by itself, so
what you type goes to the application. `ALUS+Space` switches between browse
mode and focus mode by hand.

`Escape` leaves focus mode only when ALUS entered it by itself. Focus mode
you turned on with `ALUS+Space` is turned off with `ALUS+Space` again; there
`Escape` goes to the page, for example to leave full screen on YouTube.

Documents you edit -- in LibreOffice Writer or a text editor -- are always in
focus mode: the caret belongs to the application and ALUS follows it.

## Applications

- **Web browsers** (Firefox, Chrome, Chromium): browse mode, as described
  above.
- **Thunderbird**: messages are read in browse mode; `ALUS+Down Arrow` reads
  a message from the top, and `Escape` closes it.
- **GNOME desktop**: the top bar, the activities overview (`Super`),
  notifications and settings.
- **Files (Nautilus)**: the file list follows the selection; typing the first
  letters of a name jumps to the file.
- **LibreOffice**: Writer documents are read as text, and Calc reports cells.
- **Terminals** (GNOME Terminal, Konsole and others): new output is read as
  it appears.
- **Electron applications** and most GTK and Qt applications.

`ALUS+Shift+S` (laptop: `ALUS+Shift+Z`) puts ALUS to sleep for the current
application, for programs that speak by themselves.

## Speech

ALUS speaks through speech-dispatcher. In Settings → Speech you choose the
output module (espeak-ng, RHVoice and others), a voice of that module, and
the rate, pitch, volume and inflection.

Without opening the settings: `ALUS+Control+Left/Right Arrow` chooses a voice
setting, and `ALUS+Control+Up/Down Arrow` changes it.

ALUS changes the voice's language by itself when a text says which language
it is in (Settings → Speech → Follow the language of the text).

## Braille

ALUS drives braille displays through brltty. Install and set up brltty for
your display, then choose brltty in Settings → Braille → Display, and the
translation tables on the same page. Serbian tables, Cyrillic and Latin, are
part of liblouis.

## Settings

Open the settings from the ALUS menu or with `ALUS+Control+G`. The window is
a separate program and is read like any other application. Changes take
effect when you press OK; Cancel or Escape discards them. A voice can be
tried before you save it.

| Page | What it sets |
|---|---|
| General | the language of ALUS, the log level |
| Speech | synthesizer, voice, rate, pitch, volume, punctuation, language switching |
| Braille | display, translation tables, cursor, word wrap |
| Keyboard | layout, ALUS keys, typing echo, what interrupts speech |
| Object presentation | descriptions, shortcuts, position information, tooltips, notifications |
| Review cursor | what the review cursor follows, document navigation, screen layout |
| Document formatting | which formatting and structure are announced while reading |
| Gestures | every command and its keys |

The settings are stored in `~/.config/alus/alus.ini`.

## Changing keys

Every command can be given another key, or have its key removed, in the
Input gestures dialog (ALUS menu → Input gestures…). Find the command, choose
Add and press the new key. The keys are stored in
`~/.config/alus/gestures.ini`.

## Language

The language of ALUS is independent of the desktop's: you can have an English
desktop and a Serbian screen reader. Choose English, Serbian (Cyrillic) or
Serbian (Latin) in Settings → General → Language. The change takes effect
when you press OK, without restarting ALUS. This guide is available in the
same languages.

## Troubleshooting

**ALUS speaks, but no key works.** Another screen reader has the keyboard.
Run `alus-cli input-info` to see which one, and use `alus-switch alus`.

**ALUS says nothing.** Check speech-dispatcher with `spd-say hello`. If that
is silent too, the problem is in speech-dispatcher or the sound, not in ALUS.

**An application is silent.** Close it and open it again: some applications
turn their accessibility on only when they start. `alus-cli a11y-tree 3
application-name` shows what ALUS can see in it.

**The Super key's overview says nothing.** The desktop's screen reader switch
is off. Start ALUS with `alus-switch alus` while Orca is still running,
without `Alt+Super+S`.

**ALUS stops responding for a moment.** When an application blocks, ALUS's
watchdog notices within half a second, releases the keyboard to the
applications and takes it back when ALUS recovers.

**Reporting a problem.** Record a log while you reproduce it:

```
alus-switch measure 300
```

ALUS runs for five minutes with a detailed log, then Orca comes back by
itself. Send the log (`alus-measure-…log` in your home folder) with a short
description of what you did and what you heard.

## Getting help and supporting ALUS

The project page is https://github.com/DarkoMilosevic86/ALUS, where you can
report problems and follow development.

ALUS is free, and it is made by one person. If it helps you, you can support
its development with a donation through PayPal or Google Pay, both at
daremc86@gmail.com. The ALUS menu → Donate… opens PayPal and copies the
address for Google Pay.

ALUS is copyright (C) 2026 Darko Milošević and is released under the GNU
General Public License, version 2 or later.
