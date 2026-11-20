#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# Builds ALUS's Serbian catalogues from upstream's, by msgid.
#
# WHY THIS IS LEGITIMATE AND WHY IT IS THE POINT. vision.md §6 says ALUS
# takes upstream's DERIVED WORKS -- the command tables, the symbol
# dictionaries, the command descriptions -- and that ALUS is GPL-2.0-or-later
# for exactly that reason. The translations are the same kind of derived work,
# and §7 is explicit about why they must be the same text: a user whose
# documentation says "режим прегледа" has to hear "режим прегледа". A fresh
# translation of 827 strings would be a second vocabulary for the same
# concepts, which is the one outcome §7 forbids.
#
# Two catalogues come out of one import:
#
#   po/sr@latin.po   upstream's Serbian verbatim. Upstream's `sr` IS Latin
#                    (`Language: sr_CS`, "Serbian (Latin)"), so this is the
#                    unmodified text and needs no judgement from this script.
#   po/sr.po         the same text transliterated to Cyrillic. Cyrillic is
#                    ALUS's `sr` and not a variant, because the rest of ALUS
#                    already decided it: data/symbols/sr.dic is Cyrillic
#                    (phase 2) and the braille table for `sr` is sr-Cyrl.ctb
#                    (phase 7, T7.1). A Latin catalogue with a Cyrillic symbol
#                    dictionary and a Cyrillic braille table would be three
#                    scripts in one screen reader.
#
# Serbian Latin -> Cyrillic is a strict, reversible mapping, which is the only
# reason transliterating a human translation is defensible here: the digraphs
# lj/nj/dž are the whole difficulty and they are handled before the single
# letters. What is deliberately NOT transliterated:
#
#   * anything inside a %s or {name} placeholder,
#   * product and format names (ALUS, NVDA, LibreOffice, HTML, ARIA, ...),
#   * a msgstr that upstream left as an ASCII abbreviation for braille
#     ("btn", "n%s"): those are cell counts, not words, and upstream's own
#     Serbian chose them.
"""Import upstream's Serbian translations into po/sr.po and po/sr@latin.po."""

from __future__ import annotations

import argparse
import datetime
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# .po reading and writing. Deliberately not `polib`: ALUS's build may not have
# it, and the subset of the format used here (msgid, msgid_plural, msgstr,
# msgstr[n], comments) is small and fully specified.
# ---------------------------------------------------------------------------

UNESCAPES = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}


def unquote(text: str) -> str:
    out = []
    i = 0
    while i < len(text):
        if text[i] == "\\" and i + 1 < len(text):
            out.append(UNESCAPES.get(text[i + 1], text[i + 1]))
            i += 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def quote(text: str) -> str:
    escaped = (text.replace("\\", "\\\\").replace('"', '\\"')
                   .replace("\t", "\\t").replace("\r", "\\r"))
    # A msgstr with embedded newlines is written as one line per newline, the
    # way gettext tools do, so `msgcat` and a human both read it unchanged.
    if "\n" not in escaped:
        return f'"{escaped}"'
    pieces = escaped.split("\n")
    lines = ['""']
    for piece in pieces[:-1]:
        lines.append(f'"{piece}\\n"')
    if pieces[-1]:
        lines.append(f'"{pieces[-1]}"')
    return "\n".join(lines)


class Entry:
    __slots__ = ("comments", "references", "flags", "msgctxt", "msgid",
                 "msgid_plural", "msgstrs")

    def __init__(self) -> None:
        self.comments: list[str] = []
        self.references: list[str] = []
        self.flags: list[str] = []
        self.msgctxt: str | None = None
        self.msgid: str = ""
        self.msgid_plural: str | None = None
        self.msgstrs: dict[int, str] = {}

    @property
    def key(self) -> tuple[str | None, str]:
        return (self.msgctxt, self.msgid)

    def translated(self) -> bool:
        return any(value for value in self.msgstrs.values())


