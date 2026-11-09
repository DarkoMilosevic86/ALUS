#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# Swap the running screen reader: ALUS <-> the desktop's other one.
#
# WHY THIS EXISTS. Only one screen reader can take the keyboard: GNOME's
# accessibility manager delivers key events to exactly one bus name, and both
# readers want it. So using ALUS while another reader is installed is always a
# swap, in both directions, and a swap has a failure mode that matters more
# than any other in this repository: IF IT BREAKS HALFWAY, THE USER IS LEFT
# WITH A SILENT COMPUTER AND CANNOT SEE WHY.
#
# Hence the two rules this script is built around:
#
#   1. EVERY outcome is SPOKEN, through `spd-say`, which talks to
#      speech-dispatcher directly and therefore works no matter which reader
#      is up or whether either one is. Printing alone is useless to the person
#      who needs this script most.
#   2. A failed start ROLLS BACK to the reader that was working. Better the
#      one you had than neither.
#
#     tools/alus-switch.sh alus      # ALUS takes the keyboard
#     tools/alus-switch.sh other     # give it back to the other reader
#     tools/alus-switch.sh status    # who has it now
set -uo pipefail

SWITCH_SCHEMA=org.gnome.desktop.a11y.applications
SWITCH_KEY=screen-reader-enabled
OTHER_BIN=orca                       # the other reader's executable
OTHER_NAME="the other screen reader" # what we SAY, never its product name
# `--replace` and not a bare start, MEASURED: that reader runs as two
# processes, so `pkill` clears the process name before both are really gone,
# and the next start then refuses with "a screen reader is already running for
# this session -- run `orca --replace`". Taking its own advice makes the start
# idempotent: it works whether or not a stale instance is still letting go.
OTHER_START=("$OTHER_BIN" --replace)
LOG="${ALUS_SWITCH_LOG:-$HOME/alus.log}"
# The OTHER reader's startup output. Discarding it was a real mistake the
# first time this script was tested: the swap failed, and the reason was in
# the stderr that had been thrown away.
OTHER_LOG="${ALUS_SWITCH_OTHER_LOG:-$HOME/alus-switch-other.log}"
WAIT_STOP=10                         # seconds to wait for a reader to exit
WAIT_START=8                         # seconds to wait for one to come up

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

# The installed binaries win; the build tree is the fallback, so this works
# before `meson install` as well as after it.
find_bin() {
  local name=$1
  if command -v "$name" >/dev/null 2>&1; then command -v "$name"; return 0; fi
  if [ -x "$here/build/src/$name" ]; then printf '%s\n' "$here/build/src/$name"; return 0; fi
  return 1
}

DAEMON=$(find_bin alus-daemon) || DAEMON=
CLI=$(find_bin alus-cli) || CLI=

say() {  # to the terminal AND out loud, because the second one is the point
  printf '%s\n' "$1"
  command -v spd-say >/dev/null 2>&1 && spd-say -C -w -- "$1" >/dev/null 2>&1
  return 0
}
die() { say "$1"; exit 1; }

alus_running()  { pgrep -x alus-daemon >/dev/null 2>&1; }
other_running() { pgrep -x "$OTHER_BIN" >/dev/null 2>&1; }

switch_set() { gsettings set "$SWITCH_SCHEMA" "$SWITCH_KEY" "$1" 2>/dev/null; }
switch_get() { gsettings get "$SWITCH_SCHEMA" "$SWITCH_KEY" 2>/dev/null; }

# Stop a process and WAIT for it to be gone. SIGTERM first: ALUS hands the
# grabs back to the compositor and closes the synthesizer on SIGTERM, and
# killing it outright can leave the keyboard grabbed.
stop_and_wait() {
  local name=$1 waited=0
  pgrep -x "$name" >/dev/null 2>&1 || return 0
  pkill -TERM -x "$name" 2>/dev/null
  while [ "$waited" -lt "$WAIT_STOP" ]; do
    if ! pgrep -x "$name" >/dev/null 2>&1; then
      # The NAME being gone is not the same as the process being gone: a
      # reader that runs as several processes can still be letting go of its
      # bus name. One extra second, then confirm.
      sleep 1
      pgrep -x "$name" >/dev/null 2>&1 || return 0
    fi
    pkill -TERM -x "$name" 2>/dev/null
    sleep 1; waited=$((waited + 1))
  done
  # Last resort, and worth reporting: a reader that ignores SIGTERM may not
  # have released the keyboard either.
  say "$name did not stop on its own; forcing it"
  pkill -KILL -x "$name" 2>/dev/null
  sleep 1
  pgrep -x "$name" >/dev/null 2>&1 && return 1
  return 0
}

# Is the keyboard actually free? Asked through ALUS's own health reporting:
# `alus-cli input-info` exits 0 only when the input backend is working, which
# is exactly "nothing else holds the bus name".
keyboard_free() {
  [ -n "$CLI" ] || return 0            # cannot ask; do not block on it
  "$CLI" input-info >/dev/null 2>&1
}

wait_keyboard_free() {
  local waited=0
  while [ "$waited" -lt "$WAIT_STOP" ]; do
    keyboard_free && return 0
    sleep 1; waited=$((waited + 1))
  done
  return 1
}

