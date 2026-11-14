# Faza 10 — Komande koje su se najavljivale, a nisu bile napisane

Izveštaj o izvršenoj fazi. Nije bila u `02-FAZE.md`: naručena je posle Faze 9,
iz jedne rečenice korisnika — *„ne mogu da podesim sintezu, glas, ovde ti skoro
ništa od komandi ne radi"*.

**Jedna komanda:**

```bash
meson test -C build
```

Rezultat: **21 test, 36.577 asertacija, zeleno** (1 očekivani pad:
`settings-bad-page`).

Faza je imala **tačno merljiv cilj**: `data/commands-status.ini` je u sekciji
`[announce]` imao **47 komandi** — komande koje su u opsegu v1.0, imaju taster
iz generisane mape i na pritisak kažu *„nema u ovoj verziji"*. Sada ih ima
**18**, i svih 18 je tu sa razlogom koji nije „nije stiglo na red" (tabela na
kraju). Napisano je **29 komandi**.

| Mera | Pre | Posle |
|---|---|---|
| `[announce]` u `data/commands-status.ini` | 47 | **18** |
| Dodela tastera iz ugrađene mape koje nešto rade | 241 | **279** od 316 |
| Dodela koje najavljuju da ih nema | 62 | **24** |
| Namerno nevezanih (N18) | 13 | 13 — nepromenjeno |
| Prevedenih poruka (`po/sr.po`) | 1105/1106 | **1170/1170** |

Brojevi dodela su izračunati iz `data/gestures-builtin.ini` i
`data/commands-status.ini`, i **oba stupca istom metodom** — `FAZA9.md`
navodi 238/64, što je ista stvar izmerena kroz živo traženje skripta u sviti
`commands`, gde kontekst odlučuje da li se komanda dohvata. Broj koji meri
**pokrenuti program** je i dalje tamo
(`tests/coverage/test_command_coverage.cpp`, 2309 asertacija).

---

## Zašto je faza postojala: tri nalaza, ne jedan

### N107 — `voice=\s`: ime glasa koje drajver nije prihvatio ostajalo je u drajveru

U korisnikovom `~/.config/alus/alus.ini` stajalo je:

```ini
[speech]
voice=\s
rate=49
```

`\s` je GKeyFile zapis za **razmak**. Put kroz kod je bio:

1. `speech::Service::applyConfiguration()` je pročitao `voice` i pozvao
   `synth_->setVoice(" ")` **bez provere** da takav glas postoji;
2. `SsipSynth::setVoice()` je **prvo upisao** `voice_ = id`, pa onda pozvao
   `spd_set_synthesis_voice()`, koji je odbio;
3. funkcija je vratila `false` — i niko to nije gledao.

Posle toga je `synth_->voice()` prijavljivao glas koji ne postoji. Sve nizvodno
je u to verovalo: rečnik glasa se tražio pod tim imenom, ponovno povezivanje na
speech-dispatcher je ponovo postavljalo isti nepostojeći glas, a prsten
podešavanja bi ga prikazivao. **Jedino što se nije dogodilo jeste da je neko
bio obavešten.**

Lek je na dva mesta, jer su to dve greške:

* `SsipSynth::setVoice()` **vraća staro ime** kad server odbije, i to upiše u
  dnevnik;
* `speech::Service::applyVoiceFromConfiguration()` pita drajver šta ima pre
  nego što mu nešto zada, i kad se ne poklapa — **kaže šta ima**:

```
WARNING the configured voice ' ' is not one of the 1 the driver offers
        (Marija); keeping the driver's own default. Pick one with
        `alus-cli list-voices`, in the settings window, or with
        alus+control+rightArrow
```

Ta jedna linija je ono što je nedostajalo: korisnik je imao neispravnu
konfiguraciju i nijedan trag o tome.

### N108 — s tastature se nije moglo promeniti **ni jedno** podešavanje govora

Ovo je nalaz o opsegu, ne o kodu, i veći je od N107. Prsten podešavanja sinteze
(`nextSynthSetting` i porodica, šest tastera) nikada nije bio napisan, pa je
jedini način da se promeni glas ili brzina bio:

* upisati **ime glasa rukom** u tekstualno polje u prozoru podešavanja
  (`src/gui/model.cpp`: `RowKind::Text` za `voice` i `outputModule`), ili
* otvoriti `~/.config/alus/alus.ini` u uređivaču teksta.

Oba zahtevaju da korisnik **već zna** ime glasa koji sintetizator ima. To je
jedina stvar koju korisnik kome je čitač ekrana prebrz ili govori na pogrešnom
jeziku ne može da sazna. Zato je prsten prva stvar koju je ova faza napisala, i
zato `alus::speech::SettingsRing` čita **šta drajver prijavljuje** i **izgovara
svaku vrednost na koju stane** — bez imena, bez prozora, bez pomoći osobe koja
vidi.

### N109 — dva testa su se zatezala kako proizvod postaje bolji

Uhvaćeno time što su **pali** pri pisanju ovih komandi, oba na isti način:

```
CHECK( described > 100 )   with expansion: 98 > 100
CHECK( marked > 20 )       with expansion: 20 > 20
```

* `tests/coverage/test_command_coverage.cpp` je brojao koliko komanda browse
  mode-a ima opis — ali je u taj broj ulazila i **svaka najava** „nema u ovoj
  verziji", jer lanac klasa browse mode-a završava u
  `globalCommands.GlobalCommands` i `bindAnnouncements` ih veže i tamo. Broj je
  padao za jedan sa svakom **napisanom** komandom.
