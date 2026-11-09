#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# The phase 5 measurement: the text layer against the real toolkits.
#
# The unit tests run against a mock platform, which is the only way to count
# platform calls and to make a toolkit lie on command. What a mock cannot tell
# is WHICH GRANULARITIES THE REAL TOOLKITS ANSWER, what their text attributes
# are really called, and what flat screen review costs on a real window. That
# is this script, and it is the phase 5 equivalent of
# tools/phase4-object-check.sh.
#
# It READS ONLY. No caret is moved, no selection changed, no key taken, no
# window activated -- so it is safe to run while Orca or ALUS itself is
# running.
#
# Usage:
#   tools/phase5-text-check.sh            all the automatic steps
#   tools/phase5-text-check.sh --manual   also print the manual test plan
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

say "Step 1 — the segmenter (01-ARHITEKTURA.md §7.3, T5.3)"
# ICU is what makes Japanese and Thai word navigation possible at all; nothing
# else can segment a script with no spaces in it.
if pkg-config --exists icu-uc 2>/dev/null; then
  ok "libicu-dev present: $(pkg-config --modversion icu-uc)"
else
  warn "libicu-dev is NOT installed"
  note "Serbian and English word navigation are correct without it;"
  note "Japanese and Thai are not. sudo apt install libicu-dev, then rebuild."
fi
"$CLI" text-info 2>/dev/null | sed -n '1p' | sed 's/^/        /'

say "Step 2 — which granularities each toolkit answers (§7.2)"
# THE DECISION THIS MEASURES: lines and paragraphs come from the toolkit,
# because only the toolkit knows where it WRAPPED the text; words and
# sentences come from the segmenter, because no toolkit offers NVDA's word
# unit (the word plus the whitespace after it).
python3 - <<'PY'
import gi
gi.require_version('Atspi', '2.0')
from gi.repository import Atspi

GRAN = [
    ('char', Atspi.TextGranularity.CHAR),
    ('word', Atspi.TextGranularity.WORD),
    ('sentence', Atspi.TextGranularity.SENTENCE),
    ('line', Atspi.TextGranularity.LINE),
    ('paragraph', Atspi.TextGranularity.PARAGRAPH),
]


def first_text(obj, depth=0):
    """The first descendant with a non-empty AtspiText, breadth first."""
    if depth > 6:
        return None
    try:
        if obj.get_role() not in (Atspi.Role.APPLICATION, Atspi.Role.DESKTOP_FRAME):
            text = obj.get_text_iface()
            if text is not None and Atspi.Text.get_character_count(text) > 0:
                return obj
        for index in range(min(obj.get_child_count(), 24)):
            child = obj.get_child_at_index(index)
            if child is None:
                continue
            found = first_text(child, depth + 1)
            if found is not None:
                return found
    except Exception:
        return None
    return None


desktop = Atspi.get_desktop(0)
print('        %-22s %-9s %s' % ('application', 'toolkit', 'granularities answered'))
for index in range(desktop.get_child_count()):
    app = desktop.get_child_at_index(index)
    if app is None:
        continue
    try:
        name = app.get_name() or '(unnamed)'
        toolkit = app.get_toolkit_name() or '?'
    except Exception:
        continue
    holder = first_text(app)
    if holder is None:
        continue
    text = holder.get_text_iface()
    answers = []
    for label, value in GRAN:
        try:
            segment = Atspi.Text.get_string_at_offset(text, 0, value)
            answers.append('%s=%s' % (label, 'yes' if segment.start_offset >= 0 else 'no'))
        except Exception:
            answers.append('%s=err' % label)
    print('        %-22s %-9s %s' % (name[:22], toolkit[:9], ' '.join(answers)))
PY

say "Step 3 — the text attributes the toolkits really use (§7.2's normaliser)"
# Every row of the normaliser in src/a11y/text_range.cpp is a decision about
# what a toolkit MEANT. These are the names it has to deal with on this
# desktop; a name here that the normaliser does not know is a gap.
python3 - <<'PY'
import gi
gi.require_version('Atspi', '2.0')
from gi.repository import Atspi

