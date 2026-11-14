# Faza 7 — Brajev red

Izveštaj o izvršenoj fazi. Status svakog izlaznog kriterijuma, odstupanja od
plana sa razlogom, i nalazi koje je faza proizvela.

**Jedna komanda:**

```bash
meson test -C build --suite braille
```

Rezultat: **509 asertacija, 31 test slučaj, zeleno.** Pod valgrind-om: 0
grešaka, 0 izgubljenih bajtova — i to ovde **nije formalnost**, jer je jedna
od dve greške koje je faza našla bila upis izvan bafera koji suite prolazi i
bez ispravke (N62).

## Status izlaznih kriterijuma

| Kriterijum | Status | Gde se meri |
|---|---|---|
| T7.1 — `sr-Cyrl.ctb` prevede ćirilični tekst tačno | **prošao** | `[T7.1]`, 28 asertacija, nad **stvarnim** liblouis 3.33.0 |
| T7.2 — typeform lista se NE menja na mestu (kopija se prosleđuje) | **prošao** | `[T7.2]`, 28 asertacija; + sekcija koja otkriva N62 |
| T7.3 — `dotsIO`: ćelije dolaze sa bitom 0x8000, maskira se na 0xFF | **prošao** | `[T7.3]`, 26 asertacija, protiv sirovog izlaza biblioteke |
| T7.4 — custom table resolver nađe tabelu iz add-on direktorijuma | **prošao** | `[T7.4]`, 27 asertacija; + provera svih 12 imena (N63) |
| T7.5 — word wrap: sva 4 moda tačna na granici reda | **prošao** | `[T7.5]`, 39 asertacija |
| T7.6 — single-slot upis: 100 frame-ova u jednom ciklusu ⇒ 1 upis (poslednji) | **prošao** | `[T7.6]`, mereno na 100 frame-ova |
| T7.7 — `_regionsPendingUpdate`: 50 pomeraja karete u ciklusu ⇒ 1 update | **prošao** | `[T7.7]`, mereno na 51 događaju |
| T7.8 — `saveWindow`/`restoreWindow`: prozor stabilan preko update-a regiona | **prošao** | `[T7.8]`, 11 asertacija |
| Ručni test (stvarni red ili `brltty` u virtualnom modu) | **nije izvršen** | nema `brltty`; automatska polovina je `--suite live` (N64), ostatak je `tools/phase7-braille-check.sh --manual` |

Ručni test je **jedino** što nije izvršeno, i razlog je mašina, ne kod:
`alus-cli braille-info` prijavljuje `brltty: NOT AVAILABLE` (`connect: No such
file or directory`). Sve što se može izmeriti bez hardvera izmereno je
automatski — uključujući ono što plan ostavlja ručnom testu (`bk:dot7`,
`bk:dot8`, `bk:dot7+dot8`, tethering, cursor routing), jer ručni test nije
merenje koje preživi refaktor.

Demon se pokreće i **degradira pošteno** kada reda nema:

```
INFO  louis.cpp:371    liblouis 3.33.0 ready, tables in /usr/share/liblouis/tables
INFO  service.cpp:172  no braille display: cannot reach brltty: connect: No such file or directory
INFO  handler.cpp:79   braille: display none, 0 rows x 0 cols, table en-ueb-g1.ctb, tethered focus, wrap atWordBoundaries
INFO  service.cpp:203  braille: ... input table en-ueb-g1.ctb (brltty types)
```

To je zahtev iz `meson.build:87`: bez `libbrlapi` ALUS i dalje **prevodi**
brajev tekst, samo ne vozi red.

## Šta je izgrađeno