wait_alive() {  # a process that exits one second after starting never started
  local name=$1 waited=0
  while [ "$waited" -lt "$WAIT_START" ]; do
    sleep 1; waited=$((waited + 1))
    pgrep -x "$name" >/dev/null 2>&1 && { sleep 1; pgrep -x "$name" >/dev/null 2>&1 && return 0; }
  done
  return 1
}

to_alus() {
  [ -n "$DAEMON" ] || die "ALUS is not built and not installed: nothing to start"
  # "ALUS is running" is NOT enough to be done: BOTH can be running at once,
  # and that is the broken state this script exists to get out of. It happens
  # for real -- launching ALUS from the application menu while the other
  # reader is up produces exactly it (FAZA9.md N97). Only return early when
  # ALUS is up AND nothing else is.
  if alus_running && ! other_running; then
    say "ALUS is already running"
    exit 0
  fi
  if alus_running && other_running; then
    say "both readers are running; stopping $OTHER_NAME"
  fi

  # The switch goes off FIRST. The other reader's autostart entry has
  # X-GNOME-AutoRestart, so stopping it while the switch is still on invites
  # the session to start it again underneath us.
  local had_switch; had_switch=$(switch_get)
  switch_set false
  stop_and_wait "$OTHER_BIN" || say "warning: $OTHER_NAME may still hold the keyboard"

  if ! wait_keyboard_free; then
    say "the keyboard did not come free; putting $OTHER_NAME back"
    [ "$had_switch" = "true" ] && { switch_set true; setsid "${OTHER_START[@]}" >"$OTHER_LOG" 2>&1 < /dev/null & }
    exit 1
  fi

  # Only start one if the swap did not already leave one running (the BOTH
  # case above). A second `alus-daemon` would refuse on the bus name anyway,
  # but refusing is not the same as being handled.
  if ! alus_running; then
    setsid "$DAEMON" --log-file "$LOG" >/dev/null 2>&1 < /dev/null &
  fi
  if wait_alive alus-daemon; then
    say "ALUS is running"
    exit 0
  fi

  # THE ROLLBACK. This is the branch the whole script exists for.
  say "ALUS failed to start; putting $OTHER_NAME back"
  [ -n "$LOG" ] && [ -f "$LOG" ] && tail -5 "$LOG"
  switch_set true
  setsid "${OTHER_START[@]}" >"$OTHER_LOG" 2>&1 < /dev/null &
  wait_alive "$OTHER_BIN" \
    && say "$OTHER_NAME is running; ALUS did not start, see $LOG" \
    || say "WARNING: neither reader is running. Log out and back in to get one."
  exit 1
}

to_other() {
  command -v "$OTHER_BIN" >/dev/null 2>&1 || die "$OTHER_NAME is not installed"
  if other_running && ! alus_running; then
    say "$OTHER_NAME is already running"
    exit 0
  fi
  if other_running && alus_running; then
    say "both readers are running; stopping ALUS"
  fi

  stop_and_wait alus-daemon || say "warning: ALUS may still hold the keyboard"
  wait_keyboard_free || say "warning: the keyboard did not report free"

  switch_set true
  if ! other_running; then
    setsid "${OTHER_START[@]}" >"$OTHER_LOG" 2>&1 < /dev/null &
  fi
  if wait_alive "$OTHER_BIN"; then
    say "$OTHER_NAME is running"
    exit 0
  fi

  say "$OTHER_NAME failed to start; putting ALUS back"
  [ -s "$OTHER_LOG" ] && { printf -- '--- why it did not start (%s):\n' "$OTHER_LOG"; tail -8 "$OTHER_LOG"; }
  switch_set false
  [ -n "$DAEMON" ] && { setsid "$DAEMON" --log-file "$LOG" >/dev/null 2>&1 < /dev/null & }
  wait_alive alus-daemon \
    && say "ALUS is running; $OTHER_NAME did not start" \
    || say "WARNING: neither reader is running. Log out and back in to get one."
  exit 1
}

status() {
  local who="neither"
  alus_running && who="ALUS"
  other_running && { [ "$who" = "ALUS" ] && who="BOTH -- they are fighting for the keyboard" \
                                        || who="$OTHER_NAME"; }
  printf 'running:          %s\n' "$who"
  printf "desktop switch:   %s = %s\n" "$SWITCH_KEY" "$(switch_get)"
  printf 'alus-daemon:      %s\n' "${DAEMON:-not built and not installed}"
  if [ -n "$CLI" ]; then
    # Careful with the wording: `alus-cli input-info` exits 0 while ALUS's own
    # daemon is running, and non-zero while the OTHER reader holds the bus
    # name. So this answers "could ALUS take the keyboard", not "is the
    # keyboard unheld" -- and saying the keyboard is free while ALUS is
    # holding it would be a lie in the one diagnostic a blind user leans on.
    keyboard_free && printf 'keyboard:         available to ALUS\n' \
                  || printf 'keyboard:         held by %s\n' "$OTHER_NAME"
  fi
  say "$who"
}

case "${1:-status}" in
  alus)         to_alus ;;
  other|orca)   to_other ;;
  status)       status ;;
  *) printf 'usage: %s [alus|other|status]\n' "$(basename "$0")" >&2; exit 2 ;;
esac
