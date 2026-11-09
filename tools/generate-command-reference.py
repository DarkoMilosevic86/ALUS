#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# Generates the command reference from data/scripts.csv.
#
# WHY GENERATED. 00-VIZIJA.md Z1 says the command set is checked by comparing
# "the generated command table against the upstream user guide", and §5.4 makes
# tools/extract-commands.py the source of truth for ~390 commands. A reference
# typed by hand would be a fourth copy of that truth -- after the extractor,
# the CSV and the ini -- and the first one to go stale, with no test able to
# tell. So the reference IS the CSV, formatted, and T9.5 asserts the two agree.
#
# Two outputs, because they answer different questions:
#
#   docs/commands.md    every command, grouped by category, with its keys in
#                       both layouts. What a user reads.
#   docs/commands.csv   the same rows, stable column order, for diffing one
#                       release against the next.
"""Write the ALUS command reference from the generated command table."""

from __future__ import annotations

import argparse
import csv
import datetime
import sys
from collections import defaultdict
from pathlib import Path

# The ALUS key is spelled `alus` in a gesture identifier; the reference shows
# it the way a user says it.
KEY_LABEL = "ALUS"


def pretty_gesture(identifier: str) -> str:
    """`kb:alus+shift+t` -> `ALUS+shift+t`; `kb(laptop):alus+h` -> `ALUS+h`."""
    text = identifier
    if text.startswith("kb("):
        text = text[text.index(")") + 2:]
    elif text.startswith("kb:"):
        text = text[3:]
    parts = text.split("+")
    return "+".join(KEY_LABEL if part.lower() == "alus" else part for part in parts)


def gestures_of(row: dict[str, str]) -> tuple[str, str]:
    """Returns (desktop, laptop) as display strings.

    A layout-agnostic gesture applies to both, which is why it is added to
    each rather than shown in a third column: a user reads the column for the
    keyboard they have.
    """
    agnostic = row["gestures"].split()
    desktop = agnostic + row["gestures_desktop"].split()
    laptop = agnostic + row["gestures_laptop"].split()
    return (", ".join(pretty_gesture(g) for g in desktop),
            ", ".join(pretty_gesture(g) for g in laptop))


