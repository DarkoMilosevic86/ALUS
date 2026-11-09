#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# The phase 6 measurement: browse mode against the real browsers.
#
# The unit tests run against a mock platform that answers `Collection` for
# real, which is how the 22 match rules of §9.2 are measured at all. What a
# mock cannot tell is what FIREFOX AND CHROMIUM actually do:
#
#   * whether their document roots expose `Collection` (§9.1 point 2 -- the
#     whole reason the virtual buffers are not ported);
#   * whether `sortBy = FLOW` is implemented or silently falls back to
#     CANONICAL (T6.6 -- the difference sends the user backwards);
#   * what a quick-nav press costs in milliseconds (T6.7, p95 < 20 ms);
#   * whether the root's own text is U+FFFC per child, as N47 measured on
#     Chrome -- which is what decided the compound range of §9.4.
#
# It READS ONLY. No caret is moved, no selection changed, no key taken, no
# window activated -- so it is safe to run while Orca or ALUS itself is
# running.
#
# Usage:
#   tools/phase6-browse-check.sh            all the automatic steps
#   tools/phase6-browse-check.sh --manual   also print the manual test plan
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

say "Step 1 — does this libatspi have Collection at all? (§9.2)"
# If GetMatchesFrom, the sort orders or the match types are missing, browse
# mode has no search service and §9.1's whole argument collapses.
if python3 - <<'PY'
import sys
try:
    import gi
    gi.require_version('Atspi', '2.0')
    from gi.repository import Atspi
except Exception as exc:                      # noqa: BLE001
    print("        cannot load Atspi: %s" % exc)
    sys.exit(1)

methods = [m for m in dir(Atspi.Collection) if 'match' in m]
orders = [s for s in dir(Atspi.CollectionSortOrder) if s.isupper()]
types = [t for t in dir(Atspi.CollectionMatchType) if t.isupper()]
print("        methods: %s" % ", ".join(sorted(methods)))
print("        orders:  %s" % ", ".join(sorted(orders)))
print("        match:   %s" % ", ".join(sorted(types)))
missing = []
for needed in ('get_matches', 'get_matches_from', 'get_matches_to'):
    if needed not in methods:
        missing.append(needed)
# The GI bindings strip the enum prefix: AtspiCollectionSortOrder's members
# are CANONICAL and FLOW, not SORT_ORDER_CANONICAL -- and AtspiCollectionMatchType's
# are ANY and ALL, not MATCH_ANY. The C names carry the prefix; these do not.
for needed in ('FLOW', 'CANONICAL'):
    if needed not in orders:
        missing.append('SortOrder.' + needed)
for needed in ('ANY', 'ALL', 'NONE'):
    if needed not in types:
        missing.append('MatchType.' + needed)
if missing:
    print("        MISSING: %s" % ", ".join(missing))
    sys.exit(1)
sys.exit(0)
PY
then
  ok "Collection is complete: GetMatches/From/To, FLOW and CANONICAL, ANY/ALL/NONE"
else
  bad "Collection is incomplete -- quick navigation cannot work as §9.2 specifies"
fi

say "Step 2 — the focused document (N47, T6.7)"
note "Focus a web page in Firefox or Chromium before this step."
if "$CLI" browse-info; then
  ok "browse-info answered"
else
  warn "no document in the focus chain (focus a page and run this again)"
fi

say "Step 3 — does FLOW differ from CANONICAL in this browser? (T6.6)"
# The criterion is that quick navigation follows READING order. A browser that
# implements FLOW as an alias for CANONICAL passes every unit test and still
# sends the user backwards through a floated sidebar -- so this asks the
# browser directly, with the same rule in both orders.
python3 - <<'PY'
import sys
try:
    import gi
    gi.require_version('Atspi', '2.0')
    from gi.repository import Atspi
except Exception as exc:                      # noqa: BLE001
    print("        cannot load Atspi: %s" % exc)
    sys.exit(0)

def focused_document():
    desktop = Atspi.get_desktop(0)
    for index in range(desktop.get_child_count()):
        app = desktop.get_child_at_index(index)
        if app is None:
            continue
        try:
            name = app.get_name() or ""
        except Exception:                     # noqa: BLE001
            continue
        if not any(part in name.lower() for part in ("firefox", "chrom", "soffice", "libre")):
            continue
        stack = [app]
        while stack:
            node = stack.pop()
            try:
                role = node.get_role()
            except Exception:                 # noqa: BLE001
                continue
            if role in (Atspi.Role.DOCUMENT_WEB, Atspi.Role.DOCUMENT_FRAME,
                        Atspi.Role.DOCUMENT_TEXT):
                return name, node
            try:
                for child in range(min(node.get_child_count(), 64)):
                    kid = node.get_child_at_index(child)
                    if kid is not None:
                        stack.append(kid)
            except Exception:                 # noqa: BLE001
                continue
    return None, None

app_name, document = focused_document()
if document is None:
    print("        no document found in Firefox, Chromium or LibreOffice")
    sys.exit(0)

collection = document.get_collection_iface()
if collection is None:
    print("        %s: the document has NO Collection interface" % app_name)
    sys.exit(0)

# MATCH_ALL over an EMPTY set, not MATCH_INVALID: "has all of no states" is
# vacuously true and therefore the same criterion, but MATCH_INVALID matches
# NOTHING in a real browser (FAZA6.md N89). This probe had that defect too,
# which is why step 3 used to print two empty lists and still say OK.
states = Atspi.StateSet()
rule = Atspi.MatchRule.new(states, Atspi.CollectionMatchType.ALL,
                           {}, Atspi.CollectionMatchType.ALL,
                           [Atspi.Role.HEADING], Atspi.CollectionMatchType.ANY,
                           [], Atspi.CollectionMatchType.ALL,
                           False)

