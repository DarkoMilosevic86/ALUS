#!/usr/bin/env python3
"""
S1 — Ulaz tastature na Wayland-u (Faza 0, 02-FAZE.md).

Pitanje: može li ALUS da vidi i da POTROŠI tastere na GNOME Wayland sesiji —
rizici R1 i R2 iz 03-RIZICI.md. Ako ne može, ceo plan se revidira.

Plan je tražio C + libatspi. `libatspi2.0-dev` nije instaliran na ciljnom
sistemu, pa je spike u Python/GI. To je ravnopravan dokaz: GI poziva isti
`libatspi.so.0` i isti `org.freedesktop.a11y.KeyboardMonitor` preko D-Bus-a.
Spike kod se ionako baca.

Dokazuje, u ovom redu:
  1. atspi_device_new() vrati AtspiDeviceA11yManager
  2. app_id "org.gnome.Orca" → proces drži org.gnome.Orca.KeyboardMonitor
  3. key watcher prima SVE pritiske, i u Wayland-native aplikacijama
  4. map_keysym_modifier(XK_Insert) → virtualni modifikator 15..31
  5. add_key_grab POTROŠI Insert+<taster> — aplikacija ga ne vidi
  6. dupli Insert u 500 ms se detektuje i propusti aplikaciji
  7. numpad sa NumLock isključenim daje XK_KP_Home, ne XK_KP_7

Režimi:
  --auto         samo koraci 1, 2, 4 i prihvatanje grab API-ja (bez tastera)
  --synth        + koraci 3 i 5 preko sintetizovanih tastera (F15, bezopasno)
  --interactive  + koraci 3, 5, 6, 7 preko STVARNIH pritisaka; traži TTY
                 i da ovaj terminal bude fokusiran

Orca MORA biti zaustavljena: samo jedan proces može držati
org.gnome.Orca.KeyboardMonitor.
"""

import argparse
import json
import os
import subprocess
import sys
import termios
import time
import tty

import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib  # noqa: E402

# X11 keysym-ovi (X11/keysymdef.h)
XK_Insert = 0xFF63
XK_F15 = 0xFFCC
XK_t = 0x0074
XK_KP_Home = 0xFF95
XK_KP_7 = 0xFFB7
XK_KP_End = 0xFF9C
XK_KP_1 = 0xFFB1

NUMPAD_NAV = {
    0xFF95: "KP_Home", 0xFF96: "KP_Left", 0xFF97: "KP_Up",
    0xFF98: "KP_Right", 0xFF99: "KP_Down", 0xFF9A: "KP_Prior",
    0xFF9B: "KP_Next", 0xFF9C: "KP_End", 0xFF9D: "KP_Begin",
    0xFF9E: "KP_Insert", 0xFF9F: "KP_Delete",
}
NUMPAD_DIGIT = {
    0xFFB0: "KP_0", 0xFFB1: "KP_1", 0xFFB2: "KP_2", 0xFFB3: "KP_3",
    0xFFB4: "KP_4", 0xFFB5: "KP_5", 0xFFB6: "KP_6", 0xFFB7: "KP_7",
    0xFFB8: "KP_8", 0xFFB9: "KP_9",
}

results = {}
events = []          # (t, pressed, keycode, keysym, modifiers, keystring, source)


def log(*a):
    print(*a, flush=True)


def verdict(step, ok, detail=""):
    results[step] = {"pass": bool(ok), "detail": detail}
    log(f"  [{'PASS' if ok else 'FAIL'}] {step}" + (f" — {detail}" if detail else ""))


def sh(cmd):
    try:
        return subprocess.run(cmd, shell=True, capture_output=True,
                              text=True, timeout=10).stdout.strip()
    except subprocess.SubprocessError:
        return ""


# ------------------------------------------------------------ koraci 1, 2, 4

def step1_device(app_id):
    log("\n--- 1  atspi_device_new() na GNOME Wayland ---")
    log(f"  XDG_SESSION_TYPE={os.environ.get('XDG_SESSION_TYPE')} "
        f"XDG_CURRENT_DESKTOP={os.environ.get('XDG_CURRENT_DESKTOP')} "
        f"WAYLAND_DISPLAY={os.environ.get('WAYLAND_DISPLAY')}")
    dev = Atspi.Device.new_full(app_id)
    if dev is None:
        verdict("1 device", False, "new_full() vratio None")
        return None
    cls = type(dev).__name__
    want = "DeviceA11yManager"
    log(f"  tip uređaja: Atspi.{cls}")
    log(f"  app_id: {dev.get_app_id()!r}")
    verdict("1 device", cls == want,
            f"dobijen Atspi.{cls}, očekivan Atspi.{want}"
            + ("" if cls == want else " — kompozitor ne izlaže KeyboardMonitor "
                                      "ili ga libatspi nije izabrao (R2)"))
    return dev


