# ALUS — Fazni plan

> Dokument 3 od 4. Pretpostavlja `vision.md` i `architecture.md`.
>
> **Pravilo:** izlazni kriterijum svake faze je **izvršiv test**, ne opis. Ako se ne može pokrenuti, faza nije završena.

---

## Pregled

| Faza | Naziv | Zavisi od | Procena |
|---|---|---|---|
| **0** | Spike-ovi — dokazivanje tri rizika | — | 1 nedelja |
| **1** | Infrastruktura jezgra | 0 | 3 nedelje |
| **2** | Govor | 1 | 3 nedelje |
| **3** | Ulaz i skriptovi | 1 | 3 nedelje |
| **4** | Objektni model | 1, 2, 3 | 4 nedelje |
| **5** | Tekst i review | 4 | 4 nedelje |
| **6** | Browse mode | 5 | 5 nedelja |
| **7** | Brajev red | 5 | 3 nedelje |
| **8** | App moduli i ekstenzibilnost | 6 | 3 nedelje |
| **9** | GUI i lokalizacija | 8 | 3 nedelje |

Faze 2 i 3 su nezavisne i mogu paralelno. Faza 7 može paralelno sa 6.

**Posle Faze 5 ALUS je upotrebljiv** za desktop aplikacije (GNOME Shell, Nautilus, Text Editor, terminal). Posle Faze 6 je upotrebljiv za web. To su dve prirodne tačke za alfa izdanje.

---

## Faza 0 — Spike-ovi

**Jedina svrha: dokazati tri rizika pre nego što se uloži trud u dizajn.** Bez ova tri rezultata ostatak plana je spekulacija. Kod iz Faze 0 se **baca**; ne ulazi u repo kao biblioteka, samo kao `spikes/`.

### S1 — Ulaz tastature na Wayland-u

```
spikes/s1-input.c   (~150 linija, C, libatspi + glib)
```

Dokazati, u ovom redu:
1. `atspi_device_new()` vrati `AtspiDeviceA11yManager` na GNOME Wayland.
2. Sa `atspi_device_set_app_id(dev, "org.gnome.Orca")` (Orca zaustavljena) proces dobije ime `org.gnome.Orca.KeyboardMonitor` i Mutter propusti pozive.
3. `atspi_device_add_key_watcher` prima **sve** pritiske tastera, uključujući u Wayland-native aplikacijama.
4. `atspi_device_map_keysym_modifier(dev, XK_Insert)` vrati virtualni modifikator — **MASKU `1 << indeks`, gde je indeks u opsegu 15–31** (ispravka iz nalaza N9: za `XK_Insert` se dobija 32768 = `1<<15`; `1 << 32768` je greška koja se ne vidi u tipovima).
5. `atspi_device_add_key_grab` sa tim modifikatorom **potroši** `Insert+t` — aplikacija ga ne vidi.
6. Dupli `Insert` u roku 500 ms se može detektovati i propustiti aplikaciji.
7. Numpad sa NumLock isključenim daje `XK_KP_Home` itd. (ne `XK_KP_7`).

**Izlazni kriterijum:**
```bash
# Orca zaustavljena:
gsettings set org.gnome.desktop.a11y.applications screen-reader-enabled false
pkill orca
# Spike pokrenut; u drugom prozoru editor:
./spikes/s1-input
# Očekivano u logu: svi tasteri viđeni; "Insert+t CONSUMED"; editor ne dobije "t".
busctl --user list | grep KeyboardMonitor    # mora da pokaže naš proces
```

Ako S1 padne → `risks.md` R1/R2, evdev backend, i **ceo plan se revidira** jer je obim veći.

### S2 — Latencija i tačnost govora

```
spikes/s2-speech.c   (~200 linija, C, libspeechd + glib)
```

Izmeriti, 100 ponavljanja, prijaviti p50/p95/p99:

| Metrika | Budžet |
|---|---|
| od `speak()` do prvog audio sample-a | **< 50 ms** |
| od `cancel()` do tišine | **< 30 ms** |
| odstupanje `index_mark` notifikacije od stvarne pozicije u zvuku | **< 100 ms** |
| `end` notifikacija posle poslednjeg sample-a | < 50 ms |

Isto izmeriti sa direktnim `libespeak-ng` u `AUDIO_OUTPUT_SYNCHRONOUS` modu, za poređenje.

**Izlazni kriterijum:** tabela brojeva u `spikes/RESULTS.md`, i odluka: ostaje li `SsipSynth` podrazumevani (očekivano: da) ili `EspeakDirectSynth`.