def names(order):
    try:
        # The last argument is `traverse`. FALSE searches only the document
        # root's immediate children, which is never where the headings are
        # (FAZA6.md N88).
        found = collection.get_matches(rule, order, 10, True)
    except Exception as exc:                  # noqa: BLE001
        return ["<error: %s>" % exc]
    out = []
    for node in found or []:
        try:
            out.append((node.get_name() or "")[:28])
        except Exception:                     # noqa: BLE001
            out.append("?")
    return out

canonical = names(Atspi.CollectionSortOrder.CANONICAL)
flow = names(Atspi.CollectionSortOrder.FLOW)
print("        %s" % app_name)
print("        CANONICAL: %s" % canonical)
print("        FLOW:      %s" % flow)
if not canonical and not flow:
    print("        neither order answered: this page has no headings, so there is")
    print("        nothing to compare -- open a content page and run again")
    sys.exit(0)
if not flow:
    print("        FLOW answered NOTHING while CANONICAL answered %d." % len(canonical))
    print("        This browser does not implement FLOW in GetMatches (N90). ALUS")
    print("        falls back to tree order for the elements list, so quick")
    print("        navigation still works -- but reading order is NOT what this")
    print("        browser gives, and a floated sidebar will be out of order.")
    sys.exit(3)
if flow == canonical:
    print("        FLOW == CANONICAL here (this page may simply have no floats)")
else:
    print("        FLOW differs from CANONICAL: reading order is really implemented")
PY
case $? in
  0) ok "measured (read the two orders above)" ;;
  3) warn "FLOW is not implemented in GetMatches here (N90); ALUS falls back" ;;
  *) warn "the FLOW/CANONICAL probe could not run" ;;
esac

say "Step 4 — the exit criterion itself"
if meson test -C build --suite browsemode >/dev/null 2>&1; then
  ok "meson test -C build --suite browsemode"
else
  bad "meson test -C build --suite browsemode"
fi

if [[ "${1:-}" == "--manual" ]]; then
  cat <<'PLAN'

== The manual test — the only thing a machine cannot measure ==

What the machine has measured: the field stream is balanced, the match rules
are right, the leaves cross, the selection anchor does not jump, nothing
leaks. What it CANNOT measure is WHAT THE USER HEARS. That is this plan.

Start ALUS (stop Orca first, or it holds the keyboard -- N8):

    ./build/src/alus-daemon --log-level info --log-file ~/alus.log

--- A Wikipedia article, in Firefox AND in Chromium (do both) ---

  alus+space      browse <-> focus mode.  Expect "Browse mode" / "Focus mode".
  downArrow       line by line down the article.
                  EXPECT: the article's text. NOT "object replacement
                  character" and NOT silence -- that would be N47 unresolved.
  upArrow         back up. The same lines, in reverse.
  h / shift+h     headings forwards and back. Each says its level.
  1 .. 9          headings by level. "2" on a page with no h2 says
                  "no next heading at level 2" -- that is correct, not a bug.
  k / shift+k     links.  u / v  unvisited / visited.
  t l i b f e x r c g q s d o m p a w n   the rest of §9.2's table.
                  Every one either moves or says "no next <thing>".
  enter           activate the link under the caret.
  tab             into a search box. EXPECT "Focus mode" BY ITSELF (T6.8),
                  and then typing "h" types an h -- it does NOT jump to a
                  heading (T6.9).
  escape          back to browse mode.
  alus+f7         the elements list. For now it SAYS the counts
                  ("12 headings, 143 links"); the dialog is phase 9.
  alus+downArrow  say all from the caret, through the whole page.
                  LISTEN FOR: "list with 5 items" said ONCE on entering the
                  list, not on every item (T6.3), and "out of list" on leaving.
  alus+numpad7    cycle the review mode. "Document review" must now appear --
                  in phase 5 it never did (FAZA5.md handover point 3).

--- A table ---

  control+alt+rightArrow / leftArrow    by cell
  control+alt+downArrow  / upArrow      by row
                  EXPECT on each move: the column header, "row N", "column N",
                  then the cell's text. The header is said when it CHANGES,
                  not on every cell in the same column (T6.10).
  alus+control+alt+rightArrow           read the whole row

--- LibreOffice Writer ---

  Open a document with headings and a table.
  downArrow / upArrow     read it by line
  alus+f                  the formatting at the caret
  control+alt+arrows      the table
  h                       EXPECT "no next heading": LibreOffice exposes no
                          Collection interface, so quick navigation has
                          nothing to ask. That is honest, not broken --
                          reading the document works, jumping does not.

--- Then the log, which says what happened rather than what you think did ---

    grep -E 'tree interceptor'   ~/alus.log   # which documents were intercepted
    grep -E 'pass through'       ~/alus.log   # every mode switch, and its reason
    grep -E 'collection'         ~/alus.log   # a document with no Collection
    grep -E 'not reached walking' ~/alus.log  # a leaf walk that gave up: a bug

PLAN
fi

printf '\n'
if [[ $status -eq 0 ]]; then
  echo "phase 6: the automatic steps passed."
else
  echo "phase 6: something above FAILED -- read it, do not re-run it."
fi
exit $status
