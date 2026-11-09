# ALUS — Registar rizika

> Dokument 4 od 4.
>
> Svaki rizik ima: **uzrok** (proverenu činjenicu, ne pretpostavku), **posledicu**, **meru**, i **detektor** — komandu ili test koji kaže da li se rizik ostvario.

---

## Matrica

| # | Rizik | Verovatnoća | Uticaj | Faza | Status |
|---|---|---|---|---|---|
| **R1** | Mutter/KWin dozvoljavaju KeyboardMonitor samo Orci | **Visoka** | **Kritičan** | 0 | mera spremna |
| **R2** | Kompozitori bez KeyboardMonitor-a (sway, wlroots) | Srednja | Visok | 0 / v1.1 | svesno odloženo |
| **R3** | Chromium ne puni AT-SPI keš → browse mode spor | Srednja | Visok | 0 / 6 | meri se u S3 |
| **R4** | speech-dispatcher ne drži govorni ugovor | Srednja | Visok | 0 / 2 | **delimično potvrđen, mera implementirana** |
| **R5** | Poplava AT-SPI događaja | **Visoka** | **Kritičan** | 1 | mera u arhitekturi |
| **R6** | Kodiranje offseta (UTF-8 / UTF-16 / codepoint) | **Visoka** | Visok | 5 | mera u arhitekturi |
| **R7** | `getTextWithFields` sinteza netačna | **Visoka** | **Kritičan** | 6 | port iz NVDA |
| **R8** | Varijansa pouzdanosti caret događaja po toolkit-u | **Visoka** | Srednji | 5 | mera u arhitekturi |
| **R9** | Obim projekta (NVDA je 189k linija) | **Visoka** | **Kritičan** | sve | ne-ciljevi obavezujući |
| **R10** | Koegzistencija sa Orcom | Visoka | Nizak | 3 | mera jednostavna |
| **R11** | Licencna izloženost | Niska | Visok | pre koda | odlučeno |
| **R12** | Nedostatak testne pokrivenosti za a11y | Srednja | Srednji | 4 | mock server |
| **R13** | SSML podrška zavisi od modula | **Visoka** | Srednji | 2 | **mera implementirana** |
| **R14** | Index oznaka u sredini iskaza se tiho gubi | **Visoka** | **Kritičan** | 2 | **mera implementirana i testirana** |
| **R15** | Merenje latencije nepouzdano kod modula sa dugim repom | Srednja | Nizak | 0 / 2 | mera u metodologiji |
| **R16** | `libspeechd` uništava mutex dok mu nit čeka | Srednja | Srednji | 9 | prijaviti upstream |

R13–R15 su dodati posle Faze 0 (`spikes/RESULTS.md`), R16 posle Faze 2
(`FAZA2.md`).

---

## R1 — Mutter i KWin dozvoljavaju KeyboardMonitor samo Orci

**Uzrok (provereno).** Mutter 48.7 izlaže `org.freedesktop.a11y.KeyboardMonitor` na `org.freedesktop.a11y.Manager` / `/org/freedesktop/a11y/Manager`. Kontrola pristupa zahteva da pozivalac drži D-Bus ime iz hardkodovane liste. U `libmutter-16.so.0.0.0` postoji **tačno jedno takvo ime**:

```bash
strings /usr/lib/x86_64-linux-gnu/libmutter-16.so.0.0.0 | grep KeyboardMonitor
# MetaDBusKeyboardMonitor              ← GObject tipovi, nisu D-Bus imena
# MetaDBusKeyboardMonitorProxy
# org.freedesktop.a11y.KeyboardMonitor ← ime interfejsa
# MetaDBusKeyboardMonitorSkeleton
# org.gnome.Orca.KeyboardMonitor       ← JEDINO ime u allowlist-i
```

`libatspi` traži ime po šablonu `"<app_id>.KeyboardMonitor"`:
```bash
strings /usr/lib/x86_64-linux-gnu/libatspi.so.0 | grep -A2 '%s.KeyboardMonitor'
# %s.KeyboardMonitor
# (su)
# RequestName
```

**Posledica.** Bez prolaska kontrole pristupa, ALUS na Wayland-u **ne vidi nijedan taster**. Nijedna komanda ne radi. Projekat je mrtav.

**Mera (dvoslojna).**