* `tests/gui/test_reference.cpp` je tražio da u `docs/commands.md` bude više od
  20 redova označenih sa „НЕМА". Isto: taj broj opada kako komande nastaju.

Prag koji se mora **spuštati posle svakog dobrog commita** je prag u koji
niko neće verovati dvadeset sedmi put. Oba su prepisana:

* prvi više ne broji najave i ima prag daleko ispod izmerene vrednosti (75),
  jer ono što čuva jeste da katalog **uopšte** bude povezan sa interceptorom
  (N102), a to obara broj na skoro nulu;
* drugi broji **iz `data/commands-status.ini`**: svaka komanda koja nije
  napisana i jeste u tabeli mora biti označena, pa je uslov `marked ==
  shouldBeMarked`. Kad `[announce]` ostane prazan, ta linija se briše — i
  projekat će to biti zaslužio.

### N110 — regularni izraz za „tekstualni pasus" je prepisan sa jednim delom manje

`p` i `shift+p` skaču na pasus koji **izgleda kao proza** — ne na naslov, ne na
red linkova. To nije AT-SPI upit nego regularni izraz nad tekstom pasusa, i
preuzet je od upstream-a (`DEFAULT_TEXT_PARAGRAPH_REGEX`). Upstream-ov šablon
je:

```
{lookBehind}{optQuote}{punc}{optQuote}{optWiki}{lookAhead} | {punc2} | {cjk}
```

`optQuote` se javlja **dva puta** — pre i posle točke — i prvi prepis je imao
samo prvi. Posledica: `Citat se zatvara ovako.”` nije bio prepoznat kao
rečenica. Uhvatio ga je test slučaj napisan pre nego što je izraz uključen u
komandu (`tests/nav/test_paragraphs.cpp`, 19 asertacija), što je i razlog zašto
izraz stoji u **javnom** zaglavlju (`kTextParagraphPattern`) a ne skriven u
`browse_mode.cpp`: ono što taster znači korisniku je u celosti svojstvo tog
izraza.

### N111 — ceo GTK 4 je bio „onemogućen", i ALUS-ov prozor podešavanja sa njim

**Nađeno živim merenjem prema Orci** (`MERENJE-ORCA.md`), i to je pravi uzrok
rečenice od koje je faza počela. Korisnik je, sa ALUS-om na tastaturi, otvorio
prozor podešavanja (`alus+control+s`) i prošao ga tabom i strelicama. Od **207**
stvari koje je ALUS za to vreme izgovorio, **161** je sadržalo reč
*„недоступно"* — naslov, dijalog, svaka lista, svaki izborni okvir, svako
dugme. Zaključak *„ne mogu da podesim sintezu, glas"* bio je tačan na osnovu
onoga što se čulo.

Pravilo u `src/a11y/state_map.cpp` je bilo:

```cpp
if (!hasBit(bits, Enabled) || !hasBit(bits, Sensitive)) states.add(Unavailable);
```

Nezavisnom bibliotekom — Atspi preko GObject introspection, **mimo ALUS-ovog
koda** — izmereno na GTK 4 / libadwaita 1 / AT-SPI 2.56.2:

```
button     enabled=False sensitive=True  'Врати сва подешавања'
page tab   enabled=False sensitive=True  'Говор'
combo box  enabled=False sensitive=True  'Језик'
button     enabled=False sensitive=True  'Затвори'
```

**GTK 4 postavlja `SENSITIVE` na svaki objekat, a `ENABLED` ni na jedan.** Uz
staro pravilo, svaki GTK 4 program je bio onemogućen od naslova do poslednjeg
dugmeta. Orca proverava **samo** `SENSITIVE` (`ax_utilities_state.py:303`), pa
kod nje radi.

Prva dijagnoza je bila pogrešna i vredi je zapisati: posle prvog upita činilo
se da GTK 4 ne prijavljuje bitove na **kontejnerima**, pa je prva popravka bila
lista rola. Kontejneri su se očistili, ali je `alus-cli a11y-tree 30
alus-settings` i dalje pokazivao 18 kontrola kao `[unavailable]` — i tek je
nezavisna biblioteka pokazala da je problem `ENABLED`, ne rola. Lista rola je
ostala, jer je i ona Orcino pravilo (njen `_generate_state_sensitive` se poziva
iz generatora za dugmad, polja i stavke menija, a ni iz jednog kontejnerskog)
i jer bez nje gnome-shell-ovi paneli ostaju „onemogućeni".

| `[unavailable]` u `alus-settings` | |
|---|---|
| pre | svi objekti |
| posle liste rola | 18 od 199 (sve kontrole) |
| **posle `SENSITIVE`-only** | **0 od 199** |

U gnome-shell-u ostaje 10 polja za tekst sa `[unavailable]` — i ta su, po istoj
nezavisnoj biblioteci, `sensitive=False` (skrivena polja ljuske, npr. pretraga
pregleda koja nije otvorena). Tu ALUS i Orca sada kažu isto.

Testovi: `tests/a11y/test_mapping.cpp`, tri sekcije `N111` — GTK 4-ov odgovor
nije onemogućen, kontejner bez `SENSITIVE` nije onemogućen, kontrola bez
`SENSITIVE` **jeste** (onemogućeno dugme mora i dalje da se najavi).

## Drugi živi prolaz: browse mode u Chrome-u (N112–N116)

