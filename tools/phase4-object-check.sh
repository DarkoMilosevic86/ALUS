#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# The phase 4 measurement: the object model against the real desktop.
#
# The unit tests run against a mock platform (tests/a11y/mock_backend.hpp),
# which is the only way to measure cache hits and to kill an object at an exact
# moment. What a mock cannot tell is whether the REAL toolkits say what the
# mapping tables assume, whether events actually arrive, and what it all costs.
# That is this script, and it is the phase 4 equivalent of
# tools/phase3-input-check.sh.
#
# It READS ONLY. No window is activated, no action performed, no key taken --
# so it is safe to run while Orca or ALUS itself is running.
#
# Usage:
#   tools/phase4-object-check.sh              all the automatic steps
#   tools/phase4-object-check.sh --manual     also print the manual test plan
set -uo pipefail

CLI=${ALUS_CLI:-./build/src/alus-cli}
DAEMON=${ALUS_DAEMON:-./build/src/alus-daemon}
LIVE_TEST=${ALUS_LIVE_TEST:-./build/tests/test-a11y-live}
status=0

say() { printf '\n== %s ==\n' "$1"; }
ok()   { printf '   OK   %s\n' "$1"; }
bad()  { printf '   FAIL %s\n' "$1"; status=1; }
note() { printf '        %s\n' "$1"; }

for binary in "$CLI" "$DAEMON" "$LIVE_TEST"; do
  if [[ ! -x "$binary" ]]; then
    echo "$binary is missing; build first: meson compile -C build" >&2
    exit 2
  fi
done

say "Step 1 — the accessibility bus"
# Without this the object model cannot start, and the reason is a setting
# rather than a defect: GNOME only starts at-spi2-registryd when something
# asks for accessibility.
bus="unix:path=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/at-spi/bus"
if gdbus call --address "$bus" --dest org.a11y.atspi.Registry \
     --object-path /org/a11y/atspi/registry \
     --method org.freedesktop.DBus.Introspectable.Introspect >/dev/null 2>&1; then
  ok "at-spi2-registryd answers on $bus"
else
  bad "no accessibility bus at $bus"
  note "gsettings set org.gnome.desktop.interface toolkit-accessibility true"
fi
note "toolkit-accessibility = $(gsettings get org.gnome.desktop.interface toolkit-accessibility 2>/dev/null || echo '?')"

say "Step 2 — the registered event list (§6.1)"
# What ALUS and everything else on this desktop have subscribed to. ALUS's own
# 38 types should appear once it is running.
registered=$(gdbus call --address "$bus" --dest org.a11y.atspi.Registry \
  --object-path /org/a11y/atspi/registry \
  --method org.a11y.atspi.Registry.GetRegisteredEvents 2>/dev/null | tr ',' '\n' | grep -c "object:\|window:\|document:\|mouse:" || true)
note "$registered event registrations on the bus right now"

say "Step 3 — what ALUS sees"
if "$CLI" a11y-info; then
  ok "the object model works"
else
  bad "the object model is not available"
fi

say "Step 4 — the tree, two levels deep"
"$CLI" a11y-tree 2 || bad "could not read the tree"

say "Step 5 — the live measurements"
# The cache (T4.2 against a real toolkit), the cost of a tree walk, and -- the
# one that matters most -- whether events arrive at all. Ten seconds, so that
# an idle desktop still has a chance to say something.
ALUS_LIVE_EVENT_MS=${ALUS_LIVE_EVENT_MS:-10000} "$LIVE_TEST" 2>&1 |
  grep -E "AT-SPI|properties|objects four|events dispatched|toolkit|unknown role|assertions" || true

say "Step 6 — the daemon brings everything up"
# --no-input so that this never competes with the running screen reader for the
# compositor's keyboard (N8), and --no-speech so that it does not talk over it.
config=$(mktemp -d)
if ALUS_CONFIG_DIR="$config" timeout 20 "$DAEMON" --allow-multiple --no-speech --no-input \
     --exit-after 3 --log-level info 2>&1 | grep -E "object model|AT-SPI|pump cycles"; then
  ok "the daemon started the object model and shut down cleanly"
else
  bad "the daemon could not start the object model"
fi
rm -rf "$config"

if [[ "${1:-}" == "--manual" ]]; then
cat <<'MANUAL'

== The manual test (02-FAZE.md, Faza 4) ==

This part needs a human with speech on, because what is being tested is what a
user HEARS. Stop Orca first (it holds the compositor's keyboard name, N8):

    gsettings set org.gnome.desktop.a11y.applications screen-reader-enabled false
    pkill -f orca
    ./build/src/alus-daemon --log-level debug 2>&1 | tee /tmp/alus-phase4.log

Then, with the ALUS key being insert (or caps lock):

  1. alt+tab through the applications
     EXPECT: each window announces its name and type.
  2. tab through GNOME Settings
     EXPECT: every control says its name, its role and its state --
             "Dark mode, check box, not checked".
  3. alus+tab
     EXPECT: the focused object, again. Press twice: it is spelled.
  4. alus+numpad4 / numpad6 / numpad8 / numpad2
     EXPECT: object navigation: previous, next, out of, into. At the edges a
             spoken "No next" / "No previous" / "No objects inside" /
             "No containing object" -- never silence.
  5. alus+numpad5
     EXPECT: the navigator object, without moving the focus.
  6. alus+numpadEnter
     EXPECT: the navigator object is activated (a button is pressed), and the
             toolkit's own word for the action is said.
  7. alus+numpadMinus / alus+shift+numpadMinus
     EXPECT: the navigator follows the focus / the focus follows the navigator.
  8. alus+t
     EXPECT: the title of the active window.
  9. Type in a text field with character echo on (alus+2 to cycle it)
     EXPECT: characters are echoed in an edit field and NOT in a password
             field -- that is isFocusEditable and isTypingProtected, the two
             answers phase 3 was waiting for.

What to look for in the log afterwards:

    grep -c "gainFocus" /tmp/alus-phase4.log       # focus events dispatched
    grep "event budget" /tmp/alus-phase4.log       # the limiter capping a flood
    grep -i "error\|warning" /tmp/alus-phase4.log  # anything unexpected
MANUAL
fi

say "Result"
if [[ $status -eq 0 ]]; then
  echo "every automatic step passed"
else
  echo "something failed; see above"
fi
exit $status
