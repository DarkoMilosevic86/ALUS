#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# The four steps of spike S1 that phase 0 could not prove, measured against the
# real ALUS daemon (spikes/RESULTS.md, "Šta još treba za zatvaranje Faze 0"):
#
#   step 3   the key watcher sees EVERY key press
#   step 5b  a grab really CONSUMES its key: the application does not get it
#   step 6   the ALUS key pressed twice inside 500 ms is detected, and what
#            actually reaches the application is measured rather than assumed
#   step 7   the numeric keypad with NumLock OFF reports KP_Home, not KP_7
#
# and, in the same run, the manual test phase 3 owes 02-FAZE.md:
#
#   alus+1   input help on, every key explains itself, alus+1 again turns it
#            off — the toggle must NOT eat itself
#   alus+4   speaking of command keys on and off
#
# It must be run BY HAND, in a focused terminal: the measurement is what the
# terminal receives, and that cannot be faked (synthetic keys are a no-op on
# Wayland — finding N18).
#
# WHY IT IS SAFE TO RUN. A screen reader that holds key grabs and wedges takes
# the user's keyboard away (finding N19, measured at 549 s). So:
#
#   * Orca is stopped only for the length of the test and restored by an EXIT
#     trap that runs on every path, including Ctrl+C and a kill;
#   * a reaper subshell SIGKILLs the daemon after the window expires, whatever
#     state it is in — and process death releases the grabs;
#   * the daemon itself runs under `timeout -s KILL`;
#   * only the gestures ALUS can actually run are grabbed (12 of them), so
#     every other key, Ctrl+C included, reaches the terminal throughout.
#
# Prompts are spoken through speech-dispatcher directly (spd-say), NOT through
# ALUS, so they are heard even if ALUS says nothing at all.

set -u

# SAFETY_CHECK=1 stops Orca for a few seconds, checks that the KeyboardMonitor
# name really becomes free, and restores Orca — without starting ALUS at all.
# It exists so the dangerous part of this script (taking the user's screen
# reader away) can be rehearsed before it matters.
readonly SAFETY_CHECK=${SAFETY_CHECK:-0}
readonly WINDOW_SECONDS=${WINDOW_SECONDS:-220}
readonly PHASE_SECONDS=${PHASE_SECONDS:-8}
readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly WORK="$(mktemp -d /tmp/alus-phase3-XXXXXX)"
readonly LOG="$WORK/alus.log"
readonly CONFIG_DIR="$WORK/config"
readonly GUARD="$WORK/keep-orca-down"

DAEMON="${DAEMON:-$ROOT/build/src/alus-daemon}"

ORCA_WAS_ENABLED=""
DAEMON_PID=""
REAPER_PID=""
GUARD_PID=""
STTY_SAVED=""

say() {
    printf '\n>>> %s\n' "$1"
    spd-say -w -- "$1" >/dev/null 2>&1 || true
}

note() { printf '    %s\n' "$1"; }

restore() {
    local status=$?
    rm -f "$GUARD"
    [[ -n "$STTY_SAVED" ]] && stty "$STTY_SAVED" 2>/dev/null
    [[ -n "$DAEMON_PID" ]] && kill -TERM "$DAEMON_PID" 2>/dev/null
    sleep 0.3
    [[ -n "$DAEMON_PID" ]] && kill -KILL "$DAEMON_PID" 2>/dev/null
    [[ -n "$REAPER_PID" ]] && kill -KILL "$REAPER_PID" 2>/dev/null
    [[ -n "$GUARD_PID" ]] && kill -KILL "$GUARD_PID" 2>/dev/null
    pkill -x alus-daemon 2>/dev/null
    if [[ "$ORCA_WAS_ENABLED" == "true" ]]; then
        gsettings set org.gnome.desktop.a11y.applications screen-reader-enabled true 2>/dev/null
        if ! pgrep -x orca >/dev/null; then
            setsid orca >/dev/null 2>&1 &
        fi
        printf '\n    Orca restored.\n'
        spd-say -- "Orca je vraćena." >/dev/null 2>&1 || true
    fi
    printf '    Log: %s\n' "$LOG"
    exit $status
}
trap restore EXIT INT TERM

# ---------------------------------------------------------------- preparation