Korisnik je prošao YouTube stranicu: **Tab je radio, a `h` nije skakao po
naslovima, strelice gore/dole nisu čitale red, strelice levo/desno nisu išle
znak po znak.** Faza 6 je T6.7 ostavila „delimično" jer browse mode nikad nije
susreo pravi pregledač — ovo je bio prvi susret.

Sve je reprodukovano **bez tastature** novom komandom
`alus-cli browse-walk [naslov]`: sastavlja browse mode nad živom stranicom,
izvršava iste skripte koje izvršavaju tasteri, i ispisuje šta bi bilo
izgovoreno (sintetizator na dnu beleži, ne govori), nad stalnom test
stranicom `tests/fixtures/browse-mode.html`. Svaki nalaz ispod je proveren
nezavisnom bibliotekom (Atspi/GI) pre nego što je kod promenjen, i svaki je
imao isti uzrok sakrivanja: **mock je bio zgodniji od platforme**, pa su se
mock i kod slagali u grešci.

| Nalaz | Šta se čulo | Uzrok | Popravka |
|---|---|---|---|
| **N112** | `h` uvek isti naslov; strelica dole uvek isti red; gore „ivica dokumenta" | `AtspiBackend::path()` je vraćao **AccessibleId** (HTML `id`, većinom prazan), pa je bookmark kursora vraćao poziciju na prvi list sa praznom putanjom — vrh stranice. Mock je uvek vraćao jedinstvenu putanju. | `path()` = ime magistrale + putanja objekta (javna polja AtspiObject, bez D-Bus poziva) |
| **N113** | strelica desno na „Treći": „ći", pa preskočeno „i" | AT-SPI offseti su **codepoint-i**; tekstualni sloj ih je tretirao kao UTF-8 bajtove (§7.3 plana). Mock je brojao bajtove. | `OffsetsTextRange::offsetUnit()`; `ObjectTextRange` = codepoint-i; mock broji kao platforma; odstupanje upisano u `01-ARHITEKTURA.md` §7.3 |
| **N114** | „navigation, `<U+FFFC>` `<U+FFFC>` `<U+FFFC>`" umesto linkova | List sa mešovitim sadržajem (tekst + U+FFFC za decu) išao je u tok bez razrešavanja hypertexta | svaki U+FFFC dobija polje svog deteta sa `content` = tekst linka; znak ostaje u toku (brajeva mapa), govor ga ne izgovara |
| **N115** | „lista" na svakom redu liste | Chrome ne postavlja READ_ONLY na `<ul>`, pa je lista bila „list box" (jednoredna kontrola). NVDA-ov virtuelni bafer dodaje READONLY svakom neuređivom čvoru. Mock lista je imala READONLY eksplicitno. | polje dokumenta dobija READONLY ako objekat nije ni uređiv ni fokusabilan |
| **N116** | naslov bez „heading level N"; kod `h` tekst dvaput | objektni model daje `Heading1`–`Heading6`, a pravila za polja traže `Heading` + `level`; Chrome naslovu daje ime = tekst, bez `explicit-name` | polje dokumenta: `Heading` + `level`; ime jednako sopstvenom tekstu, bez `explicit-name`, se ne izgovara posebno |

Rezultat na test-stranici, isti `browse-walk`, posle svih pet:

```
nextHeading         main landmark  Prvi naslov stranice  heading  level 1
nextHeading         Drugi naslov  heading  level 2
nextHeading         Treći naslov  heading  level 2
nextHeading         Četvrti naslov, nivo tri  heading  level 3
nextHeading         no next heading
topOfDocument       banner landmark  Glavni meni  navigation landmark  link Početna  link Vesti  link Kontakt
moveByLine_forward  main landmark  heading  level 1  Prvi naslov stranice
moveByLine_forward  list  with 2 items  •  link  Prva veza u listi
moveByLine_forward  •  link  Druga veza u listi
moveByLine_forward  out of list  heading  level 2  Treći naslov
moveByCharacter     r, e, ć, i, razmak   /   nazad: i, ć, e
```

Testovi: `[N113]` u `tests/nav/test_text_range.cpp` (latinica i ćirilica,
znak i reč, oba smera), `[N114]`, `[N115]`, `[N116]` u
`tests/nav/test_browse_mode.cpp` — svaki gradi stranicu onako kako je **Chrome**
šalje, ne kako je raniji test pretpostavljao. Valgrind nad `browsemode`, `nav`,
`a11y`, `gui`: 0 grešaka, 0 izgubljenih bajtova.