1. **Sada:** `atspi_device_set_app_id(dev, "org.gnome.Orca")`. ALUS traži `org.gnome.Orca.KeyboardMonitor` i prolazi kontrolu. Ovo je legitimno — ime je identifikator uloge „čitač ekrana ove sesije", a ne tvrdnja da je ALUS Orca. Posledica je da **ALUS i Orca ne mogu istovremeno** (vidi R10), što je ionako slučaj.
   Mutter ima i debug zastavicu `a11y-manager-without-access-control` (vidljiva u `libmutter` stringovima) kao izlaz za razvoj.

2. **Trajno:** upstream PR-ovi koji dodaju `org.alus.KeyboardMonitor` u allowlist-u Mutter-a i KWin-a. Ovo treba otvoriti **u Fazi 0**, ne kasnije — merge ciklus GNOME-a je mesecima dug, a `app_id` trik je premošćenje do tada.

**Detektor:** spike S1 u `02-FAZE.md`. Plus:
```bash
busctl --user list | grep KeyboardMonitor    # ko drži ime
gdbus introspect --session --dest org.freedesktop.a11y.Manager \
  --object-path /org/freedesktop/a11y/Manager | grep -c SetKeyGrabs
```

**Ako S1 padne:** ceo plan se revidira, jer input postaje Faza 1 sa evdev backend-om i obim raste (vidi R2).

---

## R2 — Kompozitori bez KeyboardMonitor-a

**Uzrok.** Interfejs postoji u Mutter-u od 48 i u KWin-u od 6.3.6. **sway i ostali wlroots kompozitori ga ne implementiraju.** Tamo klijent ne može da vidi tuđe tastere ni na koji podržan način.

**Posledica.** ALUS na sway/Hyprland/river ne radi. To je manjina korisnika, ali glasna manjina.

**Mera.** `IInputBackend` apstrakcija postoji od Faze 3, pa je dodavanje backend-a kasnije mehaničko, ne arhitekturno. Treći backend (`EvdevBackend`) je **odložen na v1.1** sa dokumentovanom cenom:

- čita `/dev/input/event*` direktno; zahteva da korisnik bude u grupi `input` ili udev pravilo. Provereno: na ciljnom sistemu korisnik **nije** u grupi `input`.
- „trošenje" tastera zahteva `EVIOCGRAB`, koji grabuje **ceo uređaj** — tj. ALUS mora da reinjektuje sve ostale tastere kroz `/dev/uinput`, koji je root-only (`crw------- root root`).
- To je privilegovan helper proces. Bezbednosna i instalaciona cena je realna i mora biti korisnikova svesna odluka, a ne podrazumevano stanje.

Alternativa koju treba istražiti pre evdev-a: `libei` / `org.gnome.Mutter.InputCapture` (libei 1.3.901 je instaliran). Namenjen je input leasing-u, ne a11y, ali može biti čistiji put.

**Detektor:**
```bash
gdbus introspect --session --dest org.freedesktop.a11y.Manager \
  --object-path /org/freedesktop/a11y/Manager >/dev/null 2>&1 \
  && echo "KeyboardMonitor dostupan" || echo "nema — potreban fallback"
id -nG | grep -qw input && echo "u grupi input" || echo "NIJE u grupi input"
```

---

## R3 — Chromium ne puni AT-SPI keš

**Uzrok (provereno).** `org.a11y.atspi.Cache.GetItems` po aplikaciji:

| Aplikacija | Objekata u kešu | Vreme |
|---|---|---|
| gnome-shell | 1837 | 31 ms |
| org.gnome.Terminal | 87 | 1.8 ms |
| gjs | 58 | 1.0 ms |
| **Google Chrome** | **4** | 0.4 ms |

Chromium gradi AT-SPI drvo **lenjo, na zahtev**, i ne puni keš. Dakle „pročitaj celo drvo u jednom pozivu" strategija ne radi za pregledače — a pregledači su najvažniji slučaj za browse mode.

**Posledica.** Ako quick-nav mora da hoda kroz drvo objekat-po-objekat, pritisak na `h` u velikom članku može da potraje stotinama milisekundi. To je razlika između upotrebljivog i neupotrebljivog browse mode-a.

**Mera.** Ne hodati kroz drvo. `org.a11y.atspi.Collection.GetMatchesFrom` je **server-side** pretraga — jedan D-Bus poziv po quick-nav skoku, pretraga se izvršava u procesu pregledača. Provereno da interfejs postoji na svakom Chromium objektu:

