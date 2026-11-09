#!/usr/bin/env python3
"""
S3 — Quick-nav preko AT-SPI Collection (Faza 0, 02-FAZE.md).

Pitanje: da li je `Collection.GetMatchesFrom` dovoljno brz da browse mode
(Faza 6) ne mora da drži lokalni keš drveta — rizik R3 iz 03-RIZICI.md.

Izlazni kriterijum: p95 latencija jednog quick-nav skoka < 20 ms u Firefox-u
i u Chrome-u, nad stvarnim Wikipedia člankom.

Spike kod — baca se posle Faze 0. Živi samo spikes/RESULTS.md.

Upotreba:
    python3 spikes/s3-collection.py --browser chrome  --doc-match Тесла
    python3 spikes/s3-collection.py --browser Firefox --doc-match Тесла
"""

import argparse
import json
import statistics
import sys
import time

import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib  # noqa: E402

CLOCK = time.perf_counter
R = Atspi.Role
MT = Atspi.CollectionMatchType
SO = Atspi.CollectionSortOrder
TT = Atspi.CollectionTreeTraversalType

DOC_ROLES = {R.DOCUMENT_WEB, R.DOCUMENT_FRAME, R.DOCUMENT_TEXT}

# NVDA quick-nav tipke (browseMode.py) → AT-SPI role
NAV_RULES = [
    ("h   heading",         [R.HEADING],  None),
    ("k   link",            [R.LINK],     None),
    ("t   table",           [R.TABLE],    None),
    ("1   heading lvl 1",   [R.HEADING],  {"level": "1"}),
    ("2   heading lvl 2",   [R.HEADING],  {"level": "2"}),
    ("l   list",            [R.LIST],     None),
    ("i   list item",       [R.LIST_ITEM], None),
    ("f   form field",      [R.ENTRY, R.PASSWORD_TEXT, R.CHECK_BOX,
                             R.RADIO_BUTTON, R.COMBO_BOX, R.PUSH_BUTTON], None),
    ("g   graphic",         [R.IMAGE],    None),
    ("b   button",          [R.PUSH_BUTTON], None),
]

BUDGET_MS = 20.0


def log(*a):
    print(*a, flush=True)


def pct(values, p):
    if not values:
        return float("nan")
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(round(p / 100.0 * len(s) + 0.5)) - 1))
    return s[k]


def stats(samples):
    return {
        "n": len(samples),
        "p50": pct(samples, 50), "p95": pct(samples, 95), "p99": pct(samples, 99),
        "min": min(samples), "max": max(samples), "mean": statistics.fmean(samples),
    }


def row(name, samples, budget=None):
    if not samples:
        log(f"  {name:<22} — nema uzoraka")
        return None
    s = stats(samples)
    v = ""
    if budget is not None:
        v = "  PASS" if s["p95"] < budget else "  FAIL"
    log(f"  {name:<22} n={s['n']:<4} p50={s['p50']:7.2f}  p95={s['p95']:7.2f}  "
        f"p99={s['p99']:7.2f}  max={s['max']:7.2f} ms{v}")
    s["name"] = name
    return s


# --- MatchRule ---------------------------------------------------------------

def make_rule(roles=None, attributes=None):
    """
    ZAMKA (otkrivena u ovom spike-u, vidi RESULTS.md nalaz N1):
    `interfacematchtype` MORA biti ALL. Sa ANY nad praznom listom interfejsa
    poziv uspeva, ne vraća grešku, i tiho vrati 0 rezultata — na SVAKOM
    serveru (GTK, Clutter, Gecko, Chromium). Isti obrazac važi za
    `statematchtype` nad praznim StateSet-om.
    Orca: state=ALL, attribute=ANY, role=ANY, interface=ALL (ax_collection.py).
    """
    return Atspi.MatchRule.new(
        Atspi.StateSet(), MT.ALL,          # states: prazan skup + ALL = pogodi sve
        attributes or {}, MT.ALL,          # attributes
        roles or [], MT.ANY,               # roles: bilo koja iz liste
        [], MT.ALL,                        # interfaces: prazno + ALL = pogodi sve
        False,                             # invert
    )


# --- nalaženje dokumenta -----------------------------------------------------