def step2_busname(app_id):
    log("\n--- 2  D-Bus ime i kontrola pristupa u Mutter-u ---")
    want = f"{app_id}.KeyboardMonitor"
    mine = os.getpid()
    listing = sh("busctl --user list --no-pager 2>/dev/null")
    holder = None
    for line in listing.splitlines():
        if "KeyboardMonitor" in line:
            parts = line.split()
            log(f"  {line.strip()}")
            for p in parts:
                if p.isdigit():
                    holder = int(p)
                    break
    if holder is None:
        verdict("2 bus name", False,
                f"niko ne drži {want} — libatspi nije tražio ime")
        return
    if holder == mine:
        verdict("2 bus name", True, f"{want} drži ovaj proces (pid {mine})")
    else:
        cmd = sh(f"cat /proc/{holder}/cmdline 2>/dev/null | tr '\\0' ' '")
        verdict("2 bus name", False,
                f"{want} drži pid {holder} ({cmd[:60]}), a ne mi (pid {mine}) "
                "— zaustavi taj proces (Orca?)")

    # da li Mutter propušta pozive: SetKeyGrabs je metoda koju grab koristi
    intro = sh("gdbus introspect --session --dest org.freedesktop.a11y.Manager "
               "--object-path /org/freedesktop/a11y/Manager 2>/dev/null")
    has = "SetKeyGrabs" in intro and "KeyEvent" in intro
    log(f"  KeyboardMonitor interfejs izložen: {has}")
    results["2 interface"] = {"pass": has, "detail": "SetKeyGrabs + KeyEvent"}


def step4_modifier(dev):
    """
    NALAZ: `map_keysym_modifier` vraća MASKU, ne indeks modifikatora.
    Plan (02-FAZE.md S1, tačka 4) traži „virtualni modifikator u opsegu 15–31" —
    to je indeks. libatspi vraća `1 << indeks`. Za Insert dobijamo 32768 = 1<<15,
    tj. indeks 15 — prvi virtualni modifikator, tačno na dnu očekivanog opsega.
    `AtspiKeyDefinition.modifiers` očekuje MASKU, pa se vraćena vrednost
    prosleđuje kakva je.
    """
    log("\n--- 4  map_keysym_modifier(XK_Insert) → virtualni modifikator ---")
    try:
        mask = dev.map_keysym_modifier(XK_Insert)
    except GLib.Error as e:
        verdict("4 keysym modifier", False, f"GREŠKA {e.message}")
        return None
    if mask is None or mask == 0:
        verdict("4 keysym modifier", False, f"vraćeno {mask}")
        return None
    is_pow2 = (mask & (mask - 1)) == 0
    idx = mask.bit_length() - 1 if is_pow2 else None
    log(f"  vraćena vrednost: {mask}")
    log(f"  tumačenje: {'maska 1<<%d → indeks %d' % (idx, idx) if is_pow2 else 'nije stepen dvojke — nije čista maska'}")
    ok = is_pow2 and 15 <= idx <= 31
    verdict("4 keysym modifier", ok,
            f"maska={mask} indeks={idx}, očekivan indeks 15..31"
            + ("" if ok else " — van opsega virtualnih modifikatora"))
    # kontrola: get_keysym_modifier mora da vrati isto
    try:
        again = dev.get_keysym_modifier(XK_Insert)
        log(f"  get_keysym_modifier(XK_Insert) = {again} "
            f"({'isto' if again == mask else 'RAZLIKUJE SE od map_'})")
    except GLib.Error as e:
        log(f"  get_keysym_modifier GREŠKA: {e.message}")
    return mask