```bash
python3 -c "
import gi; gi.require_version('Atspi','2.0'); from gi.repository import Atspi
d=Atspi.get_desktop(0)
for i in range(d.get_child_count()):
    a=d.get_child_at_index(i)
    if 'Chrome' in (a.get_name() or '') or 'Firefox' in (a.get_name() or ''):
        print(a.get_name(), '→', a.get_interfaces())"
# Očekivano: Accessible,Action,Collection,Component,Document,Hypertext,Hyperlink,Text
```

**Spike S3 to meri pre nego što se Faza 6 dizajnira.** Ako p95 latencija quick-nav-a prelazi 20 ms, Faza 6 uvodi lokalni keš drveta (ne virtualni bafer — samo keš `(path → role, name, level)` invalidiran na `ChildrenChanged`).

---

## R4 — speech-dispatcher ne drži govorni ugovor

**Uzrok.** NVDA-ov `SynthDriver` ugovor zahteva `IndexCommand` i notifikacije `synthIndexReached` / `synthDoneSpeaking`. NVDA-ov eSpeak drajver koristi `AUDIO_OUTPUT_SYNCHRONOUS` i **sam pušta audio** — zato je sinhronizacija karete sa zvukom u say-all-u tačna do uzorka.

Preko speech-dispatcher-a ALUS **ne poseduje audio tok**. Posledice:
- latencija prekida zavisi od SSIP modula i audio izlaza (Pulse/ALSA/PipeWire odabran u `speechd.conf`),
- tačnost index marki zavisi od modula; `espeak-ng` ima `EspeakIndexing 1`, ali RHVoice i generički moduli možda ne,
- `volume` nije podržan u svim modulima.

Ublažavajuća činjenica: SSIP **ima** sve potrebne notifikacije:
```bash
strings /usr/lib/x86_64-linux-gnu/libspeechd.so.2 | grep "SET SELF NOTIFICATION"
# begin, end, cancel, pause, resume, index_marks
grep -i indexing /etc/speech-dispatcher/modules/espeak-ng.conf     # EspeakIndexing 1
```
I NVDA ionako koristi **jedan callback po liniji** u say-all-u, ne po reči — granularnost po liniji je sasvim dovoljna.

**Posledica ako se ostvari.** Say-all „škripi": caret zaostaje za zvukom, prekid se čuje sa zakašnjenjem. Korisnici to primećuju odmah i to je glavna pritužba na postojeće Linux čitače.

**Mera (troslojna).**

1. **`ISynth` apstrakcija sa dva stvarna implementatora.** `SsipSynth` je podrazumevani (zadovoljava zahtev korisnika i radi sa svim modulima koje korisnik već ima). `EspeakDirectSynth` (direktno `libespeak-ng` + sopstveni `WavePlayer`) postoji da apstrakcija bude dokazana i da korisnici koji traže maksimalnu tačnost imaju izlaz.
2. **ALUS drži red govora, ne speech-dispatcher.** SSIP-ov prioritetni red (`IMPORTANT/MESSAGE/TEXT/NOTIFICATION/PROGRESS`) se **ne koristi** za sekvenciranje — ALUS šalje jedan iskaz u letu na jednom SSIP prioritetu, a `SpeechManager` radi NVDA semantiku (`NOW/NEXT/NORMAL`, index callback-ovi, otkazivanje zastarelih najava). Mešanje dva reda bi dalo nepredvidiv poredak.
3. **Merenje je test u CI**, ne beleška: `speak→zvuk p95 < 50 ms`, `cancel→tišina p95 < 30 ms`.

**Detektor:** spike S2, pa isti test u CI (`alus-cli bench-speech`).

> **Status posle Faze 0 i Faze 2: delimično potvrđen, mera poznata.**
> speech-dispatcher drži tri od četiri dela ugovora: prvi zvuk 11.5 ms p95,
> prekid 29.0 ms p95, index marke tačne na 46 ms p95. Četvrti (`end`) ne drži —
> kasni ~294 ms — ali mera postoji i implementirana je: ulančavanje isključivo
> preko `IndexCommand`-a (`FAZA2.md`). `EspeakDirectSynth` nije više deo mere
> za ovaj rizik nego planirana opcija za v1.1, jer SsipSynth prolazi budžete.