| Modul | Linija (.cpp + .hpp) | Napomena |
|---|---|---|
| `alus::braille::Louis` + `TableRegistry` | 620 + 256 | tri detalja iz §14: kopija typeform-a, `dotsIO` maska, custom resolver |
| `alus::braille::Region` / `TextRegion` / `ObjectRegion` | 159 + 206 | osnovni regioni, `routeTo`, `hidePreviousRegions` |
| `alus::braille::TextInfoRegion` + `CursorRegion` + `ReviewTextRegion` | 688 + 245 | čita `getTextWithFields` iz Faze 6; dve mape pozicija |
| `alus::braille::Buffer` | 558 + 176 | prozor, sva 4 moda word wrap-a, `saveWindow`/`restoreWindow` |
| `alus::braille::Handler` | 896 + 277 | `_regionsPendingUpdate`, tethering, message bafer, blink |
| `alus::braille::DisplayWriter` + `IDisplay` | 183 + 197 | **single-slot upis**, worker nit, `NullDisplay` |
| `alus::braille::BrlapiDisplay` | 230 + 73 | brltty umesto ~40 drajvera (O1) |
| `alus::braille::BrailleInput` + `InputGesture` + `DisplayGesture` | 581 + 212 | `bk:` gestovi, tri rezervisane kombinacije, modifikatori |
| `alus::braille::properties` + `labels` + `formatting` | 565 + 246 + 80 | port `braille.getPropertiesBraille` i skraćenica |
| `alus::braille::Commands` | 519 + 125 | 20+ skriptova, brlapi mapa kao **podaci** |
| `alus::braille::Service` + `Settings` | 382 + 329 | ožičenje u demon, `PumpStage::Braille`, SIGHUP |
| testovi | 1862 | T7.1–T7.8 nad **stvarnim** liblouis-om |
| `tests/braille/test_live_braille.cpp` | 140 | live svit: BrlAPI nad stvarnim brltty-jem; bez njega *skipped* (N64, N65) |

Ukupno: **6036 linija `.cpp`, 2394 `.hpp`, 1862 testa.**

## Odstupanja od plana

### O1 — nijedan drajver za red; sve ide kroz brltty

§14-ova jedna velika pojednostavljenost. NVDA nosi ~40 drajvera za pojedinačne
redove; ALUS nosi **nula** i govori sa `brltty` preko BrlAPI-ja. brltty već ima
drajvere, Linux korisnik ga već ima podešenog, i ~6000 linija protokola za
hardver koji ovde niko ne poseduje su 6000 linija koje ne moraju da budu tačne.

**Šta se gubi, pošteno:** osobina specifična za red koju brltty ne modeluje
(ALVA split point, Handy Tech touch ćelije) nije dostupna. Identifikatori
gestova ipak drže `br(...)` oblik, pa korisnikov `gestures.ini` pisan za NVDA
i dalje adresira tastere koje brltty prijavljuje.

**Imena tastera su brltty-jeva**, ne NVDA-ina po-drajveru: `brlapi_describeKeyCode`
daje `fwinlt`, `lnup`, `route`, pa je gest `br(brlapi):fwinlt`. To je
vokabular koji brltty korisnik već ima u svojoj konfiguraciji (`brltty -l`).

### O2 — keš format polja živi na regionu, ne na objektu

NVDA drži `_brailleFormatFieldAttributesCache` **na objektu**, pa keš preživi
rebuild regiona. Ovde je na regionu. U praksi je to isti životni vek — region
postoji dok red prikazuje taj objekat — a ono što keš radi (bold se prijavi
jednom, a ne na svaki pomeraj karete) radi isto.

### O3 — `reportAriaDescription` upravlja **svim** opisima u dokumentu

NVDA ovde prikazuje samo ARIA opis, jer bi običan `title` atribut napunio red
onim što je vizualno očigledno. Taj test je `descriptionFrom`, a **AT-SPI ne
kaže odakle opis dolazi** — faze 4 do 6 to nikad nisu ni tražile.

Zato `reportAriaDescription`, čija je cela svrha ta jedna odluka, upravlja svim
opisima u dokumentu. Način na koji to može da ispadne loše: `title` atribut
stigne na red korisniku koji je uključio ARIA opise. Alternativa je izgubiti
ARIA opise zbog kojih ta opcija postoji. (`src/braille/properties.cpp:297`)

### O4 — hifenacija iz same brajeve tabele, bez dodatne biblioteke

NVDA koristi odvojenu biblioteku za hifenaciju (`hyphen`). ALUS koristi onu
koju brajeva tabela već nosi (`lou_hyphenate`) — jedna zavisnost manje i isti
odgovor tamo gde tabela ima pravila. Gde ih nema, poziv vrati prazno, a
pozivalac to **mora** da čita kao „ne lomi ovu reč", ne kao grešku.
(`include/alus/braille/louis.hpp:219`)