def read_po(path: Path) -> list[Entry]:
    entries: list[Entry] = []
    entry = Entry()
    field: tuple[str, int] | None = None
    for raw in path.read_text(encoding="utf-8").split("\n"):
        line = raw.rstrip()
        if not line:
            if entry.msgid or entry.msgstrs or entry.comments:
                entries.append(entry)
            entry, field = Entry(), None
            continue
        if line.startswith("#."):
            entry.comments.append(line[2:].strip()); field = None; continue
        if line.startswith("#:"):
            entry.references.extend(line[2:].split()); field = None; continue
        if line.startswith("#,"):
            entry.flags.extend(f.strip() for f in line[2:].split(",")); field = None; continue
        if line.startswith("#"):
            field = None; continue
        if line.startswith("msgctxt "):
            entry.msgctxt = unquote(line[9:-1]); field = ("msgctxt", 0); continue
        if line.startswith("msgid_plural "):
            entry.msgid_plural = unquote(line[14:-1]); field = ("msgid_plural", 0); continue
        if line.startswith("msgid "):
            entry.msgid = unquote(line[7:-1]); field = ("msgid", 0); continue
        m = re.match(r'msgstr\[(\d+)\] "(.*)"$', line)
        if m:
            index = int(m.group(1))
            entry.msgstrs[index] = unquote(m.group(2)); field = ("msgstr", index); continue
        if line.startswith("msgstr "):
            entry.msgstrs[0] = unquote(line[8:-1]); field = ("msgstr", 0); continue
        if line.startswith('"') and field is not None:
            piece = unquote(line[1:-1])
            name, index = field
            if name == "msgid":
                entry.msgid += piece
            elif name == "msgid_plural":
                entry.msgid_plural = (entry.msgid_plural or "") + piece
            elif name == "msgctxt":
                entry.msgctxt = (entry.msgctxt or "") + piece
            else:
                entry.msgstrs[index] = entry.msgstrs.get(index, "") + piece
    if entry.msgid or entry.msgstrs or entry.comments:
        entries.append(entry)
    return entries


def write_po(path: Path, header: str, entries: list[Entry]) -> None:
    out = [header.rstrip(), ""]
    for entry in entries:
        if not entry.msgid:
            continue
        for comment in entry.comments:
            out.append(f"#. {comment}")
        for i in range(0, len(entry.references), 3):
            out.append("#: " + " ".join(entry.references[i:i + 3]))
        if entry.flags:
            out.append("#, " + ", ".join(entry.flags))
        if entry.msgctxt is not None:
            out.append(f"msgctxt {quote(entry.msgctxt)}")
        out.append(f"msgid {quote(entry.msgid)}")
        if entry.msgid_plural is not None:
            out.append(f"msgid_plural {quote(entry.msgid_plural)}")
            for index in sorted(entry.msgstrs):
                out.append(f"msgstr[{index}] {quote(entry.msgstrs[index])}")
        else:
            out.append(f"msgstr {quote(entry.msgstrs.get(0, ''))}")
        out.append("")
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Serbian Latin -> Cyrillic.
# ---------------------------------------------------------------------------

DIGRAPHS = [
    ("Lj", "Љ"), ("LJ", "Љ"), ("lj", "љ"),
    ("Nj", "Њ"), ("NJ", "Њ"), ("nj", "њ"),
    ("Dž", "Џ"), ("DŽ", "Џ"), ("dž", "џ"),
    ("Dz", "Џ"), ("dz", "џ"),
]
SINGLES = {
    "A": "А", "B": "Б", "V": "В", "G": "Г", "D": "Д", "Đ": "Ђ", "E": "Е",
    "Ž": "Ж", "Z": "З", "I": "И", "J": "Ј", "K": "К", "L": "Л", "M": "М",
    "N": "Н", "O": "О", "P": "П", "R": "Р", "S": "С", "T": "Т", "Ć": "Ћ",
    "U": "У", "F": "Ф", "H": "Х", "C": "Ц", "Č": "Ч", "Š": "Ш",
    "a": "а", "b": "б", "v": "в", "g": "г", "d": "д", "đ": "ђ", "e": "е",
    "ž": "ж", "z": "з", "i": "и", "j": "ј", "k": "к", "l": "л", "m": "м",
    "n": "н", "o": "о", "p": "п", "r": "р", "s": "с", "t": "т", "ć": "ћ",
    "u": "у", "f": "ф", "h": "х", "c": "ц", "č": "ч", "š": "ш",
}