---

## R5 — Poplava AT-SPI događaja

**Uzrok.** NVDA ima `OrderedWinEventLimiter` sa dvostepenim filtriranjem (dedup sa pomeranjem u vremenu, SHOW poništava HIDE, foreground briše focus, **max 10 događaja po nitu po ciklusu**, zadrži zadnja 4 focus događaja, pa emituj **samo jedan** focus/foreground). To nije optimizacija — to je razlog zašto NVDA ostaje responzivan.

**AT-SPI poplavljuje isto tako:** Chromium pri učitavanju strane emituje hiljade `ChildrenChanged`; LibreOffice pri skrolovanju tabele emituje `StateChanged` po ćeliji; GTK list view emituje `ChildrenChanged` po redu.

**Posledica.** Bez limitera, ALUS pumpa hiljade događaja po ciklusu, govor zaostaje sekundama, i korisnički doživljaj je „čitač ekrana se zagušio" — upravo pritužba na postojeća rešenja.

**Mera.** `alus::a11y::EventLimiter` je **deo Faze 1**, ne „optimizacija za kasnije". Pravila su u `01-ARHITEKTURA.md` §3.4; izvorna lista u `reference/nvda/source/IAccessibleHandler/orderedWinEventLimiter.py`.

**Detektor:** testovi T4.3–T4.5 u `02-FAZE.md`. Plus regresioni test: učitaj Wikipedia glavnu stranu u Chromium-u i izmeri broj događaja koji prođu kroz limiter po ciklusu (mora ≤10 po aplikaciji).

---

## R6 — Kodiranje offseta

**Uzrok.** Tri sistema u igri:
- AT-SPI2 offseti su **UTF-8 bajtovi**,
- NVDA pretpostavlja **UTF-16 jedinice** (`WCHAR_ENCODING`),
- interna logika radi u **codepoint-ima / grafemima**.

**Posledica.** Tihe greške. Svaki emoji, CJK string i kombinujući dijakritik je pomeren za nekoliko pozicija. Ne pada ništa — samo se pročita pogrešan znak, selekcija je van mesta, brajev red pokazuje pomeren tekst. **Vidi se samo u bug prijavama, mesecima kasnije.**

Za srpski je ovo direktno relevantno: ćirilica je 2 bajta po znaku u UTF-8, a 1 UTF-16 jedinica.

**Mera.** NVDA ima rešenje: `OffsetConverter` sloj (`textUtils/__init__.py`), i **`UTF8OffsetConverter` već postoji** (`textUtils/__init__.py:320`). ALUS postavlja `OffsetsTextRange::encoding = "utf_8"`, registruje taj konverter, i onda svaki `_get*Offsets` hook prima AT-SPI bajt-offsete direktno a ostatak steka radi u logičkim indeksima. **Uraditi jednom, u Fazi 5, sa testovima — i više nikad ne razmišljati o tome.**

**Detektor:** test T5.1 — round-trip bajt↔codepoint za emoji (U+1F600), CJK, kombinujući dijakritik, i ćirilicu. Plus T5.4: dekomponovano „é" se prepozna kao **jedan** znak za sricanje.

---

## R7 — `getTextWithFields` sinteza netačna

**Uzrok.** AT-SPI daje ravan tekst + atribute po offsetu, ali **ne daje** NVDA-ov ugnježdeni `controlStart` / `controlEnd` tok komandi. NVDA ga dobija besplatno — iz virtualnog bafera (koji ALUS ne gradi, vidi `01-ARHITEKTURA.md` §9.1) ili iz `CompoundDocument`-a. ALUS ga mora **sintetisati** iz lanaca predaka.

**Posledica.** Od ovog toka zavisi **sve** čitanje dokumenata:
- `commonFieldCount` kontinuitet — bez stabilnog `uniqueID`, najava „lista sa 5 stavki" se ponavlja na **svakoj liniji** liste,
- `isBlock` — cepanje iskaza u say-all-u; bez njega say-all spaja paragrafe,
- `table-*` ključevi — tabela navigacija i zaglavlja,
- najave ulaska/izlaska iz kontejnera.

Ako je ovo netačno, browse mode je neupotrebljiv iako „radi".

**Mera.** Rešenje **već postoji u NVDA, u platformski neutralnom obliku**, i portuje se doslovno:

```
reference/nvda/source/compoundDocuments.py:164-211
    _getControlFieldForObject(obj, ignoreEditableText=True)
    → sintetiše ControlField iz role, states, name, description, level, table koordinata

reference/nvda/source/compoundDocuments.py:331-368
    TreeCompoundTextInfo.getTextWithFields(formatConfig)
    → emituje controlStart/controlEnd/formatChange DIFF-ovanjem lanaca predaka
```

Uz to se prenosi koncept `virtualBuffers._normalizeControlField` (`:393-448`) — **jedno** mesto koje pretvara atribute u kanonske ključeve (`table-id`, `table-rowheadertext`, `isHidden`, `alwaysReportName`, `uniqueID`).

`uniqueID` = AT-SPI `(bus name, object path)` — **stabilniji** od NVDA-ovog `(docHandle, ID)`.

**Detektor:** testovi T6.1–T6.4. Posebno T6.3: kretanje kroz 100 linija ne sme da ponovi najavu kontejnera.

---

## R8 — Varijansa pouzdanosti caret događaja

**Uzrok.** NVDA-ov `editableText._hasCaretMoved` ima **tri strategije sa budžetom vremena**, jer Windows aplikacije lažu o caret događajima. Na Linuxu lažu **GTK, Qt, Electron, Java i LibreOffice — svaki drugačije.** Neke šalju `TextCaretMoved` pre promene, neke posle, neke ne šalju uopšte za programsko pomeranje.

**Posledica.** „Pritisnem strelicu, ne pročita novu liniju" ili „pročita staru liniju". Najiritantniji mogući bug u čitaču ekrana.

**Mera.**
- Tri strategije se **prenose**, ne pojednostavljuju.
- `caretMovementDetectionUsesEvents` ostaje override po behavior-u.
- `_useEvents_maxTimeoutSec` i `_caretMovementTimeoutMultiplier` se **podešavaju po toolkit-u**, i to je **očekivan posao Faze 5**, ne znak greške u dizajnu. Planirati vreme za to.

**Detektor:** test T5.5 — tri odvojena testa (GTK4, Qt6, LibreOffice), sa dozvoljeno različitim podešavanjem timeout-a po toolkit-u.

---

## R9 — Obim projekta

**Uzrok (provereno).** NVDA izvorni kod:

```
source/ ukupno            188.739 linija Pythona
source/NVDAObjects/        28.040
source/gui/                19.523
source/appModules/          9.838   (79 modula)
source/UIAHandler/          7.875
source/speech/              5.721
source/globalCommands.py    5.714   (~300 skriptova)
source/braille/             5.285
source/config/              4.111
source/browseMode.py        3.012
source/virtualBuffers/      2.738
```

**Posledica.** Projekat koji pokuša da bude NVDA ne izlazi nikad. Ovo je najverovatniji način da ALUS propadne — ne tehnički, nego iscrpljivanjem.

**Mera.**
1. **Ne-ciljevi iz `00-VIZIJA.md` §5 su obavezujući.** Izostavljeno: display model screen review, OCR, magnifier (21 komanda), screen curtain, touch, remote access (8 komanda), Java Access Bridge, add-on store, math, vision provideri. To je ~60% NVDA obima.
2. **Brisanja koja štede najviše:** `UIAHandler` + `IAccessibleHandler` + `JABHandler` + `displayModel` + `virtualBuffers` + `NVDAHelper` + `comInterfaces` ≈ **45.000 linija** zamenjeno jednim `alus::a11y` slojem nad `libatspi`.
3. **Brajevi drajveri:** NVDA ima ~40 drajvera za pojedinačne redove. ALUS ima **jedan** (`brlapi`) — brltty već podržava sve te redove i korisnici ga imaju podešenog.
4. **App moduli:** NVDA ima 79. ALUS v1.0 ima **pet** (LibreOffice, tri terminala kao jedan modul, Electron, Nautilus). Pregledačima app modul **ne treba** (§9.4).
5. **Ako se faza širi, obim se reže, ne rok.** Ovo je pravilo, ne preporuka.

**Detektor:** na kraju svake faze, `cloc src/` i poređenje sa budžetom. Ciljni obim ALUS v1.0: **35.000–45.000 linija C++20**.

---

## R10 — Koegzistencija sa Orcom