def find_apps(match):
    desktop = Atspi.get_desktop(0)
    out = []
    for i in range(desktop.get_child_count()):
        try:
            app = desktop.get_child_at_index(i)
            if app is not None and match.lower() in (app.get_name() or "").lower():
                out.append(app)
        except GLib.Error:
            continue
    return out


def collect_documents(node, out, depth=0, max_depth=16):
    try:
        role = node.get_role()
        nchild = node.get_child_count()
    except GLib.Error:
        return
    if role in DOC_ROLES:
        # bogatstvo sadržaja merimo Collection-om, ne brojem karaktera:
        # Text na document-web čvoru vraća samo U+FFFC po detetu.
        try:
            nrich = len(Atspi.Collection.get_matches(
                node, make_rule([R.HEADING, R.LINK, R.PARAGRAPH]),
                SO.CANONICAL, 0, True))
        except GLib.Error:
            nrich = -1
        try:
            name = node.get_name() or ""
        except GLib.Error:
            name = "<?>"
        out.append({"obj": node, "name": name, "children": nchild,
                    "rich": nrich, "depth": depth})
    if depth >= max_depth:
        return
    for i in range(nchild):
        try:
            c = node.get_child_at_index(i)
        except GLib.Error:
            continue
        if c is not None:
            collect_documents(c, out, depth + 1, max_depth)


def pick(app, want=None):
    docs = []
    collect_documents(app, docs)
    if not docs:
        return None, []
    docs.sort(key=lambda d: (d["rich"], d["children"]), reverse=True)
    if want:
        for d in docs:
            if want.lower() in d["name"].lower():
                return d, docs
        return None, docs
    return docs[0], docs


# --- merenja -----------------------------------------------------------------

def bench_nav(doc, label, roles, attrs, iterations, sortby):
    """Niz skokova kroz dokument — kao da korisnik drži quick-nav tipku."""
    rule = make_rule(roles, attrs)
    samples, cur, wraps = [], doc, 0
    while len(samples) < iterations:
        t0 = CLOCK()
        try:
            res = Atspi.Collection.get_matches_from(
                doc, cur, rule, sortby, TT.INORDER, 1, True)
        except GLib.Error as e:
            log(f"  {label}: GREŠKA {e.message}")
            break
        samples.append((CLOCK() - t0) * 1000.0)
        if not res:
            wraps += 1
            cur = doc
            if wraps >= 2 and len(samples) < 4:
                break          # takvih elemenata na strani praktično nema
        else:
            cur = res[0]
    return samples, wraps


def check_sort_orders(doc):
    """Koji sortBy je uopšte implementiran? Plan je tražio FLOW."""
    out = {}
    rule = make_rule([R.HEADING])
    for nm in ("CANONICAL", "FLOW", "TAB", "REVERSE_CANONICAL",
               "REVERSE_FLOW", "REVERSE_TAB"):
        so = getattr(SO, nm)
        try:
            t0 = CLOCK()
            res = Atspi.Collection.get_matches(doc, rule, so, 0, True)
            dt = (CLOCK() - t0) * 1000.0
            out[nm] = [(o.get_name() or "")[:30] for o in res]
            log(f"  GetMatches(sortBy={nm:<18}) -> {len(res):>4} elem. u {dt:6.2f} ms"
                + (f"  prvi={out[nm][0]!r}" if res else "   (NEIMPLEMENTIRANO)"))
        except GLib.Error as e:
            log(f"  GetMatches(sortBy={nm:<18}) -> GREŠKA {e.message[:50]}")
    # Da li GetMatchesFrom uopšte gleda sortBy?
    log("")
    log("  GetMatchesFrom, prvih 5 naslova po svakom sortBy:")
    seqs = {}
    for nm in ("CANONICAL", "FLOW", "TAB"):
        so, seq, cur = getattr(SO, nm), [], doc
        for _ in range(5):
            try:
                res = Atspi.Collection.get_matches_from(
                    doc, cur, make_rule([R.HEADING]), so, TT.INORDER, 1, True)
            except GLib.Error:
                break
            if not res:
                break
            cur = res[0]
            seq.append((cur.get_name() or "")[:22])
        seqs[nm] = seq
        log(f"    {nm:<10} {seq}")
    if len(set(tuple(v) for v in seqs.values())) == 1:
        log("    → GetMatchesFrom IGNORIŠE sortBy (sva tri daju isti niz).")
    canon = out.get("CANONICAL") or []
    if canon and seqs.get("CANONICAL") and canon[:5] == seqs["CANONICAL"]:
        log("    → CANONICAL je redosled dokumenta, isti kao redosled čitanja na ovoj strani.")
    return out


