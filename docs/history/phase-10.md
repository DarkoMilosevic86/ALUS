# Faza 10 — Komande koje su se najavljivale, a nisu bile napisane

Izveštaj o izvršenoj fazi. Nije bila u `phase-plan.md`: naručena je posle Faze 9,
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
`data/commands-status.ini`, i **oba stupca istom metodom** — `phase-9.md`
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

**Nađeno živim merenjem prema Orci** (`orca-comparison.md`), i to je pravi uzrok
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
| **N113** | strelica desno na „Treći": „ći", pa preskočeno „i" | AT-SPI offseti su **codepoint-i**; tekstualni sloj ih je tretirao kao UTF-8 bajtove (§7.3 plana). Mock je brojao bajtove. | `OffsetsTextRange::offsetUnit()`; `ObjectTextRange` = codepoint-i; mock broji kao platforma; odstupanje upisano u `architecture.md` §7.3 |
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
citiraju `architecture.md` i `risks.md`. Provereno **pre** brisanja:
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

**Potvrda N123 (korisnik, uživo):** Shift+Tab u prozoru podešavanja govori.

**N124 — u prozoru podešavanja izbor samo razmakom, bez vrednosti i bez
vidljivog dugmeta za zatvaranje.**

Tri stvari, izmerene na `alus-settings` (GTK 4.18.6, libadwaita 1.7.6):

1. **Vrednost se nije čula.** `AdwComboRow` ne daje ni `Value` ni `Selection`;
   izabrana stavka je labela u jedinoj stavci liste:
   `combo box > panel > panel > list > list item > panel > label "cr1 (hr)"`.
   ALUS je izgovarao „Глас, изборни оквир" bez glasa. Sada je vrednost
   padajuće liste bez Value/Selection tekst te stavke (`shownChoiceText`), a
   promena imena unutar fokusirane padajuće liste izgovara se kao promena
   njene vrednosti — strelice preimenuju baš tu labelu, a za samu listu ne
   stiže nijedan događaj. Važi za svaki GTK 4 program (i `GtkDropDown`).
   Provereno na živom prozoru preko `alus-cli a11y-tree`:
   `COMBOBOX "Глас" = cr1 (hr)`.
2. **Strelice nisu menjale izbor.** `AdwComboRow` otvara listu samo na
   razmak/Enter, a strelice prelaze na sledeći red. Sada strelice gore/dole,
   Home/End i Page Up/Down menjaju izbor na mestu, kao zatvorena padajuća lista
   u Windows-u i GTK 3; razmak i Enter i dalje otvaraju listu.
3. **Zatvaranje.** Dugme „Затвори" postojalo je samo kao ikonica u zaglavlju.
   Sada je na dnu svake stranice, gde Tab stigne posle poslednjeg podešavanja,
   sa opisom da zatvaranje primenjuje izmene.

Testovi: `tests/a11y/test_events.cpp` (vrednost i najava). Strelice u samom
prozoru nisu pokrivene testom (traže ekran); živa provera ostaje korisniku.

**N125 — Escape je primenio sintezu koju je korisnik samo pogledao; strelice
po listi i dalje ne govore.**

1. **Escape.** N119 je čuvanje vezao za zatvaranje prozora, pa je i Escape
   čuvao: korisnik je strelicama došao do eSpeak-a, pritisnuo Escape i čuo
   eSpeak umesto svog MBROLA NG. Sada važi Windows-ova konvencija: dugme **U
   redu** čuva i zatvara, a **Otkaži**, Escape i dugme u zaglavlju zatvaraju bez
   upisa (`Context::accept`, `Session::accepted` u `src/gui/main.cpp`). Oba
   dugmeta su na dnu svake stranice. Podnaslovi koji su obećavali „kad se
   prozor zatvori" sada kažu „kad pritisnete U redu".
2. **Strelice.** Pretpostavka iz N124 je bila pogrešna: strelice **ne**
   preimenuju labelu izabrane stavke. Dnevnik (13:13:40–13:14:04) za svaku
   strelicu pokazuje samo `object:state-changed:selected` na stavkama skrivene
   liste, bez ijedne promene imena. Najava zato više ne zavisi od vrste
   događaja: posle svakog ciklusa koji je nešto obradio, `Registry` uporedi
   vrednost fokusirane padajuće liste (bez Value/Selection) sa poslednjom
   viđenom i izgovori je ako se promenila (`announceShownChoice`). Test je
   prepravljen po izmerenom obrascu i proveren je obrnuto (bez provere pada).

**Potvrda N125 (korisnik, uživo):** strelice po listama govore, a Escape ne
primenjuje izmene.

**N126 — eSpeak: jezik i varijanta odvojeno; modul, glas i varijanta u
prstenu; dugme „Probaj".**

Izmereno: speech-dispatcher-ov `espeak-ng` prijavljuje **14805** glasova — 140
jezika × 105 varijanti, svaki kao „Serbian+Adam" sa jezikom „sr" i varijantom
„Adam"; `rhvoice` i `mbrola_ng` po jedan glas. Prsten (`alus+control+strelice`)
je stavku Glas imao samo uz bar dva glasa, pa korisnik na `mbrola_ng` u prstenu
nije imao ništa osim brojeva; na eSpeak-u bi ih bilo 14805.

* `speech::VoiceCatalog` deli listu **po podacima**, ne po imenu modula: glas
  čiji je id id drugog glasa + „+" + sopstvena varijanta je taj glas sa
  varijantom. Jezik interfejsa ide na vrh, ostalo po imenu. U konfiguraciji
  ostaje jedan ključ `voice` sa punim id-jem.