**Uzrok.** Orca i ALUS se bore za: D-Bus ime `org.gnome.Orca.KeyboardMonitor` (R1), gsettings `org.gnome.desktop.a11y.applications screen-reader-enabled`, i autostart. Na ciljnom sistemu Orca se pokreće iz **dva** mesta:

```bash
ls /etc/xdg/autostart/orca-autostart.desktop
ls ~/.config/autostart/my-orca-autostart.desktop     # korisnikov sopstveni
```

**Posledica.** Ako oba rade, tasteri idu jednom a govor drugom, ili nijedan ne dobije tastere. Zbunjujuće i teško za dijagnozu.

**Mera.** ALUS pri startu:
1. Proveri da li `org.gnome.Orca.KeyboardMonitor` već ima vlasnika. Ako da — **odbije da krene** sa jasnom porukom: „Orca je pokrenuta. Zaustavite je: `pkill orca`."
2. Instalira sopstveni `~/.config/autostart/alus.desktop` i, pri instalaciji, **ponudi** onemogućavanje Orcinih autostart fajlova (ne radi to tiho).
3. Postavi `screen-reader-enabled true` (to je ono što govori toolkit-ima da uključe a11y most).

**Detektor:**
```bash
busctl --user list | grep -q org.gnome.Orca.KeyboardMonitor && echo "KONFLIKT: Orca radi"
gsettings get org.gnome.desktop.a11y.applications screen-reader-enabled
gsettings get org.gnome.desktop.interface toolkit-accessibility
ls /etc/xdg/autostart/orca-autostart.desktop ~/.config/autostart/*orca* 2>/dev/null
```

---

## R11 — Licencna izloženost

**Uzrok.** NVDA je GPL-2.0-or-later. ALUS ne kopira kod, ali preuzima **izvedene radove**: komandne tabele, imena skriptova, `symbols.dic` i `characterDescriptions.dic` po jeziku, tekstove opisa komandi, strukturu `gestures.ini` formata. Dodatno, `tools/extract-nvda-commands.py` **uvozi NVDA module** da bi generisao ALUS podatke.

**Posledica.** Nejasan licencni status blokira pakovanje u distribucije i odbija saradnike.

**Mera (odlučeno, pre prve linije koda).** **ALUS je GPL-2.0-or-later.** Kompatibilno sa svim zavisnostima: `libatspi` (LGPL-2.1+), `liblouis` (LGPL-3.0+; tabele GPL-3.0+), `libbrlapi` (LGPL-2.1+), `glib` (LGPL-2.1+), `libspeechd` (LGPL-2.1+), GTK4 (LGPL-2.1+). Svaki fajl nosi `SPDX-License-Identifier: GPL-2.0-or-later`. `NOTICE` fajl navodi NVDA kao izvor komandnog seta i podataka, sa commit hash-om.

**Detektor:** `reuse lint` u CI; provera da svaki `.cpp`/`.h` ima SPDX zaglavlje.

---

## R12 — Nedostatak testne pokrivenosti za a11y

**Uzrok.** Testiranje čitača ekrana zahteva živu sesiju sa aplikacijama. CI runner-i nemaju display server ni a11y bus.

**Posledica.** Faze 4–8 nemaju automatizovane testove, regresije se otkrivaju ručno, i projekat se oslanja na jednog testera.

**Mera.**
1. **Mock AT-SPI server.** Mali D-Bus servis koji izlaže sintetičko drvo objekata sa `Accessible`, `Component`, `Text`, `Collection`, `Table` interfejsima. Omogućava jedinično testiranje celog `alus::a11y` i `alus::text` sloja bez ekrana. To je ~500 linija i **isplati se u Fazi 4**, pre nego što kod naraste.
2. **`xvfb` + `dbus-run-session` + `at-spi2-registryd`** za integracione testove: pokreni pravu GTK4 test aplikaciju i proveri da ALUS pročita očekivane najave.
3. **Speech viewer kao testni izlaz.** NVDA-ov `speechViewer` pattern: svaki izgovoreni tekst se piše u fajl/socket. Integracioni testovi proveravaju **tekst**, ne zvuk.
4. **Snapshot testovi najava.** „Fokus na ovom dugmetu ⇒ ova sekvenca govornih komanda" kao golden fajlovi.

**Detektor:** `meson test -C build` mora da radi na runner-u bez ekrana za suite-ove `core`, `speech`, `text`, `input`, `a11y` (mock). Suite `integration` zahteva `xvfb`.

