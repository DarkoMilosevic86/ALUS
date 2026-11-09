#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# The phase 9 measurement: the GUI and the localization against this machine.
#
# The suite already asserts the settings model, the gesture dialog's two
# writes, the Serbian catalogue's completeness and the command reference, all
# without a display. What a test cannot answer:
#
#   * whether the settings window OPENS here, and -- the finding this phase
#     turned on -- whether opening it freezes the screen reader. It did, for
#     7.5 seconds, while the window lived in the daemon's own process
#     (claude/FAZA9.md N79). The check below starts a daemon and a window and
#     reads the daemon's own watchdog log, because that is the only place the
#     freeze was ever visible;
#   * whether ALUS can READ its own settings window, which is the other half
#     of the same finding: in process it could not;
#   * whether the translated screen reader actually speaks Serbian, as opposed
#     to having a valid .po file;
#   * what the installation looks like on a machine where another screen reader
#     already starts with the session.
#
# It reads only, with one exception it announces: the window check STARTS a
# daemon with --no-input and --no-speech, so it never takes the keyboard from
# the running screen reader and never speaks over it.
#
# Usage:
#   tools/phase9-gui-check.sh            all the automatic steps
#   tools/phase9-gui-check.sh --manual   also print the manual test plan
set -uo pipefail

CLI=${ALUS_CLI:-./build/src/alus-cli}
DAEMON=${ALUS_DAEMON:-./build/src/alus-daemon}
SETTINGS=${ALUS_SETTINGS:-./build/src/alus-settings}
status=0

say() { printf '\n==> %s\n' "$1"; }
bad() { printf '    FAIL  %s\n' "$1"; status=1; }
ok()  { printf '    ok    %s\n' "$1"; }
note(){ printf '    --    %s\n' "$1"; }

for binary in "$CLI" "$DAEMON"; do
  if [ ! -x "$binary" ]; then
    echo "no $binary -- run: meson compile -C build" >&2
    exit 2
  fi
done

# ---------------------------------------------------------------------------
say "1. Is the GUI built, and can it open here"
# ---------------------------------------------------------------------------
"$CLI" gui-info
if [ ! -x "$SETTINGS" ]; then
  note "alus-settings was not built: libgtk-4-dev and libadwaita-1-dev were"
  note "missing when the build was configured. Everything below that needs a"
  note "window is skipped; the rest of phase 9 does not need one."
  HAVE_WINDOW=no
else
  HAVE_WINDOW=yes
  ok "alus-settings is built"
fi

# ---------------------------------------------------------------------------
say "2. The catalogues"
# ---------------------------------------------------------------------------
for po in po/sr.po 'po/sr@latin.po'; do
  if [ ! -f "$po" ]; then
    bad "$po is missing -- run: python3 tools/import-nvda-po.py"
    continue
  fi
  if msgfmt --check --check-format "$po" -o /dev/null 2>/dev/null; then
    counts=$(msgfmt --statistics "$po" -o /dev/null 2>&1 | tr -d '\n')
    ok "$po valid: $counts"
  else
    bad "$po does not pass msgfmt --check"
    msgfmt --check --check-format "$po" -o /dev/null 2>&1 | sed 's/^/          /'
  fi
done
# Untranslated entries are announcements that come out in English.
for po in po/sr.po 'po/sr@latin.po'; do
  [ -f "$po" ] || continue
  empty=$(msgattrib --untranslated "$po" 2>/dev/null | grep -c '^msgid "' || true)
  if [ "${empty:-0}" -le 1 ]; then
    ok "$po: nothing untranslated"
  else
    bad "$po: $((empty - 1)) untranslated messages -- they will be spoken in English"
  fi
done

# ---------------------------------------------------------------------------
say "3. Does the translated screen reader actually speak Serbian"
# ---------------------------------------------------------------------------
# Not "is the .po valid" but "does a running ALUS produce Serbian", which is
# T9.4. The command table is the largest body of translated text there is.
english=$("$CLI" --language en list-commands 2>/dev/null | head -40)
serbian=$("$CLI" --language sr list-commands 2>/dev/null | head -40)
if [ -z "$serbian" ]; then
  bad "list-commands printed nothing -- is data/scripts.csv there?"
elif [ "$english" = "$serbian" ]; then
  bad "the command table is identical in English and Serbian: no catalogue was loaded"
  note "ALUS_LOCALE_DIR, or build/po, must hold <lang>/LC_MESSAGES/alus.mo"
  note "build it with: meson compile -C build"