# Spans that must survive untouched. Placeholders first, then the names that
# are spelled the same in every script because they are products or formats.
KEEP = re.compile(
    r"%(?:\([^)]*\))?[-+ #0]*\d*(?:\.\d+)?[hlLqjzt]*[diouxXeEfgGcrsab%]"
    r"|\{[^}]*\}"
    r"|&[A-Za-z]+;"
    # Web and e-mail addresses are never words (the About and Donate dialogs).
    r"|https?://[^\s\"]+"
    r"|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}"
    r"|\b(?:Google Pay|PayPal|Google|GitHub|GNU|ALUS|NVDA|Orca|GNOME|KDE|Linux|Windows|Wayland|X11|GTK|Qt|"
    r"LibreOffice|Writer|Calc|Impress|Firefox|Chromium|Chrome|Electron|"
    r"Nautilus|VS\s?Code|HTML|XHTML|XML|CSS|MathML|ARIA|URL|URI|PDF|CSV|INI|"
    r"UTF-8|Unicode|ASCII|D-Bus|AT-SPI|SSIP|BrlAPI|liblouis|libatspi|"
    r"espeak-ng|eSpeak|RHVoice|rhvoice|Piper|PipeWire|PulseAudio|MathCAT|"
    r"speech-dispatcher|speechd|GUI|ATK|UNO|VCL|Gecko|Blink|ICU|UPower|"
    r"Braille|BRLTTY|brltty|OCR|API|ABI|CPU|RAM|ID|IDs)\b"
)

# The msgids whose translation is an ABBREVIATION and not a word: the labels
# upstream chose for the braille display, where the unit is the CELL and a
# whole word would not fit. Those are kept exactly as upstream has them --
# transliterating "btn" to Cyrillic would be nonsense on a braille display and
# would not even be shorter.
#
# AN EXPLICIT LIST, AND NOT A TEST ON THE SHAPE OF THE TEXT. It was a shape
# test -- "lower case ASCII, at most seven letters, no space" -- and that
# cannot tell "cmnt" from "meni", because both are four lower-case letters.
# The result was 76 ordinary Serbian words left in LATIN inside the CYRILLIC
# catalogue, among them the most frequently spoken words a screen reader has:
# prozor, dugme, meni, lista, naslov, tabela, slika, link (docs/history/phase-9.md
# N106). The list is verified against the .pot on every run, so a renamed or
# dropped label fails loudly instead of quietly becoming a word again.
ABBREVIATION_MSGIDS = frozenset({
    # src/braille/labels.cpp -- the role, state and position labels
    "***", "+", "-", "...", "app", "art", "bnnr", "bqt", "bsyind", "btn",
    "cap", "cbo", "chk", "cinf", "clk", "cmpl", "del", "dlg", "doc", "drbtn",
    "edt", "embedded", "enote", "fig", "fnote", "frml", "gra", "grp",
    "hlght", "hlp", "ins", "invalid", "ldesc", "lmk", "lnk", "lst", "main",
    "mln", "mnu", "mnubar", "mnubtn", "mnuitem", "msel", "navi", "nsel",
    "pnl", "prgbar", "pwdedt", "rbtn", "req", "rgn", "ro", "scrlbar", "sect",
    "sel", "sggstn", "smp", "sorted asc", "sorted desc", "splbtn", "spnbtn",
    "srch", "stbar", "submnu", "swtch", "tabctl", "tbl", "term", "tgbtn",
    "tlbar", "tltip", "tv", "tvbtn", "tvitem", "wnd",
    "\u2824\u2824\u2824\u2824\u2824",
    "\u288e\u28c0\u2871", "\u288e\u28f8\u2871", "\u288e\u28ff\u2871",
    "\u28cf\u28c0\u28f9", "\u28cf\u28f8\u28f9", "\u28cf\u28ff\u28f9",
    # src/braille/properties.cpp -- the same thing for properties. The file
    # also holds WORDS ("details"), which is why this is a list of msgids and
    # not of files.
    "bkmk", "c%s", "cmnt", "h%s", "mslst", "r%s", "vlnk",
})


def to_cyrillic(text: str) -> str:
    out: list[str] = []
    position = 0
    for keep in KEEP.finditer(text):
        out.append(_map_span(text[position:keep.start()]))
        out.append(keep.group(0))
        position = keep.end()
    out.append(_map_span(text[position:]))
    return "".join(out)