```bash
./spikes/s2-speech --iterations 100 --report
# Mora da izmeri i: SSML <mark> stiže kao 700 INDEX MARK
grep -i indexing /etc/speech-dispatcher/modules/espeak-ng.conf
```

### S3 — Quick-nav preko `Collection`

```
spikes/s3-collection.py   (~100 linija, Python/GI — spike sme Python)
```

Nad **živim** Firefox i Chromium prozorom sa stvarnom stranom (npr. Wikipedia članak):

1. `Collection.GetMatchesFrom(obj, rule{role=HEADING}, sortBy=FLOW, tree=INORDER, count=1, traverse=True)` — izmeri latenciju.
2. Isto za `role=LINK`, `role=TABLE`, i `role=HEADING + attributes{level=2}`.
3. Proveri da `sortBy=FLOW` daje **redosled čitanja**, ne redosled drveta.
4. Proveri da `attributes{level=N}` radi za `1`–`9` quick-nav.
5. Izmeri koliko traje `Text::get_text(0, get_character_count())` na telu dokumenta (koliko je „ravan tekst" brz).

**Izlazni kriterijum:** p95 latencija jednog quick-nav skoka **< 20 ms** u oba pregledača. Ako ne — `risks.md` R3, i Faza 6 mora da uvede lokalni keš drveta.

```bash
python3 spikes/s3-collection.py --browser firefox --url https://sr.wikipedia.org/
python3 spikes/s3-collection.py --browser chromium --url https://sr.wikipedia.org/
```

### Rezultat Faze 0

`spikes/RESULTS.md` sa brojevima i tri odluke: (a) koji input backend, (b) koji `ISynth` podrazumevani, (c) treba li browse mode-u keš. Tek tada se piše produkcioni kod.

---

## Faza 1 — Infrastruktura jezgra

Red je izveden iz zavisnosti. Ništa ovde ne zna o AT-SPI-u, govoru ni tasterima — zato se sve može testirati jedinično.

### Deliverables

| Modul | Sadržaj | Referenca u NVDA |
|---|---|---|
| `alus::core::Cached<T>` | keš property-ja, invalidacija po ciklusu, per-objekat registar | `baseObject.py:121-178` |
| `alus::core::Queue` | jedan FIFO, ograničen flush (`size()+1`), heartbeat po itemu | `queueHandler.py:61-69` |
| `alus::core::TaskRegistry` | C++20 coroutine-i, jedan `resume()` po ciklusu | `queueHandler.py:80-102` |
| `alus::core::Loop` | `GMainLoop`, `PumpRequest` koalescer, `tick()` sa fiksnim redom | `core.py:990-1049` |
| `alus::core::{Action,Filter,Decider,AccumulatingDecider,Chain}` | extension points, slabe reference, snapshot iteracija | `extensionPoints/` |
| `alus::core::EventDispatcher` | lanac sa `nextHandler`, brojači čekajućih događaja | `eventHandler.py:109-166` |
| `alus::core::Watchdog` | heartbeat, nadzorna nit, `isAttemptingRecovery` | `watchdog.py` |
| `alus::core::Log` | nivoi uključujući `IO` i `DEBUG_WARNING`, degradacija očekivanih izuzetaka | `logHandler.py:333-383` |
| `alus::config` | `GKeyFile`, profili, trigeri, `resetConfiguration` | `config/`, `core.py:315-419` |
| `alus-daemon` | main, parsiranje argumenata, single-instance preko D-Bus imena, SIGTERM | `core.py:684-1088` |

### Izlazni kriterijumi (svi moraju da prođu)

```bash
meson setup build && meson compile -C build
meson test -C build --suite core          # svi jedinični testovi

# T1.1 — pump koalescer
#   1000 requestPump() poziva u jednom ciklusu → najviše 1 pump
# T1.2 — Immediate nadograđuje Delayed
# T1.3 — flush je ograničen: funkcija koja se sama re-queue-uje ne zaglavi pump
# T1.4 — Cached<T>: dva get() u istom ciklusu = 1 poziv gettera;
#        posle invalidateAll() = 2 poziva
# T1.5 — coroutine task: N co_yield → N ciklusa, ni manje ni više
# T1.6 — EventDispatcher: handler koji ne pozove next() zaustavi lanac
# T1.7 — isPendingEvents() tačno prijavi noviji događaj za isti objekat
# T1.8 — Watchdog: blokiran callback → isAttemptingRecovery true u < 1 s
# T1.9 — extension point: handler koji umre se tiho deregistruje
# T1.10 — config: resetConfiguration() restartuje sve podsisteme bez restarta procesa

./build/alus-daemon --version
./build/alus-daemon --no-speech &          # digni, pumpaj prazan red
kill -TERM %1                              # čisto gašenje, bez curenja (valgrind)
```

**Definicija završenog:** `valgrind --leak-check=full ./build/alus-daemon --no-speech --exit-after 5` bez curenja i bez neinicijalizovanih čitanja.

---

## Faza 2 — Govor

### Deliverables

| Modul | Napomena |
|---|---|
| `alus::speech::ISynth` | interfejs; `IndexCommand` obavezan |
| `alus::speech::SsipSynth` | `libspeechd`, SSML mod, notifikacije, marshal na glavnu nit |
| `alus::speech::EspeakDirectSynth` | **odloženo na v1.1** sa izmerenim razlogom (S2b: uzorak-tačan `mark`); vezano za govorni kursor |
| `alus::speech::Command` | cela hijerarhija iz `speech/commands.py` |
| `alus::speech::Priority` | `NORMAL`/`NEXT`/`NOW` |
| `alus::speech::SpeechManager` | §4.1 arhitekture — indeksi, prioriteti, callback-ovi, cancel, latch |
| `alus::speech::SentenceBuffer` | granica rečenice + prenos `LangChangeCommand` |
| `alus::speech::Presenter` | `speak()` normalizacioni tok sa fiksnim redom |
| `alus::text::Symbols` | `SymbolLevel`, `.dic` parser, registar rečnika |
| `alus::speech::Dictionaries` | builtin → default → voice → temp |
| `alus::audio::WavePlayer` | `feed(data,size,onDone)`, `sync`, `stop`, `pause` |
| `alus::audio::Tones` | bip generator |
| `alus::ui::message()` | ekvivalent NVDA `ui.message` |

### Izlazni kriterijumi

```bash
meson test -C build --suite speech

# T2.1 — ui::message("zdravo") se izgovori
# T2.2 — SpeechManager: NOW prekine NORMAL; NORMAL se NASTAVI posle
# T2.3 — NOW NE prekine prethodni NOW
# T2.4 — index wrap: isIndexABeforeB tačan preko granice 9999→1
# T2.5 — CallbackCommand se izvrši na glavnoj niti, u ispravnom trenutku
# T2.6 — zastarela najava fokusa se IZBACI iz reda pre izgovaranja
#         (simulirano: queue 5 focus najava, poslednja je validna → izgovori se 1)
# T2.7 — ParamChangeTracker: posle prekida se brzina/visina VRATE
# T2.8 — SentenceBuffer: 10 linija bez tačke → 1 iskaz, ne 10
# T2.9 — simboli: alus+p nivoi menjaju izgovor interpunkcije
# T2.10 — rečnik izgovora: unos sa dijakritikom i granicom reči radi (ICU regex)
# T2.11 — latencija: p95 speak→zvuk < 50 ms
#         Budžet je PODELJEN (vidi phase-2.md O3): merenje stvarnog zvuka traži
#         audio tap (nalaz N14) i ostaje u spikes/s2-speech.c; CI test
#         (--suite live) čuva SSIP putanju, na koju ALUS-ov kod utiče.
# T2.12 — rizik R14: izgubljena index oznaka ne sme da zaustavi govor zauvek
#         (rok posle `end`-a + apsolutni rok; oznake samo na kraju poruke)

./build/alus-cli speak "Проба говора на српском" --rate 20
./build/alus-cli bench-speech --iterations 100
```

**Izvršeno:** `docs/history/phase-2.md` — status svakog kriterijuma, šest odstupanja sa
razlogom i šest novih nalaza (N20–N25).

---

## Faza 3 — Ulaz i skriptovi

### Deliverables

| Modul | Napomena |
|---|---|
| `alus::input::IInputBackend` | apstrakcija; `AtspiDeviceBackend` prva implementacija |
| `alus::input::KeyNames` | `keysym` → NVDA ime, puna numpad tabela, lokalizovane labele |
| `alus::input::KeyboardGesture` | modifikatori, ALUS taster, `isCharacter`, `shouldReportAsCommand`, `speechEffectWhenExecuted` |
| `alus::input::GestureManager` | normalizacija sa sortiranjem, tri sloja mape, `execute()` sa 14 koraka iz `inputCore.py:542-654` |
| `alus::input::GestureMap` | `gestures.ini` parser/serijalizator, `None =` odvezivanje, `kb:` emulacija |
| `alus::script::Registry` | `ALUS_SCRIPT` makro, kategorije, `lastScriptRepeatCount()` |
| `alus::script::dispatch` | 10-stepeni lookup iz §5.1 |
| `alus::input::InputHelp` | `alus+1`, `bypassInputHelp`, ne pojeda svoj prekidač |
| `alus::input::TypedChars` | echo znakova i reči, bafer reči |
| `tools/extract-commands.py` | generator `data/gestures-builtin.ini` + `data/scripts.csv` |

### Izlazni kriterijumi

```bash
python3 tools/extract-commands.py --source ./NVDA --out data/
wc -l data/scripts.csv                     # očekivano ~300 skriptova
meson test -C build --suite input

# T3.1 — normalizacija: "alus+shift+t" == "shift+alus+t" == "T+SHIFT+NVDA"
# T3.2 — kb(laptop): pobedi kb: kad je raspored laptop
# T3.3 — prioritet mapa: user pobedi locale pobedi driver pobedi built-in
# T3.4 — "None = kb:h" odveže h
# T3.5 — trappedKeys: pojeden key-down ⇒ pojeden key-up
# T3.6 — dupli ALUS taster u 500 ms ⇒ pravi Insert ide aplikaciji
# T3.7 — lastScriptRepeatCount: 0/1/2/3; reset na nevezanom gestu između
# T3.8 — 10-stepeni lookup: tree interceptor sa passThrough=on se PRESKOČI
#         osim za skriptove sa ignoreTreeInterceptorPassThrough
# T3.9 — isScriptWaiting() prekine dug govor
# T3.10 — srpski locale gestures.ini: alus+š radi leftMouseClick u laptop rasporedu

# Ručni test (jedini način za pravi ulaz):
./build/alus-daemon &
#   alus+1   → "Ulazna pomoć uključena"; svaki taster izgovori svoj skript
#   alus+1   → izlaz iz ulazne pomoći (prekidač se NE poje)
#   alus+f2  → sledeći taster ide direktno aplikaciji
#   alus+4   → "Izgovaranje komandnih tastera uključeno"

# Isti ručni test, sa merenjem i sa vraćanjem Orce na izlazu:
bash tools/phase3-input-check.sh
```

**Izvršeno:** `docs/history/phase-3.md` — status svakog kriterijuma, četrnaest
odstupanja sa razlogom i deset novih nalaza (N26–N35), uključujući **dijagnozu
N19**: uzrok je `g_dbus_proxy_call_sync(..., -1, ...)` iz glavne petlje u
`libatspi`-jevom `AtspiDeviceA11yManager`, pa ALUS govori direktno sa
`org.freedesktop.a11y.KeyboardMonitor` (O1).

---

## Faza 4 — Objektni model

### Deliverables

| Modul | Napomena |
|---|---|
| `alus::a11y::Registry` | `libatspi` init, `RegisterEvent` sa `properties`, lista događaja iz §6.1 |
| `alus::a11y::EventLimiter` | dvostepeni limiter iz §3.4 — **nije opciono** |
| `alus::a11y::Object` | ceo property set iz §6.2, sve `Cached<T>` |
| `alus::a11y::OverlayRegistry` | kompozicija behavior-a; `choose()` ugovor kao NVDA |
| `alus::a11y::{Role,State,OutputReason}` | mapirajuće tabele 133→156 i 45→53, profinjavanje preko `xml-roles`/`level`/`tag` |
| `alus::a11y::Api` | focus + lanac predaka, `getFocusDifferenceLevel`, navigator, review, foreground, mouse, desktop |
| `alus::speech::Presenter` (dopuna) | `getObjectSpeech`, `getPropertiesSpeech`, role/state labele |
| `alus::a11y::behaviors::{ProgressBar,Dialog,FocusableUnfocusableContainer,ToolTip,Notification}` | platformski neutralno |

### Izlazni kriterijumi

```bash
meson test -C build --suite a11y     # mock AT-SPI server u testovima

# T4.1 — Object property-ji: name/role/states/value/description tačni za GTK4 dugme
# T4.2 — Cached<T> nad AT-SPI: 30 property čitanja u jednom ciklusu = 1 batch
# T4.3 — EventLimiter: 1000 ChildrenChanged u 10 ms ⇒ ≤10 po aplikaciji po ciklusu
# T4.4 — EventLimiter: 20 focus događaja ⇒ emituje se 1 (najnoviji)
# T4.5 — Window:Activate obriše čekajući StateChanged:Focused za isti objekat
# T4.6 — role mapiranje: svaki AtspiRole ima ALUS Role (nema UNKNOWN osim namerno)
# T4.7 — xml-roles="banner" ⇒ landmark; level=2 ⇒ heading nivo 2
# T4.8 — getFocusDifferenceLevel tačno izračuna promenu lanca
# T4.9 — setFocusObject vrati false za mrtav objekat i pozivalac to ispita

# Ručni test:
./build/alus-daemon &
#   alt+tab kroz aplikacije  → svaka najavi ime i tip prozora
#   tab kroz GNOME Settings  → svaki kontrol: ime, uloga, stanje
#   alus+tab                 → najavi fokusirani objekat; 2× sriče; 3× opisi znakova
#   alus+numpad4/6/8/2       → objektna navigacija
#   alus+numpadEnter         → aktivira objekat (Action::do_action)
#   alus+numpad5             → najavi navigator objekat
```

**Ovo je tačka na kojoj ALUS prvi put „radi".**

**Izvršeno:** `docs/history/phase-4.md` — status svih devet kriterijuma, sedamnaest
odstupanja sa razlogom i devet novih nalaza (N36–N44). Dva od njih čitač ekrana
čine nemim bez ijedne greške u logu: **N36** (`atspi_set_main_context` pre
`atspi_init` se tiho odbacuje, pa ne stiže nijedan događaj) i **N43**
(platformski događaj mora da traži pump ciklus). **N40** je bio
use-after-free pri gašenju. Izmereno uživo: 60 čitanja property-ja u jednom
ciklusu = **0 AT-SPI poziva** (T4.2), 279 događaja u 10 s, 0 rola bez
mapiranja na stvarnoj radnoj površini.

---

## Faza 5 — Tekst i review

### Deliverables

| Modul | Napomena |
|---|---|
| `alus::text::OffsetConverter` | **UTF-8 varijanta** — §7.3, uraditi jednom i sa testovima |
| `alus::text::OffsetsTextRange` | svi hook-ovi iz §7.2 nad `AtspiText` |
| `alus::text::WordSeg` | ICU segmentacija reči i rečenica |
| `alus::nav::Review` | tri moda; `screen` = flat object review iz §10 |
| `alus::nav::EditableText` | tri strategije detekcije karete, podešavanje po toolkit-u |
| `alus::a11y::behaviors::{LiveText,Terminal}` | diff; `Terminal` stavlja otkucane znakove u red |
| `alus::speech::SayAll` | coroutine, callback-driven praćenje karete, `SentenceBuffer` |
| `alus::nav::ScreenExplorer` | port `screenExplorer.py`, 90 linija |
| `alus::nav::ParagraphNav` | tri stila paragrafa |

### Izlazni kriterijumi

```bash
meson test -C build --suite text

# T5.1 — OffsetConverter: emoji (U+1F600), CJK, kombinujući dijakritik
#         — round-trip bajt↔codepoint tačan u sva tri slučaja
# T5.2 — get_string_at_offset(LINE) granice odgovaraju ALUS UNIT_LINE
# T5.3 — UNIT_WORD preko ICU tačan za srpski, engleski, japanski
# T5.4 — unicodeNormalize: dekomponovano "é" se prepozna kao JEDAN znak za sricanje
# T5.5 — EditableText: detekcija pomeraja karete radi u GTK4, Qt6 i LibreOffice
#         (tri odvojena testa; dozvoljeno različito podešavanje timeout-a)
# T5.6 — LiveText diff: 1000 linija izlaza ⇒ najavi samo nove
# T5.7 — SayAll: 100 linija teksta ⇒ kontinuirano, bez pauze po liniji,
#         caret prati zvuk (odstupanje < 1 linija)
# T5.8 — SayAll se prekine na pritisak tastera u < 50 ms
# T5.9 — flat object review: prostorno sortiranje tačno za prozor sa 3 kolone

# Ručni test — cela NVDA review/caret grupa:
#   numpad7/8/9     prethodna/trenutna/sledeća linija
#   numpad4/5/6     prethodna/trenutna/sledeća reč
#   numpad1/2/3     prethodni/trenutni/sledeći znak
#   numpad2 2×      opis znaka;  3×  decimalna i heksadecimalna vrednost
#   numpadPlus      say all od review cursora
#   alus+downArrow  say all od karete
#   alus+upArrow    trenutna linija;  2× sriče;  3× opisi znakova
#   alus+numpad7/1  promena review moda
#   alus+f / alus+shift+f   formatiranje na kareti / review cursoru
#   alus+f9/f10     markiraj i kopiraj
# U GNOME Text Editor: pisanje, strelice, Home/End, control+strelice, selekcija
# U gnome-terminal:    `ls -la`, `cat dugacak-fajl` → najava novog izlaza
```

**Posle Faze 5 → alfa izdanje za desktop aplikacije.**

**Izvršeno:** `docs/history/phase-5.md` — status svih devet kriterijuma, petnaest
odstupanja sa razlogom i sedam novih nalaza (N45–N51). **T5.3 je jedini
kriterijum koji nije u celosti ispunjen**, i to zbog spoljnog paketa:
`libicu-dev` nije instaliran, pa je segmentacija srpskog i engleskog tačna a
japanskog nije (O2). Dva nalaza menjaju pretpostavke plana: **N45** (nijedan
toolkit ne odgovara `GRANULARITY_PARAGRAPH`, pa su dva stila paragrafa iz §11.2
jedini način da kretanje po paragrafima na Linuxu postoji) i **N49** (opseg
teksta čuvan NA objektu je referentni prsten — svako polje za unos koje je
korisnik fokusirao procurilo bi sebe; našao valgrind). Izmereno uživo: flat
screen review aktivnog prozora = 4 dela, 3 reda, 49 objekata, **67 poziva,
4,9 ms**, a `copy()` tog opsega **0 poziva**.

---

## Faza 6 — Browse mode

Najrizičnija faza. §9.3 (`getTextWithFields`) je najvažniji nov kod u projektu.

### Deliverables

| Modul | Napomena |
|---|---|
| `alus::nav::TreeInterceptor` | životni ciklus, `isReady`, `passThrough`, `RootProxyTextRange` |
| `alus::nav::ControlFieldSynth` | **port `compoundDocuments._getControlFieldForObject`** |
| `alus::nav::AncestorChainDiff` | **port `TreeCompoundTextInfo.getTextWithFields`** |
| `alus::nav::FieldNormalizer` | koncept `_normalizeControlField`: kanonski `table-*`, `uniqueID`, `isHidden` |
| `alus::nav::CursorManager` | virtualni caret, selekcija, pretraga |
| `alus::nav::BrowseMode` | `passThrough` (`alus+space`), auto focus mode, elements list |
| `alus::nav::QuickNav` | `MatchRule` tabela iz §9.2, `Collection::GetMatchesFrom` |
| `alus::nav::DocumentWithTableNavigation` | `control+alt+strelice` i rodbina |
| `alus::nav::CompoundDocument` | LibreOffice Writer |
| `alus::speech::Presenter` (dopuna) | `getTextInfoSpeech`, `getControlFieldSpeech`, `getFormatFieldSpeech` |

### Izlazni kriterijumi

```bash
meson test -C build --suite browsemode

# T6.1 — ControlFieldSynth: GTK/web objekat ⇒ ControlField sa tačnim role/states/name/level
# T6.2 — AncestorChainDiff: ugnježdena lista u tabeli ⇒ tačan controlStart/controlEnd tok
# T6.3 — commonFieldCount kontinuitet: kretanje kroz 100 linija ne ponavlja
#         najavu „lista sa 5 stavki" na svakoj liniji
# T6.4 — uniqueID stabilan preko AT-SPI (bus name, object path)
# T6.5 — QuickNav: svaki od 22 tipa iz §9.2 nađe tačan element u test strani
# T6.6 — sortBy=FLOW daje redosled ČITANJA, ne redosled drveta
# T6.7 — quick-nav latencija p95 < 20 ms (Firefox i Chromium)
# T6.8 — auto focus mode: tab u <input> ⇒ focus mode; escape ⇒ browse mode
# T6.9 — single-letter nav NE radi u focus mode-u (10-stepeni lookup filter)
# T6.10 — table nav: control+alt+strelice po ćelijama, zaglavlja se najave jednom

# Ručni test — Wikipedia članak u Firefox-u i Chromium-u:
#   alus+space      browse ↔ focus
#   strelice        čitanje po linijama/znakovima
#   h / shift+h     naslovi;  1–9 po nivou
#   k / shift+k     linkovi;  u / v  neposećeni / posećeni
#   t, l, i, b, f, e, x, r, c, g, q, s, d, o, m, p, a, w, n
#   alus+f7         lista elemenata
#   alus+k          odredište linka
#   alus+downArrow  say all cele strane
#   control+alt+strelice   navigacija po tabeli
# LibreOffice Writer: čitanje dokumenta, formatiranje, tabele
```

**Posle Faze 6 → beta izdanje.**

---

## Faza 7 — Brajev red

Može paralelno sa Fazom 6.

### Deliverables

`alus::braille::{Region,TextRegion,ObjectRegion,TextInfoRegion,CursorRegion}`, `Buffer` sa prozorom i word wrap-om, `Handler` sa `_regionsPendingUpdate` koalescenjem i single-slot upisom, `Louis` (sa custom table resolver-om, `dotsIO`, kopiranjem `typeform`), `BrlapiDisplay`, `BrailleInput` (`bk:` gestovi), tethering, cursor routing.

### Izlazni kriterijumi

```bash
meson test -C build --suite braille

# T7.1 — liblouis: sr-Cyrl.ctb prevede ćirilični tekst tačno
# T7.2 — typeform lista se NE menja na mestu (kopija se prosleđuje)
# T7.3 — dotsIO: ćelije dolaze sa bitom 0x8000, maskira se na 0xFF
# T7.4 — custom table resolver nađe tabelu iz add-on direktorijuma
# T7.5 — word wrap: sva 4 moda tačna na granici reda
# T7.6 — single-slot upis: 100 frame-ova u jednom ciklusu ⇒ 1 upis (poslednji)
# T7.7 — _regionsPendingUpdate: 50 pomeraja karete u ciklusu ⇒ 1 update
# T7.8 — saveWindow/restoreWindow: prozor stabilan preko update-a regiona

# Ručni test (sa stvarnim redom ili brltty u virtualnom modu):
brltty -b no -d /dev/null -x no &          # virtualni red za test
./build/alus-daemon &
#   alus+control+t   tethering focus ↔ review
#   cursor routing   pomeri caret na tu ćeliju
#   scroll back/forward
#   bk:dot7 / bk:dot8 / bk:dot7+dot8
```

---

## Faza 8 — App moduli i ekstenzibilnost

### Deliverables

`alus::app::{Module,Registry}` (ključ: `/proc/<pid>/exe` + app name + toolkit), pet mehanizama prilagođenja iz §12.2, `shouldAllowFocusEvent` / `lastQueuedFocusObject` / `container` override, `atomicProfileSwitch`, plugin C ABI (`alus-plugin.h`) sa `dlopen` i dvofaznom instalacijom.

App moduli za v1.0:

| Modul | Šta rešava |
|---|---|
| `libreoffice` | `CompoundDocument` za Writer, tabele u Calc-u, status bar |
| `gnome-terminal` / `konsole` / `xfce4-terminal` | `Terminal` behavior, podešavanje diff algoritma |
| `firefox` / `chromium` | **verovatno nije potreban** — overlay u objektnom sloju (§9.4) |
| `electron` (generički po toolkit-u) | VS Code i rodbina: blokiraj skup tree interceptor, `TextInfo` override |
| `nautilus` | prikaz liste fajlova |

### Izlazni kriterijumi

```bash
meson test -C build --suite app

# T8.1 — app modul se učita po imenu izvršnog fajla i po toolkit-u
# T8.2 — chooseOverlays: clsList.remove i prepend rade
# T8.3 — event_<name> koji ne pozove next() pojede događaj
# T8.4 — atomicProfileSwitch: prebacivanje aplikacije rekonstruiše profil 1×, ne N×
# T8.5 — plugin: ABI verzija se proverava, nekompatibilan plugin se odbije sa logom
# T8.6 — plugin može da doda skript, overlay i event handler
# T8.7 — plugin koji se deinstalira dok je učitan se obriše pri sledećem startu

# Ručni test:
#   LibreOffice Writer:  čitanje dokumenta, alus+shift+c/r zaglavlja
#   LibreOffice Calc:    navigacija po ćelijama, zaglavlja redova/kolona
#   VS Code:             uređivanje koda, bez zamrzavanja
#   Nautilus:            navigacija po fajlovima

# Isti ručni test, sa merenjem ključeva i ABI-ja pre njega:
bash tools/phase8-app-check.sh --manual
```

**Izvršeno:** `docs/history/phase-8.md` — status svih sedam kriterijuma, petnaest
odstupanja sa razlogom i devet novih nalaza (N66–N74). **Sva četiri app modula
su prepisana posle merenja na živim aplikacijama**, jer drvo koje LibreOffice i
Nautilus stvarno izlažu nije ono koje §12.1 pretpostavlja: LibreOffice je
`gtk` a ne `gail` (N66), njegova polja status bara nemaju ime nego Text
interfejs (N67), Calc nema `table row` (N69), a datoteka u Nautilus-u je
`table row` čije je ime duplirano (N70). **N68** je defekt Faze 4 koji je ovo
merenje našlo: Calc prijavljuje `2147483647` dece, pa je jedan `children()`
bio 32 GB alokacije i dve milijarde D-Bus poziva — čitač ekrana se tu ne
usporava, stane. Add-on ABI je proveren dvaput: četiri test plugin-a se
prevode **kao C** u build-u, i `alus-plugin.h` se **instalira**, pa se plugin
prevodi i protiv instaliranog header-a samog.

---

## Faza 9 — GUI i lokalizacija

### Deliverables

GTK4 + libadwaita prozor postavki (govor, brajev red, tastatura, pregled objekata, formatiranje dokumenta, gestovi), dijalog gestova sa `getAllGestureMappings` introspekcijom, gettext infrastruktura, `sr` prevod, `sr` `symbols.dic` i `characterDescriptions.dic` (iz NVDA), generisana referenca komandi, `.desktop` autostart fajl, systemd user unit.

### Izlazni kriterijumi

```bash
meson test -C build --suite gui
meson test -C build --suite commands       # T9.6, dodat posle merenja (N99)
meson compile -C build alus-pot            # generiše .pot
msgfmt --check po/sr.po -o /dev/null       # prevod validan

# T9.1 — alus+control+g i ostali prečici otvore odgovarajuća podešavanja
# T9.2 — dijalog gestova: dodavanje/brisanje gesta upiše u ~/.config/alus/gestures.ini
# T9.3 — skript bez opisa se NE pojavi u dijalogu (isto kao NVDA)
# T9.4 — LANG=sr_RS.UTF-8: sve najave na srpskom
# T9.5 — generisana referenca komandi se poklapa sa data/scripts.csv
# T9.6 — svaka prečica iz ugrađene mape nešto RADI: ili implementaciju, ili
#        izgovorenu najavu da komande nema u ovoj verziji, ili je namerno
#        nevezana jer taster pripada aplikaciji (N18). Kriterijum je dodat
#        posle N99: T9.5 ga nije mogao uhvatiti jer referenca i tabela komandi
#        dele izvor, pa se savršeno poklapaju i o komandi koje nema.

# Instalacija:
meson install -C build --destdir /tmp/stage
# .desktop autostart, systemd --user unit, gsettings integracija:
gsettings set org.gnome.desktop.a11y.applications screen-reader-enabled true

# Isti ručni test, sa merenjem pre njega — uključujući živu proveru da
# otvaranje prozora NE zamrzava jezgro (nalaz N79):
bash tools/phase9-gui-check.sh
bash tools/phase9-gui-check.sh --manual
```

**Izvršeno:** `docs/history/phase-9.md` — status svih pet kriterijuma **svih pet
prošlo**, petnaest odstupanja sa razlogom i trinaest novih nalaza (N75–N87).
**Prozor podešavanja je sopstveni proces, a ne deo demona**, i to je izmereno a
ne izabrano: u procesu čitača ekrana jezgro je stalo **7,5 sekundi** i prozor
nije mogao da se pročita ni jednom svojstvo, jer ALUS-ov sinhroni AT-SPI poziv
o sopstvenom prozoru odgovara iz iste petlje koju taj poziv blokira (**N79**,
`get_role failed: Did not receive a reply`). Prevod nije pisan od nule: **738 od
1102 msgid-a je uzeto iz NVDA-inog srpskog kataloga po msgid-u** i
transliterirano u ćirilicu, a 364 je napisano rukom — oba kataloga, `sr` i
`sr@latin`, su **100% prevedena**. Prvi prolaz prevoda je našao dva defekta
koja nijedan test ne bi: najave su se gradile spajanjem fragmenata rečenice
(**N75**) i množina se odlučivala u C++-u, a srpski ima tri forme (**N76**).

---

## Pravila koja važe u svim fazama

1. **Faza nije završena dok joj testovi ne prođu u CI.** Testovi iz prethodnih faza se ne smeju pokvariti.
2. **Merenja latencije su testovi, ne beleške.** `speak→zvuk < 50 ms` i `quick-nav < 20 ms` se mere u CI i obaraju build.
3. **Svaki podsistem je restartabilan u procesu** (NVDA-ov `resetConfiguration` model). Testira se u svakoj fazi koja dodaje podsistem.
4. **Ne-ciljevi iz `vision.md` su obavezujući.** Ako se faza širi, obim se reže, ne rok.
5. **Očekivane greške ne puštaju zvuk greške** (§3.8). Čitač ekrana koji cvrkuće je neupotrebljiv.
6. **Kod iz NVDA se ne kopira**, logika se prenosi. Svaki fajl nosi `SPDX-License-Identifier: GPL-2.0-or-later`.