else
  # Cyrillic in the output is the proof, and it also proves the charset
  # binding: without bind_textdomain_codeset this would be mojibake.
  if printf '%s' "$serbian" | grep -qP '\p{Cyrillic}'; then
    ok "the command table is in Serbian Cyrillic"
  else
    bad "the Serbian command table has no Cyrillic in it"
  fi
fi
latin=$("$CLI" --language 'sr@latin' list-commands 2>/dev/null | head -40)
if [ -n "$latin" ] && [ "$latin" != "$english" ]; then
  ok "sr@latin is a separate catalogue and loads"
else
  note "sr@latin did not load (only sr is needed for Z8)"
fi

# ---------------------------------------------------------------------------
say "4. The command reference"
# ---------------------------------------------------------------------------
if python3 tools/generate-command-reference.py --check --quiet 2>/dev/null; then
  ok "docs/commands.md matches data/scripts.csv (T9.5)"
else
  bad "docs/commands.md is out of date -- run: python3 tools/generate-command-reference.py"
fi
if python3 tools/update-potfiles.py --check >/dev/null 2>&1; then
  ok "po/POTFILES.in lists every translatable source"
else
  bad "po/POTFILES.in is stale -- run: python3 tools/update-potfiles.py"
  note "a source missing from it is a string no translator ever sees"
fi

# ---------------------------------------------------------------------------
say "5. The installation"
# ---------------------------------------------------------------------------
"$CLI" autostart
if command -v desktop-file-validate >/dev/null 2>&1; then
  found=0
  for entry in build/alus.desktop build/alus-settings.desktop; do
    [ -f "$entry" ] || continue
    found=1
    if desktop-file-validate "$entry" >/dev/null 2>&1; then
      ok "$(basename "$entry") is a valid desktop entry"
    else
      bad "$(basename "$entry") is not valid"
      desktop-file-validate "$entry" 2>&1 | sed 's/^/          /'
    fi
  done
  [ "$found" = 1 ] || note "no generated .desktop files: run meson compile -C build"
else
  note "no desktop-file-validate (desktop-file-utils); entries not checked"
fi
if [ -f build/alus.service ]; then
  if command -v systemd-analyze >/dev/null 2>&1; then
    if systemd-analyze verify build/alus.service >/dev/null 2>&1; then
      ok "alus.service passes systemd-analyze verify"
    else
      note "systemd-analyze verify complains (often only about the unit not"
      note "being installed yet):"
      systemd-analyze verify build/alus.service 2>&1 | sed 's/^/          /' | head -5
    fi
  else
    note "no systemd-analyze; the unit was not checked"
  fi
fi

# ---------------------------------------------------------------------------
say "6. THE FINDING: does opening the window freeze the screen reader"
# ---------------------------------------------------------------------------
# This is the check that exists because of N79. A window in the daemon's own
# process froze the core for 7.5 s and could not be read at all, because ALUS's
# synchronous AT-SPI calls about it were answered from the loop they blocked.
# The cure was to put the window in its own process, and THIS is what proves
# the cure is still in place on this machine.
if [ "$HAVE_WINDOW" != yes ]; then
  note "skipped: no alus-settings"