def check_heading_levels(doc):
    found = {}
    for level in range(1, 10):
        try:
            t0 = CLOCK()
            res = Atspi.Collection.get_matches(
                doc, make_rule([R.HEADING], {"level": str(level)}),
                SO.CANONICAL, 0, True)
            dt = (CLOCK() - t0) * 1000.0
        except GLib.Error as e:
            log(f"  level={level}: GREŠKA {e.message}")
            continue
        found[level] = len(res)
        if res:
            log(f"  level={level}: {len(res):>3} naslova ({dt:6.2f} ms)  "
                f"prvi={(res[0].get_name() or '')[:34]!r}")
    log(f"  nivoi sa rezultatima: {sorted(k for k, v in found.items() if v)}")
    if not any(found.values()):
        log("  UPOZORENJE: filter po atributu `level` ne radi na ovom serveru — "
            "quick-nav 1..9 mora da filtrira na klijentu.")
    # cena rezervne varijante: svi naslovi + čitanje atributa na klijentu
    t0 = CLOCK()
    allh = Atspi.Collection.get_matches(doc, make_rule([R.HEADING]),
                                        SO.CANONICAL, 0, True)
    t_fetch = (CLOCK() - t0) * 1000.0
    t0 = CLOCK()
    lv2 = [o for o in allh if (o.get_attributes() or {}).get("level") == "2"]
    t_attr = (CLOCK() - t0) * 1000.0
    log(f"  rezerva (klijentski filter): {len(allh)} naslova za {t_fetch:.2f} ms + "
        f"atributi svih za {t_attr:.2f} ms → nivo 2: {len(lv2)}")
    return found


def bench_flat_text(doc, iterations=3):
    """
    Plan je tražio Text::get_text(0, get_character_count()) na telu dokumenta.
    Nalaz: document-web čvor NE izlaže tekst strane — njegov Text sadrži samo
    po jedan U+FFFC za svako dete. Ravan tekst se mora sastaviti obilaskom.
    Merimo to, jer je to ono što browse mode zapravo mora da uradi.
    """
    try:
        n = Atspi.Text.get_character_count(doc)
        txt = Atspi.Text.get_text(doc, 0, n) if n > 0 else ""
    except GLib.Error as e:
        log(f"  Text na dokumentu nedostupan: {e.message}")
        n, txt = -1, ""
    log(f"  Text na document-web čvoru: {n} karaktera, sadržaj={txt[:40]!r}")

    samples, total_chars, total_nodes = [], 0, 0
    for _ in range(iterations):
        t0 = CLOCK()
        chars = nodes = 0
        stack = [doc]
        while stack:
            x = stack.pop()
            nodes += 1
            try:
                if "Text" in (x.get_interfaces() or []):
                    c = Atspi.Text.get_character_count(x)
                    if c > 0:
                        s = Atspi.Text.get_text(x, 0, c)
                        chars += len(s.replace("￼", ""))
                for i in range(x.get_child_count()):
                    ch = x.get_child_at_index(i)
                    if ch is not None:
                        stack.append(ch)
            except GLib.Error:
                pass
        samples.append((CLOCK() - t0) * 1000.0)
        total_chars, total_nodes = chars, nodes
    log(f"  obilazak celog dokumenta: {total_nodes} objekata, "
        f"{total_chars} korisnih karaktera")
    return row("ravan tekst (obilazak)", samples), total_nodes, total_chars


