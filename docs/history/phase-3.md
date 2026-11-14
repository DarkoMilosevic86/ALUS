# Faza 3 — Ulaz i skriptovi: rezultati

> Izveštaj o izvršenoj fazi. Prati `phase-plan.md` → *Faza 3 — Ulaz i skriptovi*.
> Datum: 2026-11-05. Prethodne faze: 1 (jezgro) i 2 (govor), zelene.

## Status: ZAVRŠENA — svi kriterijumi prošli, merenje izvršeno i potvrđeno

```bash
python3 tools/extract-commands.py --source ./NVDA --out data/
wc -l data/scripts.csv                 # 441 linija: 440 skriptova + zaglavlje
meson test -C build --suite input      # 48 slučajeva, 773 tvrdnje, zeleno
meson test -C build                    # core + config + daemon + speech + input
meson test -C build --setup=valgrind --suite input --suite core   # 0 grešaka
```

Ulazni sloj je napisan, povezan u demon i izmeren — uključujući **četiri
koraka S1-a koje je Faza 0 ostavila nedokazanim** i ručni test iz plana.
Merenje je izvršeno 2026-11-05 u 10:47 na ciljnom sistemu
(`tools/phase3-input-check.sh`, Orca zaustavljena, govor kroz RHVoice); svi
koraci su prošli, a log je otkrio dva defekta koja su ispravljena. Vidi
poglavlje „Interaktivno merenje".

---

## Prvi posao faze: dijagnoza N19 — **rešena, u izvornom kodu `libatspi`-ja**

Faza 0 je ostavila N19 kao najozbiljniji otvoren nalaz: klijent sa aktivnim
`add_key_grab` se zaglavio, držao korisnikovu tastaturu 549 s i izašao samo na
`SIGKILL`. „Uzrok zaglavljivanja nije dijagnostikovan i to je prvi posao Faze
3" (`spikes/RESULTS.md`).

Dijagnoza je završena i nije spekulacija — stoji u izvornom kodu
`at-spi2-core 2.56.2`, `atspi/atspi-device-a11y-manager.c`:

```c
static void refresh_grabs (AtspiDeviceA11yManager *manager_device) {
  …
  g_dbus_proxy_call_sync (manager_device->keyboard_monitor, "SetKeyGrabs",
                          g_variant_builder_end (&builder),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,        /* ← bez roka */
                          NULL, NULL /* ← GError se odbacuje */);
}
static gboolean refresh_keys_timeout_cb (gpointer user_data) {   /* ← iz petlje */
  refresh_grabs (device); …
}
```

Tri činjenice, sve tri u pet linija koda:

1. **`timeout = -1`** — poziv čeka **neograničeno**.
2. **Poziv se izvršava iz `g_timeout_add` callback-a**, to jest **iz glavne
   petlje klijenta**. Kompozitor koji ne odgovori zato blokira celu klijentovu
   petlju, bez roka, dok grab-ovi ostaju instalirani. To je tačno ponašanje
   koje je Faza 0 izmerila: GLib rok nije stigao da se izvrši jer petlja nije
   radila, a Python nije mogao da obradi `SIGTERM` jer se ne izvršava bajtkod
   dok je glavna nit u C pozivu.
3. **`GError` je `NULL`** — `AccessDenied` od kompozitora **ne može** da se
   vidi. To je N7 iz Faze 0, isti red koda.

Isto važi za `grab_keyboard` i `ungrab_keyboard` (takođe `call_sync`, takođe
`-1`, takođe bez greške).

**Posledica za ALUS i za ovaj izveštaj:** mera protiv N19 nije „pažljivije
koristiti `AtspiDevice`" — taj sloj se ne može popraviti izvana. ALUS zato
govori direktno sa `org.freedesktop.a11y.KeyboardMonitor` (odstupanje O1), gde
je **svaki poziv asinhron sa konačnim rokom i svaki odgovor se čita**, a
`emergencyRelease()` može da otpusti grab-ove sa druge niti i bez glavne petlje.

---

## Izlazni kriterijumi

| Test | Šta proverava | Status |
|---|---|---|
| **T3.1** | normalizacija: `alus+shift+t` == `shift+alus+t` == `T+SHIFT+NVDA` | **PASS** — `test_gesture.cpp` |
| **T3.2** | `kb(laptop):` pobedi `kb:` kad je raspored laptop | **PASS** — `test_dispatch.cpp` |
| **T3.3** | prioritet mapa: user → locale → driver → built-in | **PASS** — `test_dispatch.cpp`, sva četiri sloja uzastopno |
| **T3.4** | `None = kb:h` odveže `h` | **PASS** — i provera da odvezivanje važi **po klasi**, ne globalno |
| **T3.5** | pojeden key-down ⇒ pojeden key-up | **PASS** — `test_manager.cpp`, i obrnuti slučaj (nepojeden down ⇒ nepojeden up) |
| **T3.6** | dupli ALUS taster u 500 ms ⇒ pravi Insert ide aplikaciji | **PASS** — detekciju radi ALUS, dostavu **kompozitor sam** (N34); izmereno: drugi pritisak 120 ms posle otpuštanja |
| **T3.7** | `lastScriptRepeatCount` 0/1/2/3, reset na nevezanom gestu | **PASS** — i provera da se broji **skript**, ne taster (dva različita tastera na isti skript) |
| **T3.8** | tree interceptor sa `passThrough=on` se preskoči, osim `ignoreTreeInterceptorPassThrough` | **PASS** — plus `isReady` filter i ceo redosled 10 koraka |
| **T3.9** | `isScriptWaiting()` prekine dug govor | **PASS u dva dela** — vidi O8 |
| **T3.10** | srpski locale `gestures.ini`: `alus+š` radi `leftMouseClick` u laptop rasporedu | **PASS** — `test_catalog.cpp`, nad **stvarnim** `data/locale/sr/gestures.ini` |