def escape(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", " ")


# data/commands-status.ini -> {script: (state, reason)}; see
# include/alus/script/command_status.hpp. NOT generated, unlike everything
# else this script reads.
def read_status(path: Path) -> dict[str, tuple[str, str]]:
    if not path.exists():
        print(f"warning: no {path}; the reference will promise commands that "
              "are not in this version", file=sys.stderr)
        return {}
    out: dict[str, tuple[str, str]] = {}
    section = ""
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("["):
            section = line.strip("[]")
            continue
        if "=" not in line or section not in ("announce", "platform"):
            continue
        name, _, reason = line.partition("=")
        out[name.strip()] = (section, reason.strip())
    return out


# What the Opis column says before the description, for a command that is not
# in this version. The point is that the reference STOPS PROMISING it.
def status_prefix(state: str) -> str:
    if state == "announce":
        return "**НЕМА У ОВОЈ ВЕРЗИЈИ** (тастер то и каже кад се притисне) — "
    if state == "platform":
        return ("**НЕМА НА WAYLAND-У** (тастер остаје апликацији; ALUS не може "
                "да га пошаље назад, N18) — ")
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scripts", default="data/scripts.csv", type=Path)
    parser.add_argument("--status", default="data/commands-status.ini", type=Path)
    parser.add_argument("--out-dir", default="docs", type=Path)
    parser.add_argument("--check", action="store_true",
                        help="exit non-zero if the reference is out of date")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    if not args.scripts.exists():
        print(f"no command table: {args.scripts} "
              "(run: python3 tools/extract-commands.py)", file=sys.stderr)
        return 2

    with args.scripts.open(encoding="utf-8", newline="") as handle:
        rows = [r for r in csv.DictReader(handle) if r["scope"] == "v1"]

    # Which commands ALUS has not written yet. scripts.csv cannot say: it is
    # generated from upstream's source and knows only what a command IS.
    # Without this the reference promises every one of them, which is exactly
    # how `quit` came to be documented, key-bound and silent (FAZA9.md N99).
    status = read_status(args.status)

    # A command with no description is not documented, for the same reason the
    # gesture dialog does not list it (T9.3): there is nothing to say about it.
    documented = [r for r in rows if r["description"]]
    by_category: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in documented:
        by_category[row["category"] or "Miscellaneous"].append(row)

    bound = sum(1 for r in documented if any(
        (r["gestures"], r["gestures_desktop"], r["gestures_laptop"])))

    lines = [
        "# ALUS — referenca komandi",
        "",
        "> **GENERISANO** iz `data/scripts.csv` sa "
        "`tools/generate-command-reference.py`.",
        "> Ne menjati rukom: izmena se gubi pri sledećem generisanju, a test "
        "T9.5",
        "> (`meson test -C build --suite gui`) pada kad se ovaj fajl i tabela "
        "komandi raziđu.",
        "",
        "`ALUS` je ALUS taster: Insert, numpad Insert i/ili Caps Lock, po "
        "podešavanju",
        "u `[keyboard]`. Komanda bez tastera je u tabeli ali nije vezana — "
        "vezuje se",
        "u dijalogu gestova (`alus+f6`) ili u `~/.config/alus/gestures.ini`.",
        "",
        f"Komandi: **{len(documented)}** u {len(by_category)} kategorija, od "
        f"toga **{bound}** vezanih.",
        "",
        f"Od toga **{sum(1 for r in documented if r['script'] in status)}** "
        "nije u ovoj verziji i tako je i označeno u koloni *Opis*: "
        f"**{sum(1 for r in documented if status.get(r['script'], ('',))[0] == 'announce')}** "
        "ima taster koji **izgovori** da komande nema, a "
        f"**{sum(1 for r in documented if status.get(r['script'], ('',))[0] == 'platform')}** "
        "namerno nije vezano jer taster pripada aplikaciji (N18). Izvor je "
        "`data/commands-status.ini`.",
        "",
    ]

    for category in sorted(by_category):
        lines.append(f"## {category}")
        lines.append("")
        lines.append("| Komanda | Desktop | Laptop | Opis |")
        lines.append("|---|---|---|---|")
        for row in sorted(by_category[category], key=lambda r: r["description"]):
            desktop, laptop = gestures_of(row)
            prefix = status_prefix(status.get(row["script"], ("", ""))[0])
            lines.append(
                f"| `{row['script']}` | {escape(desktop) or '—'} | "
                f"{escape(laptop) or '—'} | {prefix}{escape(row['description'])} |")
        lines.append("")

    lines += [
        "---",
        "",
        "## Odakle ovo dolazi",
        "",
        "Imena skriptova, opisi i podrazumevani tasteri su preuzeti iz NVDA "
        "izvornog",
        "koda sa `tools/extract-commands.py` (vidi `01-ARHITEKTURA.md` §5.4 i "
        "`00-VIZIJA.md` §6).",
        "Zato su komande **iste** kao u NVDA: korisnik koji prelazi sa Windows-a "
        "ne uči",
        "ništa novo (Z1).",
        "",
        "Opisi se prevode preko gettext-a; na srpskom ih prikazuje",
        "`alus-cli --language sr list-commands`.",
        "",
        f"Generisano {datetime.date.today().isoformat()}.",
        "",
    ]
    markdown = "\n".join(lines)

    # The CSV side: the same rows, fewer columns, for diffing releases.
    csv_lines = ["category,script,location,desktop,laptop,description"]
    for category in sorted(by_category):
        for row in sorted(by_category[category], key=lambda r: r["script"]):
            desktop, laptop = gestures_of(row)
            fields = [category, row["script"], f"{row['module']}.{row['class']}",
                      desktop, laptop, row["description"]]
            csv_lines.append(",".join(
                '"' + f.replace('"', '""') + '"' if any(c in f for c in ',"\n') else f
                for f in fields))
    csv_text = "\n".join(csv_lines) + "\n"

    markdown_path = args.out_dir / "commands.md"
    csv_path = args.out_dir / "commands.csv"

    if args.check:
        stale = []
        for path, wanted in ((markdown_path, markdown), (csv_path, csv_text)):
            current = path.read_text(encoding="utf-8") if path.exists() else ""
            # The generation DATE is in the markdown, so comparing it whole
            # would fail every day. The rows are what must match.
            if path.suffix == ".md":
                current = "\n".join(l for l in current.splitlines()
                                    if not l.startswith("Generisano "))
                wanted_ = "\n".join(l for l in wanted.splitlines()
                                    if not l.startswith("Generisano "))
            else:
                wanted_ = wanted
            if current.strip() != wanted_.strip():
                stale.append(path)
        if stale:
            for path in stale:
                print(f"out of date: {path}", file=sys.stderr)
            print("run: python3 tools/generate-command-reference.py", file=sys.stderr)
            return 1
        if not args.quiet:
            print(f"{markdown_path} and {csv_path}: up to date "
                  f"({len(documented)} commands)")
        return 0

    args.out_dir.mkdir(parents=True, exist_ok=True)
    markdown_path.write_text(markdown, encoding="utf-8")
    csv_path.write_text(csv_text, encoding="utf-8")
    if not args.quiet:
        print(f"{markdown_path}: {len(documented)} commands in "
              f"{len(by_category)} categories ({bound} bound)")
        print(f"{csv_path}: the same rows, for diffing releases")
        hidden = len(rows) - len(documented)
        print(f"{hidden} commands have no description and are not documented, "
              "as upstream does not document them either")
    return 0


if __name__ == "__main__":
    sys.exit(main())
