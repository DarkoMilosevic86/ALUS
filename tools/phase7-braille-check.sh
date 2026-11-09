#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# The phase 7 measurement: braille against the real liblouis and the real
# brltty.
#
# The unit tests already run against a REAL liblouis (that is the whole
# argument of tests/braille/support.hpp), so unlike phases 4 to 6 the library
# is not what this script is for. What a test cannot measure is the machine:
#
#   * which liblouis is installed and whether its table directory holds the
#     tables ALUS names -- `sr-Cyrl.ctb` above all, which is T7.1;
#   * whether the round trip holds on THIS machine's tables: text -> cells ->
#     text, which is the one check that proves the whole path at once;
#   * whether brltty is running and what size display it offers -- and,
#     without hardware, whether a virtual display can stand in;
#   * whether the typeform buffer is written past its end, which only a
#     memory checker sees (see FAZA7.md N62).
#
# It READS ONLY. `braille-info` opens the BrlAPI connection, asks the size and
# closes it, so it is safe to run while ALUS itself is running.
#
# Usage:
#   tools/phase7-braille-check.sh            all the automatic steps
#   tools/phase7-braille-check.sh --manual   also print the manual test plan
set -uo pipefail

CLI=${ALUS_CLI:-./build/src/alus-cli}
status=0

say() { printf '\n== %s ==\n' "$1"; }
ok()   { printf '   OK   %s\n' "$1"; }
bad()  { printf '   FAIL %s\n' "$1"; status=1; }
warn() { printf '   WARN %s\n' "$1"; }
note() { printf '        %s\n' "$1"; }

if [[ ! -x "$CLI" ]]; then
  echo "$CLI is missing; build first: meson compile -C build" >&2
  exit 2
fi

say "Step 1 — liblouis, the tables, the round trip, and brltty (T7.1)"
# `braille-info` is the measurement; this step only decides whether what it
# printed is a pass. A missing sr-Cyrl.ctb is a FAIL (T7.1 cannot be met on
# this machine); a missing brltty is a WARN (translation still works, which is
# the whole point of the display being optional -- meson.build:87).
info=$("$CLI" braille-info 2>&1)
printf '%s\n' "$info" | sed 's/^/        /'
if printf '%s\n' "$info" | grep -q 'sr-Cyrl.ctb'; then
  if printf '%s\n' "$info" | grep -q 'round trip exact'; then
    ok "liblouis translates and back-translates exactly"
  else
    bad "the round trip is NOT exact -- the table list or the masking is wrong"
  fi
else
  bad "sr-Cyrl.ctb did not translate: T7.1 cannot be measured on this machine"
fi
if printf '%s\n' "$info" | grep -q 'brltty:     NOT AVAILABLE'; then
  warn "no brltty: the display half of the manual test needs one (see --manual)"
else
  ok "brltty answered; the display size is above"
fi

say "Step 2 — are the tables ALUS names actually installed?"
# A table ALUS offers in its settings and that liblouis cannot find is a
# setting that silently falls back. Each name here is one line of
# TableRegistry::addKnownTables().
tables_dir=$(printf '%s\n' "$info" | awk '/^tables:/ { print $2 }')
if [[ -z "$tables_dir" || ! -d "$tables_dir" ]]; then
  bad "no table directory: braille-info said '$tables_dir'"