---

## R13 — SSML podrška zavisi od modula

**Dodato posle Faze 0** (nalaz N17). **Verovatnoća visoka, uticaj srednji.**

**Uzrok.** `SsipSynth` gradi iskaze SSML-om (`<mark>`, `<break>`, `<prosody>`,
`<voice>`). Šta od toga modul poštuje nije deo SSIP protokola: `espeak-ng`
poštuje `<break time=...>`, `rhvoice` ga **ignoriše**.

**Posledica.** Ponašanje se menja sa modulom koji korisnik izabere, a korisnik
to ne povezuje sa modulom — za njega „ALUS ne pravi pauze".

**Mera (implementirana u Fazi 2).** `ISynth::supports(SynthFeature::Breaks)`
odgovara po modulu, a `supportedCommands()` izostavlja `BreakCommand` kad modul
ne poštuje pauze, pa ga gornji slojevi ne koriste. `timingIsGuaranteed()` je
`false` za svaki modul koji ALUS nije izmerio, i to se **kaže u logu pri
dizanju** (nalaz N24 u `FAZA2.md`: na ovom sistemu podrazumevani modul je
`rhvoice`).

**Detektor:** S2 faza C na svakom modulu; `alus-cli bench-speech --module X`.

---

## R14 — Index oznaka u sredini iskaza se tiho gubi

**Dodato posle Faze 0** (nalazi N15/N16). **Verovatnoća visoka, uticaj
kritičan.** Najteži nov rizik koji je Faza 0 otkrila.

**Uzrok.** espeak-ng izgubi `<mark>` posle granice rečenice kada je klauza pre
njega dovoljno dugačka. Nema greške, nema događaja — oznaka prosto ne postoji.
SSIP putanja to nasleđuje.

**Posledica.** `SpeechManager` koji prati napredak preko index marki zablokira
čekajući indeks koji ne dolazi: **govor stane i ne nastavi se**, a u logu ne
piše zašto. Za čitač ekrana to je potpuni otkaz.

**Mera (implementirana i testirana u Fazi 2).**

1. **Oznake samo na kraju SSIP poruke.** `SsipSynth` cepa iskaz na odvojene
   poruke tamo gde bi oznaka pala odmah posle završne interpunkcije — jedini
   oblik koji je dokazano preživeo. Poruke idu jedna za drugom, pa server sam
   ulančava.
2. **`SpeechManager` ima rokove.** `end` notifikacija (koja kasni, N12) je
   nadzornik: ako server kaže da je završio a oznake nisu stigle, posle kratkog
   roka (`lostIndexGraceMs`, 300 ms) menadžer nastavlja uz `DEBUG_WARNING`.
   Postoji i apsolutni rok (`stallTimeoutMs`, 10 s) za backend koji ne kaže
   ništa. `SpeechManager::lostIndexCount()` je brojač za dijagnostiku.
3. Prijaviti bug upstream espeak-ng-u.

**Detektor:** `spikes/s2-markloss.c` po modulu; `tests/speech/test_manager.cpp`
oznaka `[r14]`; `tests/speech/test_ssip_messages.cpp`; `--suite live` obara
build ako ijedan iskaz ne stigne do kraja.

---

## R15 — Merenje latencije nije pouzdano za sintetizatore sa dugim repom

**Dodato posle Faze 0** (nalaz N17). **Verovatnoća srednja, uticaj nizak.**

**Uzrok.** Čekanje na tišinu pre iteracije istekne kod modula sa dugim repom,
pa detektor onset-a uhvati zaostali zvuk prethodne iteracije (negativan `min`
kod `rhvoice`).

**Mera.** Pri merenju novog modula produžiti rok čekanja na tišinu i odbaciti
negativne uzorke. `--suite live` između iteracija pravi pauzu i otkazuje govor.

**Detektor:** negativan ili nemoguće mali `min` u izveštaju je znak da merenje
ne važi, ne da je modul brz.

---

## R16 — `libspeechd` uništava svoj mutex dok njegova nit još čeka

**Dodato posle Faze 2** (nalaz N25). **Verovatnoća srednja, uticaj nizak do
srednji, faza 9 (putanja gašenja).**

