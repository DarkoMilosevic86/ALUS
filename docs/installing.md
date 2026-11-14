# Installing ALUS

## Install

After [building](building.md):

```bash
meson setup build --prefix=/usr --buildtype=release   # or keep the default /usr/local
meson compile -C build
sudo meson install -C build
```

To see what would be installed, or to stage an install for a package:

```bash
meson install -C build --destdir /tmp/alus-stage
find /tmp/alus-stage -type f
```

### What is installed

| Path (under the prefix) | What |
|---|---|
| `bin/alus-daemon`, `bin/alus-cli`, `bin/alus-settings`, `bin/alus-elements` | the programs |
| `share/alus/` | the command table (`scripts.csv`), the built-in gesture map, key labels, the command status list, symbol and character dictionaries, per-language data in `locale/` |
| `share/locale/*/LC_MESSAGES/alus.mo` | translations |
| `share/applications/alus.desktop` | the autostart entry (**not enabled** by installing) |
| `share/applications/alus-settings.desktop` | the settings window in the application menu |
| `lib/systemd/user/alus.service` | a systemd user unit, for sessions without XDG autostart (**not enabled**) |
| `include/alus/alus-plugin.h` | the add-on ABI header, the only header ALUS installs |

Nothing outside the prefix is touched, and **nothing is enabled**: installing
ALUS does not start it with your session and does not stop any other screen
reader. Which screen reader starts is your decision.

## Starting ALUS with the session

```bash
alus-cli autostart              # what starts now, and who holds the keyboard
alus-cli autostart --enable     # start ALUS with the session
alus-cli autostart --disable    # stop starting it
```

`--enable` needs an installed ALUS (it copies the installed `alus.desktop`
into `~/.config/autostart/`, so the path in it is the one you installed). The entry
starts ALUS when the desktop's screen reader switch is on
(`org.gnome.desktop.a11y.applications screen-reader-enabled`), which is also
what tells applications to turn their accessibility support on.

If the desktop's own screen reader also starts from `/etc/xdg/autostart`, the
two would fight over the keyboard. `alus-cli autostart --enable` tells you,
and prints the one-line command that hides the other reader's entry **for
your user only** (an XDG `Hidden=true` override in `~/.config/autostart`). It
never does it silently.

For a session without XDG autostart (sway, a plain login shell) use the
systemd unit instead:

```bash
systemctl --user enable --now alus.service
```

## Swapping screen readers by hand

`tools/alus-switch.sh` (in the source tree) swaps the running screen reader
in either direction. It speaks every outcome through `spd-say`, which works
whichever reader is up, and if a start fails it rolls back to the reader that
was working:

```bash
tools/alus-switch.sh alus          # ALUS takes the keyboard
tools/alus-switch.sh other         # give it back to the other reader
tools/alus-switch.sh status        # who has it now
tools/alus-switch.sh measure 300   # ALUS for 300 s with a debug log, then the
                                   # other reader comes back by itself
```

It uses the installed `alus-daemon` if there is one, otherwise
`build/src/alus-daemon`.

## Updating

```bash
git pull
meson compile -C build
sudo meson install -C build
```

Then restart ALUS (`ALUS+Q` and start it again, or log out and in). Your
configuration in `~/.config/alus/` is kept.

## Uninstalling

```bash
alus-cli autostart --disable
systemctl --user disable alus.service 2>/dev/null
sudo ninja -C build uninstall
```

`ninja uninstall` removes exactly the files `meson install` recorded. Your
own configuration stays in `~/.config/alus/` and add-ons in
`~/.local/share/alus/`; delete those directories by hand if you want them
gone too.