def step5_grab_api(dev, mod_mask):
    """Prihvatanje grab API-ja. Stvarno trošenje se dokazuje u 5b."""
    log("\n--- 5a  add_key_grab(Insert+t) — prihvatanje ---")
    if not mod_mask:
        verdict("5a grab api", False, "nema modifikatora iz koraka 4")
        return None
    kd = Atspi.KeyDefinition()
    kd.keycode = 0
    kd.keysym = XK_t
    kd.modifiers = mod_mask          # MASKA, ne indeks — vidi korak 4
    kd.keystring = "t"
    log(f"  KeyDefinition: keysym=0x{XK_t:04x} modifiers=0x{mod_mask:x}")
    try:
        gid = dev.add_key_grab(kd, None)
    except GLib.Error as e:
        verdict("5a grab api", False, f"GREŠKA {e.message}")
        return None
    log(f"  grab id: {gid}")
    verdict("5a grab api", gid is not None and gid >= 0,
            f"add_key_grab vratio {gid}")
    return gid


# ----------------------------------------------------------------- watcher

def step2b_dbus_direct(app_id, mod_mask):
    """
    Jači dokaz od samog vlasništva nad imenom: dok držimo
    `<app_id>.KeyboardMonitor`, zovemo Mutter-ove metode DIREKTNO i gledamo da
    li prolaze kontrolu pristupa. Ako allowlist ne bi prošao, ovde bi došao
    `AccessDenied` ili `Not allowed`.

    Interfejs (introspekcija, Mutter 16):
      WatchKeyboard(); UnwatchKeyboard(); GrabKeyboard(); UngrabKeyboard();
      SetKeyGrabs(in au modifiers, in a(uu) keystrokes);
      signal KeyEvent(b released, u state, u keysym, u unichar, q keycode);
    """
    from gi.repository import Gio

    log("\n--- 2b  direktan poziv Mutter-ovih metoda (kontrola pristupa) ---")
    DEST = "org.freedesktop.a11y.Manager"
    PATH = "/org/freedesktop/a11y/Manager"
    IFACE = "org.freedesktop.a11y.KeyboardMonitor"
    try:
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    except GLib.Error as e:
        verdict("2b dbus", False, f"nema sesijske magistrale: {e.message}")
        return

    idx = (mod_mask.bit_length() - 1) if mod_mask else 15
    calls = [
        ("WatchKeyboard", None),
        ("SetKeyGrabs", GLib.Variant("(aua(uu))", ([idx], [(XK_t, 1 << idx)]))),
        ("GrabKeyboard", None),
        ("UngrabKeyboard", None),
        ("SetKeyGrabs", GLib.Variant("(aua(uu))", ([], []))),   # ciscenje
        ("UnwatchKeyboard", None),
    ]
    # Napomena: zadnji SetKeyGrabs([], []) brise SVE grab-ove, pa ovaj korak
    # mora da se izvrsi PRE koraka 5a.
    denied = []
    for method, params in calls:
        try:
            bus.call_sync(DEST, PATH, IFACE, method, params, None,
                          Gio.DBusCallFlags.NONE, 5000, None)
            log(f"  {method:<16} OK")
        except GLib.Error as e:
            msg = e.message
            log(f"  {method:<16} GREŠKA: {msg[:80]}")
            if "AccessDenied" in msg or "not allowed" in msg.lower() \
               or "Not allowed" in msg:
                denied.append(method)
    verdict("2b kontrola pristupa", not denied,
            "Mutter je propustio sve metode"
            if not denied else f"odbijeno: {denied} — allowlist nije prošao (R1)")


def make_watcher(dev, source_tag="real"):
    def cb(device, pressed, keycode, keysym, modifiers, keystring, *rest):
        events.append({
            "t": time.monotonic(),
            "pressed": bool(pressed),
            "keycode": keycode,
            "keysym": keysym,
            "mod": modifiers,
            "keystring": keystring,
            "source": source_tag,
        })
        name = (NUMPAD_NAV.get(keysym) or NUMPAD_DIGIT.get(keysym)
                or (keystring if keystring else f"0x{keysym:x}"))
        log(f"    KEY {'DOWN' if pressed else 'UP  '} "
            f"keycode={keycode:<4} keysym=0x{keysym:04x} mod=0x{modifiers:04x} "
            f"keystring={keystring!r:<8} → {name}")
        return False     # ne trošimo u watcher-u
    dev.add_key_watcher(cb)
    return cb


# ------------------------------------------------------------ sintetizovano

