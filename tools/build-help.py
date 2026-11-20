#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# Builds the HTML help the ALUS menu opens (Help → User guide, What's new).
#
#   build-help.py SOURCE.md OUTPUT.html [--cyrillic] [--lang CODE]
#   build-help.py --check BUILDDIR/docs/help
#
# The sources are docs/help/<lang>/*.md. Serbian Cyrillic is not written by
# hand: it is the Serbian Latin source transliterated with the same rules as
# the program's own catalogue (tools/import-nvda-po.py, to_cyrillic), so the
# two scripts never drift apart. Inline code -- key names such as `ALUS+Q` and
# commands -- and fenced blocks stay as written in both scripts.
#
# A deliberately small Markdown: headings, paragraphs, lists (one level),
# tables, fenced code, inline code, **bold**, *emphasis* and [links](url).
# No dependency beyond the standard library: the build must not need one.

import argparse
import html
import importlib.util
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load_transliteration():
    spec = importlib.util.spec_from_file_location("import_nvda_po", HERE / "import-nvda-po.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.to_cyrillic


def slug(text: str) -> str:
    """GitHub's heading anchors: lower case, spaces to dashes, punctuation out."""
    text = re.sub(r"`([^`]*)`", r"\1", text).lower().strip()
    text = re.sub(r"[^\w\- ]", "", text, flags=re.UNICODE)
    return re.sub(r" ", "-", text)


CODE = re.compile(r"`([^`]+)`")

# The Serbian words of key names, written in the source's Latin script.
KEY_WORDS = re.compile(r"\b(?:Strelica|gore|dole|levo|desno|Numerički|Numerička|minus|plus|"
                       r"kosa|crta|zvezdica)\b")


def transliterate_line(line: str, to_cyrillic) -> str:
    """Cyrillic for the prose, inline code and link targets left alone."""
    out = []
    last = 0
    for match in re.finditer(r"`[^`]+`|\]\([^)]*\)", line):
        out.append(to_cyrillic(line[last:match.start()]))
        # Anchors inside the page are mapped afterwards, from the headings
        # themselves (main): transliterating the anchor would turn "alus"
        # into Cyrillic while the heading keeps "ALUS" in Latin.
        piece = match.group(0)
        if piece.startswith("`"):
            # Key names stay as the keyboard prints them (Insert, Control),
            # but the Serbian words in them read in the page's script.
            piece = KEY_WORDS.sub(lambda m: to_cyrillic(m.group(0)), piece)
        out.append(piece)
        last = match.end()
    out.append(to_cyrillic(line[last:]))
    return "".join(out)


def inline(text: str) -> str:
    """Escape, then mark up code, bold, emphasis and links."""
    parts = []
    last = 0
    for match in CODE.finditer(text):
        parts.append(("text", text[last:match.start()]))
        parts.append(("code", match.group(1)))
        last = match.end()
    parts.append(("text", text[last:]))
    out = []
    for kind, value in parts:
        if kind == "code":
            out.append("<kbd>" + html.escape(value) + "</kbd>")
            continue
        value = html.escape(value, quote=False)
        value = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", value)
        value = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", value)
        value = re.sub(r"\[([^\]]+)\]\(([^)]+)\)",
                       lambda m: '<a href="%s">%s</a>' % (html.escape(m.group(2), quote=True),
                                                         m.group(1)), value)
        out.append(value)
    return "".join(out)


def convert(lines, title: str, lang: str) -> str:
    body = []
    paragraph = []
    list_kind = None
    in_code = False
    table = []

    def flush_paragraph():
        if paragraph:
            body.append("<p>" + inline(" ".join(paragraph)) + "</p>")
            paragraph.clear()

    def flush_list():
        nonlocal list_kind
        if list_kind:
            body.append("</%s>" % list_kind)
            list_kind = None

    def flush_table():
        if not table:
            return
        rows = [[cell.strip() for cell in row.strip().strip("|").split("|")] for row in table]
        body.append("<table>")
        header, rest = rows[0], rows[2:] if len(rows) > 1 else []
        body.append("<thead><tr>" + "".join("<th scope=\"col\">%s</th>" % inline(c) for c in header)
                    + "</tr></thead><tbody>")
        for row in rest:
            body.append("<tr>" + "".join("<td>%s</td>" % inline(c) for c in row) + "</tr>")
        body.append("</tbody></table>")
        table.clear()

    for raw in lines:
        line = raw.rstrip("\n")
        if line.startswith("```"):
            if in_code:
                body.append("</code></pre>")
                in_code = False
            else:
                flush_paragraph()
                flush_list()
                flush_table()
                body.append("<pre><code>")
                in_code = True
            continue
        if in_code:
            body.append(html.escape(line))
            continue
        stripped = line.strip()
        if stripped.startswith("|"):
            flush_paragraph()
            flush_list()
            table.append(stripped)
            continue
        flush_table()
        if not stripped:
            flush_paragraph()
            flush_list()
            continue
        heading = re.match(r"^(#{1,6})\s+(.*)$", stripped)
        if heading:
            flush_paragraph()
            flush_list()
            level = len(heading.group(1))
            text = heading.group(2)
            body.append('<h%d id="%s">%s</h%d>' % (level, slug(text), inline(text), level))
            continue
        item = re.match(r"^(?:[-*]|(\d+)\.)\s+(.*)$", stripped)
        if item and not line.startswith("    "):
            flush_paragraph()
            kind = "ol" if item.group(1) else "ul"
            if list_kind != kind:
                flush_list()
                body.append("<%s>" % kind)
                list_kind = kind
            body.append("<li>" + inline(item.group(2)) + "</li>")
            continue
        if list_kind and line.startswith("  "):
            # A continuation of the list item before it.
            body[-1] = body[-1][:-len("</li>")] + " " + inline(stripped) + "</li>"
            continue
        flush_list()
        paragraph.append(stripped)
    flush_paragraph()
    flush_list()
    flush_table()

    return (
        "<!doctype html>\n"
        '<html lang="%s">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
        "<title>%s</title>\n"
        "<style>\n"
        "body{font-family:sans-serif;max-width:46em;margin:0 auto;padding:1em;line-height:1.55}\n"
        "table{border-collapse:collapse;width:100%%;margin:1em 0}\n"
        "th,td{border:1px solid #888;padding:.35em .6em;text-align:left;vertical-align:top}\n"
        "kbd{font-family:monospace;background:#eee;border-radius:3px;padding:0 .2em}\n"
        "pre{background:#f4f4f4;padding:.6em;overflow-x:auto}\n"
        "@media (prefers-color-scheme: dark){body{background:#1b1b1b;color:#eee}"
        "kbd,pre{background:#333}a{color:#8ab4f8}}\n"
        "</style>\n</head>\n<body>\n%s\n</body>\n</html>\n"
    ) % (html.escape(lang), html.escape(title), "\n".join(body))


def check(directory: Path) -> int:
    """--check DIR: every page of every language exists, and every link inside
    a page leads to one of its headings. Run by `meson test` (help-links)."""
    failures = []
    for lang in ("en", "sr", "sr@latin"):
        for page in ("user-guide.html", "whats-new.html"):
            path = directory / lang / page
            if not path.exists():
                failures.append("missing: %s" % path)
                continue
            text = path.read_text(encoding="utf-8")
            ids = set(re.findall(r'id="([^"]+)"', text))
            for target in re.findall(r'href="#([^"]+)"', text):
                if target not in ids:
                    failures.append("%s: #%s leads nowhere" % (path, target))
    for failure in failures:
        print(failure)
    return 1 if failures else 0


def main() -> int:
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        return check(Path(sys.argv[2]))
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cyrillic", action="store_true",
                        help="transliterate a Serbian Latin source to Cyrillic")
    parser.add_argument("--lang", default="en")
    args = parser.parse_args()

    lines = args.source.read_text(encoding="utf-8").splitlines(keepends=True)
    if args.cyrillic:
        to_cyrillic = load_transliteration()
        converted = []
        in_code = False
        for line in lines:
            if line.startswith("```"):
                in_code = not in_code
                converted.append(line)
            elif in_code:
                converted.append(line)
            else:
                converted.append(transliterate_line(line, to_cyrillic))
        anchors = {}
        for before, after in zip(lines, converted):
            if re.match(r"^#{1,6}\s", before):
                anchors[slug(re.sub(r"^#+\s*", "", before).strip())] = \
                    slug(re.sub(r"^#+\s*", "", after).strip())
        lines = [re.sub(r"\]\(#([^)]+)\)",
                        lambda m: "](#%s)" % anchors.get(m.group(1), m.group(1)), line)
                 for line in converted]

    title = next((re.sub(r"^#+\s*", "", line).strip() for line in lines if line.startswith("# ")),
                 "ALUS")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(convert(lines, title, args.lang), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