if [[ ! -t 0 && "$SAFETY_CHECK" != "1" ]]; then
    echo "This has to run in a terminal: the measurement is what the terminal receives." >&2
    exit 2
fi
if [[ ! -x "$DAEMON" ]]; then
    echo "alus-daemon not found at $DAEMON (build it first)." >&2
    exit 2
fi

mkdir -p "$CONFIG_DIR"
# Step 5b asks for `Insert+t` specifically. Phase 3 implements five commands, so
# `alus+t` is bound to one of them here — an audible one, so that the user hears
# whether the script ran, while the terminal shows whether the key leaked.
cat > "$CONFIG_DIR/gestures.ini" <<'INI'
[globalCommands.GlobalCommands]
	toggleSpeakCommandKeys = kb:alus+t
INI

echo "=============================================================="
echo " ALUS phase 3 — S1 steps 3, 5b, 6 and 7, against alus-daemon"
echo "=============================================================="
note "daemon:    $DAEMON"
note "log:       $LOG"
note "window:    ${WINDOW_SECONDS}s, then the daemon is killed whatever happens"

ORCA_WAS_ENABLED="$(gsettings get org.gnome.desktop.a11y.applications screen-reader-enabled 2>/dev/null)"
say "Zaustavljam Orku na oko minut i po. Ne diraj tastaturu dok ne kažem."

gsettings set org.gnome.desktop.a11y.applications screen-reader-enabled false 2>/dev/null
touch "$GUARD"
# gnome-session restarts Orca by itself (finding N8), so it is held down for the
# length of the test rather than killed once.
( while [[ -f "$GUARD" ]]; do pkill -x orca 2>/dev/null; sleep 0.5; done ) &
GUARD_PID=$!

for _ in $(seq 1 20); do
    busctl --user list --no-pager 2>/dev/null | grep -q 'org.gnome.Orca.KeyboardMonitor' || break
    sleep 0.5
done
if busctl --user list --no-pager 2>/dev/null | grep -q 'org.gnome.Orca.KeyboardMonitor'; then
    echo "FAIL: org.gnome.Orca.KeyboardMonitor is still held; cannot test." >&2
    exit 1
fi
note "Orca stopped, the KeyboardMonitor name is free."

# ------------------------------------------------------------------- start up

if [[ "$SAFETY_CHECK" == "1" ]]; then
    note "SAFETY CHECK: not starting ALUS. Restoring Orca in 3 seconds."
    sleep 3
    rm -f "$GUARD"
    kill -KILL "$GUARD_PID" 2>/dev/null; GUARD_PID=""
    gsettings set org.gnome.desktop.a11y.applications screen-reader-enabled true 2>/dev/null
    if ! pgrep -x orca >/dev/null; then
        setsid orca >/dev/null 2>&1 &
    fi
    for _ in $(seq 1 40); do
        busctl --user list --no-pager 2>/dev/null | grep -q 'org.gnome.Orca.KeyboardMonitor' && break
        sleep 0.5
    done
    printf '\n--- safety check ---\n'
    if pgrep -x orca >/dev/null; then
        note "Orca is running again (pid $(pgrep -x orca | head -1))."
    else
        note "WARNING: Orca is NOT running; start it with: orca &"
    fi
    if busctl --user list --no-pager 2>/dev/null | grep -q 'org.gnome.Orca.KeyboardMonitor'; then
        note "org.gnome.Orca.KeyboardMonitor is held again — the restore path works."
    else
        note "WARNING: the KeyboardMonitor name is still free."
    fi
    note "screen-reader-enabled = $(gsettings get org.gnome.desktop.a11y.applications screen-reader-enabled)"
    ORCA_WAS_ENABLED=""   # already restored; the trap must not do it twice
    exit 0
fi

( sleep "$WINDOW_SECONDS"; pkill -x alus-daemon 2>/dev/null; sleep 1; pkill -KILL -x alus-daemon 2>/dev/null ) &
REAPER_PID=$!

ALUS_CONFIG_DIR="$CONFIG_DIR" ALUS_DATA_DIR="$ROOT/data" \
    timeout -s KILL "$WINDOW_SECONDS" "$DAEMON" \
    --allow-multiple --log-level io --log-file "$LOG" >"$WORK/daemon.out" 2>&1 &