elif [ -z "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]; then
  note "skipped: no display in this session"
else
  if [ -n "${SKIP_WINDOW_CHECK:-}" ]; then
    note "skipped: SKIP_WINDOW_CHECK is set"
  else
  log=$(mktemp -t alus-phase9-XXXXXX.log)
  cfg=$(mktemp -d -t alus-phase9-cfg-XXXXXX)
  note "starting a daemon for 14 s with --no-input --no-speech (it will not"
  note "take the keyboard and will not speak over the running screen reader)"
  (
    ALUS_CONFIG_DIR="$cfg" "$DAEMON" --no-speech --no-input --no-braille \
      --exit-after 14 --log-level info >"$log" 2>&1 &
  )
  sleep 3
  if ! grep -q "ALUS .* starting" "$log"; then
    note "the daemon did not start (another instance may own the bus name);"
    note "the freeze check needs it, so it is skipped"
    sed 's/^/          /' "$log" | head -3
  else
    ALUS_CONFIG_DIR="$cfg" "$SETTINGS" --page braille >/dev/null 2>&1 &
    settings_pid=$!
    sleep 6

    if grep -q "core stuck" "$log"; then
      bad "the core STALLED while the settings window opened -- N79 is back"
      grep -E "core stuck|recovered" "$log" | sed 's/^/          /'
    else
      ok "the core did not stall while the settings window opened"
    fi
    if grep -q "get_role failed" "$log"; then
      bad "AT-SPI calls about the settings window timed out -- N79 is back"
      grep "get_role failed" "$log" | head -2 | sed 's/^/          /'
    else
      ok "no AT-SPI call about the window timed out"
    fi
    # And the other half: ALUS has to be able to READ it.
    if "$CLI" a11y-tree 1 2>/dev/null | grep -q "alus-settings"; then
      ok "ALUS sees the settings window as an application of its own"
    else
      bad "ALUS does not see the settings window in the accessible tree"
    fi
    kill "$settings_pid" 2>/dev/null
  fi
  wait 2>/dev/null
  rm -rf "$cfg"
  note "daemon log: $log"
  fi
fi

# ---------------------------------------------------------------------------
if [ "${1:-}" = "--manual" ]; then
cat <<'PLAN'

==> 7. Manual test plan — the only thing a machine cannot measure
    (what the user HEARS and what the window LOOKS like)

Before: stop the other screen reader, or ALUS gets no keys at all.
    gsettings set org.gnome.desktop.a11y.applications screen-reader-enabled false
    ./build/src/alus-cli input-info          # must say the keyboard is available
    ./build/src/alus-daemon --log-level info --log-file ~/alus.log &

T9.1 — the shortcuts open the right page. Each one must open the window with
       THAT page showing, and must not freeze the speech while it opens:
    alus+control+g    General
    alus+control+v    Speech            (and alus+control+s, the synthesizer)
    alus+control+a    Braille
    alus+control+k    Keyboard
    alus+control+o    Object presentation
    alus+control+d    Document formatting
    alus+control+m    must SAY "there are no such settings in this version"
                      (ALUS has no mouse settings: 00-VIZIJA.md §5)

T9.2 — the gesture dialog writes the file:
    On the Gestures page, press Open.
    Choose a command, press Add, type  kb:alus+shift+y , press Add.
      -> it says the gesture is bound
      -> grep -A2 'GlobalCommands' ~/.config/alus/gestures.ini
      -> press alus+shift+y: the command runs, WITHOUT restarting ALUS
    Select that gesture, press Remove.
      -> the line is gone from the file
    Select a BUILT-IN gesture of the same command and press Remove.
      -> it says the default is kept for everything else
      -> the file now holds a `None = ...` line
      -> restart ALUS: the key is still unbound (this is the part that only a
         restart can prove)

T9.3 — a command with no description is not listed:
    In the dialog, search for `tab`. The browse-mode `tab` script has no
    description upstream and must NOT appear as its own row.

T9.4 — Serbian:
    pkill alus-daemon
    LANGUAGE=sr ./build/src/alus-daemon --log-level info &
    alus+1                 -> "улазна помоћ укључена..." in Serbian
    tab through a dialog   -> roles and states in Serbian ("дугме", "изборно поље")
    alus+control+g         -> the window itself is in Serbian
    numpad2 twice          -> the character description from data/characters/sr.dic
    With a braille display: the abbreviations are the Serbian ones (dgm, not btn)

    LANGUAGE=sr@latin ./build/src/alus-daemon &
                           -> the same text in Latin

T9.5 — the reference:
    less docs/commands.md
    Spot-check five rows against what alus+1 says for the same keys.

Reading the settings window ITSELF — the finding this phase turned on:
    With ALUS running, open the settings window and navigate it with ALUS:
    tab through the rows, read them with alus+tab, change a switch with space.
    Every one of those must speak. While the window lived in the daemon's own
    process NONE of it did, and the screen reader froze for 7.5 s on opening
    (N79). If any of it goes silent again, run step 6 above.

Installation:
    meson install -C build --destdir /tmp/stage
    ls /tmp/stage/usr/local/share/applications/alus*.desktop
    ls /tmp/stage/usr/local/lib/systemd/user/alus.service
    ls /tmp/stage/usr/local/share/locale/*/LC_MESSAGES/alus.mo
    ./build/src/alus-cli autostart            # what starts with the session
PLAN
fi

printf '\n'
if [ "$status" = 0 ]; then
  echo "phase 9: every automatic check passed"
else
  echo "phase 9: SOMETHING FAILED above"
fi
exit "$status"