**Uzrok.** `spd_close()` poziva `pthread_mutex_destroy` nad mutexom na kome
reader nit te iste konekcije još stoji u `pthread_cond_wait`. ThreadSanitizer
to prijavljuje kao data race, i to je po standardu nedefinisano ponašanje.
Oba učesnika su u `libspeechd`-u; ALUS samo poziva `spd_close`, a drugog API-ja
za zatvaranje nema.

```
WARNING: ThreadSanitizer: data race
  Write of size 1 by main thread: pthread_mutex_destroy ← spd_close
  Previous atomic read by thread T1: pthread_cond_wait ← libspeechd reader
SUMMARY: ... in spd_close
```

**Posledica.** U najboljem slučaju ništa; u najgorem pad pri gašenju ili pri
`resetConfiguration()` koji menja sintetizator. Pad pri gašenju je za korisnika
skoro nevidljiv, ali pad pri promeni glasa nije.

**Mera.** Zasad: `SsipSynth::terminate()` prvo zove `spd_cancel()` i tek onda
`spd_close()`, pa reader nit ima manje posla u trenutku zatvaranja; i ALUS
nikad ne drži dve konekcije. Ako se pad pojavi u praksi: prijaviti upstream i
razmotriti da se konekcija pri `resetConfiguration()` **ne zatvara** nego
ponovo koristi.

**Detektor:** `./build-tsan/tests/test-speech-live` — jedino mesto gde se vidi,
jer traži pravi server.

---

## Rizici koji su **razrešeni** istraživanjem

Navedeni zato da se ne istražuju ponovo:

| Pretpostavljeni rizik | Nalaz |
|---|---|
| „AT-SPI je previše spor za čitač ekrana" | **Netačno.** Property round-trip 0.053–0.064 ms; `Cache.GetItems` 1837 objekata u 31 ms (~17 µs/objekat). Arhitektura odlučuje, ne IPC. |
| „Treba nam virtualni bafer kao NVDA" | **Ne.** `Collection.GetMatchesFrom` je server-side pretraga; pregledač već održava ravan `Text` sa U+FFFC i `Hypertext`. §9.1 |
| „Wayland ne dozvoljava globalne prečice, treba hak" | **Rešeno i standardizovano.** `org.freedesktop.a11y.KeyboardMonitor` u Mutter 48+ i KWin 6.3.6+. Preostaje samo allowlist (R1). |
| „ALUS taster (Insert/CapsLock) treba ručno implementirati" | **Ne.** `atspi_device_map_keysym_modifier` daje virtualni modifikator (15–31) koji sprovodi kompozitor. |
| „Treba podržati X11 i Wayland odvojeno" | **Ne u jezgru.** `libatspi` ima tri backend-a (`A11yManager`, `X11`, `Legacy`) iza jednog API-ja. |
| „Numpad vs. navigacioni tasteri su problem kao na Windows-u" | **Nisu.** Sa NumLock isključenim, keysym-ovi su imenovani (`XK_KP_Home` ≠ `XK_Home`). Čistije nego NVDA-ov `(vkCode, extended)` par. |
| „Nema brajevih tabela za srpski" | **Ima.** `liblouis` 3.33: `sr-Cyrl.ctb`, `sr-g1.ctb`, `sr-cyrletters.cti`, `sr-latletters.cti`. |
| „Treba pisati drajver po brajevom redu" | **Ne.** `brlapi` (brltty 6.7) pokriva sve redove; ~40 NVDA drajvera se ne portuje. |
| „Treba app modul za Firefox i Chrome" | **Ne.** NVDA ih nema ni na Windows-u — podrška je u objektnom sloju. Na Linuxu jedan overlay za `toolkit_name ∈ {gecko, Chromium}`. |
| „Terminali zahtevaju display model kao u NVDA" | **Ne.** VTE i Konsole izlažu pravi `AtspiText` sa `TextChanged` događajima; `Terminal` behavior radi direktno. |
| „AT-SPI ne pokriva sve što NVDA treba" | **Pokriva.** Provereno interfejs po interfejs: `Action::get_key_binding` → `keyboardShortcut`, `Component::scroll_to` → `scrollIntoView`, `TableCell::get_row_header_cells` → zaglavlja, `Hyperlink::get_uri` → `alus+k`, `FLOWS_TO/FROM` → `alus+numpad3/9`, `DETAILS` → `alus+d`. Vidi `01-ARHITEKTURA.md` §6.2. |