### O5 — „već prikazano" je `uniqueId` polja, ne `_startOfNode`

Druga i treća porcija jedinice čitanja ponovo otvaraju kontejnere koje je prva
već prikazala. NVDA testira `_startOfNode`. ALUS može bolje, jer je §9.1 dao
svakom polju stabilan identitet, pa je test **„da li sam ovu tačno kontrolu
već prikazao u ovom update-u"**. (`src/braille/text_region.cpp:304`)

### O6 — `rawToContentPos` se skraćuje; NVDA poslednju ćeliju mapira na obrisan `\n`

NVDA ostavlja `_rawToContentPos` neskraćen („ionako nikad ne pitamo za indeks
koji ne važi u `rawText`"), a onda **dopiše** stavku za završni razmak — koja
padne izvan teksta i ostavi poslednju ćeliju mapiranu na obrisan `\n` umesto na
kraj linije. Skraćivanje ne košta ništa i čini mapu tačnom, pa rutirajući
taster na poslednjoj ćeliji linije stigne na **kraj te linije**.
(`src/braille/text_region.cpp:486`)

### O7 — single slot se prazni **jednom po pump ciklusu**, ne iz I/O niti

Namerno odstupanje. NVDA prazni slot iz I/O niti čim se prethodni upis završi,
pa **broj upisa zavisi od toga koliko je hardver brz**. ALUS prazni u
`PumpStage::Braille`, koji §3.1 već definiše kao trenutak kada je stanje
ciklusa konačno.

Posledica je **merenje umesto trke**: N frame-ova u jednom ciklusu su tačno
jedan upis, i T7.6 to tvrdi kao broj. Worker nit ostaje, jer
`brlapi_writeDots` ka otkačenom Bluetooth redu može da blokira sekundama i to
ne sme da zaustavi pump. (`include/alus/braille/display.hpp:27`)

### O8 — `windowRawText` vraća jedan znak manje nego NVDA, i to je ispravka

`bufferPositionsToRawText` **uključuje** ćeliju na `endPos` — što njegov drugi
pozivalac, pretraga za hifenacijom, i traži — pa ekskluzivni kraj prozora mora
da se pomeri za jedan nazad. NVDA prosleđuje `windowEndPos` nepromenjen i
prijavljuje jedan znak previše: bezopasno za log liniju, netačno za bilo šta
što **meri** prozor. (`src/braille/buffer.cpp:499`)

### O9 — speech-output mod drži **tekstove**, ne drugu listu regiona

NVDA drži `_showSpeechInBrailleRegions`, drugu listu regiona pored bafer-ove.
ALUS drži tekstove i pravi regione iznova, jer bafer **poseduje** svoje regione
— a dva vlasnika jednog regiona su stvar koja radi do prvog prebacivanja
profila, koje jednu listu očisti. (`include/alus/braille/handler.hpp:268`)

## Nalazi

### N62 — liblouis upisuje **izlazni** typeform u isti niz; bafer mora biti dužine izlaza

§14 upozorava da `lou_translate` menja typeform listu na mestu, pa se
prosleđuje **kopija** (to je T7.2 i to je bilo tačno od početka). Ono što
upozorenje ne kaže je **koliko ta kopija mora biti dugačka.**

liblouis upisuje typeform **izlaza** nazad u isti niz, jednu stavku po
**ĆELIJI**. Bafer dužine ulaza je zato upis izvan niza na svakom tekstu koji
proizvede više ćelija nego što ima znakova — a to radi svaki capital sign,
number sign i grade 2 kontrakcija. NVDA alocira `(c_ushort*outlen.value)()` i
puni samo `inlen` stavki; upravo zbog ovoga.

Kako je nađeno, i zašto to nije slučajnost: suite je **prolazio** sa greškom.
Valgrind pod `[T7.2]`:

```
Invalid write of size 2
   at _lou_translate (liblouis.so.20.0.21)
   by alus::braille::Louis::translate (louis.cpp:445)
 Address 0x57e43b6 is 0 bytes after a block of size 22 alloc'd
```

Ispravka: `attemptForms` se alocira na `capacity` (dužina izlaznog bafera), a
prvih `input.size()` stavki se prepiše iz pozivaočevog typeform-a.
`src/braille/louis.cpp` nosi obrazloženje na mestu upisa.

**Testu je dodata sekcija koja ovo hvata**, jer greška koju vidi samo valgrind
vraća se čim neko „pojednostavi" alokaciju: tekst `"ABC DEF"` u grade 1 tabeli
daje više ćelija nego znakova, pa sekcija *„a text that produces MORE cells
than characters still fits the buffer"* prolazi kroz isti put. Sa vraćenom
greškom valgrind na njoj prijavi dve greške; sa ispravkom nula.

Zato je **memorijski proveravač deo izlaznog kriterijuma ove faze**, a ne
dodatak — `tools/phase7-braille-check.sh` ga ima kao Korak 4.

### N63 — liblouis 3.33 nosi `hr-comp8.utb`, ne `.ctb`

`TableRegistry::addKnownTables()` je **ručno pisana lista od dvanaest imena
fajlova**, i jedno od njih (`hr-comp8.ctb`) liblouis ne isporučuje: hrvatska
computer-braille tabela je `hr-comp8.utb`. Ime u toj listi koje liblouis ne
nađe je **opcija koja ne može da radi**: resolver ne nađe ništa, liblouis odbije
listu, i red padne na sirove codepoint-e maskirane na bajt (§3.8).

Greška je bila nevidljiva do trenutka kada neko izabere hrvatski, jer je lista
**podatak**, a ne kod. Zato je dodat test koji je čita: svaka tabela koju ALUS
imenuje sam mora da se razreši u fajl koji postoji (`[T7.4]`). Na ovoj mašini
sada prolazi svih 12.

### N64 — na ovoj mašini brltty nije pokrenut, pa je polovina ručnog testa nedostupna

Nije greška, ali je činjenica koja se mora zapisati, jer bi inače „faza
završena" značilo više nego što jeste. `brltty` nije na ovoj radnoj površini,
pa nijedan frame nije otišao na stvarni ili virtualni red. Sve iza
`IDisplay`-ja izmereno je protiv `RecordingDisplay`-ja (koji broji upise
tačno, što je ono što T7.6 i T7.7 i traže); `BrlapiDisplay` je izmeren samo
tako što `braille-info` otvori konekciju i dobije odbijenicu.

Ono što ostaje nemereno: da li `brlapi_writeDots` sa 0x8000 bitom stigne do
ćelija, i da li `brlapi_describeKeyCode` daje imena koja `kBrlapiBindings`
očekuje.

Zato to **nije ostavljeno ručnom testu**, nego live svitu:
`tests/braille/test_live_braille.cpp` (`meson test -C build --suite live`)
traži veličinu reda, pošalje jedan frame **koji je liblouis stvarno proizveo**
i pročita imena tastera — a bez brltty-ja se prijavi kao *skipped*, ne kao
greška. Čim red postoji (ili `brltty -b no -d /dev/null -x no -n`), to se
**izvršava** umesto da se čita. Ručni test ostaje samo za ono što je pod
prstima: `tools/phase7-braille-check.sh --manual`.

### N65 — Catch2 izlazi sa 4 kad su svi testovi preskočeni; meson to čita kao pad

Nađeno pri pisanju live testa iz N64, i **važi za sve live testove u projektu
od Faze 2 naovamo**. Dve nezavisne greške u istom obećanju („bez zavisnosti se
prijavi kao preskočen, a ne kao pad"):

1. `WARN(...)` + `SUCCEED(...)` kao straža: TAP reporter broji **i upozorenje**
   kao test tačku, pa plan linija (`1..1`) ne odgovara broju ispisanih tačaka
   (`warning 1`, `ok 2`) i meson prijavi `ERROR: Unknown TAP output lines`.
   Ispravka je `SKIP(...)`, koji TAP ispiše kao `ok 1 - # SKIP`.
2. Catch2 vrati **4** kada su *svi* njegovi test slučajevi preskočeni
   (`catch_session.cpp:427`, `AllTestsSkippedExitCode`), a meson svaki
   nenulti izlaz čita kao pad. Ispravka je `--allow-running-no-tests`
   (`zeroTestsCountAsSuccess`), i sada ga nosi **svaki** live test.

Mereno, ne pretpostavljeno — `test-a11y-live` sa namerno pokvarenom adresom
magistrale:

```bash
env -u DBUS_SESSION_BUS_ADDRESS AT_SPI_BUS_ADDRESS="unix:path=/nonexistent" \
    ./build/tests/test-a11y-live --reporter tap
# pre:    exit 4   (meson: ERROR)
# posle:  exit 0   ok 1 -  # SKIP 'no accessibility bus: ...'
```

Posledica je da su izveštaji Faza 2 i 4 bili tačni o kodu i netačni o CI-ju:
na mašini bez govorne magistrale ili bez radne površine live svit **nije**
bio zelen, nego crven — i to na tačno onim mašinama za koje je ta straža i
napisana.

## Šta ova faza nije dirala

- **Nema dijaloga.** `br(brlapi):prefmenu` je vezan na
  `activateBrailleSettingsDialog`, koji **postoji u katalogu**
  (`data/scripts.csv:179`) i **nijedan objekat ga ne implementira** — kao i
  `activateGeneralSettingsDialog` i ostali. Pritisak tog tastera ne radi
  ništa. Podešavanja se za sada menjaju skriptovima koji ciklusiraju jednu
  opciju i prijave je rečima (`braille_toggleTether`, `braille_cycleCursorShape`
  i srodni). UI sloj je Faza 9; O10 iz `phase-6.md` ostaje otvoren.
- **Nema ubacivanja tastera.** `handleBrailleInput` je podrazumevano
  isključen i brajeva tastatura ide kroz brltty-jev sopstveni uinput. Sa
  uključenim, ALUS prevodi, pokaže ćelije i **pošteno kaže** da ne može da
  tipka (N18) — što je i jedna asertacija u `test_input.cpp`.
- **Nema app modula.** Ponašanje po aplikaciji je Faza 8.

## Predaja Fazi 8

Faza 8 je app moduli i ekstenzibilnost. Šta joj Faza 7 ostavlja:

1. **`TableRegistry::addSource` je već mehanizam za add-on.** Faza 8 treba da
   ga pozove iz instalacije plugin-a: tabela iz add-on direktorijuma pobedi
   sistemsku sa istim imenom (mereno u `[T7.4]`).
2. **Treća globalna gesture mapa je napunjena podacima** (`kBrlapiBindings`).
   App modul koji hoće svoj brajev taster piše sekciju gesture mape, bez linije
   C++-a.
3. **`RegionHost` je seam.** App modul koji hoće drugačiji region (terminal,
   Calc ćelija) implementira `Region`, a ne menja `Handler`.
4. **Pravilo N49 važi i za brajev red.** Region drži **izvor**, izvor drži
   objekat; nijedan region ne drži `TextRange` duže od jednog update-a.
5. **N62 je opšte pravilo, ne liblouis anegdota**: svaka C biblioteka kojoj se
   prosleđuje bafer za upis mora dobiti bafer dužine **izlaza**, i svaki takav
   poziv treba da se izmeri pod valgrind-om, ne samo pod suite-om.

## Stanje na kom se nastavlja

```bash
meson setup build && meson compile -C build
meson test -C build                      # 11 svita, zeleno
meson test -C build --suite braille      # 509 asertacija — izlazni kriterijum Faze 7
meson test -C build --setup=valgrind --suite core --suite config --suite speech \
         --suite input --suite a11y --suite text --suite nav --suite browsemode \
         --suite braille
                                         # 11/11, 0 grešaka, 0 izgubljenih bajtova
meson test -C build --suite live         # 4/4; braille-live preskoči bez brltty-ja
tools/phase7-braille-check.sh            # mašina: liblouis, tabele, round trip, brltty
tools/phase7-braille-check.sh --manual   # + plan ručnog testa (traži brltty)
./build/src/alus-cli braille-info        # šta brajev red može ovde i zašto ne može više
```