```bash
python3 tools/extract-commands.py --source ./NVDA --out data/
# data/scripts.csv: 440 scripts (390 in v1.0 scope, 50 deferred)
# data/gestures-builtin.ini: 319 built-in gesture bindings
# data/key-labels.ini: 78 key labels
# data/locale: 28 locale gesture maps (sr included)
```

Plan je očekivao „~300 skriptova"; ekstraktor ih nalazi 440 jer `browseMode`
registruje quick-nav **programski** (44 tipa × 2 smera), što ručno prebrojavanje
ne vidi. 21 kategorija, tačno kao u NVDA.

---

## Odstupanja od plana, sa razlogom

### O1 — ulazni backend je direktan D-Bus, ne `AtspiDevice`

`architecture.md` §5.2 kaže „Backend: `AtspiDevice`". ALUS umesto toga
razgovara sa `org.freedesktop.a11y.KeyboardMonitor` direktno
(`src/input/a11y_manager_backend.cpp`). Razlog je dijagnoza N19 iznad: taj
sloj **jeste** zaglavljivanje (sinhroni pozivi bez roka iz glavne petlje) i
**jeste** N7 (`GError` se odbacuje). Oba problema su u biblioteci i ne mogu se
izbeći njenim korišćenjem.

Šta se time dobija:

- svaki poziv kompozitoru je asinhron, sa rokom (`[input] callTimeout`, 2000 ms)
  i **pročitanim** odgovorom; neuspeh ide u log kao `DebugWarning`, ne kao
  greška sa zvukom (§3.8, pravilo 5);
- `RequestName` se zove sa `DO_NOT_QUEUE` i **rezultat se proverava**, pa ALUS
  zna kad drugi čitač ekrana drži ime (N8) i to **kaže naglas**;
- `WatchKeyboard` pri dizanju je poziv čiji se odgovor čita — to je „jedna
  Mutter metoda direktno preko D-Bus-a" koju N7 zahteva pre nego što ALUS
  prijavi da ulaz radi;
- `emergencyRelease()` radi sa bilo koje niti i bez glavne petlje.