KNOWN = {
    'family-name', 'size', 'weight', 'style', 'underline', 'strikethrough',
    'text-position', 'rise', 'fg-color', 'bg-color', 'invalid', 'language',
    'justification', 'vertical-align', 'indent', 'left-margin', 'right-margin',
    'line-height', 'paragraph-style', 'invisible',
}

seen = {}


def walk(obj, depth=0):
    if depth > 5:
        return
    try:
        text = obj.get_text_iface()
        if text is not None and Atspi.Text.get_character_count(text) > 0:
            attributes = Atspi.Text.get_attribute_run(text, 0, True)
            for key in (attributes[0] or {}):
                seen.setdefault(key, 0)
                seen[key] += 1
        for index in range(min(obj.get_child_count(), 16)):
            child = obj.get_child_at_index(index)
            if child is not None:
                walk(child, depth + 1)
    except Exception:
        return


desktop = Atspi.get_desktop(0)
for index in range(desktop.get_child_count()):
    app = desktop.get_child_at_index(index)
    if app is not None:
        walk(app)

for key in sorted(seen):
    mark = ' ' if key in KNOWN else '   <-- NOT NORMALISED'
    print('        %-24s seen %3d time(s)%s' % (key, seen[key], mark))
if not seen:
    print('        (no object with text attributes found)')
PY

say "Step 4 — the text layer on the focused object, and flat screen review (§10)"
# The numbers that matter: how many AT-SPI calls a flat review of the active
# window costs, and how long it takes. Phase 4 measured 578 us per object, so
# a window with sixty controls is about 35 ms -- once, because the snapshot is
# shared by every copy of the range.
if "$CLI" text-info; then
  ok "text-info answered"
else
  bad "text-info failed"
fi

say "Step 5 — the test suites"
for suite in text nav; do
  if meson test -C build --suite "$suite" >/dev/null 2>&1; then
    ok "meson test --suite $suite"
  else
    bad "meson test --suite $suite"
  fi
done

if [[ "${1:-}" == "--manual" ]]; then
  cat <<'PLAN'

== The manual test (02-FAZE.md, phase 5) ==

What a machine cannot check is WHAT THE USER HEARS. Start the screen reader
and press the keys; each line says what should happen.

  ./build/src/alus-daemon --log-level debug

In GNOME Text Editor, with a few paragraphs of text:

  alus+upArrow        the line at the caret
  alus+upArrow x2     the same line, spelled
  alus+upArrow x3     the same line, with character descriptions
  upArrow/downArrow   the new line, once per press and once only
  leftArrow/rightArrow  the character, and an accented letter ONCE (not twice)
  control+arrows      the word
  home/end            the first and last character of the line
  shift+arrows        the selected text, with "selected"
  backspace           what was deleted
  delete              what is now under the caret
  alus+downArrow      say all from the caret; the caret follows the voice
  any key             say all stops at once
  alus+f              the formatting at the caret
  numpad7/8/9         the review cursor by line
  numpad4/5/6         by word
  numpad1/2/3         by character
  numpad2 x2          the character described ("a, Avala" in Serbian)
  numpad2 x3          its decimal and hexadecimal value
  numpadPlus          say all from the review cursor
  alus+numpad7        "Screen review"; numpad8 then reads a ROW of the window,
                      left to right -- not one control of it
  alus+numpad1        back to "Object review"
  alus+f9, numpad6 x3, alus+f10   mark, move, select

In gnome-terminal:

  ls -la              the new lines, and only the new ones
  cat <a long file>   continuous output, with nothing repeated
  type a command      each character once (the shell's echo is not announced
                      a second time)
  sudo <something>    the password prompt, and NOT the password

What to look for in the log afterwards:

  grep -E 'caret move detected' alus.log      which strategy caught each move
  grep -E 'say all stopped' alus.log          how many lines each say-all read
  grep -E 'live text monitoring' alus.log     the terminal's baseline
PLAN
fi

printf '\n'
if [[ $status -eq 0 ]]; then
  echo "phase 5: every automatic step passed"
else
  echo "phase 5: at least one step failed"
fi
exit $status
