# Faza 0 — rezultati spike-ova

> Stanje: **FAZA 0 ZAKLJUČENA.** S3, S2 i S2b završeni. S1 automatski deo
> završen (6/6); četiri koraka sa stvarnim pritiscima tastera ostaju
> nedokazana (N19) i prenose se u Fazu 3.
> **Sve tri odluke koje Faza 0 traži su donesene — nijedna ne zavisi od
> ta četiri koraka.**
> Merено 2026-11-04 na: Debian 13, GNOME 48.7, Wayland, AT-SPI 2.56.2,
> Mutter 16 (`libmutter-16.so.0.0.0`), Firefox (Gecko) 153.6.0, Chrome 156.0.8078.79,
> speech-dispatcher 0.12.0, PipeWire (izlaz: Bluetooth `bluez_output`, kodek mSBC).

---

## Sažetak odluka

| Odluka | Status |
|---|---|
| (a) koji input backend | **`A11yManagerBackend` + app_id `org.gnome.Orca`** (S1) |
| (b) koji `ISynth` podrazumevani | **`SsipSynth` ostaje**; `end` nije signal kraja zvuka; `EspeakDirectSynth` planiran za v1.1 (S2, S2b) |
| (c) treba li browse mode-u keš drveta | **ODLUČENO: za quick-nav NE, za ravan tekst DA** (S3) |

---

## S3 — Quick-nav preko `Collection` — **PASS**

Kod: `spikes/s3-collection.py`. Nad živim Wikipedia člankom
(`sr.wikipedia.org/wiki/Никола_Тесла`), 100 skokova po pravilu, oba pregledača
u izolovanoj instanci sa svežim profilom.

### Latencija jednog quick-nav skoka
`GetMatchesFrom(doc, current, rule, CANONICAL, INORDER, count=1, traverse=True)`

| Pravilo | Firefox p50 / p95 | Chrome p50 / p95 |
|---|---|---|
| `h` heading | 0.08 / **0.17** | 0.17 / **0.58** |
| `k` link | 0.07 / **0.13** | 0.10 / **0.13** |
| `t` table | 0.11 / **0.35** | 0.18 / **5.17** |
| `1` heading lvl 1 | 0.29 / **0.92** | 0.63 / **6.10** |
| `2` heading lvl 2 | 0.15 / **0.43** | 0.38 / **1.73** |
| `l` list | 0.09 / **0.17** | 0.17 / **1.49** |
| `i` list item | 0.07 / **0.11** | 0.11 / **0.16** |
| `f` form field | 0.08 / **0.15** | 0.14 / **5.21** |
| `g` graphic | 0.08 / **0.13** | 0.11 / **0.66** |
| `b` button | 0.10 / **0.52** | 0.14 / **4.90** |

**Najgori p95: Firefox 0.92 ms, Chrome 6.10 ms. Budžet 20 ms → PASS u oba.**
Chrome je 6–10× sporiji od Firefox-a, ali i dalje 3× unutar budžeta.

### Odluka (c)

**Browse mode NE treba lokalni keš drveta za quick-nav.** `Collection` je
server-side pretraga i drži se budžeta čak i na Chrome-u, koji ne puni AT-SPI keš
(R3). **Ali** za „ravan tekst cele strane" keš/inkrementalni bafer je obavezan —
vidi nalaz N4.

### Nalazi

**N1 — `interfacematchtype` MORA biti `ALL`. Ovo je zamka koja tiho laže.**
Sa `interfacematchtype = ANY` nad **praznom** listom interfejsa, `GetMatches`
uspeva, ne vraća D-Bus grešku, vraća se za ~0.2 ms i daje **0 rezultata** — na
svakom serveru (GTK/atk-bridge, Clutter, Gecko, Chromium). `ANY` nad praznim
skupom je prazan pogodak. Isto važi za `statematchtype` nad praznim `StateSet`-om.

Dokaz (gnome-shell, rola `push button`, istina po obilasku drveta = 160):

| | rezultata |
|---|---|
| `interfacematchtype = ALL` | **160** |
| `interfacematchtype = ANY` | **0** |

Ispravna kombinacija je Orcina (`orca/ax_collection.py:54-60`):
`state=ALL, attribute=ANY (ili ALL), role=ANY, interface=ALL`.

> Posledica za plan: `architecture.md` mora da zabeleži ove match-tipove kao
> deo ugovora, a ne kao detalj. Pogrešna vrednost ne pada — vrati prazno, pa
> izgleda kao „pregledač ne podržava Collection".

**N2 — `sortBy=FLOW` NIJE implementiran. Plan ga je tražio.**
`GetMatches` sa `FLOW`, `TAB`, `REVERSE_FLOW`, `REVERSE_TAB` vraća **0** u oba
pregledača. Rade samo `CANONICAL` i `REVERSE_CANONICAL`.
`GetMatchesFrom` **ignoriše** `sortBy` u celosti — `CANONICAL`, `FLOW` i `TAB`
daju identičan niz objekata.

Provereno da `CANONICAL` **jeste** redosled čitanja na ovoj strani: prvih pet
naslova izlaze kao `Никола Тесла → Биографија → Детињство... → Школовање → Студије`,
što je redosled u članku.