* Prsten: **Izlazni modul, Glas, Varijanta**, Brzina, Visina, Modulacija,
  Jačina — svaka stavka samo kad ima bar dve vrednosti. Promena modula bira
  glas istog jezika (sa `mbrola_ng` `cr1` (hr) na eSpeak → Croatian) i
  preuređuje prsten ostajući na modulu. `alus+control+c` čuva sve to kao i
  brzinu. `ISynth` je dobio `availableModules`/`outputModule`/`setOutputModule`
  sa podrazumevanim „nema modula".
* Prozor: red **Varijanta** ispod Glasa, vidljiv samo kad modul ima varijante;
  oba reda upisuju `voice`. Promena modula u prozoru takođe bira glas istog
  jezika. Dugme **Probaj** izgovara rečenicu izabranim modulom, glasom i
  varijantom (i neprimenjenom brzinom, visinom, jačinom) preko sopstvene veze
  `alus-settings`-a, pa se glas čitača ne menja.

Živo (privremena konfiguracija, `alus-cli a11y-tree`): `COMBOBOX "Глас" =
Croatian (hr)`, `COMBOBOX "Варијанта" = Adam`, `BUTTON "Пробај глас"`. Testovi:
`tests/speech/test_settings_ring.cpp` (katalog, eSpeak u prstenu, modul u
prstenu), `tests/gui/test_model.cpp`.

**Potvrda N126 (korisnik, uživo):** jezik i varijanta, prsten i „Probaj" rade.

**N127 — Nautilus ne kaže prvi fajl ni fajl nađen slovom; „čitaj sve" u
Writer-u čita jedan red.**