DAEMON_PID=$!

for _ in $(seq 1 40); do
    grep -q 'watching the keyboard' "$LOG" 2>/dev/null && break
    grep -q 'input is NOT available' "$LOG" 2>/dev/null && break
    sleep 0.25
done
if grep -q 'input is NOT available' "$LOG" 2>/dev/null; then
    echo "FAIL: ALUS did not get the keyboard:" >&2
    grep 'input is NOT available' "$LOG" >&2
    exit 1
fi
grep -q 'watching the keyboard' "$LOG" 2>/dev/null || { echo "FAIL: the daemon did not start." >&2; exit 1; }
note "ALUS is watching the keyboard."
grep -E 'key grabs installed|ALUS key:' "$LOG" | sed 's/^/    /'

# ---------------------------------------------------------------- measurement

STTY_SAVED="$(stty -g)"
stty raw -echo

drain_stdin() {
    local buffer="" char
    while IFS= read -r -t 0.05 -N 1 char; do buffer+="$char"; done
    printf '%s' "$buffer"
}

log_mark() { wc -l < "$LOG"; }
log_since() { tail -n +"$(( $1 + 1 ))" "$LOG"; }

phase() {  # phase <title> <spoken prompt>
    printf '\r\n--- %s ---\r\n' "$1"
    stty "$STTY_SAVED"
    say "$2"
    stty raw -echo
    drain_stdin >/dev/null
    local mark; mark="$(log_mark)"
    sleep "$PHASE_SECONDS"
    PHASE_STDIN="$(drain_stdin)"
    PHASE_LOG="$(log_since "$mark")"
}

# --- step 3: does the watcher see every key? -------------------------------
phase "step 3: does ALUS see every key" \
      "Korak tri. Pritisni slova a, s, d, f, jedno po jedno."
STEP3_LOG="$PHASE_LOG"; STEP3_STDIN="$PHASE_STDIN"

# --- step 5b: does a grab consume its key? ---------------------------------
phase "step 5b: does the grab consume Insert+t" \
      "Korak pet b. Drži Insert i pritisni t. Trebalo bi da čuješ poruku o komandnim tasterima."
STEP5_LOG="$PHASE_LOG"; STEP5_STDIN="$PHASE_STDIN"

# --- step 6: the ALUS key pressed twice ------------------------------------
phase "step 6: the ALUS key twice inside 500 ms" \
      "Korak šest. Pritisni Insert dva puta brzo, u okviru pola sekunde."
STEP6_LOG="$PHASE_LOG"; STEP6_STDIN="$PHASE_STDIN"

# --- step 7: the numeric keypad without NumLock ----------------------------
phase "step 7: the keypad with NumLock off" \
      "Korak sedam. Isključi NumLock, pa pritisni numerički sedam i numerički jedan."
STEP7_LOG="$PHASE_LOG"; STEP7_STDIN="$PHASE_STDIN"

# --- the manual test: input help ------------------------------------------
phase "manual: input help (alus+1)" \
      "Ručni test. Pritisni Insert i jedan da uključiš ulaznu pomoć. Onda pritisni slovo a, pa Insert i dva. Pa opet Insert i jedan da je isključiš."
HELP_LOG="$PHASE_LOG"; HELP_STDIN="$PHASE_STDIN"

# --- the manual test: speak command keys ----------------------------------
phase "manual: speak command keys (alus+4)" \
      "Pritisni Insert i četiri. Pa Insert i četiri ponovo."
CMDKEYS_LOG="$PHASE_LOG"; CMDKEYS_STDIN="$PHASE_STDIN"

stty "$STTY_SAVED"
STTY_SAVED=""

# ------------------------------------------------------------------ analysis

printf '\n==============================================================\n'
printf ' RESULTS\n'
printf '==============================================================\n'

keysyms_of() { grep -oE 'keysym=0x[0-9a-f]+' <<<"$1" | sort -u | tr '\n' ' '; }
down_count() { grep -c 'key down' <<<"$1"; }