Šta se time gubi: X11 i legacy AT-SPI putevi koje bi `libatspi` dao gratis.
Oni su **drugi `IInputBackend`** — apstrakcija za to postoji od početka, a Z6
(„isti build, bez uslovne kompilacije") se ispunjava **izborom backend-a u
vremenu izvršavanja**, ne drugačijim prevođenjem. Faza 3 nema libatspi
zavisnost uopšte; ona dolazi u Fazi 4, za objektni model.

### O2 — `libxkbcommon` ulazi u stek

`architecture.md` §1 ga nije imao. Potreban je zbog jedne konkretne
rečenice: gest `kb(laptop):alus+š` iz srpskog locale-a mora da postane keysym
koji kompozitor poredi sa grab-om, a to je **legacy** keysym `XK_scaron`
(0x1b9), ne Unicode forma 0x1000161. `xkb_utf32_to_keysym` to radi kanonski i
to je ista biblioteka koju kompozitor koristi, pa dve strane ne mogu da se
razidu. Alternativa (ručna tabela za srpska slova, greška u logu za ostalo) je
obrazac tihog otkaza koji je Faza 0 našla četiri puta. Odluku je odobrio
korisnik.

### O3 — ekstraktor **parsira** NVDA izvor, ne uvozi ga

Plan (§5.4) kaže „uveze NVDA module". To je na Linuxu nemoguće:
`globalCommands` tranzitivno uvozi `comtypes`, `winUser`, `winInputHook` i
`ctypes.windll`. Lažiranje ~40 modula bi značilo verovati atrapama da se
ponašaju kao original. `tools/extract-commands.py` zato čita izvor
Python-ovim `ast` modulom: ništa se ne izvršava, radi na svakoj mašini, i to je
isti izvor istine. Razume četiri načina na koja NVDA zapisuje vezivanja
(`@script` dekorator, `__gestures` rečnik, docstring, programski `qn()` iz
`browseMode`).

### O4 — opisi, kategorije i built-in vezivanja su **podaci**, ne kod

Plan je tražio `ALUS_SCRIPT` makro sa `.description` i `.gestures` na mestu
vezivanja. ALUS ima taj makro, ali on nosi **samo ono što menja ponašanje**;
opis, kategorija, zastavice i podrazumevani tasteri dolaze iz
`data/scripts.csv` i `data/gestures-builtin.ini` preko `script::Catalog`.
Razlog: ~300 komandi sa opisima koji su istovremeno NVDA korisnička
dokumentacija. Ručna transkripcija bi bila nekoliko hiljada linija i svaka
greška u njoj je komanda koja slepom korisniku pogrešno kaže šta radi.
§5.4 to i traži: ekstraktor je izvor istine.

### O5 — T3.6: detekcija je ALUS-ova, dostava je **kompozitorova** (ispravljeno posle merenja)

Prva verzija ovog odstupanja je tvrdila da je dostava pravog Insert-a na
Wayland-u nemoguća, zbog N18 („AT-SPI ne može da ubaci taster"). **To je bilo
pogrešno i merenje ga je oborilo:** ugovor `KeyboardMonitor` interfejsa
*zahteva* da kompozitor sam propusti drugi pritisak grabovanog modifikatora
aplikaciji (N34).

ALUS zato:

1. detektuje dupli pritisak u roku `multiPressTimeout` (izmereno: 120 ms
   između otpuštanja i drugog pritiska) i **ne troši** drugi pritisak;
2. pita backend `modifierDoublePressReachesApplication()`; na ovom backend-u je
   odgovor „da", pa ALUS **ne pokušava** ništa da ubacuje;
3. na platformi koja ne radi ni jedno ni drugo to kaže u logu kao
   `DebugWarning` (ne greška sa zvukom) — pošteno umesto tiho.

Z2 je time ispunjen **bez** `/dev/uinput` helpera. N18 i dalje stoji za ono na
što se stvarno odnosi: emulaciju tastera (`kb:` unosi u mapi) i brajev unos,
gde ALUS treba da **pošalje** taster koji korisnik nije pritisnuo.

### O6 — skup grab-ova **zavisi od konteksta** (novo, nije bilo u planu)

Ovo je najvažnija arhitekturna posledica Wayland modela i nije se videla kad je
plan pisan. NVDA odlučuje **po pritisku**, posle lookup-a, da li da pojede
taster. Kompozitor tu odluku traži **unaprede**, kroz `SetKeyGrabs`.

Ako bi ALUS grabovao sve što je u mapama, grabovao bi `h`, `k`, `t`, `1`–`9`
**globalno**, jer browse mode veže ta slova — i kucanje bi prestalo da radi u
svakoj aplikaciji, pri čemu taster ne bi išao ni aplikaciji ni ALUS-u.

Zato `GestureManager::computeGrabs()` **pita dispečer** šta bi se razrešilo
*sada*: grabuje se tačno onaj gest koji bi u ovom trenutku našao skript. U
Fazi 3 to je 12 grab-ova (2 ALUS tastera + 5 implementiranih komandi × 2
virtualna modifikatora). Mereno: `alus-cli input-info` prijavljuje 12, a
`kb:h` ostaje nevezan i stiže aplikaciji.

**Obaveza za Faze 4 i 6:** `refreshGrabs()` se mora pozvati kad se promeni
skup živih objekata (fokus, app modul, tree interceptor, browse ↔ focus mode).
Bez toga quick-nav u browse mode-u neće imati grab-ove.

### O7 — `trappedKeys` je implementiran, ali je za ovaj backend knjigovodstvo

§5.2 traži `trappedKeys` (pojeden down ⇒ pojeden up) i to je implementirano i
testirano (T3.5). Ali sa `KeyboardMonitor` backend-om kompozitor je taster
**već potrošio** kad ALUS o njemu sazna, pa je povratna vrednost
`KeyHandler`-a knjigovodstvo za ovaj backend, a stvarna odluka za X11/legacy
backend koji dolazi kasnije. Zapisano u `backend.hpp`, jer je to razlika koju
čitalac koda inače ne može da pogodi.

### O8 — T3.9 je dokazan u dva dela, jer mu potrošači dolaze u Fazama 5 i 6

`isScriptWaiting()` u NVDA proveravaju `editableText`, `cursorManager`,
`browseMode`, `documentBase` — svi iz Faza 5 i 6. U Fazi 3 bi test „dug govor
se prekida jer je skript u redu" morao da izmisli potrošača. Zato:

- **flag**: `test_dispatch.cpp` proverava da je `isScriptWaiting()` tačan u
  prozoru između `queueScript` i pumpe (i da unutar samog skripta nije tačan);
- **stvarni prekid**: `test_input_speech.cpp` povezuje ulaz sa **pravim**
  govornim lancem Faze 2 i proverava da pritisak tastera prekine govor u letu,
  da shift pauzira i da drugi shift nastavi.

Drugi deo je ono što korisnik oseti; prvi je ono što Faza 5 koristi.

### O9 — opisi komandi prolaze kroz ograničenu zamenu reči

Generisani opisi su tekst koji se **čita naglas**. „Shows NVDA's audio
settings" iz čitača koji nije NVDA je prosto netačno, a „passed directly
through to Windows" imenuje pogrešan operativni sistem. Ekstraktor zato
primenjuje tri zamene (`NVDA` → `ALUS`, `Windows clipboard` → `system
clipboard`, `to Windows` → `to the application`) i to je **jedino** mesto gde
se NVDA tekst menja. Vidljivo je u generisanom CSV-u i proverljivo; prevodi iz
Faze 9 ga zamenjuju u celosti.

### O10 — `Watchdog` je dobio ekstenzionu tačku `freezeDetected()`

Kod Faze 1 je dopunjen jednom tačkom, iz jednog razloga: mera 2 za N19 traži
da se grab-ovi otpuste **kad watchdog otkrije blokadu, pre nego što pokuša
oporavak**. Handler se izvršava na watchdog niti, pre čekanja na oporavak;
`A11yManagerBackend::emergencyRelease()` je napisan tačno za to (GDBus šalje sa
svoje radne niti, ništa ne čeka). Upisano i u `watchdog.hpp`, da se ne pomeša
sa generičkim hook-om.

Mera 3 za N19 (handler signala koji otpušta grab) **nije** implementirana kao
handler signala, i to je svesno: u `SIGSEGV` handler-u nije bezbedno zvati
GDBus. Umesto toga se koristi činjenica koju je Faza 0 izmerila — kad proces
umre, kompozitor pusti grab-ove (posle `SIGKILL`-a u N19 tastatura se vratila).
Opasan je **živ a zaglavljen** proces, i to pokriva watchdog.

### O11 — ulazna pomoć traži **celu** tastaturu

Plan to nije rekao izričito, ali drugačije ne može: u ulaznoj pomoći svaki
taster mora da dođe do ALUS-a, pa `toggleInputHelp` zove
`backend->grabKeyboard(true)`. To je najrizičnije stanje u kom ALUS može da
bude i zato `emergencyRelease()` otpušta i njega.

### O12 — eho znakova je podrazumevano „samo u polja za unos", što do Faze 4 ne može da se aktivira

NVDA-ov podrazumevani `speakTypedCharacters` je `1` = *edit controls*
(`config/configSpec.py`), i ALUS ga preuzima. Odgovor na pitanje „je li fokus
polje za unos" dolazi iz objektnog modela (Faza 4), pa do tada hook vraća
`false` i eho je tih. To je ispravno ponašanje, ne nedostatak: korisnik ga može
prebaciti na „uvek" sa `alus+2`, a Faza 4 ga oživljava bez izmene ulaznog sloja.

### O13 — lista odloženih skriptova je proširena

Plan je odlagao kategorije (lupa, dodir, vision, remote). Merenje je pokazalo
da to nije dovoljno: dijalozi podešavanja za te podsisteme imaju kategoriju
*Configuration* i prošli bi kao v1.0. Dodato u `DEFERRED_SCRIPTS`:
`activateMagnifierSettingsDialog`, `activateVisionSettingsDialog`,
`activateRemoteAccessSettings`, `activateWindowsOCRSettings`,
`cycleOcrLanguage`, `toggleAutomaticRefreshOfRecogResult`,
`toggleScreenCurtain`, `reportScreenCurtainState`, `nextMath`,
`previousMath`, `nextBrowseElement`, `prevBrowseElement`. Ukupno 50 odloženih.
Razlog: gest vezan za komandu koja ne postoji je mrtav unos u mapi i laž u
dijalogu gestova.

### O14 — ALUS taster se zove `alus`, a staro ime se prihvata samo na ulazu

ALUS nije drugi čitač ekrana i nigde ne piše niti izgovara tuđe ime. Zato:

| Gde | Pre | Sada |
|---|---|---|
| ime tastera u gestu | `kb:nvda+1` | **`kb:alus+1`** |
| `data/gestures-builtin.ini` (319 vezivanja) | `kb:NVDA+1` | **`kb:ALUS+1`** |
| `data/locale/*/gestures.ini` (28 jezika) | `kb(laptop):nvda+š` | **`kb(laptop):ALUS+š`** |
| imena skriptova | `braille_toggleNVDAKey` | **`braille_toggleAlusKey`** |
| opisi komandi | „Shows NVDA's audio settings" | **„Shows ALUS's audio settings"** (O9) |
| generator | `tools/extract-nvda-commands.py` | **`tools/extract-commands.py`** |
| `--version` opis demona | „NVDA-compatible screen reader" | **„the ALUS screen reader"** |

**Kompatibilnost se ne gubi, i to je važno zbog Z3** („korisnikov `gestures.ini`
se učita bez izmene"). Korisnik koji dolazi sa svojom mapom u njoj ima
`kb:NVDA+tab`. To ime se **prihvata pri čitanju** i kanonizuje u `alus` na
jednom jedinom mestu — `normalizeGestureIdentifier` (odatle i
`KeyNames::kImportedAlusKeyName`) — pa su `kb:NVDA+tab` i `kb:alus+tab` **jedan
gest**, a ALUS nikad ne upisuje ni ne izgovara prvo. Pokriveno testom
(`test_gesture.cpp` T3.1 i `test_gesture_map.cpp`), i proverljivo na ruku:

```bash
./build/src/alus-cli gesture 'kb:alus+1'   # gesture: kb:1+alus, display: alus+1
./build/src/alus-cli gesture 'kb:NVDA+1'   # isti gest, isti prikaz: alus+1
```

Tri posledice koje treba znati:

1. **Dve braille skripte su preimenovane** (`braille_toggleNVDAKey` →
   `braille_toggleAlusKey`, i `…KeyShift`). Ime skripta je API — plugin i mapa
   gestova vežu po njemu — pa polovično rešenje nije opcija. Uvezena mapa koja
   veže **ta dva** skripta po starom imenu je jedino što ovim prestaje da radi;
   relevantno je od Faze 7 (brajev unos) i zapisano je ovde.
2. **Iz locale mapa su izbačene sekcije klasa koje ALUS ne može da ima**
   (`NVDAObjects.window.winword.*`, `…excel.*`, `appModules.winword.*`,
   `appModules.outlook.*`) — Microsoft Word, Excel i Outlook na Windows-u.
   To su mrtva vezivanja, pa je uklanjanje i čišćenje i ispravka, ne samo
   kozmetika. `appModules.soffice.*` (LibreOffice) **ostaje**.
3. **Atribucija ostaje, bez imena proizvoda u podacima.** Generisani fajlovi
   kažu „derived from the upstream screen reader's data under
   GPL-2.0-or-later" i pokazuju na `vision.md` §6 i `LICENSE`, gde atribucija
   stoji eksplicitno. Komentari u izvornom kodu koji kažu odakle je dizajn
   prenet (`// Port of …`) takođe ostaju: to je provenijencija, zahtev licence
   za izvedeni rad, i jedini način da se kod uporedi sa originalom pri
   održavanju. Ako želiš i njih drugačije formulisane, to je jedna izmena
   komentara i mogu da je uradim.

---

## Nalazi (nastavak numeracije iz Faze 2)

### N26 — `AtspiDevice` je uzrok N19, i isti kod je uzrok N7

Vidi poglavlje „Prvi posao faze" iznad. Dodatno, iz istog izvora:

- `atspi.h` **ne uključuje** `atspi-device.h`; ceo device API se dobija samo
  eksplicitnim `#include <atspi/atspi-device.h>`. Zamka za Fazu 4.
- `AtspiKeyCallback` vraća **`void`** — callback ne može da pojede taster ni u
  libatspi modelu. Potrošnja je uvek stvar grab registracije, nikad odgovora.

### N27 — prvi argument `SetKeyGrabs` su **keysym-ovi**, ne indeksi modifikatora

Interfejs je `SetKeyGrabs(in au modifiers, in a(uu) keystrokes)`. Iz
`refresh_grabs()`:

```c
manager_device->grabbed_modifiers =
    g_slist_append (manager_device->grabbed_modifiers, GUINT_TO_POINTER (keysym));
```

Dakle prvi niz nosi **keysym-ove** koje kompozitor treba da tretira kao
modifikatore. Spike iz Faze 0 je tamo poslao **indeks** (15), što znači da je
kompozitor dobio keysym `0x0F` i da korak 5b **nije ni mogao** da uspe, nezavisno
od zaglavljivanja. To je drugi uzrok neuspeha koraka 5b i nije se videlo dok se
nije pročitao izvor.

Uz to, dve stvari koje kompozitor **ne** radi:

1. **Virtualni modifikator je klijentska fikcija.** Indeks (15–31) i bit u
   maski su knjigovodstvo klijenta; `map_keysym_modifier` ne šalje nijedan
   D-Bus poziv za indeks.
2. **`state` u `KeyEvent`-u ne sadrži virtualni modifikator.** Klijent sam
   prati pritisak i otpuštanje mapiranog keysym-a i OR-uje bit u `state`. Bez
   toga `alus+t` nikad ne može da se prepozna. ALUS to radi u backend-u
   (`enabledVirtualModifiers_`), a `GestureManager` ima i svoje praćenje kao
   rezervu.

### N28 — svaki ALUS taster je **svoj** virtualni modifikator

`Insert` i numpad `Insert` dobijaju različite indekse (mereno: maske `0x8000` i
`0x10000`, indeksi 15 i 16). Prva implementacija je spajala maske u jednu
(`0x18000`) i gradila jedan grab — takav grab znači „drži **oba** tastera" i
nikad se ne bi poklopio. Ispravno: **jedan grab po ALUS tasteru**, a provera
„je li ALUS taster pritisnut" je test na bilo koji bit spojene maske.
Pokriveno testom (`NullInputBackend` sada deli različite maske po keysym-u,
baš kao pravi backend, da test vidi pravi oblik problema).

### N29 — Wayland daje keysym **na tekućem nivou shift-a**, pa identitet tastera treba rekonstruisati

Nasleđeni model taster identifikuje po `vkCode`, koji je svojstvo
**fizičkog** tastera:
`shift+2` je `shift+2` šta god raspored crtao na tasteru. Kompozitor daje
keysym na tekućem nivou, pa isti pritisak dolazi kao `@` na američkom i `"` na
srpskom rasporedu — a gest `kb:alus+shift+'` (koji srpski locale **stvarno**
koristi) se onda nikad ne poklopi.

Zato `alus::input::KeyMap` iz keycode-a vraća keysym **nivoa 1** (xkbcommon
keymap, raspored iz GSettings-a `org.gnome.desktop.input-sources`). Mereno na
ovoj mašini: aktivan raspored je `hr`, 415 tastera, keycode offset 0 (isti
numerisanje kao xkbcommon).

Drugi deo istog pitanja **nije** izmeren: ne zna se da li kompozitor grab
poredi sa keysym-om nivoa 1 ili nivoa 2. Zato se za gest sa `shift` registruju
**oba** keysym-a (jedan dodatni unos u skupu grab-ova protiv vezivanja koje
tiho nikad ne radi).

### N30 — mešanje muteksa i asinhronih poziva: zaglavljivanje u ALUS-ovom kodu

Prva verzija `A11yManagerBackend`-a je imala `setGrabs()` koji drži muteks i
zove `callAsync()` koji traži isti (ne-rekurzivni) muteks — proces se zaglavio
pri dizanju, pre ijedne poruke. Nalaz je dvostruk:

1. **Konvencija je sada eksplicitna** u zaglavlju: `sendGrabsLocked` i
   `callAsyncLocked` zahtevaju držan muteks, `callAsync` ga uzima. Bez zapisane
   konvencije ovo se ponavlja.
2. **Dijagnostika je ono što ga je našla.** `alus-cli input-info` je postojao i
   zaglavio se vidljivo. Da ga nije bilo, isti bug bi se pojavio kao „ALUS ne
   reaguje na tastere" — najgori režim otkaza ovog projekta.

### N31 — Orca se i dalje sama vraća; mera je držati je dole, ne ubiti je jednom

N8 iz Faze 0 je rekao da `gnome-session` oživljava Orcu. Na ovoj sesiji Orca
više ne radi kao `app-orca\x2dautostart@autostart.service` nego kao **scope**
(`app-gnome-orca\x2dautostart-<pid>.scope`), pa maskiranje systemd jedinica
nema efekta. `tools/phase3-input-check.sh` zato drži Orcu dole petljom za
vreme merenja i vraća je u `trap`-u na svakom izlazu. Provereno u
`SAFETY_CHECK=1` prolazu: Orca dole ~5 s, pa vraćena (nov pid, ime ponovo
njeno, `screen-reader-enabled=true`).

### N32 — GLib-ova GDBus radna nit i redosled gašenja

Sa pravim backend-om valgrind je prijavljivao jedan blok od 368 bajtova kao
„possibly lost", sa stack trace-om koji pokazuje na **ALUS-ov** watchdog
(`Watchdog::start`). Mereno sa `--no-watchdog`: isti blok, ali pripisan
GLib-ovim okvirima. Uzrok: watchdog nit se spoji pri gašenju, glibc keširá njen
stek i DTV, a GDBus nit koja se posle stvara ga preuzme — i ostane živa do
izlaska procesa.

Dve posledice, obe primenjene:

1. **Redosled gašenja je izmenjen**: podsistemi se gase **pre** watchdog-a, sa
   watchdog-om još uključenim. To nije kozmetika — vraćanje grab-ova i
   zatvaranje sintetizatora su upravo pozivi koji mogu da blokiraju na nečemu
   izvan procesa, a u Fazi 3 je u pitanju korisnikova tastatura.
2. Dodata je suppression za GLib-ovu neimenovanu nit, vezana za **biblioteku** u
   okviru iznad `pthread_create` (Debian-ov libglib ne izvozi simbol), tako da
   nit koju ALUS procuri i dalje bude prijavljena.

Posle oba: `definitely lost: 0`, `possibly lost: 0`, `ERROR SUMMARY: 0`.

### N33 — isti događaj tastature dolazi dva puta

Izmereno u interaktivnom prolazu: **14 od 56** događaja je stiglo dva puta,
svaki duplikat **0–2 ms** posle originala. Nije teorija — čulo se: „numpad 7"
izgovoreno dva puta.

`MetaA11yManager::notify_clients` (mutter 48.7,
`src/backends/meta-a11y-manager.c`) obaveštava **svakog klijenta jednom po
događaju**, a `meta_display_handle_event` je registrovan kao **jedan** Clutter
filter (`src/core/events.c:518`). Dakle ne duplira se obaveštavanje, nego **sam
događaj prolazi kroz filter dva puta** — najverovatnije re-dispatch-om kroz
Wayland text-input/IME, što nije dokazano i nije potrebno za meru.

Mera je u backend-u, gde platformske čudnovatosti i pripadaju: `KeyEventFilter`
odbacuje događaj identičan prethodnom (`pressed`, `keysym`, `keycode`, `state`)
unutar **5 ms**. Prozor je izabran iz merenja: najveći izmereni razmak duplikata
je 2 ms, a najbliže stvarne pojave su autorepeat (≥30 ms na najbržoj stopi) i
ljudski dupli pritisak (≥50 ms).

### N34 — kompozitor **sam** propušta dupli pritisak ALUS tastera

Ovo je oborilo odstupanje O5. Ugovor interfejsa
(`mutter-48.7/data/dbus-interfaces/org.freedesktop.a11y.xml`, `SetKeyGrabs`)
kaže doslovno:

> If any of the keys in `@modifiers` is pressed alone, the compositor is
> required to ignore the key press and release event if a second key press of
> the same modifier is not received within a reasonable time frame, for example,
> the key repeat delay. **If such event is received, this second event is
> processed normally.**

I to je u kodu: `meta_a11y_manager_notify_clients` prvo obavesti klijente, pa
onda `if (is_grabbed_modifier_key (…) && !is_ignorable) return FALSE;`, gde
`is_ignorable` dolazi iz `CLUTTER_EVENT_FLAG_A11Y_MODIFIER_FIRST_CLICK`. Prvi
pritisak nosi tu zastavicu i bude potrošen; **drugi je ne nosi, ne troši se**,
pa ga aplikacija dobija.

Posledica za ALUS: **ne treba ništa da ubacuje** (N18 kaže da i ne može). Treba
samo da ne reaguje na drugi pritisak i da ga ne potroši — što bypass logika u
`GestureManager`-u već radi. `IInputBackend` zato ima
`modifierDoublePressReachesApplication()`, a Z2 je ispunjen bez `/dev/uinput`
helpera.

### N35 — virtualni modifikator mora da se skine sa sopstvenog događaja

Pošto kompozitor ne prati virtualne modifikatore (N27), backend OR-uje bit u
`state` — uključujući u događaj **samog ALUS tastera**. Prva verzija je zato
gradila `alus+alus`, a kod duplog pritiska `alus+numpadInsert`; izmereno, jer
je demon izgovorio „alus plus numpad insert".

Bit se sada skida sa sopstvenog događaja, **osim kad je pritisnut drugi ALUS
taster** (Insert držan + numpad Insert pritisnut je stvarno ALUS gest; NVDA tu
daje `NVDA+NVDA`, ALUS daje `alus+alus` — nije vezano ni za šta i ne šteti).

---

## Interaktivno merenje: četiri koraka S1-a i ručni test — **IZVRŠENO**

```bash
SAFETY_CHECK=1 bash tools/phase3-input-check.sh   # proba staze, Orca dole ~5 s
bash tools/phase3-input-check.sh                  # pravo merenje
```

Prolaz: **2026-11-05, 10:47:15 – 10:48:40**, GNOME 48.7 Wayland, Orca
zaustavljena, govor kroz RHVoice preko speech-dispatcher-a. Log: 238 linija,
56 događaja tastature. Stanje pri dizanju, iz loga:

```
keymap: layout 'hr', 415 keys, keycode offset 0
locale gesture map: data/locale/sr/gestures.ini
input backend a11y-manager: watching the keyboard as org.gnome.Orca.KeyboardMonitor
ALUS key: 2 virtual modifier(s), combined mask 0x18000     (0x8000 + 0x10000)
14 key grabs installed (a11y-manager backend)              (12 + alus+t × 2 maske)
```

| Korak | Pitanje | Izmereno | Ishod |
|---|---|---|---|
| **3** | vidi li watcher **sve** tastere | `a s d f` → keysym `0x61 0x73 0x64 0x66`, svi u logu, svi i u terminalu | **PASS** — watcher vidi sve i ne troši ništa |
| **5b** | **troši** li grab `Insert+t` | `input: kb(desktop):alus+t` jednom; demon izgovorio „speak command keys on"; `t` **nije** stiglo u terminal | **PASS** — grab troši taster |
| **6** | dupli ALUS taster u 500 ms | numpad Insert, keycode 90: pritisak 70019 ms → otpuštanje 70091 ms → **drugi pritisak 70211 ms, tj. 120 ms posle otpuštanja**; ALUS ga nije potrošio | **PASS** — a dostavu radi kompozitor sam (N34) |
| **7** | numpad bez NumLock-a | samo `0xff95` (KP_Home) i `0xff9c` (KP_End); **nijedan** `0xffb*` | **PASS** — imenovani keysym-ovi, bez cifara |
| ručni | `ALUS+1` ulazna pomoć | „input help on" → objasnio `a`, `alus`, `alus+2` (sa opisom skripta) → `alus+1` → „input help off" | **PASS** — prekidač ne pojeda sam sebe |
| ručni | `ALUS+4` komandni tasteri | dva pritiska: „speak command keys on", pa „off" | **PASS** |

Za korak 5b harness veže `alus+t` na `toggleSpeakCommandKeys` kroz privremeni
`gestures.ini`, jer je to komanda koju korisnik **čuje** — pa je dokaz
dvostruk: skript se izvršio, a taster nije stigao do terminala.

**Time su četiri koraka S1-a zatvorena i Faza 0 nema više otvorenih delova.**

### Dva defekta koja je merenje otkrilo, oba ispravljena

Harness je prijavio PASS na svim koracima, ali je log pokazao dve stvari koje
testovi sa lažnim backend-om nisu mogli da vide — i obe su bile **slušne**:

1. **Svaki događaj tastature dolazio je dva puta** (N33): 14 duplikata u 56
   događaja, svi u **0–2 ms**. U zvuku: „numpad 7" izgovoreno dva puta, „alus
   plus numpad insert" dva puta; eho otkucanog znaka bio bi takođe dvostruk.
   Apsorbovano u backend-u (`KeyEventFilter`, prozor 5 ms).
2. **ALUS taster je modifikovao sam sebe** (N35): drugi pritisak duplog
   pritiska je dao gest `alus+numpadInsert` umesto `numpadInsert`, jer backend
   mora da OR-uje virtualni bit u **sopstveni** događaj tastera (N27). Bit se
   sada skida, osim kad je stvarno pritisnut **drugi** ALUS taster.

Oba imaju regresione testove sa izmerenim brojevima u komentarima
(`test_manager.cpp`): prozor od 5 ms stoji protiv autorepeat-a (≥30 ms) i
ljudskog duplog pritiska (≥50 ms).

**Drugi prolaz, posle ispravki** (2026-11-05, isti harness): sve ponovo prošlo,
najave se čuju po jednom. Time su obe ispravke potvrđene i protiv stvarnog
kompozitora, ne samo u testovima. Brojač upijenih duplikata je od tada deo
dijagnostike:

```bash
./build/src/alus-cli input-info      # „key events: N received, M duplicates dropped (N33)"
```

## Provere koje su pokrenute

> Sve komande ispod su pokrenute u `build` na ciljnom sistemu, posle
> `apt install libxkbcommon-dev` (2026-11-05, `pkg-config --modversion
> xkbcommon` → 1.7.0).

```bash
# Generisanje komandne tabele iz NVDA izvora
python3 tools/extract-commands.py --source ./NVDA --out data/

# Jedinični testovi
meson test -C build --suite input     # 45 slučajeva, 720 tvrdnji
meson test -C build                   # sve podrazumevane svite, zeleno
meson test -C build --setup=valgrind --suite input --suite core

# Definicija završenog za demon, sa ulazom i bez njega
valgrind --leak-check=full --suppressions=tests/valgrind.supp \
  ./build/src/alus-daemon --allow-multiple --no-speech --exit-after 1
#   definitely lost: 0, possibly lost: 0, ERROR SUMMARY: 0 (5 suppressed)

# Dijagnostika ulaza protiv žive sesije (Orca radi, pa ime nije slobodno)
./build/src/alus-cli input-info
#   input: a11y-manager backend, bus name taken, layout desktop, 12 grabs,
#   ALUS modifier mask 0x18000, 390 catalogued scripts, keymap hr
#   keyboard: NOT AVAILABLE (bus name taken)
#   held by: :1.65
./build/src/alus-cli gesture 'kb:alus+1'     # toggleInputHelp, korak 10, built-in
./build/src/alus-cli gesture 'kb:h'          # nevezan (browse mode je Faza 6)
./build/src/alus-cli list-commands Input     # komandna tabela po kategoriji
```

Pravilo 3 (restartabilnost u procesu) za novi podsistem: `input::Service` je
`config::Subsystem`, pa `SIGHUP` → `resetConfiguration()` ponovo čita
`[keyboard]`, ponovo učitava sve tri mape i ponovo objavljuje skup grab-ova.

---

## Šta je napravljeno

| Modul | Linija | Napomena |
|---|---|---|
| `alus::input::KeyNames` | 230 + 420 | keysym ↔ NVDA ime ↔ izgovorena labela; puna numpad tabela |
| `alus::input::KeyMap` | 95 + 200 | identitet tastera nezavisan od shift nivoa (N29) |
| `alus::input::Gesture`, `KeyboardGesture` | 215 + 300 | identifikatori, normalizacija, `isCharacter`, `speechEffect` |
| `alus::input::GestureMap` | 95 + 290 | `gestures.ini` u NVDA formatu, `None =`, `kb:` emulacija |
| `alus::input::GestureManager` | 215 + 520 | 14 koraka `executeGesture`, trapped keys, ALUS taster, grab-ovi |
| `alus::input::InputHelp` | 80 + 140 | ne pojeda svoj prekidač |
| `alus::input::TypedChars` | 95 + 150 | eho znakova i reči, bafer reči |
| `alus::input::IInputBackend`, `NullInputBackend` | 185 | apstrakcija + režim bez tastature |
| `alus::input::A11yManagerBackend` | 120 + 450 | direktan D-Bus, asinhroni pozivi, `emergencyRelease` |
| `alus::input::InputCommands` | 65 + 165 | `alus+1/2/3/4`, `alus+f2` |
| `alus::input::Service` | 125 + 290 | restartabilan podsistem, watchdog, start-up dijagnoza |
| `alus::script::{ScriptInfo,Script,ScriptSpec}` | 110 + 35 | metapodaci koji menjaju dispatch |
| `alus::script::Catalog` | 70 + 215 | `scripts.csv` + `gestures-builtin.ini` + labele |
| `alus::script::ScriptableObject` | 95 + 130 | klasni lanac, `ALUS_SCRIPT`, built-in vezivanja |
| `alus::script::Dispatcher` | 165 + 330 | 10-stepeni lookup, repeat count, `isScriptWaiting` |
| `alus::core::findDataDirectory` | 30 + 46 | jedno pravilo za dva podsistema |
| `tools/extract-commands.py` | 953 | generator komandne tabele |
| `tools/phase3-input-check.sh` | 332 | merenje koraka 3, 5b, 6, 7 + ručni test |
| `alus::input::KeyEventFilter` | 70 | upija duplirane događaje platforme (N33) |
| testovi | 1840 | 48 slučajeva, 773 tvrdnje |

Generisani podaci: `data/scripts.csv` (440 skriptova),
`data/gestures-builtin.ini` (319 vezivanja), `data/key-labels.ini` (78
labela), `data/locale/*/gestures.ini` (28 jezika, srpski uključen).

---

## Predaja Fazi 4

1. **`refreshGrabs()` se mora zvati pri promeni fokusa** (O6). Bez toga će
   skriptovi objektnog sloja postojati a njihovi tasteri neće biti grabovani.
2. **`DispatchContext` ima prazna polja koja Faza 4 puni**: `focus`,
   `focusAncestors`, `appModule`, `treeInterceptor`, `sleepMode`. Lookup je
   napisan i testiran za sva 10 koraka; Faza 4 samo isporučuje objekte.
3. **Dva hook-a čekaju odgovor**: `isFocusEditable` (eho znakova, O12) i
   `isSayAllRunning` (Faza 5).
4. **`libatspi` zavisnost ulazi u Fazi 4**, za objektni model. Device API iz te
   biblioteke se **ne** koristi (O1) i N26 objašnjava zašto.
5. **`alus::input::KeyNames::shared()`** je proces-globalna tabela; Faza 4 ne
   treba da je dira osim ako dodaje labele.