else
  missing=()
  for table in sr-Cyrl.ctb sr-g1.ctb en-ueb-g1.ctb en-ueb-g2.ctb en-us-g1.ctb \
               en-us-g2.ctb en-us-comp8.ctb hr-comp8.utb de-g1.ctb \
               fr-bfu-comp8.utb ru.ctb unicode-braille.utb; do
    [[ -f "$tables_dir/$table" ]] || missing+=("$table")
  done
  if [[ ${#missing[@]} -eq 0 ]]; then
    ok "all 12 named tables are in $tables_dir"
  else
    warn "not installed: ${missing[*]} (those settings would fall back)"
  fi
fi

say "Step 3 — the exit criteria themselves (T7.1 … T7.8)"
if meson test -C build --suite braille >/dev/null 2>&1; then
  ok "meson test -C build --suite braille"
else
  bad "meson test -C build --suite braille"
fi

say "Step 4 — the typeform buffer, which only a memory checker sees (N62)"
# liblouis writes the OUTPUT typeform back into the array it was given, one
# entry per CELL. A buffer sized at the input length is a heap overflow on
# every text that produces more cells than characters -- and the suite passes
# anyway. So the memory checker is part of the criterion, not an extra.
if ! command -v valgrind >/dev/null 2>&1; then
  warn "no valgrind: install it, or build with -Db_sanitize=address, and re-run"
elif valgrind --error-exitcode=9 --leak-check=full \
              --show-leak-kinds=definite,indirect \
              --suppressions=tests/valgrind.supp --quiet \
              ./build/tests/test-braille >/dev/null 2>&1; then
  ok "valgrind: 0 errors, 0 definite leaks"
else
  bad "valgrind found something -- read it: valgrind ./build/tests/test-braille"
fi

if [[ "${1:-}" == "--manual" ]]; then
  cat <<'PLAN'

== The manual test — the only thing a machine cannot measure ==

What the machine has measured: liblouis translates, the typeform survives, the
cells are masked, the four wrap modes cut where they should, a hundred frames
are one write, fifty caret moves are one update, the window holds still, and
nothing leaks. What it CANNOT measure is WHAT IS UNDER THE FINGERS.

--- Without hardware: a virtual display ---

  brltty -b no -d /dev/null -x no -n &    # a display that exists and shows nothing
  ./build/src/alus-daemon &
  ./build/src/alus-cli braille-info       # must now print a size, not NOT AVAILABLE

  A virtual display measures the PROTOCOL, not the reading: the frames go to
  brltty and the keys come back, which is every path except the dots on the
  fingers.

--- With a display: reading ---

  Focus a text editor and type a line, then walk it.
  EXPECT: the line under the fingers, the cursor on the caret's cell, and the
  window NOT jumping back to the start of the control on every keystroke
  (T7.8 is the mechanism; the fingers are the test).

  Focus a check box, a list and a table in turn.
  EXPECT the abbreviations, not the words: "chk", "lst5", "r2 c3". A role
  spelled out in full is a bug in braille/labels.cpp, not a style choice --
  it costs cells the display does not have.

--- With a display: the keys ---

  scroll back / forward (fwinlt / fwinrt)
                  EXPECT the window to move by its own width minus the
                  overlap, and the continuation mark (dots 7-8) at the cut.
  cursor routing  press a routing key over a cell in the middle of a word.
                  EXPECT the application's caret to land on THAT character --
                  not one before, not one after. This is the measurement that
                  the position maps are right (O6).
  alus+control+t  tethering: focus -> review -> auto -> focus. In review the
                  display follows the review cursor and stops following the
                  caret; the tether must survive a focus change.
  bk:dot7         erase. With cells pending it eats a cell; with none, a
                  backspace reaches the application.
  bk:dot8         enter.
  bk:dot7+dot8    translate what is pending now, mid-word.

  WITH `braille.handleBrailleInput` OFF (the default) the dots never reach
  ALUS at all: brltty types them through its own uinput device. That is the
  supported path on Wayland. Turning it ON is what the three keys above are
  for, and typing then reports honestly that it cannot inject a keystroke
  (N18) rather than swallowing the text.

--- Then the log, which says what happened rather than what you think did ---

    grep -E 'braille'         ~/alus.log   # the display, its size, its driver
    grep -E 'liblouis'        ~/alus.log   # a table that failed to compile
    grep -E 'braille table'   ~/alus.log   # a table name that resolved nowhere
    grep -E 'cannot type'     ~/alus.log   # N18, said once per session

PLAN
fi

printf '\n'
if [[ $status -eq 0 ]]; then
  echo "phase 7: the automatic steps passed."
else
  echo "phase 7: something above FAILED -- read it, do not re-run it."
fi
exit $status
