#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# The phase 8 measurement: app modules and plugins against this machine.
#
# The suite already tests the five mechanisms, the three hooks and the whole
# add-on ABI against real compiled `.so` files. What a test cannot answer is
# what the applications on THIS machine report, and that is the thing an app
# module is entirely made of:
#
#   * what the lookup key really is -- `basename(/proc/<pid>/exe)`, the
#     accessible name and the toolkit string, which are nothing like what the
#     user calls the program (GNOME Terminal runs as `gnome-terminal-server`,
#     LibreOffice as `soffice.bin`, VS Code reports the same toolkit as
#     Google Chrome);
#   * which module each running application would therefore get;
#   * whether the add-on ABI still compiles with a plain C compiler and one
#     header, which is §12.3's whole promise;
#   * whether a plugin directory exists, and whether anything in it is marked
#     for removal from a previous session.
#
# It READS ONLY: `app-info` creates modules in its own process, takes no key,
# activates no window and LOADS NO PLUGIN -- loading one would run third-party
# code from a diagnostic command. Safe to run while ALUS or Orca is running.
#
# Usage:
#   tools/phase8-app-check.sh            all the automatic steps
#   tools/phase8-app-check.sh --manual   also print the manual test plan
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

say "Step 1 — the lookup key of every running application (T8.1)"
info=$("$CLI" app-info 2>&1)
printf '%s\n' "$info" | sed 's/^/        /'
if printf '%s\n' "$info" | grep -q 'app module rules:'; then
  rules=$(printf '%s\n' "$info" | sed -n 's/^app module rules: *//p')
  if [[ "${rules:-0}" -ge 6 ]]; then
    ok "$rules module rules registered"
  else
    bad "only $rules module rules: the built-in list did not register"
  fi
else
  bad "app-info printed no rule count"
fi
if printf '%s\n' "$info" | grep -q 'running applications'; then
  if printf '%s\n' "$info" | grep -qE '^ +\S+.*-> '; then
    ok "the desktop was read and every application resolved to a module"
  else
    warn "no applications on the accessibility bus (nothing running, or no bus)"
  fi
else
  warn "the accessibility bus could not be read -- run this inside a session"
fi

say "Step 2 — the executables behind the names the user knows"
# The point of the step: these are the values the rules key on, and every one
# of them differs from what a user would guess. Measured on the target system:
#   gnome-terminal  -> /usr/libexec/gnome-terminal-server
#   libreoffice     -> /usr/lib/libreoffice/program/soffice.bin
for program in gnome-terminal soffice nautilus konsole code; do
  path=$(command -v "$program" 2>/dev/null) || path=""
  if [[ -z "$path" ]]; then
    note "$program: not installed"
    continue
  fi
  # -f and not -x: a process name in /proc is truncated to 15 characters, so
  # `gnome-terminal-server` is `gnome-terminal-` there and an exact match on
  # the name the user typed finds nothing. That truncation is itself the reason
  # this step exists.
  pids=$(pgrep -f "$program" 2>/dev/null || true)
  if [[ -n "$pids" ]]; then
    for pid in $pids; do
      exe=$(readlink "/proc/$pid/exe" 2>/dev/null || echo "?")
      base=$(basename "$exe")
      # -f matches command lines, so it also finds the shell that is running
      # this script. Only a process whose EXECUTABLE is related to the name is
      # the program being asked about.
      case "$base" in
        *"$program"*) ;;
        *) continue ;;
      esac
      note "$program (pid $pid) runs as $base"
    done
  else
    note "$program: installed at $path, not running"
  fi
done
# The one that is almost always wrong if it is wrong: the terminal.
if pgrep -f gnome-terminal-server >/dev/null 2>&1; then
  ok "gnome-terminal-server is running, so the terminal module can be measured"
else
  warn "no gnome-terminal-server: the terminal module cannot be measured here"
fi

say "Step 3 — the add-on ABI compiles with a plain C compiler (§12.3)"
# The promise is "one header, any C compiler". This step checks it the only way
# that counts, against the header in the source tree rather than the build.
cc=${CC:-cc}
if ! command -v "$cc" >/dev/null 2>&1; then
  warn "no C compiler ($cc): the ABI check is skipped"
else
  tmp=$(mktemp -d)
  cat > "$tmp/probe.c" <<'PROBE'
#include "alus/alus-plugin.h"
#include <stddef.h>
static int init(AlusHost* host, const AlusHostApi* api) {
    (void)host; (void)api; return 0;
}
static const AlusPluginV1 kPlugin = {
    ALUS_PLUGIN_ABI_V1, "probe", NULL, &init, NULL, NULL
};
const AlusPluginV1* alus_plugin_v1(void) { return &kPlugin; }
PROBE
  if "$cc" -shared -fPIC -std=c11 -Wall -Wextra -I include -o "$tmp/probe.so" \
      "$tmp/probe.c" 2>"$tmp/err"; then
    ok "a plugin builds from alus-plugin.h alone ($("$cc" --version | head -1))"
    if command -v nm >/dev/null 2>&1; then
      if nm -D --defined-only "$tmp/probe.so" 2>/dev/null | grep -q alus_plugin_v1; then
        ok "the entry point $(printf 'alus_plugin_v1') is exported"
      else
        bad "the entry point is not exported: dlsym would find nothing"
      fi
    fi
  else
    bad "alus-plugin.h does not compile as C on its own:"
    sed 's/^/        /' "$tmp/err"
  fi
  rm -rf "$tmp"