def step3_5_synth(dev):
    log("\n--- 3/5  sintetizovani tasteri (F15, bezopasan) ---")
    log("  OGRANIČENJE: sintetizovan događaj ne dokazuje da watcher vidi")
    log("  HARDVERSKI taster. Za to je potreban --interactive.")
    before = len(events)
    loop = GLib.MainLoop()

    def fire():
        for ks in (XK_F15, XK_F15):
            ok = Atspi.generate_keyboard_event(ks, None,
                                               Atspi.KeySynthType.PRESSRELEASE)
            log(f"  generate_keyboard_event(F15) → {ok}")
            time.sleep(0.15)
        GLib.timeout_add(600, lambda: (loop.quit(), False)[1])
        return False

    GLib.timeout_add(200, fire)
    GLib.timeout_add(4000, lambda: (loop.quit(), False)[1])
    loop.run()
    got = [e for e in events[before:] if e["keysym"] == XK_F15]
    verdict("3 watcher (sintetizovano)", len(got) > 0,
            f"{len(got)} F15 događaja viđeno")


# ------------------------------------------------------------- interaktivno

def drain_stdin():
    """Šta je aplikacija (ovaj terminal) ZAISTA dobila."""
    got = b""
    try:
        while True:
            ch = os.read(sys.stdin.fileno(), 1024)
            if not ch:
                break
            got += ch
    except BlockingIOError:
        pass
    except OSError:
        pass
    return got


def prompt_phase(title, instruction, seconds):
    log(f"\n  >>> {title}")
    log(f"  >>> {instruction}")
    log(f"  >>> imaš {seconds} sekundi...")
    before = len(events)
    loop = GLib.MainLoop()
    GLib.timeout_add(int(seconds * 1000), lambda: (loop.quit(), False)[1])
    loop.run()
    return events[before:]


def step_interactive(dev, mod, grab_id):
    log("\n--- interaktivni koraci (3, 5b, 6, 7) ---")
    if not sys.stdin.isatty():
        log("  PRESKOČENO: stdin nije TTY. Pokreni ovaj spike direktno u "
            "terminalu:")
        log("      python3 spikes/s1-input.py --interactive")
        return

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)
        os.set_blocking(fd, False)

        # --- 3: svi tasteri ---
        drain_stdin()
        ev = prompt_phase(
            "KORAK 3 — da li watcher vidi SVE tastere",
            "Pritisni nekoliko običnih tastera (npr. a s d f) U OVOM terminalu.",
            6)
        seen = {e["keysym"] for e in ev if e["pressed"]}
        app_got = drain_stdin()
        log(f"  watcher video {len(seen)} različitih keysym-ova; "
            f"terminal dobio {app_got!r}")
        verdict("3 watcher (stvarni tasteri)", len(seen) >= 2,
                f"{len(seen)} keysym-ova; aplikacija ih je takođe dobila "
                f"({len(app_got)} bajtova) — watcher NE troši, što je ispravno")

        # --- 5b: trošenje Insert+t ---
        drain_stdin()
        ev = prompt_phase(
            "KORAK 5b — da li grab TROŠI Insert+t",
            "Pritisni Insert+t (Insert drži, pa t) U OVOM terminalu.",
            8)
        app_got = drain_stdin()
        grabbed = [e for e in ev if e["pressed"] and e["keysym"] == XK_t]
        consumed = b"t" not in app_got
        log(f"  watcher video 't': {len(grabbed)}x; "
            f"terminal dobio {app_got!r}")
        verdict("5b grab troši", consumed and len(grabbed) > 0,
                ("grab je potrošio 't' — aplikacija ga nije dobila"
                 if consumed else
                 "aplikacija JE dobila 't' — grab NE troši (R1)"))

        # --- 6: dupli Insert ---
        drain_stdin()
        ev = prompt_phase(
            "KORAK 6 — dupli Insert u 500 ms",
            "Pritisni Insert dva puta BRZO (u okviru pola sekunde).",
            8)
        ins = [e["t"] for e in ev if e["pressed"] and e["keysym"] == XK_Insert]
        gaps = [round((b - a) * 1000) for a, b in zip(ins, ins[1:])]
        log(f"  Insert pritisaka: {len(ins)}, razmaci: {gaps} ms")
        ok6 = any(g <= 500 for g in gaps)
        verdict("6 dupli Insert", ok6,
                f"najmanji razmak {min(gaps) if gaps else '—'} ms "
                "(≤500 ms je detektabilno)")

        # --- 7: numpad sa NumLock isključenim ---
        log("\n  >>> KORAK 7 — ISKLJUČI NumLock pre ovog koraka!")
        drain_stdin()
        ev = prompt_phase(
            "KORAK 7 — numpad bez NumLock-a",
            "Sa ISKLJUČENIM NumLock-om pritisni numpad 7 i numpad 1.",
            10)
        navs = [NUMPAD_NAV[e["keysym"]] for e in ev
                if e["pressed"] and e["keysym"] in NUMPAD_NAV]
        digs = [NUMPAD_DIGIT[e["keysym"]] for e in ev
                if e["pressed"] and e["keysym"] in NUMPAD_DIGIT]
        log(f"  navigacioni keysym-ovi: {navs}")
        log(f"  cifreni keysym-ovi: {digs}")
        verdict("7 numpad bez NumLock", bool(navs) and not digs,
                f"nav={navs} cifre={digs} — očekivano samo nav "
                "(KP_Home/KP_End), bez KP_7/KP_1")
    finally:
        try:
            os.set_blocking(fd, True)
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
        except OSError:
            pass


