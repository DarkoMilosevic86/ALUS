# ALUS — Access Linux Using Speech

ALUS is a screen reader for Linux, written in C++20. It is designed for
blind and partially sighted people who know NVDA from Windows: it uses the
same commands, the same desktop and laptop keyboard layouts and the same
`gestures.ini` format, so moving to Linux does not mean learning a screen
reader again.

ALUS speaks through **speech-dispatcher**, so it works with the voices you
already have (espeak-ng, RHVoice, Piper and any other speech-dispatcher
module). It reads the desktop through **AT-SPI**, gets the keyboard through
the Wayland accessibility keyboard monitor, and drives braille displays
through **brltty** and **liblouis**.

> **Status: beta (2026.1.0).** ALUS is in daily use on GNOME 48 / Wayland
> (Debian 13). The desktop, Nautilus, LibreOffice Writer, Firefox, Chromium
> and terminals work. Expect rough edges; please report them (see
> [Reporting a problem](#reporting-a-problem)). Versions are numbered
> year.release.patch: 2026.1.0 is the first release of 2026.

## Features

- **The NVDA command set**: 316 commands in 17 categories, 178 of them bound
  to keys by default, with desktop and laptop layouts. The ALUS key is Insert, numpad Insert and/or
  Caps Lock. See the [command reference](docs/commands.md).
- **Fast**: under 50 ms from key press to first sound is a tested
  requirement. Properties are cached per cycle and event floods are limited,
  the same way NVDA does it.
- **Speech**: speech-dispatcher with any output module, voice, rate, pitch,
  volume and inflection; symbol levels; pronunciation dictionaries; automatic
  language switching; say all that moves the caret as it reads.
- **Browse mode** for web pages and documents: single-letter quick navigation
  (`h`, `k`, `t`, `f`, `1`–`9` and the rest), the elements list, tables,
  focus mode and pass-through.
- **Object navigation and the review cursor** on the numpad, with object,
  document and screen review modes.
- **Braille**: any display brltty supports, liblouis translation including
  Serbian Cyrillic tables, cursor routing, braille keyboard input.
- **App modules** for LibreOffice, GNOME Shell, Nautilus, terminals and
  Electron applications.
- **Add-ons** through a stable C ABI (`alus/alus-plugin.h`).
- **A settings window** (`alus-settings`, GTK 4) with eight pages and a
  gestures dialog for rebinding any command.
- **Localisation**: English and Serbian (Cyrillic and Latin), 100%
  translated. The screen reader's language is independent of the desktop's.

## Quick start

On Debian 13 (other distributions: see [Building](docs/building.md)):

```bash
sudo apt install build-essential meson ninja-build pkg-config gettext \
    libglib2.0-dev libatspi2.0-dev libspeechd-dev libxkbcommon-dev \
    liblouis-dev libpulse-dev libicu-dev libbrlapi-dev \
    libgtk-4-dev libadwaita-1-dev \
    speech-dispatcher espeak-ng

git clone https://github.com/DarkoMilosevic86/ALUS.git alus && cd alus
meson setup build
meson compile -C build
meson test -C build            # optional: the whole test suite
```

Only one screen reader can hold the keyboard. To try ALUS from the source
tree while another screen reader is running, swap them with the helper
script. It speaks every step through `spd-say`, and if ALUS fails to start
it brings the other reader back:

```bash
tools/alus-switch.sh alus      # stop the other reader, start ALUS
tools/alus-switch.sh other     # give the keyboard back
```

Or start ALUS directly, from the repository root, once the other reader is
stopped:

```bash
./build/src/alus-daemon
```

Press **ALUS+Q** to quit, **ALUS+1** for input help (every key says what it
does without doing it), and **ALUS+Control+G** to open the settings.

To install ALUS system-wide and start it with your session, see
[Installing](docs/installing.md).

## Documentation

| Document | For |
|---|---|
| [User guide](docs/user-guide.md) | running ALUS, the most important keys, settings, languages, troubleshooting |
| [Help for users](docs/help/) | the guide and What's new that the ALUS menu opens, in English, Serbian Cyrillic and Latin |
| [Command reference](docs/commands.md) | every command with its desktop and laptop keys (generated) |
| [Configuration](docs/configuration.md) | `alus.ini`, `gestures.ini`, profiles, dictionaries, environment variables |
| [Building](docs/building.md) | dependencies per distribution, build options, running from the source tree |
| [Installing](docs/installing.md) | `meson install`, autostart, the systemd unit, uninstalling |
| [Development](docs/development.md) | source layout, tests, diagnostics, regenerating data, translations, conventions |
| [Writing add-ons](docs/add-ons.md) | the C plugin ABI |
| [Design](docs/design/vision.md) | vision, [architecture](docs/design/architecture.md), [risks](docs/design/risks.md) |
| [History](docs/history/README.md) | phase reports and the findings (N1–N153) that shaped the code |

## Requirements

- Linux with **GNOME 48 or later** (Mutter) or **KDE Plasma 6.3.6 or later**
  (KWin) on Wayland. These provide `org.freedesktop.a11y.KeyboardMonitor`,
  which ALUS needs for the keyboard. Compositors without it (sway and other
  wlroots compositors) are not supported yet.
- AT-SPI 2.46 or later, speech-dispatcher 0.11 or later.
- A C++20 compiler with `<format>` (tested with GCC 14) and Meson 1.3+ to
  build.

## Reporting a problem

Start ALUS with a debug log, reproduce the problem, and attach the log:

```bash
./build/src/alus-daemon --log-level debug --log-file ~/alus-debug.log
```

`tools/alus-switch.sh measure 300` does the same for five minutes and then
brings the other screen reader back by itself. `alus-cli input-info`,
`alus-cli a11y-info` and the other diagnostics in the
[user guide](docs/user-guide.md#diagnostics) explain most "it says nothing"
problems.

## Supporting ALUS

ALUS is free, and it is made by one person. If it helps you, you can support
its development with a donation:

- **PayPal**: daremc86@gmail.com
  ([donate](https://www.paypal.com/donate/?business=daremc86%40gmail.com&no_recurring=0&currency_code=EUR))
- **Google Pay**: daremc86@gmail.com

The same is in ALUS itself: ALUS menu (`ALUS+A`) → Donate….

## Licence

ALUS is free software under the **GNU General Public License, version 2 or
later** ([LICENSE](LICENSE)). It takes its command set, symbol and character
dictionaries, command descriptions and shared translations from NVDA, which
is also GPL-2.0-or-later; the C++ implementation is new. See
[vision.md §6](docs/design/vision.md#6-licence).