fi

say "Step 4 — the plugin directories, and anything left marked for removal"
data_home=${XDG_DATA_HOME:-$HOME/.local/share}
found_any=0
for dir in "$data_home/alus/scratchpad" "$data_home/alus/addons" \
           /usr/local/share/alus/plugins /usr/share/alus/plugins; do
  if [[ -d "$dir" ]]; then
    found_any=1
    count=$(find "$dir" -maxdepth 2 -name '*.so' 2>/dev/null | wc -l)
    note "$dir: $count plugin(s)"
    # The two-phase uninstall (T8.7): a marker left from a session that was
    # killed rather than shut down is acted on at the next start, so it is
    # worth seeing here.
    while IFS= read -r marker; do
      [[ -z "$marker" ]] && continue
      warn "marked for removal, goes at the next start: $marker"
    done < <(find "$dir" -maxdepth 2 -name '.alus-pending-remove*' 2>/dev/null)
  else
    note "$dir: does not exist"
  fi
done
if [[ $found_any -eq 0 ]]; then
  ok "no plugin directories: a stock ALUS with no add-ons, which is the normal case"
else
  ok "plugin directories read"
fi

say "Step 5 — the suite (T8.1-T8.7)"
if command -v meson >/dev/null 2>&1 && [[ -d build ]]; then
  if meson test -C build --suite app >/dev/null 2>&1; then
    ok "meson test --suite app passed"
  else
    bad "meson test --suite app FAILED -- run it and read the output"
  fi
  # The rule that applies to every phase: the earlier suites must not break.
  if meson test -C build >/dev/null 2>&1; then
    ok "every suite still passes"
  else
    bad "a suite outside phase 8 broke: meson test -C build"
  fi
else
  warn "no meson build directory: the suite was not run"
fi

if [[ "${1:-}" == "--manual" ]]; then
  cat <<'PLAN'

== The manual test — the part only a user can do ==

Nothing below can be measured by a machine: it is whether the ADJUSTMENT each
module makes is the right one. Start ALUS with Orca stopped:

    pkill orca
    ./build/src/alus-daemon --log-level debug --log-file ~/alus.log &

--- LibreOffice Writer ---

  Open a document with a few paragraphs.
  alus+end        EXPECT the status line: "Page 1 of 1, 123 words, ...". The
                  fields with nothing in them must NOT be read as pauses.
                  (Measured: the fields have no accessible name at all; their
                  content is in the Text interface, and seven of thirteen are
                  empty. If you hear silence, the module's statusBarText
                  override is not being reached.)
  alus+end 2x     spells it.
  down arrow      reading by paragraph, as phase 6 left it. Browse mode is NOT
                  used here and that is deliberate.

--- LibreOffice Calc ---

  alus+shift+c    EXPECT the column header of the cell you are in, or "No
                  column header" when the sheet has none. Never silence.
  alus+shift+r    the row header, likewise.
  arrow keys      moving by cell. EXPECT the cell reference and the content.
                  (Measured: Calc exposes table -> table cell with NO row in
                  between, so the container override is inert here; if you
                  hear a "table row" announced, the hierarchy has changed and
                  the override is what makes it right again.)
  control+End     to the last used cell. EXPECT it to RETURN -- a Calc sheet
                  reports 2147483647 children, and the object model caps the
                  child list for exactly this (src/a11y/object.cpp,
                  kMaxChildren). A freeze here is that cap failing.

--- VS Code, or any Electron application ---

  Open a source file and type.
  EXPECT          typing to stay responsive, and the editor to read as a text
                  field -- NOT as a document in browse mode.
  h               must type the letter h, not jump to the next heading. If it
                  jumps, the tree-interceptor veto is not reaching this
                  process; check the log for "browse mode is blocked for".
  alus+space      must do nothing useful here (there is no browse mode to
                  leave), and must not make the editor silent.

--- Nautilus ---

  Open Files in list view and arrow through a folder.
  EXPECT          the file name, then its size and date as the DESCRIPTION --
                  so a user who turned object descriptions off hears only the
                  name. The name must not be said twice (measured: the name
                  column contains it doubled).
  EXPECT          the view announced once on entry, not "list" inside "list".

--- A plugin, if you have one to try ---

    mkdir -p ~/.local/share/alus/addons/hello
    cp hello.so ~/.local/share/alus/addons/hello/hello.so
    # restart ALUS, or: kill -HUP $(pgrep alus-daemon)
  EXPECT          one line in the log: "plugin hello loaded from ...".
                  A plugin that is refused says WHY on one line, and ALUS
                  starts anyway -- a broken add-on must never stop the screen
                  reader from coming up.

--- Then the log, which says what happened rather than what you think did ---

    grep -E 'app module'      ~/alus.log   # which module each application got
    grep -E 'app switch'      ~/alus.log   # the choreography, one line each
    grep -E 'plugin'          ~/alus.log   # loaded, refused, uninstalled
    grep -E 'reports .* children' ~/alus.log   # a toolkit over the child cap
    grep -E 'vetoed'          ~/alus.log   # browse mode blocked, and where

PLAN
fi

printf '\n'
if [[ $status -eq 0 ]]; then
  echo "phase 8: the automatic steps passed."
else
  echo "phase 8: something above FAILED -- read it, do not re-run it."
fi
exit $status