# ------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description="S1 — ulaz tastature na Wayland-u")
    ap.add_argument("--app-id", default="org.gnome.Orca",
                    help="D-Bus app_id; 'org.gnome.Orca' je jedino ime u "
                         "Mutter allowlist-i (R1)")
    ap.add_argument("--auto", action="store_true",
                    help="samo koraci bez pritiska tastera")
    ap.add_argument("--synth", action="store_true",
                    help="dodaj sintetizovane tastere (F15)")
    ap.add_argument("--interactive", action="store_true",
                    help="dodaj korake sa stvarnim pritiscima (treba TTY)")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()
    if not (args.auto or args.synth or args.interactive):
        args.auto = True

    log("=" * 78)
    log(f"S1 — ulaz tastature na Wayland-u | app_id={args.app_id}")
    log("=" * 78)

    orca = sh("pgrep -x orca")
    if orca:
        log(f"UPOZORENJE: Orca radi (pid {orca.split()[0]}) i drži "
            "org.gnome.Orca.KeyboardMonitor.")
        log("            Korak 2 će pasti dok je ne zaustaviš:")
        log("            gsettings set org.gnome.desktop.a11y.applications "
            "screen-reader-enabled false && pkill orca")

    dev = step1_device(args.app_id)
    if dev is None:
        log("\nS1 pada na prvom koraku — bez uređaja nema ni jednog drugog testa.")
        return 1
    step2_busname(args.app_id)
    mod = step4_modifier(dev)
    # 2b MORA pre 5a: zavrsava sa SetKeyGrabs([], []), sto obrise sve grab-ove.
    step2b_dbus_direct(args.app_id, mod)
    grab_id = step5_grab_api(dev, mod)
    make_watcher(dev)

    if args.synth:
        step3_5_synth(dev)
    if args.interactive:
        step_interactive(dev, mod, grab_id)

    # čišćenje
    if grab_id is not None and grab_id >= 0:
        try:
            dev.remove_key_grab(grab_id)
            log("\n  grab uklonjen")
        except GLib.Error as e:
            log(f"\n  remove_key_grab GREŠKA: {e.message}")
    if mod is not None:
        try:
            dev.unmap_keysym_modifier(XK_Insert)
        except GLib.Error:
            pass

    log("\n" + "=" * 78)
    npass = sum(1 for v in results.values() if v["pass"])
    log(f"S1: {npass}/{len(results)} koraka prošlo")
    for k, v in results.items():
        log(f"  {'PASS' if v['pass'] else 'FAIL'}  {k}")
    critical = ["1 device", "2 bus name", "2b kontrola pristupa",
                "4 keysym modifier", "5a grab api"]
    crit_ok = all(results.get(k, {}).get("pass") for k in critical)
    log(f"\nKritični koraci (1, 2, 2b, 4, 5a): {'PASS' if crit_ok else 'FAIL'}")
    if not crit_ok:
        log("→ R1/R2: ulaz na Wayland-u nije dokazan. Vidi 03-RIZICI.md.")
    log("=" * 78)

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"app_id": args.app_id, "results": results,
                       "events": events, "critical_pass": crit_ok},
                      f, ensure_ascii=False, indent=2)
        log(f"sirovi podaci: {args.json}")
    return 0 if crit_ok else 1


if __name__ == "__main__":
    sys.exit(main())