> Posledica za plan: `phase-plan.md` S3 tačka 3 („proveri da `sortBy=FLOW` daje
> redosled čitanja") je **nemerljiva** — FLOW ne postoji. Faza 6 koristi
> `CANONICAL` i mora da **pretpostavi** da je to redosled čitanja. To je nova,
> nezatvorena pretpostavka: treba je proveriti na stranama sa CSS-om koji menja
> vizualni red (`order`, `flex-direction: row-reverse`, `position: absolute`).

**N3 — `attributes{level=N}` RADI, server-side, u oba pregledača.**
Quick-nav tipke 1–9 ne moraju da filtriraju na klijentu.

| nivo | Firefox | Chrome |
|---|---|---|
| 1 | 1 | 1 |
| 2 | 11 | 10 |
| 3 | 7 | 7 |
| 4 | 21 | 21 |

Radi i sa `attributematchtype` `ALL` i `ANY`; radi i `{"tag": "h2"}`.
Filtriranje po atributu **ne košta ništa dodatno** (Chrome 5.9 ms sa filterom vs
5.7 ms bez). Rezervna varijanta (svi naslovi + čitanje atributa na klijentu) je
merena za slučaj servera koji filter ne podržava: Firefox 0.38 + 2.24 ms,
Chrome 5.80 + 3.12 ms.

**N4 — Nema jeftinog „ravnog teksta strane". Ovo je stvarni oblik rizika R3.**
Plan je tražio merenje `Text::get_text(0, get_character_count())` na telu
dokumenta. Nalaz: `document-web` čvor **ne izlaže tekst strane** — njegov `Text`
sadrži samo po jedan `U+FFFC` (embedded object) za svako dete.
Chrome: `char_count = 1`. Firefox: `char_count = 11`.

Ravan tekst se mora sastaviti obilaskom drveta, i to je skupo:

| | objekata | korisnih karaktera | vreme (p50) |
|---|---|---|---|
| Firefox | 2 151 | 65 525 | **640 ms** |
| Chrome | 4 778 | 130 284 | **1 271 ms** |

> Posledica za plan: Faza 6 **mora** da gradi tekstualni bafer inkrementalno
> (kao NVDA `getTextWithFields`), uz keš, i nikada sinhrono na jedan pritisak
> tastera. Sekundа i po blokiranja je neupotrebljivo. Ovo pojačava R7, ne R3.

**N5 — Pregledač ne gradi a11y drvo bez registrovanog AT klijenta.**
Dok nije registrovan AT-SPI listener, Chrome izlaže samo `frame` bez sadržaja.
Registracija jednog listener-a (`object:state-changed:focused`) je dovoljan
signal. `org.a11y.Status.ScreenReaderEnabled` je bio `true` i to **nije** bilo
dovoljno samo po sebi.

> Posledica: ALUS mora da registruje listener-e **pre** prvog upita nad
> pregledačem, ne lenjo na prvi quick-nav.

---

## S1 — Ulaz tastature na Wayland-u — **automatski deo PASS (6/6), 4 koraka nedokazana**

Kod: `spikes/s1-input.py`. Plan je tražio C + `libatspi`; `libatspi2.0-dev` nije
instaliran, pa je spike u Python/GI. To je ravnopravan dokaz — GI poziva isti
`libatspi.so.0` i isti `org.freedesktop.a11y.KeyboardMonitor` preko D-Bus-a.

Pokrenuto sa **zaustavljenom Orcom** (vidi nalaz N8 o tome kako se zaustavlja).

| Korak | Rezultat |
|---|---|
| 1 — `atspi_device_new()` | **PASS** — `Atspi.DeviceA11yManager` |
| 2 — ime `org.gnome.Orca.KeyboardMonitor` | **PASS** — drži ga naš proces |
| 2b — Mutter propušta metode | **PASS** — sve 6 metoda OK |
| 4 — `map_keysym_modifier(XK_Insert)` | **PASS** — maska 32768 = indeks 15 |
| 5a — `add_key_grab` prihvaćen | **PASS** — grab id 0 |
| 3 — watcher vidi sintetizovan taster | **nemerljivo** — sinteza je no-op (N18) |
| 3, 5b, 6, 7 — stvarni pritisci | **NEDOKAZANO** — klijent se zaglavio (N19) |

### Odluka (a)

**Input backend: `A11yManagerBackend` preko `libatspi` + `atspi_device_set_app_id("org.gnome.Orca")`.**
Mera iz R1 radi. `EvdevBackend` ostaje odložen na v1.1 kako plan i kaže.
**Ali** tri koraka (3, 5b, 6, 7) traže stvarne pritiske tastera i nisu
dokazana — vidi „Šta još treba" na kraju.

### Nalazi

**N6 — Mutter-ov allowlist je stvaran, i mera iz R1 radi. Kontrolni eksperiment.**
Isti spike pokrenut dva puta, samo sa različitim `app_id`-em:

| `app_id` | ime na magistrali | `WatchKeyboard` / `SetKeyGrabs` / `GrabKeyboard` |
|---|---|---|
| `org.gnome.Orca` | dobijeno | **sve OK** |
| `org.alus` | dobijeno | **sve `org.freedesktop.DBus.Error.AccessDenied`** |

Dakle: ime se dobije uvek (D-Bus ga daje svakome), ali **Mutter odbija pozive**
od imena koje nije u allowlist-i. Potvrđeno i da debug zastavica
`a11y-manager-without-access-control` **postoji** u `libmutter-16.so.0.0.0`.

> Potvrđuje R1 i njegovu meru u celosti. Takođe potvrđuje da upstream PR
> (`org.alus.KeyboardMonitor` u allowlist) nije kozmetika — bez njega ALUS
> trajno pozajmljuje Orcino ime.

**N7 — `libatspi` PROGUTA `AccessDenied`. `add_key_grab` laže.**
Sa `app_id = org.alus`, gde je svaki D-Bus poziv odbijen, `add_key_grab()` je
ipak vratio **0 (uspeh)**, bez greške i bez upozorenja. Isto važi za
`map_keysym_modifier`, koje je vratilo ispravnu masku 32768 iako Mutter ne
propušta ništa.

> Posledica za arhitekturu: **ALUS ne sme da zaključuje o zdravlju ulaza iz
> povratnih vrednosti `libatspi`-ja.** `IInputBackend` mora da pri dizanju
> pozove jednu Mutter metodu **direktno preko D-Bus-a** i proveri odgovor, pa
> da tek onda prijavi da ulaz radi. Bez toga ALUS na pogrešnom `app_id`-u
> izgleda zdravo i ne reaguje ni na jedan taster — najgori mogući režim otkaza
> za čitač ekrana.
> Isto važi i za detekciju iz N1 (Collection): dva nezavisna podsistema tiho
> vraćaju „uspeh" na neuspeh. To je obrazac, ne slučaj.

**N8 — Orca se sama vraća. `screen-reader-enabled=false` nije dovoljno.**
`/etc/xdg/autostart/orca-autostart.desktop` i
`~/.config/autostart/my-orca-autostart.desktop` nose `X-GNOME-AutoRestart=true`,
pa je `gnome-session-binary` oživljava posle `pkill`-a — i kada je
`org.gnome.desktop.a11y.applications screen-reader-enabled` već `false`.
Restartovala se **četiri puta**; generisane systemd jedinice
(`app-orca\x2dautostart@autostart.service`) su pri tom bile `inactive` sa
`Restart=no`, dakle nije systemd — nego gnome-session, koji odustane posle
nekoliko pokušaja.

> Posledica za R10 (koegzistencija sa Orcom): „korisnik zaustavi Orcu" nije
> uputstvo koje radi. ALUS-ova instalacija mora da ponudi da **ukloni ili
> maskira autostart unos**, a `alus-daemon` pri dizanju mora da prepozna da ime
> već drži drugi proces i da to **jasno kaže**, umesto da tiho ne radi (N7).

**N9 — `map_keysym_modifier` vraća MASKU, ne indeks.**
Plan (`phase-plan.md` S1 tačka 4) traži „virtualni modifikator u opsegu 15–31" —
to je indeks. `libatspi` vraća `1 << indeks`: za `XK_Insert` dobijamo
**32768 = 1<<15**, tj. indeks **15**, prvi virtualni modifikator.
`AtspiKeyDefinition.modifiers` takođe očekuje masku, pa se vrednost prosleđuje
kakva je. `get_keysym_modifier` vrati istu vrednost.

> Posledica: ispraviti formulaciju u `phase-plan.md`; i upozoriti u arhitekturi,
> jer `1 << 32768` je greška koja se ne vidi u tipovima (u Pythonu je ogroman
> int, u C-u je UB).

**N18 — `Atspi.generate_keyboard_event` je na Wayland-u PRAZNA OPERACIJA.**
Vraća `True`, ali Mutter ne emituje nijedan `KeyEvent` signal. Provereno u tri
oblika (`PRESSRELEASE` sa keysym-om, `STRING`, `SYM`), uz aktivan
`WatchKeyboard` i pretplatu na signal direktno preko D-Bus-a: **0 signala**.
To je put preko XTEST-a, koji na Wayland-u ne postoji.

> **Četvrti slučaj tihog uspeha na neuspeh** u Fazi 0 (posle N1, N7, N15).
> Posledica za obim: NVDA ima funkcije koje **šalju** tastere aplikaciji —
> „pošalji sledeći taster aplikaciji", unos sa brajevog reda, emulacija
> tastature. Na Wayland-u ALUS to **ne može** preko AT-SPI-ja.
> Mora da se eksplicitno upiše u `vision.md` kao **ne-cilj za v1.0**, ili da
> se reši privilegovanim `/dev/uinput` helperom — što je ista cena kao evdev
> backend iz R2, i isto pripada v1.1.

**N19 — klijent koji drži key grab može da se zaglavi i ne pusti ga.**
Pri pokušaju da se koraci 3/5b/6/7 izmere slušanjem stvarnih pritisaka, Python
klijent sa aktivnim `add_key_grab(Insert+t)` i callback-om koji troši taster
**zaglavio se**: nije izašao ni na GLib rok od 90 s, ni na `timeout 140`, ni na
`SIGTERM` — tek na `SIGKILL`, posle 549 s. Za to vreme je držao grab na
korisnikovoj tastaturi i nije prijavio **nijedan** pritisak.

> Zato koraci 3, 5b, 6 i 7 **ostaju nedokazani**: nula uhvaćenih događaja je
> posledica zaglavljivanja, a ne dokaz da watcher ne radi.
>
> Ali samo zaglavljivanje je nalaz, i to ozbiljan: **zaglavljen ALUS može da
> zarobi korisnikovu tastaturu.** Za čitač ekrana to je najgori mogući otkaz —
> korisnik bez govora i bez tastature.
> Mere za Fazu 3:
> 1. Grab se nikada ne drži iz procesa koji može da blokira u D-Bus pozivu;
>    `IInputBackend` radi na svojoj niti sa rokom na svaki poziv.
> 2. `Watchdog` iz Faze 1 mora da **otpusti grab** kada otkrije blokadu, pre
>    nego što pokuša oporavak.
> 3. `alus-daemon` instalira `SIGTERM`/`SIGSEGV` handler koji otpušta grab i
>    oslobađa D-Bus ime — bezuslovno, pre svega ostalog.
> Uzrok zaglavljivanja nije dijagnostikovan i to je prvi posao Faze 3.
>
> **Dijagnostikovano u Fazi 3** (`docs/history/phase-3.md`, N26): uzrok je u
> `libatspi`-jevom `atspi/atspi-device-a11y-manager.c`, koji kompozitor zove
> sa `g_dbus_proxy_call_sync(..., timeout = -1, ...)` **iz `g_timeout_add`
> callback-a**, to jest iz glavne petlje klijenta, i pri tom odbacuje `GError`.
> Petlja zato staje bez roka dok grab-ovi stoje — tačno izmereno ponašanje. To
> je i uzrok N7. ALUS zato ne koristi taj sloj nego govori direktno sa
> `org.freedesktop.a11y.KeyboardMonitor` (phase-3.md O1).

## S2 — Latencija i tačnost govora — **3 od 4 budžeta PASS**

Kod: `spikes/s2-speech.c` (744 linije, C11 + `libspeechd`).
Meri **stvaran zvuk**, ne samo SSIP notifikacije: proces hvata monitor
podrazumevanog izlaza preko `pw-record` i detektuje početak/kraj zvuka sa
tačnošću od 1 ms. Vremenska baza se procenjuje kao minimum
`t_čitanja − ukupno_frejmova/48000` kroz sva čitanja, što daje najtešnju
procenu i ispravlja drift.

Pokrenuto sa **zaustavljenom Orcom** (šumni pod 0), modul `espeak-ng`,
jezik `sr`, 100 iteracija za faze A i B, 20 za index marks, 3 warm-up izgovora.
Izlaz: podrazumevani sink = **Bluetooth, kodek mSBC**.

| Metrika | p50 | p95 | p99 | min | max | Budžet | |
|---|---|---|---|---|---|---|---|
| `speak()` → prvi audio sample | 8.71 | **11.50** | 13.38 | 5.50 | 13.73 | < 50 ms | **PASS** |
| `speak()` → `begin` notifikacija | 1.18 | 1.39 | 1.49 | 0.75 | 1.55 | — | |
| `cancel()` → tišina | 26.47 | **28.95** | 29.29 | 25.58 | 214.81 | < 30 ms | **PASS** |
| `cancel()` → `cancel` notifikacija | 33.24 | 34.61 | 35.21 | 32.38 | 35.24 | — | |
| `end` notifikacija posle zvuka | 293.93 | **301.54** | 302.52 | 183.37 | 302.60 | < 50 ms | **FAIL** |
| abs(`index_mark` − pozicija u zvuku) | 39.18 | **46.12** | 46.23 | 33.60 | 46.23 | < 100 ms | **PASS** |

Nijedna iteracija nije otpala: 100/100 za A i B, 20/20 za index marks.

### Odluka (b)

**`SsipSynth` ostaje podrazumevani `ISynth`** — kako je plan i očekivao.
Početak govora (11.5 ms p95) i prekid (29.0 ms p95) su daleko unutar budžeta,
a index marks su tačni na 46 ms p95 pri budžetu od 100 ms. Pored toga, SSIP
daje korisniku **sve njegove instalirane glasove**, što direktni espeak ne može.

**`EspeakDirectSynth` menja status: iz „samo ako S2 pokaže da treba" u
„planiran za v1.1, sa izmerenim razlogom".** S2b pokazuje da nosi prednost koju
SSIP ne može da da: `mark` je **uzorak-tačan** (0 ms) nasuprot postojanom
pomeraju od −40 ms, a prekid je ograničen ALUS-ovim sopstvenim baferom umesto
29 ms p95 sa repom od 215 ms. To nije važno za čitanje teksta, ali **jeste**
za govorni kursor i praćenje reči. Cena: ALUS mora sam da vlada audio izlazom.

**Ali uz jednu obavezujuću izmenu ugovora:** `end` notifikacija **ne sme** da
bude signal da je zvuk završen (vidi N12). Ulančavanje izgovora ide preko
index marks, kao u NVDA.

> Poređenje sa direktnim `libespeak-ng` u `AUDIO_OUTPUT_SYNCHRONOUS` modu,
> koje plan traži, **nije izmereno** — `libespeak-ng-dev` nije instaliran.
> Odluka (b) ne zavisi od njega jer SSIP prolazi budžete; poređenje ostaje
> otvoreno kao ulaz za v1.1.

### Nalazi

**N10 — `begin` notifikacija nije signal početka zvuka. Stiže 7.5 ms PRE zvuka.**
`begin` dolazi 1.18 ms posle `speak()`, a zvuk 8.71 ms posle. To je SSIP
potvrda prijema, ne događaj iz audio toka.

> Posledica: ALUS ne sme da koristi `begin` da obeleži trenutak početka
> izgovora (npr. za govorni kursor). Jedini pouzdan signal iz toka je
> `index_mark`.

**N11 — tišina posle `cancel()` dolazi PRE `cancel` notifikacije.**
Zvuk stane na 26.47 ms, notifikacija stigne na 33.24 ms — tišina prednjači
~7 ms.

> Posledica: prekid je efektivan odmah; notifikacija je potvrda, a ne događaj.
> ALUS može da računa da je kanal slobodan čim pozove `cancel()`, bez čekanja.
> **Rep postoji:** jedna od 100 iteracija je trajala 214.81 ms (p99 je 29.29).
> Watchdog mora da računa na pojedinačne ispade, ne samo na p95.

**N12 — `end` notifikacija kasni 294 ms. Jedini prekoračen budžet.**
Meri se od poslednjeg **čujnog** sample-a do notifikacije. Raspon je uzak
(183–303 ms), što govori da je uzrok sistemski — tišina koju sintetizator
dodaje na kraj izgovora, plus drenaža bafera — a ne slučajna varijansa.
Bafer Bluetooth sink-a (~20 ms, vidi N11) ovo **ne** objašnjava.

> Posledica za arhitekturu: ako `SpeechManager` ulančava izgovore čekajući
> `end`, dobija **~294 ms mrtvog vazduha između svaka dva izgovora**. Za čitač
> ekrana to je razlika između tečnog i mucavog govora.
> Mera: ulančavanje ide preko `IndexCommand`-a na kraju svakog izgovora, kao
> `speech/manager.py` u NVDA — što `architecture.md` već propisuje kao
> obavezan deo `ISynth` interfejsa. S2 sada daje **merljiv razlog** zašto, a ne
> samo zato što NVDA tako radi.

**N13 — `index_mark` stiže 40 ms PRE svog mesta u zvuku, i to konstantno.**
Srednje odstupanje je **−39.9 ms** (opseg 33.6–46.2 ms). Znak je uvek isti:
notifikacija prednjači zvuku, jer je modul već izgovorio tekst u svoj bafer
dok se zvuk još nije odsvirao.

> Plan je tražio odstupanje < 100 ms i to je zadovoljeno, ali **nije predvideo
> da odstupanje ima postojan znak**. To nije šum, to je pomeraj.
> Posledica: za govorni kursor i praćenje reči, ALUS mora da odloži reakciju
> za izmereni pomeraj, ili da prihvati da kursor ide 40 ms ispred zvuka.
> Pomeraj zavisi od audio uređaja, pa `SsipSynth` treba da ga **izmeri pri
> prvom dizanju** umesto da ga hardkoduje.

**N14 — `pw-record` ne može da hvata izlazni stream tuđe aplikacije.**
Pokušaj merenja pre sink-a (`--target speech-dispatcher-espeak-ng`
bez `stream.capture.sink`) ne pročita nijedan frejm. Jedini praktičan tap je
monitor sink-a, pa izmereni brojevi **sadrže latenciju audio uređaja**.
Za ovaj slučaj to ne menja nijednu odluku: doprinos sink-a je ~20 ms, sva tri
prolazna budžeta prolaze sa rezervom, a `end` kasni red veličine više.

**N15 — espeak-ng TIHO IZGUBI `<mark>` posle granice rečenice.**
Otkriveno u S2b, pa proverено i na SSIP putanji (N16). Oblik koji pada:

```
<speak>dovoljno dugačka klauza. <mark name="m"/> ostatak teksta</speak>
```

Nema greške, nema događaja — oznaka prosto ne postoji. Isti tekst bez tačke
pre oznake radi. Matrica (direktni `libespeak-ng`, 1.52.0):

| Oblik | `mark` stigao |
|---|---|
| kratka klauza, bez tačke pre oznake | da |
| kratka klauza, tačka pre oznake | da |
| **dugačka klauza, tačka pre oznake** | **NE** |
| dugačka klauza, bez tačke pre oznake | da |
| oznaka na samom kraju izgovora (posle tačke) | da |
| oznaka na početku izgovora | da |

**N16 — SSIP putanja NASLEĐUJE N15. Ali oblik za ulančavanje je bezbedan.**
Isti testovi kroz `speech-dispatcher` + `espeak-ng` modul:

| Oblik | `mark` stigao |
|---|---|
| dugačka klauza, tačka pre oznake | **NE (0/1)** |
| dva marka oko dugačkih klauza | **samo prvi (1/2)** |
| **oznaka na samom kraju izgovora** | **da (1/1), latinica i ćirilica** |

> **Ovo je najvažniji nalaz S2-a, i plan ga nije predvideo.**
>
> Dobra vest: mera iz N12 — ulančavanje izgovora preko `IndexCommand`-a na
> **kraju** izgovora — radi. To je upravo oblik koji preživljava.
>
> Loša vest: `mark` **unutar** izgovora je nepouzdan. NVDA-ov `SpeechManager`
> ubacuje index marks **između** delova govorne sekvence da bi znao gde je
> (`speech/manager.py`). Ako se ALUS tako napravi, a deo se završi tačkom i
> dovoljno je dug, oznaka se izgubi i menadžer **zablokira čekajući indeks
> koji nikada ne dolazi**.
>
> Tri mere, po redu prioriteta:
> 1. **Nikada ne stavljati oznaku odmah posle završne interpunkcije.** Umesto
>    toga razbiti izgovor na odvojene `spd_say` pozive, tako da je svaka
>    oznaka na kraju svog izgovora — oblik koji je dokazano bezbedan.
> 2. **`SpeechManager` mora da ima rok po oznaci.** Izostala oznaka ne sme da
>    bude mrtva petlja; posle roka se prelazi na sledeći deo i to se loguje.
> 3. Prijaviti bug upstream espeak-ng-u. Ovo nije ALUS-ov problem po prirodi.

---

## S2b — Direktni `libespeak-ng` u `AUDIO_OUTPUT_SYNCHRONOUS` modu

Kod: `spikes/s2-espeak.c`. Poređenje koje plan traži za S2. 100 iteracija,
glas `sr`, `buflength` 10 ms, sample rate 22050 Hz, espeak-ng 1.52.0.

| Metrika | p50 | p95 | p99 | max |
|---|---|---|---|---|
| `espeak_Synth()` → prvi PCM blok | **0.05** | 0.10 | 0.11 | 0.13 ms |
| ukupna sinteza izgovora | 0.87 | 1.24 | 1.34 | 1.40 ms |
| trajanje zvuka tog izgovora | 1421.7 | — | — | — ms |
| real-time faktor | **0.00061** | 0.00087 | — | — |

**Sinteza je ~1640× brža od realnog vremena.** Za izgovor od 1.42 s potrebno je
0.87 ms procesorskog vremena.

**`mark` je uzorak-tačan: 200/200 oznaka u 100/100 iteracija, odstupanje 0.000 ms.**
`espeak_EVENT.audio_position` uvek pada unutar bloka isporučenog u istom
callback-u, po konstrukciji — nasuprot postojanom pomeraju od −40 ms kod
SSIP-a (N13).

Šta se ovde **ne** meri: „`speak()` → prvi sample na zvučnoj kartici" i
„`cancel()` → tišina". U `SYNCHRONOUS` modu espeak ne pušta zvuk — ALUS vlada
izlazom, pa oba broja zavise od ALUS-ovog izlaznog bafera, koji ALUS bira
(ovde 10 ms), a ne od sintetizatora.

### Poređenje tri putanje

| | SSIP + espeak-ng | SSIP + rhvoice | direktni espeak-ng |
|---|---|---|---|
| prvi zvuk, p95 | **11.5 ms** | 54.6 ms (FAIL) | 0.1 ms sinteza + ALUS bafer |
| `cancel()` → tišina, p95 | 29.0 ms (rep 215 ms) | 1057 ms (FAIL) | ALUS bafer (10 ms) |
| `end` posle zvuka | 294 ms (FAIL) | 136 ms (FAIL) | nije primenljivo |
| tačnost `mark`-a | −40 ms, postojan pomeraj | 9/10 promašeno | **0 ms, uzorak-tačna** |
| SSML `<break>` | poštuje | **ignoriše** | poštuje |
| `<mark>` posle tačke | **gubi (N16)** | neizmerljivo | **gubi (N15)** |
| korisnikovi glasovi | svi instalirani | — | samo espeak |
| ALUS vlada zvukom | ne | ne | **da** |

**N17 — `rhvoice` nije upotrebljiv kao podrazumevani.**
Lošiji je na svakoj izmerenoj osi: prvi zvuk 47.5 ms p50 (espeak 8.7),
prekid p99 2200 ms, i ignoriše SSML `<break>` pa je 9 od 10 index marks
promašeno. Negativan `min` od −39.78 ms u merenju prvog zvuka je **artefakt
moje metode**, ne rezultat: čekanje na tišinu pre iteracije istekne kod
sintetizatora sa dugim repom, pa detektor onset-a uhvati zaostali zvuk.
Za espeak-ng ta zamka se nije aktivirala (šumni pod 0, `min` 5.50 ms, svih
100 uzoraka verodostojno), pa espeak brojevi stoje.

> Posledica: `rhvoice` ostaje kao korisnikov izbor glasa kroz SSIP, ali
> `architecture.md` ne sme da računa na vremenske garancije kada korisnik
> izabere modul koji nije espeak-ng. Budžeti su svojstvo **modula**, ne SSIP-a.

## Ispravke koje plan duguje

### `phase-plan.md`

1. **S1 tačka 4** — „virtualni modifikator u opsegu 15–31" je indeks, a
   `map_keysym_modifier` vraća **masku** (`1 << indeks`). Preformulisati (N9).
2. **S3 tačka 1 i 3** — `sortBy=FLOW` **ne postoji** ni u Firefox-u ni u
   Chrome-u; `GetMatchesFrom` sortBy uopšte ignoriše. Zameniti `CANONICAL`, i
   dodati novu proveru: „da li je CANONICAL redosled čitanja i na stranama sa
   preuređenim CSS-om (`order`, `row-reverse`, `position:absolute`)" (N2).
3. **S3 tačka 5** — `Text::get_text(0, char_count)` na telu dokumenta ne daje
   tekst strane, nego niz `U+FFFC`. Zameniti merenjem obilaska (N4).
4. **S2 tačka „end notifikacija posle poslednjeg sample-a < 50 ms"** — budžet
   je prekoračen 6×, i to sistemski. Ili se budžet briše i zamenjuje zahtevom
   „ulančavanje ne sme da zavisi od `end`", ili ostaje kao poznat otkaz sa
   dokumentovanom merom (N12).
5. **S2 tačka za `index_mark`** — dodati da se meri i **znak** odstupanja, ne
   samo apsolutna vrednost. Znak je postojan (−40 ms) i to je pomeraj koji se
   kompenzuje, ne šum (N13).
6. **Faza 2, tabela deliverable-a** — `EspeakDirectSynth` više nije
   „samo ako S2 pokaže da treba". S2b je pokazao izmerenu prednost
   (uzorak-tačan `mark`), pa ulazi kao **planiran za v1.1**, vezan za
   govorni kursor.

### `architecture.md`

1. **Match-tipovi za `Collection` su obavezujući ugovor,** ne detalj:
   `state=ALL, attribute=ALL/ANY, role=ANY, interface=ALL`. Pogrešna vrednost
   ne pada — vrati prazno (N1).
2. **`IInputBackend` mora da verifikuje pristup direktnim D-Bus pozivom** pri
   dizanju. `libatspi` proguta `AccessDenied` i `add_key_grab` vrati uspeh na
   neuspeh (N7).
3. **`SsipSynth` ne sme da koristi `begin` ni `end` kao audio događaje.**
   `begin` je potvrda prijema (N10), `end` kasni ~294 ms (N12).
   Ulančavanje ide isključivo preko `IndexCommand`-a.
4. **`SsipSynth` treba da izmeri pomeraj `index_mark`-a pri prvom dizanju**
   (−40 ms na ovom sistemu), jer zavisi od audio uređaja (N13).
5. **Obrazac „tihi uspeh na neuspeh" se javlja u TRI nezavisna podsistema**
   (N1 za `Collection`, N7 za `libatspi` ulaz, N15/N16 za espeak `<mark>`).
   Vredi ga zapisati kao arhitekturno pravilo: svaki eksterni podsistem dobija
   proveru živosti pri dizanju i rok na svaki očekivan odgovor, a ne veru u
   povratnu vrednost.
6. **`SpeechManager` ne sme da stavlja index mark odmah posle završne
   interpunkcije,** nego da razbije izgovor na odvojene SSIP poruke tako da je
   svaka oznaka na kraju svoje poruke (N16). Oznaka u sredini izgovora se
   tiho gubi.
7. **`SpeechManager` mora da ima rok po oznaci.** Izostala oznaka ne sme da
   bude mrtva petlja — posle roka se prelazi na sledeći deo i loguje se
   `DEBUG_WARNING` (N16).
8. **Vremenski budžeti govora su svojstvo MODULA, ne SSIP-a** (N17).
   Arhitektura ne sme da obećava latenciju kada korisnik izabere modul koji
   nije `espeak-ng`.

### `risks.md`

1. **R1** — uzrok, posledica i mera su **potvrđeni merenjem** (N6).
   Dopisati kontrolni eksperiment: `org.alus` → `AccessDenied` na svaku
   metodu, `org.gnome.Orca` → sve prolazi. Status: „mera dokazana".
2. **R3** — uzrok je tačan, ali **posledica je pogrešno locirana**. Quick-nav
   nije u opasnosti (N3: p95 ≤ 6.1 ms pri budžetu 20 ms). U opasnosti je
   sastavljanje ravnog teksta (N4: 1.3 s na Chrome-u) — to pripada R7 i tamo
   treba da bude pojačano.
3. **R4** — speech-dispatcher drži tri od četiri dela ugovora. Četvrti
   (`end`) ne drži, ali postoji mera. Status: „delimično potvrđen, mera
   poznata" umesto „dva backend-a".
4. **R10** — dopisati N8: `screen-reader-enabled=false` **ne zaustavlja
   Orcu**, jer `gnome-session` je oživljava preko `X-GNOME-AutoRestart=true`.
   Mera: instalacija nudi uklanjanje autostart unosa; `alus-daemon` pri dizanju
   prepozna da ime već drži drugi proces i to **jasno kaže**.
5. **Novi rizik R13 — SSML podrška zavisi od modula.** `rhvoice` ignoriše
   `<break time=...>`; `espeak-ng` ga poštuje. Ako `SsipSynth` gradi izgovore
   SSML-om, ponašanje se menja sa modulom. Detektor: S2 faza C na svakom
   modulu. Verovatnoća visoka, uticaj srednji.
6. **Novi rizik R14 — index mark u sredini izgovora se tiho gubi.**
   **Verovatnoća visoka, uticaj kritičan, faza 2.** Uzrok: espeak-ng izgubi
   `<mark>` posle granice rečenice kada je klauza pre njega dovoljno dugačka
   (N15); SSIP putanja to nasleđuje (N16). Posledica: `SpeechManager` koji
   prati napredak preko index marks zablokira čekajući indeks koji ne dolazi —
   govor stane i ne nastavi se. Mera: oznake samo na kraju poruke + rok po
   oznaci. Detektor: `spikes/s2-markloss.c`, pokrenut na svakom modulu.
   **Ovo je najteži novi rizik koji je Faza 0 otkrila,** i nije bio u
   početnom registru.
7. **Novi rizik R15 — merenje latencije govora nije pouzdano za sintetizatore
   sa dugim repom.** Verovatnoća srednja, uticaj nizak. Uzrok: čekanje na
   tišinu pre iteracije istekne, pa detektor onset-a uhvati zaostali zvuk
   (negativan `min` kod `rhvoice`, N17). Mera: pri merenju novog modula
   produžiti rok čekanja na tišinu i odbaciti negativne uzorke.

---

## Šta još treba za zatvaranje Faze 0

### 1. Četiri koraka S1-a — prenose se u Fazu 3

Koraci 3 (watcher vidi sve tastere), 5b (grab troši `Insert+t`), 6 (dupli
`Insert`) i 7 (numpad bez NumLock-a) **nisu dokazani**. Dva puta probano:

- **sintetizovanim tasterima** → nemoguće: `generate_keyboard_event` je na
  Wayland-u prazna operacija (N18);
- **slušanjem stvarnih pritisaka** → klijent sa aktivnim grab-om se zaglavio i
  nije prijavio nijedan događaj (N19).

**Zašto to ne blokira Fazu 0.** Odluka (a) ne stoji na ovim koracima. Ono što
R1 pita — „propušta li Mutter ALUS do tastature" — dokazano je korakom 2b i
kontrolnim eksperimentom N6: sa `app_id = org.gnome.Orca` prolaze sve metode,
sa `org.alus` svaka vraća `AccessDenied`. Da pristup ne postoji, S1 bi pao
tamo, a nije.

Ono što ostaje su pitanja o **ponašanju** (koji keysym-ovi, da li se troši,
koliki je prozor za dupli pritisak) i ona pripadaju Fazi 3, gde se ulaz i piše.
Prvi posao Faze 3 je zato dijagnoza N19 — zaglavljivanja — jer bez stabilnog
klijenta ta merenja ionako nisu verodostojna.

> **ZATVORENO u Fazi 3**, 2026-11-05 (`docs/history/phase-3.md`, „Interaktivno
> merenje"). Sva četiri koraka su prošla protiv `alus-daemon`-a, sa izmerenim
> brojevima: watcher vidi sve tastere i ne troši ih; grab troši `Insert+t`
> (taster ne stiže do terminala); dupli ALUS taster je detektovan 120 ms posle
> otpuštanja, a aplikaciji ga propušta **kompozitor sam** (nalaz N34, pa N18
> ovde nije prepreka); numpad bez NumLock-a daje `KP_Home`/`KP_End`, bez cifara.
> Merenje je uz to otkrilo dva defekta u ALUS-u (N33, N35), oba ispravljena.
>
> Istorijska napomena ispod ostaje jer opisuje stanje pre toga.

Kada se N19 reši, pokrenuti u terminalu sa zaustavljenom Orcom:

```bash
cd /home/dare/ALUS
python3 spikes/s1-input.py --interactive --json /tmp/s1-interactive.json
```

### 2. Vraćanje Orce — **urađeno**

Orca je vraćena: maske `systemd` jedinica uklonjene,
`screen-reader-enabled=true`, proces radi i drži
`org.gnome.Orca.KeyboardMonitor`. Sistem je u prvobitnom stanju.

Za interaktivni S1 prolaz Orcu treba **ponovo** zaustaviti, jer samo jedan
proces može da drži to ime:

```bash
gsettings set org.gnome.desktop.a11y.applications screen-reader-enabled false
systemctl --user stop 'app-orca\x2dautostart@autostart.service' \
                      'app-my\x2dorca\x2dautostart@autostart.service'
pkill -x orca
# ... pokreni S1 --interactive ...
# pa vrati:
gsettings set org.gnome.desktop.a11y.applications screen-reader-enabled true
orca &
```

Ako se Orca ipak vrati sama (N8), maskiraj jedinice privremeno:
`systemctl --user mask ...`, pa `unmask` posle.

### 3. Paketi — **instalirani**

`libatspi2.0-dev` 2.56.2, `libespeak-ng-dev` 1.52.0, `liblouis-dev` 3.33.0,
`libbrlapi-dev` 6.7, `meson` 1.7.0, `ninja-build`, `valgrind` 3.24.0.

- Poređenje sa direktnim `libespeak-ng` je **izmereno** → S2b iznad.
- S1 **nije** prepisan u C. Python/GI verzija dokazuje isti rizik preko istog
  `libatspi.so.0` i istog D-Bus interfejsa, a kod iz Faze 0 se po planu baca.
  Prepisivanje ne bi dalo nijedan nov podatak.
- `libbrlapi` nema `.pc` fajl u Debianu 13 (`pkg-config --modversion libbrlapi`
  pada), ali su header i biblioteka tu. Faza 7 mora da ga linkuje ručno
  (`-lbrlapi`), bez `pkg-config`-a.

### 4. Čišćenje

Ostale su dve izolovane instance pregledača sa profilima u
`/tmp/claude-1000/.../scratchpad/` (`chrome-profile`, `ff-profile`). Mogu se
zatvoriti; ništa od rezultata ne zavisi od njih.

## Datoteke

| Datoteka | Sadržaj |
|---|---|
| `spikes/s1-input.py` | S1 — ulaz tastature (Python/GI, 430 linija) |
| `spikes/s2-speech.c` | S2 — govor preko SSIP-a (C11 + libspeechd, 744 linije) |
| `spikes/s2-espeak.c` | S2b — direktni libespeak-ng (C11, ~290 linija) |
| `spikes/s2-markloss.c` | S2c — **detektor za R14**: tiho izgubljen index mark |
| `spikes/s3-collection.py` | S3 — Collection quick-nav (Python/GI, 425 linija) |
| `spikes/RESULTS.md` | ovaj dokument — **jedino što preživljava Fazu 0** |

---

## Predaja Fazi 1

Faza 1 (`phase-plan.md`, Infrastruktura jezgra) je jedino što može da počne —
Faze 2 i 3 zavise od nje. Ništa u Fazi 1 ne zna o AT-SPI-ju, govoru ni
tasterima, pa je sve jedinično testabilno i ništa od nedokazanog u S1 je ne
blokira.

**Okruženje je spremno:** `meson` 1.7.0, `ninja-build`, `valgrind` 3.24.0,
`g++` 14.2 sa C++20 (`<coroutine>`, `<format>`, `<ranges>` provereni),
`glib-2.0` 2.84.4, `atspi-2` 2.56.2, `speech-dispatcher` 0.12.0,
`liblouis` 3.33.0.

**Šta Faza 0 nosi u Fazu 1 kao ulaz:**

1. **Arhitekturno pravilo iz N1/N7/N15/N18** — četiri nezavisna podsistema
   tiho prijavljuju uspeh na neuspeh. Svaki eksterni podsistem dobija proveru
   živosti pri dizanju i rok na svaki očekivan odgovor. Ovo oblikuje
   `alus::core::Watchdog` i `alus::core::Log` iz Faze 1.
2. **N19** — `Watchdog` mora da ume da **otpusti resurs** (grab, D-Bus ime)
   pri otkrivenoj blokadi, ne samo da prijavi `isAttemptingRecovery`. To menja
   njegov interfejs i treba ga ugraditi odmah, ne dograđivati u Fazi 3.
3. **N12/N16** — `SpeechManager` iz Faze 2 ne sme da čeka na `end` ni na
   oznaku u sredini izgovora. To je zahtev koji Faza 1 mora da podrži rokovima
   u `TaskRegistry`-ju.
4. **21 ispravka plana** iz sekcije „Ispravke koje plan duguje" — pre pisanja
   koda Faze 1 treba primeniti bar one za `architecture.md` (8 stavki).

**Prvi korak Faze 1:** `meson setup build`, skelet `alus::core` i testovi
T1.1–T1.10 iz `phase-plan.md`. Izlazni kriterijum je već napisan i izvršiv.