printf '\n[step 3] the watcher sees every key\n'
printf '  key events: %s down\n' "$(down_count "$STEP3_LOG")"
printf '  keysyms:    %s\n' "$(keysyms_of "$STEP3_LOG")"
printf '  terminal received: %q\n' "$STEP3_STDIN"
if (( $(down_count "$STEP3_LOG") >= 4 )) && [[ -n "$STEP3_STDIN" ]]; then
    printf '  PASS — ALUS saw the keys AND the application still got them (a watcher does not consume)\n'
else
    printf '  FAIL/INCONCLUSIVE — expected at least 4 key downs and non-empty terminal input\n'
fi

printf '\n[step 5b] a grab consumes its key\n'
printf '  gesture lines:\n'
grep -E 'input: kb' <<<"$STEP5_LOG" | sed 's/^/    /'
printf '  terminal received: %q\n' "$STEP5_STDIN"
if grep -q 'input: kb(desktop):alus+t\|input: kb:alus+t' <<<"$STEP5_LOG"; then
    if [[ "$STEP5_STDIN" != *t* ]]; then
        printf '  PASS — the gesture ran and "t" did NOT reach the terminal\n'
    else
        printf '  FAIL — the gesture ran but "t" ALSO reached the terminal: the grab does not consume\n'
    fi
else
    printf '  INCONCLUSIVE — no alus+t gesture in the log (was Insert held down?)\n'
fi

printf '\n[step 6] the ALUS key pressed twice\n'
printf '  insert presses: %s\n' "$(grep -c 'keysym=0xff63' <<<"$STEP6_LOG")"
grep -E 'pressed twice|pass the ALUS key' <<<"$STEP6_LOG" | sed 's/^/    /'
printf '  terminal received: %q\n' "$STEP6_STDIN"
if grep -q 'pressed twice' <<<"$STEP6_LOG"; then
    if [[ -n "$STEP6_STDIN" ]]; then
        printf '  PASS — detected, and the application DID receive something for the second press\n'
    else
        printf '  PARTIAL — detected, but nothing reached the application: the compositor had already\n'
        printf '            consumed the key and Wayland offers no way to inject one (N18)\n'
    fi
else
    printf '  INCONCLUSIVE — the double press was not detected (too slow? more than 500 ms apart)\n'
fi

printf '\n[step 7] the keypad with NumLock off\n'
printf '  keysyms: %s\n' "$(keysyms_of "$STEP7_LOG")"
NAV=0; DIGITS=0
grep -qE 'keysym=0xff9[5-9a-f]' <<<"$STEP7_LOG" && NAV=1
grep -qE 'keysym=0xffb[0-9]' <<<"$STEP7_LOG" && DIGITS=1
if (( NAV == 1 && DIGITS == 0 )); then
    printf '  PASS — named keypad keysyms (KP_Home/KP_End), no KP_7/KP_1\n'
elif (( DIGITS == 1 )); then
    printf '  FAIL — digit keysyms seen: NumLock was on, or this keyboard reports digits anyway\n'
else
    printf '  INCONCLUSIVE — no keypad keysyms in the log\n'
fi

printf '\n[manual: input help]\n'
printf '  input help lines:\n'
grep -E 'input help' <<<"$HELP_LOG" | sed 's/^/    /'
if grep -q 'input help on' <<<"$HELP_LOG" && grep -q 'input help off' <<<"$HELP_LOG"; then
    printf '  PASS — input help went on AND off: the toggle does not eat itself\n'
elif grep -q 'input help on' <<<"$HELP_LOG"; then
    printf '  FAIL — input help went on but not off: the toggle ate its own switch\n'
else
    printf '  INCONCLUSIVE — input help was never turned on\n'
fi
printf '  terminal received: %q\n' "$HELP_STDIN"

printf '\n[manual: speak command keys]\n'
grep -E 'input: kb' <<<"$CMDKEYS_LOG" | sed 's/^/    /'
printf '  terminal received: %q\n' "$CMDKEYS_STDIN"

printf '\n[N33: duplicated key events absorbed]\n'
grep -c 'dropped as a duplicate' "$LOG" | sed 's/^/  dropped: /'
printf '  (measured before the fix: 14 duplicates in 56 events, all within 0-2 ms;\n'
printf '   with the fix each key is announced once)\n'

printf '\n[counters]\n'
grep -E 'input:|keys seen' "$LOG" | tail -3 | sed 's/^/  /'
printf '\nFull log: %s\n' "$LOG"
