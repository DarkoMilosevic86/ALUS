# Building ALUS

ALUS is built with [Meson](https://mesonbuild.com) and Ninja. The build is
strict: warnings are errors (`werror=true`, `warning_level=3`, plus
`-Wconversion`, `-Wshadow` and friends), so a dependency version that
produces new warnings shows up immediately.

## Dependencies

### Required

| What | Why | pkg-config name |
|---|---|---|
| C++20 compiler with `<format>`, `<ranges>`, `<coroutine>` | the code base (tested with GCC 14.2) | — |
| A C11 compiler | compiles the test add-ons against the C plugin header | — |
| Meson ≥ 1.3, Ninja | the build | — |
| GLib, GObject, GIO ≥ 2.74 | the main loop, D-Bus, `GKeyFile` | `glib-2.0`, `gobject-2.0`, `gio-2.0` |
| libatspi ≥ 2.46 | the accessibility tree and events | `atspi-2` |
| libspeechd ≥ 0.11 | speech through speech-dispatcher | `speech-dispatcher` |
| libxkbcommon ≥ 1.0 | key names and keysym ↔ character conversion | `xkbcommon` |
| liblouis ≥ 3.0 | braille translation | `liblouis` |
| gettext | translations (`msgfmt`, `xgettext`) | — |

### Optional (the build succeeds without them and says what is missing)

| What | Without it | pkg-config name |
|---|---|---|
| GTK ≥ 4.10 and libadwaita ≥ 1.4 | no settings window; configure through `~/.config/alus/alus.ini` | `gtk4`, `libadwaita-1` |
| ICU ≥ 70 | word and sentence segmentation falls back to a simpler algorithm: Serbian and English are fine, Japanese and Thai are not | `icu-uc` |
| libpulse-simple | no tones or sounds of ALUS's own; speech still works | `libpulse-simple` |
| libbrlapi | braille is translated but no display can be driven | (header `brlapi.h` + `libbrlapi`) |
| Catch2 3.x | Meson downloads it through `subprojects/catch2.wrap` for the tests | `catch2-with-main` |

### At run time

- `speech-dispatcher` with at least one output module (`espeak-ng` is the
  best measured; ALUS logs a warning for modules whose latency it has not
  measured),
- the AT-SPI bus (`at-spi2-core`), which every GNOME and KDE session runs,
- `brltty` if you use a braille display,
- `python3` only for the generator scripts in `tools/`.

### Package names

**Debian 13 / Ubuntu 25.04 or later** (the tested platform):

```bash
sudo apt install build-essential meson ninja-build pkg-config gettext \
    libglib2.0-dev libatspi2.0-dev libspeechd-dev libxkbcommon-dev \
    liblouis-dev libpulse-dev libicu-dev libbrlapi-dev \
    libgtk-4-dev libadwaita-1-dev \
    speech-dispatcher espeak-ng
```

**Fedora** (not tested yet):

```bash
sudo dnf install gcc-c++ meson ninja-build pkgconf-pkg-config gettext \
    glib2-devel at-spi2-core-devel speech-dispatcher-devel \
    libxkbcommon-devel liblouis-devel pulseaudio-libs-devel libicu-devel \
    brlapi-devel gtk4-devel libadwaita-devel \
    speech-dispatcher espeak-ng
```

**Arch Linux** (not tested yet):

```bash
sudo pacman -S --needed base-devel meson ninja pkgconf gettext \
    glib2 at-spi2-core speech-dispatcher libxkbcommon liblouis libpulse \
    icu brltty gtk4 libadwaita espeak-ng
```

If a package name is wrong on your distribution, look for the pkg-config
name from the tables above: `pkg-config --modversion atspi-2` and so on.

## Configure and compile

```bash
meson setup build
meson compile -C build
```

`meson setup` prints a summary and a warning for every optional dependency it
did not find. The three programs end up in `build/src/`:

| Program | What it is |
|---|---|
| `alus-daemon` | the screen reader |
| `alus-settings` | the settings window and gestures dialog (a separate process; see [architecture §1](design/architecture.md#1-technology-stack)) |
| `alus-elements` | the browse mode elements list (ALUS+F7), started by the daemon with the page's elements; also a separate process |
| `alus-clipboard` | reads and sets the system clipboard for ALUS+C and ALUS+Control+X, through Xwayland; started by the daemon |
| `alus-cli` | diagnostics and one-shot tools: speak a text, list voices, inspect the accessible tree, check the keyboard, enable autostart |

### Build types and options

ALUS uses Meson's standard options; it has no options of its own.

```bash
meson setup build --buildtype=release          # optimised, assertions off
meson setup build --buildtype=debugoptimized   # optimised with debug info
meson setup build --prefix=/usr                # install prefix (default /usr/local)
meson configure build                          # show every option of an existing build
meson setup --wipe build                       # reconfigure from scratch
                                               # (after installing a new optional dependency)
```

Sanitizers:

```bash
meson setup build-asan -Db_sanitize=address,undefined
meson setup build-tsan -Db_sanitize=thread
```

## Running from the build tree

Run the programs **from the repository root**. ALUS looks for its data
(command table, symbol dictionaries, gesture maps) in this order:
`$ALUS_DATA_DIR`, the installed data directory, then `data/` in the current
directory, so the source tree works without installing anything. The
translations are found in `build/po` automatically.

```bash
./build/src/alus-cli input-info     # can ALUS get the keyboard here?
./build/src/alus-daemon             # start the screen reader
./build/src/alus-settings           # the settings window (or ALUS+Control+G)
```

Only one screen reader can hold the keyboard, so stop the other one first,
or use `tools/alus-switch.sh` (see the [user guide](user-guide.md#starting-and-stopping)).

## Tests

```bash
meson test -C build                       # everything except the live suites
meson test -C build --suite speech        # one suite
meson test -C build --print-errorlogs     # show the output of failing tests
meson test -C build --suite live          # against the real session: speaks out loud
meson test -C build --setup=valgrind --suite core   # under Memcheck
```

The suites, what they need and how to read the results are described in
[development.md](development.md#tests).

## Installing

See [installing.md](installing.md).