1. **Nautilus 48 (GTK 4)** pomera **izbor**, a ne fokus (dnevnik 13:52:59):
   otvaranje fascikle daje fokus listi („листа") i `selected` za prvi fajl;
   prva strelica posle otvaranja opet samo `selected`; skok slovom isto. Fokus
   za red stiže tek od druge strelice. Sada, kao NVDA (IAccessible `selection`)
   i Orca (`on_selected_changed`): stavka (list item, table row, tree item,
   icon) koja postane izabrana dok je fokus na njenoj listi — pretku do četiri
   nivoa — ili na drugoj stavci iste liste, **postaje fokus** i izgovara se
   (`Registry::selectionFollowsFocus`). Izuzetak: ako je trenutni fokus i dalje
   izabran, izbor se proširuje (Ctrl+A, Shift) i fokus ostaje.
   Sporost (≈200 ms od strelice do govora, bez ijednog reda u dnevniku između)
   su sinhroni AT-SPI pozivi ka Nautilusu; od sada se svaki događaj duži od
   50 ms beleži („slow event: … took N ms") sa objektom i ulogom, da sledeći
   prolaz pokaže koje svojstvo košta.
2. **„Čitaj sve" u LibreOffice Writer-u** (13:54:32): pročitan jedan red, pa
   `say-all:stop`. Svaki pasus je poseban objekat, a čitač je radio samo nad
   tekstom tekućeg. Sada se na kraju objekta prelazi na sledeći — `FLOWS_TO`
   (Writer tako vezuje pasuse, i preko stranica), inače sledeći tekstualni
   brat — preskačući prazne pasuse, najviše 200 skokova. Kursor već prati glas
   (karet se pomera kad sinteza stigne do reda), pa i u novim pasusima: posle
   prekida Ctrl-om karet je gde je čitanje stalo. Samo za karet; pregledni
   kursor ostaje u svom objektu, kao u NVDA.

Testovi: `tests/a11y/test_events.cpp` (izbor u listi: otvaranje, skok slovom,
Ctrl+A, druga lista), `tests/nav/test_say_all.cpp` (četiri pasusa sa praznim,
karet završava u poslednjem; bez popravke pada).

**N128 — Writer: „Yes/No" na `ALUS+strelica dole`, „čitaj sve" ponavlja i
govori „pasus"; Nautilus i dalje ćuti na prvoj strelici u fascikli.**
(Dnevnik `alus-measure-20261114-161004.log`.)

1. **Yes/No je LibreOffice-ov „Confirm overwrite mode"** (16:14:44): numerički
   Insert (ALUS taster) stigao je do Writer-a, a Writer ga čita kao prelazak u
   režim prepisivanja. Uzrok nije Writer: pri svakom zastoju jezgra (≥500 ms,
   ovde dok se Writer otvarao, 16:14:38 i 16:14:39) čuvar (N19) pusti sve
   tastere (`emergencyRelease`), ali ih posle oporavka niko ne vrati —
   `GestureManager::pushGrabs` vidi isti skup kao pre i ne šalje ništa. Taster
   je bio slobodan do prve promene skupa (izlazak iz Writer-a, 16:16:48). Sada
   čuvar, posle puštanja, stavlja u red glavne petlje
   `GestureManager::reinstallGrabs()`; izvršava se čim petlja ponovo radi
   („key grabs reinstalled after the core recovered").
2. **Ponavljanje i „pasus" pri čitanju**: Writer pomera FOKUS zajedno sa
   karetom iz pasusa u pasus, pa je svaki pasus u koji je „čitaj sve" uveo
   karet bio najavljen kao fokus — „пасус, вишелинијско, измењиво" i tekst
   pasusa ponovo, usred čitanja. Sada `SayAll::ownsFocusChangeTo` pamti
   poslednjih osam objekata u koje je pomerio karet (do `stop()`), a
   `PresentationHooks::isFocusAnnouncementSuppressed` gasi tu najavu; fokus se
   i dalje pomera.
3. **„Pasus" na strelicama**: pasus koji dobije fokus izgovara samo tekst, bez
   uloge i stanja (višelinijsko, izmenjivo). Stil pasusa ide u podešavanja
   formatiranja dokumenta, kasnije (korisnik).
4. **Nautilus, ulazak u fasciklu** (16:13:41–43): redovi se zamene, fokusirani
   red umre, GTK ne šalje fokus (lista ga već ima), a prva strelica daje
   `selected` za red čiji roditelj mrtvi fokus više ne zna. Sada se pita lanac
   predaka zapamćen kad je fokus postavljen: red do dva nivoa ispod roditelja
   ili dede starog fokusa je u istoj listi. Bočna traka je dalje od toga.
5. **Sporost**: „slow event" sada kaže i koliko AT-SPI poziva je događaj
   koštao, a svaki pojedinačni poziv od 20 ms ili duže beleži se imenom
   („slow AT-SPI call: … took N ms"). Merenje 161004: događaji za red u
   Nautilusu 177–225 ms, za listu do 245 ms.

Testovi: `tests/nav/test_say_all.cpp` (fokus prati karet kroz pasuse, glas
sporiji od događaja; bez popravke pada: tekst dva puta i „paragraph"),
`tests/a11y/test_events.cpp` (mrtav fokus posle ulaska u fasciklu; bez
popravke pada), `tests/input/test_manager.cpp` (tasteri se vraćaju posle
puštanja).

---

*From N129 on, findings are written in English (the documentation moved to
English on 2026-11-14).*

**N129 — Nautilus: one arrow key cost ~300 AT-SPI calls (~190 ms).**
(Log `alus-measure-20261114-181408.log`, then `tests/measure_focus.cpp` on
the live desktop.)

The N128 instrumentation showed no single slow call: a row's `gainFocus` was
177–225 ms for 290–317 calls, i.e. many cheap calls. To see which, the
backend now counts calls by name (`Backend::callsByName`) and the "slow
event" line lists them (`states×40 parent×31 …`). A new developer tool,
`build/tests/measure-focus`, assembles the daemon's layers silently against
the live bus and replays a focus event for the current focus, so the
breakdown could be measured without touching the user's desktop. A focus
move inside one window (desktop icons, 9 ancestors) cost **99 calls**:

1. **`appInfo` + `application`: 49 of 99.** The application's name, toolkit,
   toolkit version, AT-SPI version and pid (five calls) were read again for
   every object of every event, because the app module lookup, the tree
   interceptor veto and the overlay choice all key on them. They cannot
   change while the application is on the bus, and a unique bus name is never
   reused, so `AtspiBackend::appInfo` now remembers the answer per bus name
   (only a complete answer: toolkit and pid present; bounded at 256
   applications).
2. **The tree interceptor walked every ancestor on every key**, asking each
   one `alive`, `parent`, and its refined role (`role` + `states` +
   `attributes`) to see whether it is a document. Three changes:
   * an object's **platform role is read once per object** instead of once
     per cycle (GTK fixes it at construction; Gecko and Chromium create a new
     accessible when an element's role changes); a failed read is not kept;
   * `BrowseMode::isDocumentRoot` filters on the mapped platform role first
     (only document roles map to `Role::Document`, and no refinement or
     behaviour produces it otherwise) and asks `alive` only for a document;
   * for the focus, `TreeInterceptorRegistry::update` walks the ancestry
     `Api` already holds (and reuses across keys) instead of asking every
     ancestor for its parent again.

Result on the live desktop: **99 → 10 calls** for the same focus move. In the
mock Nautilus window (12 nested panels, rows with cells) one arrow is 22
calls, with the role read once and no walk to the top.

Seen while measuring, not changed: a focus inside a Chromium document costs
~180 calls in browse mode (`text`, `childAt`, `childCount` over ~16
objects, reading the focused line). Chromium answers fast (~10 ms in total),
so it is not the Nautilus problem; it is a candidate for the same treatment.

Tests: `tests/app/test_modules.cpp` ("N129 an arrow key in Nautilus costs a
few dozen platform calls"), `tests/nav/test_browse_mode.cpp` ("N129 a focus
move does not ask every ancestor whether it is a document"; without the fix
47 calls per move, `role` 17 times). T4.2 in `tests/a11y/test_object.cpp` now
expects the role to cost nothing in a later cycle.

**N130 — Chrome: silent when it opens; `e` does not find the YouTube search
field; typed characters are not heard.**
(Log `alus-measure-20261114-191835.log`.)

1. **Silent start** (19:19:19): Chrome sent `window:activate` and
   `document:load-complete`, and no focus event until the user pressed Tab.
   ALUS only cancelled speech on the foreground event (NVDA's rule: a focus
   event follows), so nothing was said. Now `Registry::armForegroundFocusCheck`
   waits 250 ms after a window comes to the foreground; if the focus is still
   outside it, the focused object is looked for in the window (breadth first,
   the start-up scan's budget) and announced as a focus, or the window itself
   when nothing reports FOCUSED. GTK's own focus event arrives within
   milliseconds, so the check then finds the focus inside and does nothing.
   The start-up scan (`findInitialFocus`) now searches breadth first too.
2. **`e` said "no next edit field"** on YouTube (19:19:43, 19:19:58), and `f`
   found the field: it is ARIA `combobox` over a text input, which Chromium
   exposes as COMBO_BOX with EDITABLE. The edit rule now matches entries,
   password fields and combo boxes that are EDITABLE (NVDA includes editable
   combo boxes too). EDITABLE (ALL) replaces "not READ_ONLY" (NONE): the one
   criterion that takes the combo box in and keeps both a read-only entry and
   a plain drop-down out.
3. **Typed characters silent** (19:20:09–23): every letter reached the
   synthesizer, and every one was followed by `ssip cancel acknowledged`
   within a millisecond. Each key's speech interrupt was *queued*
   (`GestureManager::applySpeechEffect`), while the echo was spoken *at once*,
   so the cancel ran after the echo and silenced it. The echo (character and
   word) now goes through the same queue, behind the cancel.
4. **Doubled letters** in the echo ("peeku", "suuze"): the compositor's
   duplicate key events (N33) interleave when keys overlap — `up p, down e,
   up p, down e` — and `KeyEventFilter` compared each event only with the
   previous one. It now remembers the last four events and drops a copy of
   any of them within the 5 ms window.

Tests: `tests/a11y/test_events.cpp` ("N130 a window that comes up without a
focus event gets its focus found"; fails without the fix),
`tests/nav/test_browse_mode.cpp` (editable combo box for `e`),
`tests/input/test_manager.cpp` (cancel before echo, in key order; interleaved
duplicates).

**N131 — YouTube in browse mode: "blank" on every other line, and the player's
buttons say "button, graphic" instead of their names.**
(Log `alus-measure-20261114-193516.log`.)

1. **Blank lines** (19:35:35–19:36:06): YouTube builds its layout from empty
   `<div>`s, which Chromium exposes as sections with a Text interface and no
   characters. Each was a leaf, so each was a line, and each said "blank".
   `DocumentLeaves::isBlankLeaf` now names them (a layout role — section,
   panel, filler, unknown, paragraph, text frame, grouping — with no children,
   no name and no characters), and the public walks (`first`, `last`, `next`,
   `previous`) step over them; the raw walks are kept as `*Any`.
2. **"button, graphic"** (19:36:06–09): each player button (play, next,
   subtitles, settings…) holds only an unnamed SVG icon, which was the leaf;
   the button's name (`aria-label`, "Pause (k)") was never said, because a
   container's name is not read on a line. NVDA's buffer renders such a
   button's name as its text. `DocumentLeaves::isNamedControlLeaf` makes a
   button, toggle button, menu item or tab with a name, no words of its own
   and no words below it (at most four children) a leaf in its own right, an
   embedded one, so its content is its name. Links are left out on purpose:
   a link often holds a heading that quick navigation lands inside.
3. Found while testing it: wherever an embedded leaf's content is its name
   (a named image too), a quick-nav jump said the name twice, once as the name
   and once as the content ("Pause, Pause, button"). The field is now marked
   `labelledByContent`.

Test: `tests/nav/test_browse_mode.cpp` ("N131 empty layout containers are not
lines, and an icon button says its name"; without the fix the arrow lands on
a blank and the button's content is empty).

**N132 — Gmail's message list stays in browse mode, a new Chrome window types
into nothing, and "skip ad" throws the reader to the top of the page.**
(Logs `alus-measure-20261114-214520.log`, `alus-measure-20261114-214734.log`.)

1. **Gmail**: the message list is an ARIA grid whose rows take the focus.
   `shouldPassThroughFor` knew no table roles, so the arrows read the page
   instead of moving between messages, and `x` (select a message) was the
   checkbox quick-nav key. Following NVDA (#5118, #13221), a table row, cell
   or header that is focusable or focused, and a focused list item inside a
   focusable list, now switch to focus mode. A plain table's cells never take
   the focus, so reading tables is unchanged. `onGainFocus` now receives the
   object, so arriving in a document *on* such a control (Gmail opens with
   the focus in the list) starts in focus mode too.
2. **Chrome start** (21:45:29): the N130 scan found the New Tab page's
   document FOCUSED and took it, but the real focus was the address bar, so
   ctrl+l sent no focus event. Browse mode kept the letters, and typing an
   address hit quick-nav keys (`o`: "no next embedded object"). Two fixes:
   `findFocusedIn` searches the window's own controls before any document,
   and a focused document is the answer only when nothing outside a document
   has the focus; and a caret event in an editable object that reports
   FOCUSED, while ALUS's focus is elsewhere, now becomes a focus event
   (`Registry::caretMovesTheFocus`). The second fix also recovers from any
   other focus event that was missed.
3. **"Skip ad"** (21:47–21:49): pressing the button removes it, and the page
   puts the focus back on its document. `onInnerFocus` moved the caret to the
   first leaf of whatever got the focus, which for the document is the top of
   the page. Following NVDA ("the caret isn't within the focused node"), a
   focus on the document itself, on the object under the caret, or on a
   container around the caret no longer moves the caret
   (`BrowseMode::focusContainsCaret`). The caret's own leaf was gone too, and
   a bookmark that does not resolve fell back to the top. A bookmark now also
   carries the path of the leaf before it (after ASCII GS), and restores to
   whatever follows that leaf now.

Tests: `tests/nav/test_browse_mode.cpp` ("N132 a focused grid row is focus
mode; the focus around the caret leaves it alone", whose five checks each fail
without their fix), and `tests/a11y/test_events.cpp` ("N132 a new Chrome
window's focus is its address bar, not the page").

**N133 — the elements list (ALUS+F7) is a dialog.** Until now the command only
said how many headings and links the page had (phase 6 owed the list; the
dialog was left for later). It is now NVDA's dialog: the element type as radio
buttons (links, headings, form fields, buttons, landmarks), the list, a
filter, and **Activate** (the default; Enter on an element too), **Move to**
and **Cancel** (Escape).

It is a separate program, `alus-elements` (`src/gui/elements_main.cpp`), for
the reason `alus-settings` is (phase-9.md N79): ALUS cannot read a window in
its own process. The daemon writes the labels to a temporary file, spawns the
child without waiting (`src/nav/elements_dialog.cpp`), and reads one line back
on a pipe watched by the main loop: `activate|move <kind> <index>`, or nothing
on Cancel. The objects never leave the daemon. The choice is held until the
focus comes back to the document, or for 600 ms if no focus event arrives,
because the browser's own focus event on returning would otherwise move the
caret off the element (`BrowseMode::chooseElement`,
`performPendingElement`). The type shown first is the one used last.

Test: `tests/nav/test_browse_mode.cpp` ("alus+f7: the elements list's choice
is performed when the focus comes back"): the protocol, Move to, Activate, the
timer path, and an index that is not in the list.

First live test: every row was read as just "list", and Tab stayed in the
list. GTK names neither a `GtkListBoxRow` from the label inside it nor a
mnemonic check button without its underscore ("Lin_ks"), so both are now
named explicitly. `gtk_list_box_set_tab_behavior(GTK_LIST_TAB_ITEM)` makes Tab
leave the list; the default walks Tab through every row.

**N134 — on YouTube every menu link was listed twice.** (Log
`alus-measure-20261114-224906.log`, 22:49:20: "Home 2 of 121", "Home 3 of
121", "Shorts 4", "Shorts 5"…) YouTube's guide entry is `<a role=link
title="Home">` holding a `<tp-yt-paper-item role=link>`, and the collection
returns both. For links, buttons, clickables and headings, an element inside
another of the same kind (four levels at most) is now the same place: the
elements list leaves it out, and quick navigation (`k`, `shift+k`) steps past
it instead of reading the same link twice. Lists, tables and landmarks nest
for real and are not affected (`BrowseMode::nestingIsSamePlace`).

Test: `tests/nav/test_browse_mode.cpp` ("N134 a link nested in a link is one
entry, in the list and for k").

**N135 — the elements list opens where the user is, and headings have
levels.** NVDA's dialog selects the element nearest the caret; ALUS's always
started on the first one of the page, so on YouTube the link under the caret
was dozens of arrows away. The daemon now marks one entry per type, the one
the list opens on (`BrowseMode::initialElementIndex`, NVDA's
`_initialElement`): the element the caret is on or inside (the innermost one,
for nested landmarks), otherwise the last one before the caret, otherwise the
first. "Before the caret" is not a position comparison per element, which
walks the document's leaves (`DocumentTextRange::comparePositions`); it is
one quick-nav search forwards from the caret, the one `k` makes, and the entry
before what it finds. The file gains a column for it:
`kind<TAB>level<TAB>caret<TAB>label`. While filtering, the first match is
selected instead, as that is what the typed text is looking for.

The list is now a tree, as NVDA's is: the list box is created with the
`tree` role and each row as a `tree item`, and a heading's row carries
`GTK_ACCESSIBLE_PROPERTY_LEVEL`. Checked over AT-SPI before writing it:
GTK 4.18 exports the roles and a `level` attribute (only when set), which
ALUS already reads for tree items and announces when it changes, and the
tree item role is silent on focus. Nothing folds: every heading is a top
level row, indented by its level.

Test: `tests/nav/test_browse_mode.cpp` ("N135 the elements list opens on the
element at the caret"): before every link, on a link, inside a link nested in
a link, between two, after the last, and an empty list. Without the fix three
checks fail. The dialog itself was run with a sample file and read over
AT-SPI: the marked row is focused and selected, headings have `level`.

**N136 — speech was cut off whenever a window opened.** Reported for
Nautilus, the elements list and the settings window: the announcement
stopped a moment after it began, as if Ctrl had been pressed. Log
`alus-measure-20261115-095219.log`, 09:52:40: alus-elements focused its row
at .280 and sent `window:activate` at .284. The focus was handled first
(app switch, "Elements List", "Links tree view", the row queued), and then
the activation, which `Object::handleEvent` answers with `cancelSpeech`
(NVDA's foreground handling) -- the server acknowledged the cancel at .423
and nothing queued was ever spoken. On Windows the foreground event comes
before the focus; GTK 4 sends them the other way round. `handleGainFocus`
already dispatches its own foreground, and its cancel, before the focus is
spoken, so a `window:activate` for a window that already holds the focus
now only records the foreground and goes no further
(`Registry`, the kForeground branch). An activation with no focus yet
(Chrome, N130) is unchanged.

Test: `tests/a11y/test_events.cpp` ("N136 window:activate after the
window's focus does not cut its announcement"): no cancel and nothing more
said after the focus; an activation that comes first still cancels. Without
the fix the first section fails.

**N137 — the elements list asks Chrome a quarter less.** Measured with the new
`measure-focus elements` mode against a local page of 300 links, 60 headings,
240 buttons and a few forms, in a separate Chrome profile: alus+f7 made 5156
AT-SPI calls in 318 ms, all but nothing of it waiting for Chrome (about 60 µs
a call). Two things were waste:

- the N134 nesting check walked up through `Object::parent()`, which makes an
  Object of each ancestor and so reads its role, states and attributes: 634
  calls of each for 300 links. It now walks raw references and compares
  paths, which cost no call, with each parent asked once and remembered for
  the whole list (`BrowseMode::ParentMemo`). The raw walk without the memo
  was worse (1199 parent calls), because siblings no longer shared their
  list item and list;
- `QuickNav::all` checked `isAlive()` on every match, which reads the state
  set the object had just read; it now looks for DEFUNCT in those states.
  Doing the same inside `Object::isAlive` broke T4.9: states cached when an
  event arrives are stale once the object dies before the pump, so the
  general check stays fresh.

Now 3816 calls, 240 ms (the dialog's own run is a little lower, as one memo
serves all five types). The rest is four calls an entry -- role, states,
attributes, name -- that every new Object reads; fetching them in bulk is a
larger change, left for when a page shows the need. A focus move inside the
page (`measure-focus webfocus`) costs 270 calls, 15 ms: not worth touching.
An optimised build gains only 18% (196 ms), because the time is Chrome's.

YouTube could not be measured: the clean profile stops at the cookie consent
page, and accepting it on the user's behalf was not done.

Test: `tests/nav/test_browse_mode.cpp` ("N137 the elements list's nesting
check makes no Object of an ancestor"): ten links in list items read
attributes ten times, and parents at most 21 times. Without the fix: 21
attributes reads.

**N138 — the release build did not compile.** `docs/installing.md` says
`--buildtype=release`, and GCC 14 at -O2/-O3 warns "may be used
uninitialized" for two copied `std::optional<int>` (`braille/properties.cpp`,
`a11y/text_speech.cpp`) and "array bounds" for copying a one-element vector
in `test_louis.cpp`; `-Werror` made each a failure. All three are false
positives, rewritten so the compiler can follow them (an int with 0 for none,
a flag and an int, a vector built rather than copied). The debug build, which
is what `build/` and `alus-switch.sh` use, never showed them. Checked:
debugoptimized and release both build with -Werror and pass every test.

**N139 — a nameless GTK row takes its name from its labels.** The plan said
GTK 4 applications probably have list items with no name (seen in the
elements list's own rows, N133). Measured first, on the live desktop: every
focusable object without a name in Files, Settings, Text Editor, System
Monitor and Calculator was listed. Not one list item, tree item or row was
nameless -- only containers (scroll panes, lists) and the main text areas,
which is right. GNOME's own applications name their rows; the problem is an
application that fills a plain `GtkListBoxRow` with labels, as ALUS's dialog
did.

For those, `NamelessItemBehavior` (ALUS's own, `alus.behaviors.NamelessItem`,
attached to list items, tree items and table rows): when the item has no
name and no LABELLED_BY, and the application's toolkit is GTK, the name is
the visible labels inside it, four levels down at most, eight labels at
most, a repeated label once. Not for the web: a page's list item has no
name either, but its text is read as text, and naming it would say it
twice. Checked live with a GTK 4 list box of unnamed rows: `alus-cli
a11y-tree` reads them as "Wi-Fi Connected" and "Bluetooth Off".

Test: `tests/a11y/test_behaviors.cpp` ("N139 a nameless GTK list item is
named from the labels inside it"): labels in order, once each, icons and
hidden labels left out; its own name wins; a Chromium list item stays
nameless. Without the behaviour the first section fails.

**N140 — the formatting settings, as asked for earlier.** The request
(phase 10, Writer): no "paragraph" style spoken all the time, and a switch
per item -- paragraph style, font, alignment -- for what is spoken. The
Document formatting page already had almost all of it, with the style name
off by default. One setting the speech reads, `reportParagraphIndentation`,
had no row, so the user could not reach it; it now has one ("Paragraph
indentation", Serbian in `po/overrides.po`). `config::Manager::defaultKeys`
lists a section's registered keys, and a test now fails for any
documentFormatting setting without a row.

Test: `tests/gui/test_model.cpp` ("N140 every document formatting setting
has a row"). Without the row it names `reportParagraphIndentation`.

**N141 — forms on the web: a password field and a select.** A local page of
common form and table patterns, walked live in Chrome with `alus-cli
browse-walk` (which now takes a line count and runs `f` and `t` too):

- `<label>Password <input type=password></label>` was read as "Password",
  by line and by `f`; the field was never said. NVDA's buffers give a
  password field EDITABLETEXT plus PROTECTED, so every editable rule ported
  from NVDA tests EDITABLETEXT; ALUS keeps AT-SPI's PASSWORDEDIT role (it
  says "password edit"), which fell outside all of them and was layout. The
  presentation category, content-first and editable-text checks now include
  it, and "protected", which ALUS adds to every password field for the
  typing echo, is not said after "password edit". Now: "Password, password
  edit, invalid entry".
- A collapsed `<select>`: Chrome hangs the options under the box as a hidden
  menu, and the arrows read each option as a line ("combo box collapsed menu
  item Serbia", then "... Croatia"). A combo box is now a leaf, as in NVDA's
  buffers, and its value comes from the selection of its popup when the box
  has none (Chrome). Now one line: "combo box collapsed Serbia Country".

Seen and left as it is: a label and its field on one visual line are two
lines (ALUS's lines are leaf-based); `f` reads the whole line, so a field
with a visible label says the label and then its name ("Clear Clear
button", "Message edit Message"); NVDA speaks only the field. Changing what
quick navigation reads affects every element type, so it waits for a
decision. Tables read correctly: caption, size, headers, coordinates, a
spanned cell ("column 1 through 2"), and a layout table is not a table.

Tests: `tests/nav/test_browse_mode.cpp` ("N141 a password field inside its
label is read as a password field", "N141 a collapsed select is one line
with its chosen option"). Both fail without the fix.

**N142 — the screen layout (ALUS+V), and a jump reads the element.** Asked
for after N141: make it as NVDA has it. Two things NVDA does and ALUS did
not:

- *What a line is.* ALUS's lines were leaf-based: every leaf at least one
  line, so `<p><label>User name</label> <input></p>` was two lines, while a
  link inside a sentence stayed in it. NVDA's `useScreenLayout`
  (virtualBuffers, on by default) decides this, and ALUS+V toggles it. ALUS
  had the key answering "not in this version" (phase-10.md, the four kinds
  of missing command) because there was no layout to switch. Now
  `DocumentTextRange::lineAt` is the one definition of a line: on, a leaf's
  line goes on into its neighbours while no block box separates them --
  CSS `display`, which Chrome and Firefox both send as an attribute
  (measured: label inline, input inline-block, p block, li list-item, text
  nodes none, which counts as inline) -- capped at 16 leaves and 240
  characters, and stopping after a `<br>` (a text node holding "\n"). Off, a
  leaf's line is cut at every U+FFFC, so a link or a field is a line of its
  own, and a `<br>` still belongs to the line it closes. `move(Line)` is made
  of `lineAt`, so the arrows and the reading agree. A toolkit that sends no
  `display` keeps the old leaf-per-line layout.
- *What a jump reads.* NVDA's `TextInfoQuickNavItem.report` speaks the
  element's own range, and one line of it for a table, list, edit, frame,
  not-link block, landmark or clickable. ALUS read the line the element
  starts on, so `f` on a labelled field said the label and then the name,
  and `b` on the second of two buttons read both. `BrowseMode::moveTo` takes
  the element type now and reads `elementRange`; the elements list's Move to
  does the same.

Found on the way, live: the label in `<p><label>Message</label><br>
<textarea></p>` was read as "blank" -- the paragraph is one leaf ("U+FFFC \n
U+FFFC"), the label's text travels as the `content` of its field, and a
label's field is layout, which said nothing. A layout field with content now
says the content. And a field labelled by text on the page no longer gets its
name as its content as well ("User name edit required User name"); a field
named only by aria-label keeps it, since nothing on screen says it. A button
whose explicit name is its own text says it once ("Clear Clear button").

The setting is `[virtualBuffers] useScreenLayout`, NVDA's own section and
key, with a row in the settings window (Review cursor, Document navigation).
The user asked that the key's choice stay as it was left: ALUS has no save on
exit, so `nav::Service::toggleScreenLayout` saves the configuration at once.
Another open document takes the new value when it gets the focus back.
`alus-cli browse-walk` now loads the user's configuration (it printed the
defaults), and `ALUS_WALK_POSITIONS=1` prints where each line starts and
ends. The browse mode tests got their own `ALUS_CONFIG_DIR`, as ALUS+V saves.

Live, Chrome, a form page: on -- "User name edit required", "Sign in button
Clear button", "Read the link terms and the link privacy policy before you
go."; off -- "User name" / "edit required", "Sign in button" / "Clear
button", "Read the" / "link terms" / ...

Tests: `tests/nav/test_browse_mode.cpp`: "N142 the screen layout decides
what a line is" (on and off over the same page), "N142 a quick-nav jump
reads the element, not its line", "N142 alus+v switches the screen layout
and keeps the choice" (a fresh configuration reads it back).

**N143 — ALUS+V lost the focus, and where the screen layout is heard.**
First live test of N142 (log `alus-measure-20261115-132727.log`): "it
sounds the same on and off, and ALUS+V throws the focus somewhere until I
press Tab".

- *The focus.* 13:27:44.975 the toggle saved the configuration (as asked,
  so the choice survives a restart); 13:27:45.041 the daemon's file monitor
  saw the file change and did what it does for the settings window or a
  hand edit: `resetConfiguration`, every subsystem terminated and started
  again. Browse mode, the caret and the focus went with it, and the arrows
  said nothing until Tab gave the browser a focus event. `config::Manager`
  now keeps the text of its last save, and the monitor skips a change when
  the file is exactly that (`Manager::isOwnWrite`). The settings window is
  another process with another manager, so its saves still reload.
- *Same on and off.* On the part of YouTube the log covers (the banner,
  "Skip navigation", the search form), every element is a block box of its
  own, so it is one line either way -- as in NVDA, whose rule is the same
  (inline boxes share a line). The difference is where links sit inside
  text. Measured on en.wikipedia.org/wiki/Screen_reader with `browse-walk`:
  on, "This article needs link more citations Please help link improve this
  article by link adding citations ..." is one line; off, every link is a
  line. Two defects showed there and are fixed: a link that wraps on screen
  is one U+FFFC that Chrome puts on both lines, so with the layout on it was
  read again at the start of the next line (`leafLineAt` gives it to the
  line before); and with it off, the " · " between "news · books" was a
  line that said nothing (separators -- spaces, no-break spaces, middle
  dot, bullet, dashes, ASCII punctuation -- go with the link before).

Tests: `tests/config/test_config.cpp` ("N143 a save of ALUS's own is told
from a change made elsewhere"), `tests/nav/test_browse_mode.cpp` ("N143
with the screen layout off, the separator between links is not a line").

**N144 — Thunderbird: say all read nothing, and Escape did not close the
message.** Log `alus-measure-20261115-134011.log`.

- *Say all.* 13:40:34, ALUS+Down Arrow in an open message: three
  `lineReached` marks and no text, then the end. Say-all took its range from
  the FOCUS OBJECT (`Object::makeTextRange`), never from browse mode, so in a
  document it read the document object's own text -- in Gecko one U+FFFC per
  child. NVDA's `script_sayAll` reads the tree interceptor's caret when there
  is one and it is not passing keys through; ALUS now does the same
  (`SayAll::documentForFocus`), browse mode's caret follows the speech, and
  the end of the document is the end (no hop to a next object). Two defects
  in `DocumentTextRange::move` for an END point showed at once: it was not
  allowed onto a leaf's exclusive end, so the last paragraph of a document
  was never read, and crossing into the next leaf stopped on its start, so
  every crossing was an empty chunk whose caret landed in the leaf before.
  Chrome documents had the same say-all defect; the user had not tried it
  there.
- *Escape.* 13:41:03: Escape ran browse mode's `disablePassThrough` in
  browse mode, which does nothing outside focus mode -- and the key, grabbed,
  never reached Thunderbird. NVDA sends it on (`gesture.send()`); ALUS cannot
  hand a grabbed key on (N18), so outside focus mode `alternativeScript` now
  answers nothing for it and the key is not grabbed. And the script lacked
  NVDA's `ignoreTreeInterceptorPassThrough`, so in focus mode, the one place
  it is for, the lookup dropped it: Escape never left focus mode either. Both
  fixed; the command coverage test now also resolves keys in focus mode.

Tests: `tests/nav/test_browse_mode.cpp` ("N144 say all in browse mode reads
the document from browse mode's caret", "N144 Escape is the application's
in browse mode and ALUS's in focus mode").

**N145 — the commands that said "not in this version".** Seventeen were
left. Each was looked at again against what the platform allows:

- *The clipboard* (`reportClipboardText` ALUS+C, `copyLastSpokenInformation`
  ALUS+Control+X, `log_markStartThenCopy`). Phase 9's O10 said Wayland gives
  the clipboard only to the focused client, and ALUS has no window: measured,
  a GTK client on Wayland reads "empty clipboard". But GNOME bridges the
  clipboard to Xwayland, and an X11 client may own and read the CLIPBOARD
  selection with no focused window (measured: set by one X11 process, read
  by another, 20 characters back). `alus-clipboard` (src/gui/clipboard_main.cpp)
  forces GDK's X11 backend; `--get` prints the text, `--set` reads it from
  stdin and stays alive owning the selection until somebody else copies
  (an X11 selection dies with its owner). The daemon spawns it without
  waiting (`core/clipboard.cpp`), as it does `alus-elements`. The messages
  are NVDA's ("Copied to clipboard: …", "There is no text on the clipboard",
  the count above 1024 characters). Whether a Wayland application pastes
  what ALUS copied is the one step only a live test can show.
- *The mouse* (`leftMouseClick`, `rightMouseClick`). ALUS can neither move
  the pointer nor inject a click (N18), but the click an NVDA user makes
  after routing the mouse to the navigator object is the object's own
  action: left click runs its click/press/activate/jump action (or focuses
  it), right click its show-menu action, and says to use Shift+F10 or the
  menu key when there is none.
- *`refreshBuffer`* (ALUS+F5). No buffer, but ALUS caches names, states and
  text per object; the key now drops the caches and reads the line again,
  saying NVDA's "Refreshed".
- *The rest* cannot be done here and now say why instead of "not in this
  version" (`script::bindAnnouncements`, a table of translated reasons):
  moving or holding the pointer, knowing where it is, a Python console,
  ducking, sound split, reloading add-ons (restart ALUS), the display model,
  native selection mode.

Tests: `tests/a11y/test_commands.cpp` ("N145 the mouse keys click the
navigator object"), `tests/coverage/test_command_coverage.cpp` ("N145 a
command ALUS does not have says why"). The helper itself was run live:
`--set` then `--get` returned the text.

**Version 2026.1.0 beta.** The first stable release is numbered by year:
year.release.patch (meson.build, README, the catalogues' headers, the add-on
example). Add-ons compare versions numerically, so one asking for at least
0.1.0 still loads; the test add-on that asks for a newer ALUS now asks for
9999.0.0. `tools/alus-switch.sh alus nolog` runs ALUS for everyday use with
no log at all (`--log-level off`, file /dev/null).

**N146 — lines as NVDA makes them.** Reported after N143: "on Wikipedia
nothing is right; with the screen layout off it still reads 'Ljubo Miloš
Wikipedia https://…' as one line". That is a search result: a link holding
the title (h3), the site name and the address. Reproduced with a page of the
same structure, and on sh.wikipedia.org with `browse-walk`. Three defects:

- *The screen's wrapping.* A line inside a leaf was the toolkit's LINE
  boundary, i.e. where the window wraps the text. A visually hidden skip
  link one pixel wide read "Prijeđi" / "na" / "sadržaj", one word a line,
  and a paragraph was as long as the window was wide. NVDA's buffer never
  looks at the screen: a line ends at a line break or a block, and a longer
  one is cut at the last space within 100 characters
  (`virtualBuffers.maxLineLength`). `leafLineAt` now does exactly that, and
  the screen layout joins leaves only up to the same 100. (This also retires
  N143's wrapped-link fix: there is no wrapping left to repeat a link.)
- *Blocks inside a leaf.* Chrome makes such a link one leaf with the title
  and the address as embedded objects, so the line went straight through
  them. A line is now always cut at an embedded object whose `display` is a
  block, with the screen layout on too (`segmentAt`); off, at every embedded
  object, as before. The screen layout does not join across such a cut.
- *Empty boxes.* Wikipedia puts an empty element in every tab link, and a
  cut at it was a line that said only "link". An empty embedded object
  (`DocumentLeaves::isBlankLeaf`) cuts nothing and counts as a separator.

Tests: `tests/nav/test_browse_mode.cpp` ("N146 a line is NVDA's, not the
screen's wrapping"), on and off.

**N147 — YouTube: the volume went down by itself, and Enter played and
paused at once.** Log `alus-measure-20261117-142209.log`. ALUS never sets
a volume. What happened is in the key events:

- 14:22:36, the video link activated: the focus went to "YouTube video
  player" while the page was still BUSY, and the grab set was computed
  then -- 210 keys, without browse mode, because the key lookup only takes
  the tree interceptor of a focus that is ready. When the page finished
  loading, `handleInterceptorEvent` marked the EVENT's object ready and not
  the focus, and the grabs were never recomputed. From 14:22:39 to 14:23:01
  every down arrow arrived ungrabbed: the player got it (volume -5% a press;
  the slider then read 0) while browse mode, now ready, ALSO ran
  `moveByLine_forward` on it. Now the focus is marked with its document's
  readiness and the grabs are refreshed when that changes.
- 14:24:05, Enter on the play button: ungrabbed again (focus on the button,
  stale grabs), so the page played, and browse mode's `activatePosition`
  ran too and paused. As a rule, a backend that knows which keys it grabbed
  (`IInputBackend::reportsGrabs`, the a11y-manager backend) now never runs a
  script for a plain key that reached the application -- only a script that
  merely watches its key (`needsKeyPassThrough`) does -- and the grab set
  is recomputed, since it was stale.

Tests: `tests/input/test_manager.cpp` ("N147 a key that reached the
application ungrabbed does not also run its script"),
`tests/nav/test_browse_mode.cpp` ("N147 the focus becomes ready with its
document, and the keys follow it"); the second fails without the fix.