# Serbian Latin (gajica) has no q, w, x or y. So a word containing one is NOT
# Serbian and must survive untouched: transliterating it letter by letter
# leaves the foreign letters behind and produces a word in neither script.
# That is how "windows" became "wиндоwс" and "Waylandu" became "Wаyланду" in
# the shipped Cyrillic catalogue -- names KEEP does not list, or lists only in
# their uninflected form (docs/history/phase-9.md N106). The rule costs nothing and
# removes the whole class.
FOREIGN_LETTERS = frozenset("qwxyQWXY")


def _map_span(text: str) -> str:
    out: list[str] = []
    for piece in re.split(r"([A-Za-zÀ-ž]+(?:[-'][A-Za-zÀ-ž]+)*)", text):
        if piece and piece[0].isalpha() and FOREIGN_LETTERS & set(piece):
            out.append(piece)
            continue
        out.append(_map_serbian(piece))
    return "".join(out)


def _map_serbian(text: str) -> str:
    for latin, cyrillic in DIGRAPHS:
        text = text.replace(latin, cyrillic)
    return "".join(SINGLES.get(ch, ch) for ch in text)


# ---------------------------------------------------------------------------
# Product-name remapping.
#
# tools/extract-commands.py rewrites the product name in every inherited
# command description, so ALUS's msgid is "Shows ALUS's braille settings" while
# upstream's is "Shows NVDA's braille settings" -- 35 strings that are the same
# sentence and would otherwise all be left untranslated. The lookup is done
# with the upstream spelling and the PRODUCT NAME IS REWRITTEN BACK in the
# result, so nothing ALUS ships or speaks carries the other product's name.
# ---------------------------------------------------------------------------

UPSTREAM_NAME = "NVDA"
OUR_NAME = "ALUS"


def as_upstream(msgid: str) -> str:
    return msgid.replace(OUR_NAME, UPSTREAM_NAME)


def as_ours(msgstr: str) -> str:
    # Also the genitive upstream's own translations use ("NVDA-a", "NVDA-ov").
    return msgstr.replace(UPSTREAM_NAME, OUR_NAME)