# --- main --------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="S3 — Collection quick-nav spike")
    ap.add_argument("--browser", required=True,
                    help="deo imena aplikacije na a11y magistrali: chrome, Firefox")
    ap.add_argument("--doc-match", default=None,
                    help="deo naslova dokumenta koji se meri")
    ap.add_argument("--iterations", type=int, default=100)
    ap.add_argument("--settle", type=float, default=2.0)
    ap.add_argument("--sortby", default="CANONICAL",
                    choices=["CANONICAL", "FLOW", "TAB"],
                    help="redosled za merenje skokova (podrazumevano CANONICAL "
                         "jer FLOW nije implementiran — vidi nalaz N2)")
    ap.add_argument("--json", default=None, help="upiši sirove brojeve u JSON")
    args = ap.parse_args()

    log("=" * 86)
    log(f"S3 — Collection quick-nav | pregledač={args.browser} "
        f"| iteracija={args.iterations} | sortBy={args.sortby}")
    log("=" * 86)

    listener = Atspi.EventListener.new(lambda e: None)
    listener.register("object:state-changed:focused")
    log("registrovan AT-SPI listener (pregledači grade a11y drvo samo kad vide AT klijenta)")

    app = chosen = None
    docs = []
    deadline = time.time() + max(args.settle, 1.0) + 25
    while time.time() < deadline:
        best = None
        for cand in find_apps(args.browser):
            c_doc, c_docs = pick(cand, args.doc_match)
            if c_doc is None:
                continue
            key = (c_doc["rich"], c_doc["children"])
            if best is None or key > best[0]:
                best = (key, cand, c_doc, c_docs)
        if best is not None:
            _, app, chosen, docs = best
            if chosen["rich"] > 50:
                break
        time.sleep(0.7)

    if app is None or chosen is None:
        log(f"GREŠKA: nijedna aplikacija koja sadrži {args.browser!r} ne izlaže dokument.")
        return 2

    log(f"aplikacija: {app.get_name()!r} toolkit={app.get_toolkit_name()!r} "
        f"{app.get_toolkit_version()!r}")
    log(f"dokumenata u aplikaciji: {len(docs)}")
    for d in docs[:6]:
        log(f"    bogatstvo={d['rich']:>6}  dece={d['children']:>4}  "
            f"dubina={d['depth']:>2}  {d['name'][:48]!r}")
    doc = chosen["obj"]
    if chosen["rich"] <= 50:
        log(f"UPOZORENJE: izabrani dokument ima samo {chosen['rich']} "
            "naslova/linkova/paragrafa — merenje nije reprezentativno.")
    log(f"merim: {chosen['name'][:55]!r} (bogatstvo {chosen['rich']})")
    log(f"interfejsi: {doc.get_interfaces()}")

    sortby = getattr(SO, args.sortby)
    log("")
    log("--- 1  latencija quick-nav skoka: GetMatchesFrom(count=1, INORDER, traverse=True) ---")
    rows = []
    for label, roles, attrs in NAV_RULES:
        samples, wraps = bench_nav(doc, label, roles, attrs, args.iterations, sortby)
        s = row(label, samples, budget=BUDGET_MS)
        if s:
            s["wraps"] = wraps
            rows.append(s)

    log("")
    log("--- 2  koji sortBy je implementiran ---")
    sort_info = check_sort_orders(doc)

    log("")
    log("--- 3  attributes{level=1..9} (quick-nav tipke 1..9) ---")
    levels = check_heading_levels(doc)

    log("")
    log("--- 4  ravan tekst cele strane ---")
    text_row, nodes, chars = bench_flat_text(doc)

    log("")
    log("=" * 86)
    worst = max((r["p95"] for r in rows), default=float("nan"))
    verdict = "PASS" if worst < BUDGET_MS else "FAIL"
    log(f"REZULTAT {args.browser}: najgori p95 quick-nav skoka = {worst:.2f} ms "
        f"(budžet {BUDGET_MS:.0f} ms) → {verdict}")
    log("=" * 86)

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({
                "browser": args.browser,
                "app": app.get_name(),
                "toolkit": app.get_toolkit_name(),
                "document": chosen["name"],
                "richness": chosen["rich"],
                "sortby_used": args.sortby,
                "nav": rows,
                "sort_orders_implemented": {
                    k: len(v) for k, v in sort_info.items()},
                "heading_levels": levels,
                "flat_text": {"row": text_row, "nodes": nodes, "chars": chars},
                "worst_p95_ms": worst,
                "verdict": verdict,
            }, f, ensure_ascii=False, indent=2)
        log(f"sirovi brojevi: {args.json}")
    return 0 if worst < BUDGET_MS else 1


if __name__ == "__main__":
    sys.exit(main())