Usput: N107-ova provera glasa je upozoravala na svakoj `alus-cli` komandi koja
samo ispisuje (radi bez govora, na NullSynth-u, koji nudi samo glas „null").
Provera se sada preskače bez pravog sintetizatora.

**Šta `browse-walk` ne pokriva i ostaje za živi prolaz:** znak po znak preko
linka **unutar** reda je jedan korak (link zauzima jedan offset u roditelju), a
NVDA ulazi u tekst linka; i kašnjenje taster → govor.

## Treći živi prolaz (N111 dopuna, N117–N119)

| Nalaz | Šta se čulo | Uzrok | Popravka |
|---|---|---|---|
| **N111, dopuna** | kartica terminala: „jezičak, nedostupno, izabrano" | GTK3 kartica javlja **ENABLED bez SENSITIVE** — obrnuto od GTK4. Pravilo „samo SENSITIVE" (Orcino) ju je proglasilo onemogućenom. | onemogućeno **samo kad nema nijednog** od dva bita — tačno i za GTK4, GTK3 karticu, stvarno onemogućene kontrole u oba toolkita i skrivena polja ljuske |
| **N117** | strelica levo/desno preskače link unutar reda u jednom koraku | link u redu je jedan U+FFFC u roditeljskom listu, a pozicija je bila samo (list, pomeraj) | pozicije **unutar linka**: znak i reč ulaze u tekst linka i izlaze iz njega u oba smera; red/pasus/rečenica ostaju roditeljevi; bookmark nosi roditelja (ASCII RS) |
| **N118** | „popunjavanje" pri kretanju po ikonicama radne površine | proširenje za ikonice pakuje svaku kolonu u FILLER bez imena; AT-SPI ima tu rolu, MSAA nema, pa je NVDA-ina lista „layout" nije sadržala | filler bez imena je layout (ne najavljuje se kao predak), a reč „filler" se ne izgovara nikad — samo ime, ako ga ima |
| **N119** | u podešavanjima sinteze govor ćuti i seče se; glas se menja dok se tek bira | **svaka** promena u prozoru je upisivala `alus.ini`, a ALUS na svaki upis radi **pun `resetConfiguration`** — gasi i diže govor, tastaturu (sve grabove) i objektni model | prozor sve promene čuva i **upisuje jednom, pri zatvaranju** (NVDA-ino „U redu"); izbor izlaznog modula **odmah osvežava listu glasova** u istom prozoru (nov upit speech-dispatcher-u u procesu podešavanja); glas starog modula postaje „Podrazumevano"; fabrički reset se i dalje upisuje odmah |

N119 je proveren na živom prozoru u delu koji se spolja može proveriti: dok je
prozor otvoren, `alus.ini` ostaje **nepromenjen**. Osvežavanje liste glasova
nije moguće izazvati spolja — AdwComboRow prema AT-SPI ne izlaže ni akciju ni
izbor — pa ostaje za živi prolaz.

`tools/alus-switch.sh`: drugi čitač dobija 20 s da se digne (dvaput nije stigao
za 8 s i rollback je vratio ALUS), a njegov dnevnik se dopisuje sa vremenom
umesto da se prepisuje.

Testovi: `[N117]` (napred i nazad kroz link, red u linku je red pasusa,
bookmark u linku), filler u `test_object.cpp` i `test_object_speech.cpp`,
GTK3 kartica u `test_mapping.cpp`. Valgrind nad `browsemode`, `nav`, `a11y`,
`gui`: 0 grešaka, 0 izgubljenih bajtova.

**N120 — Super taster: uzrok nađen delimično, popravka delimično proverena.**

Četiri čuvane žive probe (ALUS drži tastaturu, čuvar vraća Orcu; korisnik je
pristao) i novi trag dolaznih događaja na IO nivou (`event in: …`) pokazali su:

1. Posle pravog Super tastera ALUS **dve sekunde ne prima nikakav fokus**, pa
   ne kaže ništa — a pošto ne zna da je fokus otišao u pregled aktivnosti,
   zadržava grabove browse mode-a iz Chrome-a, i prvo otkucano slovo u
   pretrazi („t") ode brzoj navigaciji („nema sledeće tabele") umesto u polje.
   Watchdog za to vreme ne reaguje: petlja nije blokirana, događaji fokusa
   jednostavno ne stižu.
2. U čistoj probi sa isključenim `screen-reader-enabled` gnome-shell je pri
   otvaranju pregleda poslao 344 `showing` i 132 `children-changed` događaja i
   **nijedan fokus**. `alus-switch.sh` je taj ključ isključivao za sve vreme
   rada ALUS-a, da sesija ne bi vratila Orcu.
3. Ključ više nije isključen: Orca se gasi bez diranja ključa — preko session
   managera (`Client.Stop`) ako ju je pokrenula sesija, signalom inače — i
   provereno je uživo da se **ne vraća sama** (sesija je pokreće kad ključ
   PROMENI vrednost u `true`, i vraća onu koju je sama pokrenula kad umre —
   prva verzija popravke je ključ isključila pa uključila, i baš to uključivanje
   je pokrenulo Orcu za 4 s; skripta je to uhvatila, zaustavila je ponovo i
   izgovorila upozorenje).

Šta **nije** dokazano: da je ključ jedini uzrok. Poslednja poređenja su rađena
dok je u Chrome-u bio otvoren prozor Messenger poziva, koji uzima fokus kad se
pregled otvori preko D-Bus-a — i sa Orcom na tastaturi i uključenim ključem
gnome-shell tada nije poslao fokus. Konačna provera je pravi Super taster sa
ALUS-om pokrenutim novom skriptom, bez otvorenog poziva.

Usput nađeno, otvoreno: pri kucanju u Messenger-ovom polju svako otkucano
slovo dolazi sa „naslov nivo 1" ispred sebe.

**Ranija beleška (pre N120):** Super taster ne govori ništa.

**Potvrda N120 (korisnik, uživo):** sa novom skriptom Super taster govori.

**N121 — rezultat pretrage u pregledu se ne izgovara dok se ne pritisne
strelica.**

Kucanje „datot" u pretrazi pregleda nađe Datoteke, ali ALUS ništa ne kaže.
Izmereno (dnevnik od 11:21): gnome-shell posle prvog slova pošalje **jedan**
`object:state-changed:selected` za prvi rezultat, dok fokus tastature ostaje u
polju; posle svakog sledećeg slova samo **preimenuje** postojeće rezultate (19
`accessible-name` događaja po slovu), bez novog `selected`. ALUS je promenu
stanja izgovarao samo za fokus i njegove pretke, pa rezultat nije čuo nikad —
dok strelica nije pomerila pravi fokus.

Prvi pokušaj (`07a3900`, preusmeravanje fokusa na rezultat, kao Orca) **nije
radio** uživo. Druga živa proba korisnika (dnevnik od 11:46) pokazala je tri
uzroka, nijedan u sintezi:

1. **Ograničivač događaja** propušta 10 događaja po aplikaciji u ciklusu,
   najnovije. Gnome-shell za tri sekunde kucanja pošalje 1693 `showing`, 398
   `children-changed` i 45 preimenovanja — prvi `selected` je odbačen i modul
   ga nije ni video.
2. **Lažni fokus prozora:** na prvo slovo stižu dva fokusa, polje pa prozor
   „Main stage". ALUS je zadržao prozor: slova nisu izgovarana, a „Main stage
   prozor" i „Pregled panel" su izgovoreni ispred rezultata. „Датотеке, дугме"
   je u 11:46:36.267 stavljeno u red, ali nikad nije stiglo do sinteze — sledeći
   taster je prekinuo govor dok je još trajao „Main stage".
3. Preusmeren fokus na rezultat gasi **odjek otkucanih slova** (odjek je samo
   za polja za unos, `speakTypedCharacters=editControls`, kao NVDA).

Ćirilica nije uzrok: hrvatski glas RHVoice-a ima ćirilična slova u
`graph.txt` i `g2g.fst`.

Popravka (`src/app/modules/gnome_shell.cpp`, `src/core/event_limiter.cpp`,
`src/a11y/events.cpp`):

* `object:state-changed:showing` se više ne prima (ALUS ga ne koristi);
* `selected` sa detail1=1 je **prioritetan** i ne troši budžet; `children-changed`
  ima svoj budžet; objekat koji modul prati (`Registry::setWatchedObject`)
  izuzet je kao fokus;
* modul odbija fokus gnome-shell prozora (Orca isto);
* izabrani rezultat se **izgovara, a fokus ostaje u polju** (kao NVDA u
  Windows Start pretrazi), pa se slova i dalje izgovaraju; kad gnome-shell
  preimenuje taj rezultat, novo ime se izgovori jednom.

Testovi u `tests/app/test_modules.cpp`; test poplave je proveren i obrnuto
(bez prioriteta pada). U prolazu je ALUS video pritisak
(Super_L stiže, nije progutan, GNOME otvara pregled aktivnosti), posle toga su
stigli događaji iz gnome-shell-a, ali je upit o objektu vraćao „The
application no longer exists" i ništa nije izgovoreno. Kad se pregled otvori
preko D-Bus-a dok Orca radi, ALUS ga **čita** („Pregled, panel", „polje za
uređivanje, Upišite željeno"). Razlika između dva uslova je ili
`screen-reader-enabled=false` (koji `alus-switch.sh` postavlja), ili put
samog tastera — što traži živu probu sa ALUS-om na tastaturi.

### Sitniji nalazi

* `nav::TextCommands::reportReviewCursorLocation()` je izgovarao **neprevedeni
  string literal**, dok je njegov blizanac za sistemski kursor dva reda više
  bio uredno kroz `_()`. Svita `gui` to sada hvata, jer traži 0 neprevedenih od
  1170 — i upravo je ona pala kad je ovaj niz dodat u katalog.
* `input::InputCommands::toggleSpeakCommandKeys()` ima isti oblik greške
  (`"speak command keys off"` bez `_()`). **Nije dirano u ovoj fazi** i ostaje
  kao jedini poznati primer; vidi „Šta je ostalo".

---

## Šta je napisano (29 komanda)

| Taster | Komanda | Gde živi |
|---|---|---|
| `alus+control+→` / `←` | `nextSynthSetting`, `previousSynthSetting` | `src/speech/global_commands.cpp` nad `speech::SettingsRing` |
| `alus+control+↑` / `↓` | `increaseSynthSetting`, `decreaseSynthSetting` | isto |
| `alus+control+pageUp` / `pageDown` | `increaseLargeSynthSetting`, `decreaseLargeSynthSetting` | isto |
| `alus+s` | `speechMode` | isto |
| `alus+p` | `cycleSpeechSymbolLevel` | isto |
| `alus+x` | `repeatLastSpokenInformation` | isto, uz `Presenter::lastSpeech()` |
| `alus+control+c` | `saveConfiguration` | `src/config/commands.cpp` |
| `alus+control+r` | `revertConfiguration` | isto (1 pritisak = sa diska, 3 = fabrički) |
| `alus+6`, `alus+7` | `toggleCaretMovesReviewCursor`, `toggleFocusMovesNavigatorObject` | `src/a11y/commands.cpp` |
| `alus+5`, `alus+u` | `toggleReportDynamicContentChanges`, `toggleProgressBarOutput` | isto |
| `shift+numpad2` | `reportFocusObjectAccelerator` | isto |
| `alus+d` | `reportDetailsSummary` | isto (ciklus kroz više meta na ponovni pritisak) |
| `alus+f1` | `navigatorObject_devInfo` | isto, uz novi `a11y::Object::devInfo()` |
| `alus+k` | `reportLinkDestination` | isto, uz novi `a11y::Object::linkDestination()` |
| `alus+f12` | `dateTime` | `src/commands/system.cpp` |
| `alus+shift+b` | `say_battery_status` | isto, iz `/sys/class/power_supply` |
| `alus+control+f1` | `reportAppModuleInfo` | `src/app/commands.cpp` |
| `alus+shift+s` | `toggleCurrentAppSleepMode` | isto |
| `alus+b` | `speakForeground` | `src/nav/text_commands.cpp` |
| `alus+control+space` | `moveToParentTreeInterceptor` | isto |
| `alus+numpadDelete` | `caretPos_currentDimensions` | isto |
| `alus+shift+space` | `toggleSingleLetterNav` | `src/nav/browse_mode.cpp` |
| `p` / `shift+p` | `nextTextParagraph`, `previousTextParagraph` | isto |

### Novi testovi

| Svita | Fajl | Šta meri |
|---|---|---|
| `speech` | `tests/speech/test_settings_ring.cpp` | prsten: šta ulazi u njega, koraci (5 / 20, glas po 1), opsezi 0–100, upis u konfiguraciju, zamena drajvera. 97 asertacija |
| `commands` | `tests/commands/test_speech_commands.cpp` | osam komanda govora i dve za konfiguraciju, **pokrenute kroz pravi dispečer**, sa brojanjem ponovnih pritisaka. 211 asertacija |
| `commands` | `tests/commands/test_system_commands.cpp` | baterija nad `/sys` drvetom koje test sam napiše (prazna, puna, bez baterije, bez klase, dva para brojača), sat sa zadatim trenutkom. 47 asertacija |
| `nav` | `tests/nav/test_paragraphs.cpp` | izraz za tekstualni pasus: proza da, naslovi i meniji ne, `3. novembar` ne. 19 asertacija |
| `browsemode` | `tests/nav/test_browse_mode.cpp` | `toggleSingleLetterNav`: traženje skripta prestaje da odgovara na slovo, komandni taster se ne menja |

---

## Odstupanja od upstream-a, sa razlogom

**O1 — nova biblioteka `libalus_commands`.** Biblioteka govora (faza 2) ne sme
da zavisi od skript sloja (faza 3), a ulazna biblioteka ne sme da zavisi od
govora — `src/meson.build` objašnjava oba, i drugo je ono što dozvoljava da se
ceo lanac gestova testira bez sintetizatora. Skript koji **menja podešavanje
govora** traži obe polovine, pa živi iznad obe, isto kao komande brajevog reda
u fazi 7.

**O2 — `alus+x` drugi pritisak sricanjem, ne prozorom.** Upstream otvara
*browseable message* — prozor sa tekstom, dugmetom za kopiranje i klizačem.
ALUS ima samo jedan prozor i on je u drugom procesu (N79). Sricanje je ono što
svaka druga „pritisni opet" komanda u ALUS-u radi, a drugi pritisak se skoro
uvek traži zbog jedne reči koja se nije razumela.

**O3 — `shift+numpad2` izgovara akcelerator onako kako ga aplikacija daje.**
Upstream ga razlaže na imena tastera (`shortcutKeys.speakKeyboardShortcuts`).
Oznake u `data/key-labels.ini` su ključene po **ALUS imenima gestova**, a
akcelerator je niz iz tuđeg rečnika: GTK piše `<Control>o`, Qt piše `Ctrl+O`.
Mapiranje je posao za kasnije; izgovoriti tekst aplikacije je ispravno u svakom
slučaju i pogrešno ni u jednom.

**O4 — `alus+f1` ide u dnevnik i to kaže.** Upstream otvara svoj prikazivač
dnevnika; ALUS ga nema. Dvadeset redova detalja o platformi nije za
sintetizator, ali taster koji ništa ne kaže ne razlikuje se od tastera koji
nije stigao — pa kaže *„Razvojne informacije su upisane u dnevnik"*.

**O5 — `alus+f12` nikada ne izgovara sekunde.** Upstream pita Windows da li
sat na traci prikazuje sekunde i poravna se sa njim. Linux nema tu jednu
postavku, a izgovoreni sat koji broji sekunde je šum u svakom slučaju u kome se
ovaj taster pritisne. Format vremena je **lokalizacijski** (`nl_langinfo(T_FMT)`
bez polja za sekunde); raspored dugog datuma je ALUS-ov, jer glibc ne izlaže
„dugi datum" kao Windows — **imena** meseca i dana su lokalizacijska.

**O6 — baterija se čita iz `/sys/class/power_supply`.** Upstream zove
`GetSystemPowerStatus`. Kernelov interfejs ne traži ni demona, ni D-Bus, ni
`upower`. Izgovorene poruke su upstream-ove, reč u reč, jer su već prevedene na
srpski. Prazna klasa `power_supply` je **odgovor** („Nema sistemske baterije"),
a klasa koje nema nije („Nepoznato stanje napajanja") — to su dve različite
činjenice i ne smeju da se izgovore isto.

**O7 — `alus+shift+s` ne najavljuje fokus ponovo pri izlasku iz režima
spavanja.** Upstream ponovo pokrene `gainFocus`. App sloj do govora stiže samo
kroz svoje hook-ove i nema način da zamoli objektni model da predstavi fokus
drugi put; korisnik gubi jedno ponavljanje koje dobija sa `alus+tab`, a drugi
put događaja bio bi veća šteta.

**O8 — `NVDA/` radno stablo je obrisano; ostao je podskup.**
`reference/nvda/` (1,9 MB, 55 fajlova) nosi samo module koji su **izvor
istine**: ono što skenira `tools/extract-commands.py`, `keyLabels.py`,
`controlTypes/`, 28 locale mapa gestova, srpski katalog, i tri fajla koja
citiraju `01-ARHITEKTURA.md` i `03-RIZICI.md`. Provereno **pre** brisanja:
ekstraktori daju bajt-identične `scripts.csv`, `gestures-builtin.ini`,
`key-labels.ini` i `locale/sr/gestures.ini`, a `extract-controltypes.py
--check` javlja `unchanged` za svih 6 generisanih fajlova. Commit upstream-a je
zakucan u `reference/nvda/UPSTREAM_COMMIT`, jer `git rev-parse` nad putanjom
unutar ALUS-ovog repozitorijuma odgovara **ALUS-ovim** HEAD-om i upisivao bi
tuđe ime u svaki generisani fajl.

---

## Šta je ostalo: 18 komanda, u četiri grupe

Nijedna nije ostala zato što „nije stigla na red". Razlozi su u
`data/commands-status.ini` uz svaku, a ovde su grupisani:

**1. Platforma ne dozvoljava — miš (7).** `leftMouseClick`, `rightMouseClick`,
`toggleLeftMouseButton`, `toggleRightMouseButton`, `moveMouseToNavigatorObject`,
`moveNavigatorObjectToMouse`, `toggleMouseTracking`. Wayland ne dopušta
klijentu da sintetiše ili pomeri pokazivač (N18). Ovo nije „nije napisano" nego
„ne postoji put"; menja se jedino novim protokolom ili portalom.

**2. Platforma ne dozvoljava — clipboard (3).** `reportClipboardText`,
`copyLastSpokenInformation`, `log_markStartThenCopy`. Čitanje clipboard-a na
Wayland-u zahteva da klijent ima fokus tastature; ALUS nema prozor. Ni GNOME ne
izlaže clipboard preko D-Bus-a, a portala za clipboard nema.

**3. Izvan opsega v1.0 (5).** `activatePythonConsole` (ALUS nema ugrađeni
interpreter), `cycleAudioDuckingMode` i `cycleSoundSplit` (v1.1 audio),
`reloadPlugins` (faza 8 ne skida dodatke), `test_navigatorDisplayModelText`
(razvojna komanda za model ekrana koji ALUS nema).

**4. Treba funkcija koju ALUS nema (3).** `toggleScreenLayout` i
`toggleNativeAppSelectionMode` — prekidač nad režimom koji ne postoji;
prekidač koji ne menja ništa bio bi laž. `refreshBuffer` — ALUS-ov browse mode
čita **živo** AT-SPI drvo, pa nema bafera za osvežavanje; taster ipak mora da
odgovori, i odgovara.

### Predlog za `refreshBuffer` i njemu slične

Trenutna najava je generička: *„nema u ovoj verziji"*. Za `refreshBuffer` to je
**pogrešna informacija** — nje neće biti ni u jednoj verziji, jer na ovoj
platformi nema šta da osveži. `data/commands-status.ini` bi mogao da dobije
treću sekciju (npr. `[explain]`) u kojoj vrednost **nije** komentar za čitaoca
fajla nego **poruka koja se izgovara**. Posao je mali (`bindAnnouncements` već
prima tekst), a razlika je između tastera koji laže i tastera koji objasni.

### Ostali poznati — zatvoreni posle merenja

* **Lista glasova u prozoru podešavanja.** `voice` i `outputModule` više nisu
  tekstualna polja nego izbori, popunjeni od speech-dispatcher-a kad se prozor
  otvori (`gui::querySpeechChoices`, `gui::resolveChoices`). Prvi izbor je
  „Podrazumevano" (prazna vrednost); glas iz konfiguracije koji sintetizator ne
  nudi ostaje **vidljiv**, sa oznakom da ga sintetizator ne nudi — `voice=\s`
  iz N107 bi se tako video. Provereno na živom prozoru: red „Glas" pokazuje
  *Marija (hr)*. Ograničenje: glasovi su glasovi modula koji je bio podešen kad
  se prozor otvorio; posle promene modula prozor treba ponovo otvoriti, i to
  piše u podnaslovu. Testovi: `tests/gui/test_model.cpp`, `[gui][speech]`.
* **`toggleSpeakCommandKeys`** — neprevedeni literal zamenjen sa `_()`;
  prevod je stigao iz upstream-ovog kataloga.
* **Valgrind nad kodom ove faze:** svite `speech`, `commands`, `a11y`, `nav`,
  `browsemode`, `core` pod `--setup=valgrind` — 9/9, **0 grešaka, 0 izgubljenih
  bajtova**.

Ostaje predlog za `refreshBuffer` (sekcija `[explain]`, gore).

---

## Kako se proverava

```bash
# sve
meson test -C build

# samo ono što je ova faza dodala
meson test -C build --suite commands      # 2 testa: coverage + ponašanje komanda
./build/tests/test-speech '[ring]'        # prsten podešavanja sinteze
./build/tests/test-nav '[paragraphs]'     # izraz za tekstualni pasus
./build/tests/test-browsemode '[T6.9]'    # navigacija jednim slovom

# da li konfiguracija ima ispravan glas (N107)
./build/src/alus-cli list-voices
grep -A2 '^\[speech\]' ~/.config/alus/alus.ini

# N111: da li se ijedan GTK 4 objekat čita kao onemogućen
./build/src/alus-settings &
./build/src/alus-cli a11y-tree 30 alus-settings | grep -c unavailable   # mora 0

# šta još nije napisano, i zašto
sed -n '/^\[announce\]/,$p' data/commands-status.ini
```

Žive provere (traže pokrenutu sesiju i govor) nisu automatizovane u ovoj fazi:
prsten se čuje tek kad ga čovek pritisne. Minimalni ručni prolaz:

1. `alus+control+→` dok ne kaže *Brzina*, pa `alus+control+↑` — mora da
   izgovori novu vrednost i odmah da govori tom brzinom;
2. `alus+control+c`, pa restart ALUS-a — brzina mora da ostane;
3. `alus+control+r` tri puta — mora da se vrati na fabričko;
4. `alus+f12` jednom (vreme), dvaput (datum);
5. u pregledaču: `p` i `shift+p` moraju da preskaču menije i da staju na
   pasuse članka; `alus+shift+space` isključuje navigaciju jednim slovom i
   tada `h` upisuje `h` u polje za pretragu.

## Sledeće: merenje prema Orci

Posle ove faze poređenje sa Orcom ima smisla u oblasti u kojoj se najčešće
koristi: **koliko se komanda može izvršiti s tastature bez otvaranja prozora.**
Što treba izmeriti, i čime:

| Šta | Čime |
|---|---|
| promena brzine i glasa bez prozora | ručno, 1. i 2. korak gore; Orca: `Orca+Insert+arrows` u njenom „voice" ciklusu |
| kašnjenje od tastera do prvog zvuka | `./build/src/alus-cli bench-speech` (ALUS ima meru; Orca je nema, pa je potreban audio tap iz `spikes/s2-speech.c`) |
| čitanje veb strane | `p`, `h`, `k`, `alus+f7` u istom dokumentu u oba čitača |
| koliko komanda uopšte postoji | `./build/src/alus-cli list-commands` (**390** u katalogu, 316 dokumentovanih u `docs/commands.md`, **279** dodela tastera koje nešto rade) prema `orca --list-commands` |

**Pre merenja** treba `alus-cli autostart --disable` i proveriti da samo jedan
čitač drži tastaturu (N8): dva čitača na istoj sesiji znače da jedan od njih ne
dobija ništa, i rezultat ne meri ni jednog.

**Potvrda N121 (korisnik, uživo):** rezultat pretrage u pregledu se izgovara.

**N122 — `alus+f12` dvaput ne kaže datum; vreme se izgovara sa sekundama.**

Dnevnik (12:17:46): numpad Insert držan, F12 dvaput u razmaku od 200 ms, prag
je 500 ms — oba puta „12:17:46". Ulazni put je ispravan (novi test u
`tests/input/test_manager.cpp` sa istim tasterima daje brojeve ponavljanja 0 pa
1). Greška je u povezivanju daemona: `src/daemon/main.cpp` je komandama
govora, podešavanja i sistema davao `input.dispatcher()` **pre**
`subsystems.initializeAll()`, a dispečer nastaje tek kad se ulaz pokrene. Taj
pokazivač je bio prazan za ceo život daemona, pa je svaki drugi pritisak bio
prvi — i za `alus+f12`, i za `alus+x` (srikanje), i za trostruki
`alus+control+r` (fabrička podešavanja). Konteksti sada traže dispečer pri
svakom pritisku (`std::function`), što preživljava i restart ulaza.

Sekunde: `sr_RS` (i većina glibc locale-a) za vreme kaže `%T`, skraćenicu za
`%H:%M:%S`; kod je tražio samo `:%S`. Skraćenice `%T`, `%r`, `%X` se sada
razvijaju pre uklanjanja sekundi. Testovi u `tests/commands/test_system_commands.cpp`.

**Potvrda N122 (korisnik, uživo):** `alus+f12` dvaput kaže datum.

**N123 — u prozoru podešavanja Shift+Tab „ruši" govor.**

Nije pad: posle Shift+Tab speech-dispatcher ne počne nijednu poruku dok se
prozor ne zatvori (dnevnik 12:45:27.739–33.514, nijedan `ssip begin`, a ALUS
je sve vreme slao tekst; isto u prolazu od 12:44:47). Prozor nije bio kriv —
vezivalo se za Shift: Tab je radio, Shift+Tab nije.

Uzrok: Shift sam po sebi pauzira govor (kao NVDA), a Tab koji sledi ga
prekida. `Presenter::cancelSpeech` je pri prekidu brisao samo svoju oznaku
`paused_`; sintezi nije javljao da pauza prestaje. speech-dispatcher drži
vezu pauziranom i posle `spd_cancel`, pa je svaka sledeća poruka čekala iza
pauze — do ponovnog povezivanja, koje se desilo tek kad je zatvaranje prozora
upisalo podešavanja.

Izmereno na speech-dispatcher 0.12 + rhvoice, svaka proba na novoj vezi:

| redosled (dok poruka traje) | posle toga |
|---|---|
| pauza, prekid, govor (staro) | tišina |
| pauza, prekid, nastavak, govor | **tišina** |
| pauza, nastavak, prekid, govor | govori |

Popravka: `cancelSpeech` sada, ako je govor pauziran, **prvo** javi sintezi
nastavak pa tek onda prekida. Usput: pauza od Shift-a se zakazuje kao hitna,
kao i prekid, da bi se izvršavale redom kojim su tasteri stigli. Testovi u
`tests/speech/test_presenter.cpp` (bez popravke padaju dva).