HEADER = """# SPDX-License-Identifier: GPL-2.0-or-later
# ALUS {name} translation.
#
# GENERATED by tools/import-nvda-po.py from
#   {source}
# at upstream commit {commit}. Do not edit by hand: run the importer, or add
# the string to po/{overrides} and run it again.
#
# {provenance}
#
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
# The msgstr values are derived from NonVisual Desktop Access (NVDA),
# Copyright (C) NV Access Limited and contributors, GPL-2.0-or-later.
msgid ""
msgstr ""
"Project-Id-Version: alus {version}\\n"
"Report-Msgid-Bugs-To: daremc86@gmail.com\\n"
"POT-Creation-Date: {date}\\n"
"PO-Revision-Date: {date}\\n"
"Last-Translator: tools/import-nvda-po.py\\n"
"Language-Team: {team}\\n"
"Language: {language}\\n"
"MIME-Version: 1.0\\n"
"Content-Type: text/plain; charset=UTF-8\\n"
"Content-Transfer-Encoding: 8bit\\n"
"Plural-Forms: nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);\\n"
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pot", default="po/alus.pot", type=Path)
    parser.add_argument("--source",
                        default="reference/nvda/source/locale/sr/LC_MESSAGES/nvda.po",
                        type=Path, help="upstream's Serbian catalogue")
    parser.add_argument("--commit", default="84756356c")
    parser.add_argument("--out-dir", default="po", type=Path)
    parser.add_argument("--version", default="2026.1.0")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    if not args.pot.exists():
        print(f"no template: {args.pot} (run: meson compile -C build alus-pot)",
              file=sys.stderr)
        return 2
    if not args.source.exists():
        print(f"no upstream catalogue: {args.source}", file=sys.stderr)
        return 2

    template = read_po(args.pot)

    # The abbreviation list is only safe while it still names strings that
    # exist. A label upstream renamed would silently drop off the list and be
    # transliterated into nonsense on the braille display, so the mismatch is
    # reported here rather than discovered by a braille user.
    present = {e.msgid for e in template}
    stale = sorted(ABBREVIATION_MSGIDS - present)
    if stale:
        print("import-nvda-po: ABBREVIATION_MSGIDS names "
              + str(len(stale))
              + " string(s) that are not in the template any more: "
              + ", ".join(repr(s) for s in stale[:8])
              + ("…" if len(stale) > 8 else "")
              + " — a braille label was renamed or removed; update the list in "
                "tools/import-nvda-po.py", file=sys.stderr)
        return 1

    upstream = {e.key: e for e in read_po(args.source) if e.translated()}
    date = datetime.datetime.now(datetime.UTC).strftime("%Y-%m-%d %H:%M+0000")

    # ONE overrides file, written in Serbian LATIN, for every string upstream
    # does not have: the ALUS-only messages, and the ones whose English ALUS
    # spells differently. Latin and not Cyrillic so that there is a single
    # script to hand-write in -- the same transliteration that turns upstream's
    # Latin into ALUS's Cyrillic turns these too, so a string is translated
    # once and appears correctly in both catalogues.
    overrides_name = "overrides.po"
    overrides_path = args.out_dir / overrides_name
    overrides = ({e.key: e for e in read_po(overrides_path) if e.translated()}
                 if overrides_path.exists() else {})

    for language, team, cyrillic in (
        ("sr", "Serbian (Cyrillic)", True),
        ("sr@latin", "Serbian (Latin)", False),
    ):

        entries: list[Entry] = []
        from_upstream = from_overrides = untranslated = renamed_count = 0
        for source in template:
            if not source.msgid:
                continue
            entry = Entry()
            entry.comments = list(source.comments)
            entry.references = list(source.references)
            entry.msgctxt = source.msgctxt
            entry.msgid = source.msgid
            entry.msgid_plural = source.msgid_plural
            forms = 3 if source.msgid_plural is not None else 1

            found = overrides.get(entry.key)
            which = "overrides"
            renamed = False
            if found is None:
                found = upstream.get(entry.key)
                which = "upstream"
            if found is None and OUR_NAME in entry.msgid:
                # The same sentence with the product name rewritten.
                found = upstream.get((entry.msgctxt, as_upstream(entry.msgid)))
                which, renamed = "upstream", found is not None
            if found is None:
                # Not in upstream: an ALUS-only string, or one whose English
                # ALUS spells differently (the %s patterns upstream writes with
                # {named} placeholders). Left empty on purpose -- an empty
                # msgstr speaks correct English, a guessed one speaks nonsense.
                entry.msgstrs = {i: "" for i in range(forms)}
                untranslated += 1
                entries.append(entry)
                continue

            for index in range(forms):
                value = found.msgstrs.get(index, found.msgstrs.get(0, ""))
                if renamed and value:
                    value = as_ours(value)
                if cyrillic and value and entry.msgid not in ABBREVIATION_MSGIDS:
                    value = to_cyrillic(value)
                entry.msgstrs[index] = value
            if which == "overrides":
                from_overrides += 1
            else:
                from_upstream += 1
                renamed_count += 1 if renamed else 0
            entries.append(entry)

        header = HEADER.format(
            name=team, source=args.source.as_posix(), commit=args.commit,
            overrides=overrides_name, version=args.version, date=date,
            team=team, language="sr_RS" if language == "sr" else "sr_RS@latin",
            provenance=("Transliterated from upstream's Latin Serbian; the mapping is "
                        "strict and\n# reversible (see to_cyrillic). Braille abbreviations "
                        "are kept as upstream\n# has them, because they are cell counts "
                        "rather than words."
                        if cyrillic else
                        "Upstream's Serbian verbatim, matched by msgid."))
        out = args.out_dir / f"{language}.po"
        write_po(out, header, entries)
        total = len(entries)
        if not args.quiet:
            covered = from_upstream + from_overrides
            print(f"{out}: {covered}/{total} translated "
                  f"({100 * covered // max(total, 1)}%) "
                  f"— {from_upstream} from upstream, {from_overrides} from "
                  f"{overrides_name} ({renamed_count} matched after rewriting "
                  f"the product name), {untranslated} left to translate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
