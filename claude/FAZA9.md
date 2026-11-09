# Faza 9 — GUI i lokalizacija

Izveštaj o izvršenoj fazi. Status svakog izlaznog kriterijuma, odstupanja od
plana sa razlogom, i nalazi koje je faza proizvela.

**Jedna komanda:**

```bash
meson test -C build --suite gui
```

Rezultat: **8474 asertacije, 22 test slučaja, zeleno.** Pod valgrind-om:
**13/13 svita, 0 grešaka, 0 izgubljenih bajtova.**

Faza je proizvela **jedan nalaz koji je promenio arhitekturu**: prozor
podešavanja **ne može da stoji u procesu čitača ekrana**. U demonu se gradio za
83 ms, prikazivao za 122 ms, a onda je jezgro stajalo **7,5 sekundi** i prozor
nije mogao da se pročita ni jednom svojstvo. Uzrok je u logu, bez nagađanja
(**N79**):

```
AT-SPI get_role failed: Did not receive a reply.
core stuck for 516 ms, recovery started
```

ALUS čita radnu površinu **sinhronim** AT-SPI pozivima iz glavne petlje. Poziv
o nekom objektu odgovara se iz procesa koji taj objekat poseduje. Kad je
objekat ALUS-ov sopstveni prozor, to je **ista petlja** — blokirana unutar
poziva koji čeka samu sebe. Zato je svako čitanje svojstva čekalo pun rok
poziva od 500 ms i vraćalo ništa; 516 ms nije slučajnost, to je taj rok.

Sa klijentske strane to se ne može popraviti: odgovor ne može da stigne dok
petlja ne radi, a petlja ne radi dok odgovor ne stigne. Lek je da prozor
pripada **drugom procesu** — `alus-settings`. ALUS ga tada čita isto kao GNOME
Settings, mašinerijom koju su faze 4–8 već dokazale na stvarnim aplikacijama.

Izmereno posle izmene, na istoj mašini:

```
$ bash tools/phase9-gui-check.sh
==> 6. THE FINDING: does opening the window freeze the screen reader
    ok    the core did not stall while the settings window opened
    ok    no AT-SPI call about the window timed out
    ok    ALUS sees the settings window as an application of its own
```

## Status izlaznih kriterijuma

| Kriterijum | Status | Gde se meri |
|---|---|---|
| `meson test -C build --suite gui` | **prošao** | 3 testa: `gui` (**8474 asertacije**, 22 slučaja), `settings-smoke`, `settings-bad-page` (očekivani pad) |
| `meson compile -C build alus-pot` | **prošao** | meson `i18n` modul; `po/alus.pot`, **1102 msgid-a** |
| `msgfmt --check po/sr.po` | **prošao** | svita `external`; isto proveravano i u C++-u (vidi O6) |
| T9.1 — `alus+control+g` i ostali prečici otvore odgovarajuća podešavanja | **prošao** | `[T9.1]` × 2 u `test_model.cpp`; tabela skript → stranica, plus da prečica bez stranice **kaže** da je nema |
| T9.2 — dijalog gestova upiše u `~/.config/alus/gestures.ini` | **prošao** | `[T9.2]` × 3 u `test_gestures.cpp`: dodavanje, brisanje svog, zaklanjanje ugrađenog — poslednje **provereno ponovnim učitavanjem** |
| T9.3 — skript bez opisa se NE pojavi u dijalogu | **prošao** | `[T9.3]`; nad celom generisanom tabelom: svih **74** skripta bez opisa je odsutno |
| T9.4 — `LANG=sr_RS.UTF-8`: sve najave na srpskom | **prošao** | `[T9.4]` × 4: 0 neprevedenih od 1105, **i na pravom pismu** (dodato posle N106: 76 reči je bilo na latinici u ćiriličnom katalogu), čuvari mesta se poklapaju, ime drugog proizvoda se ne pojavljuje; živo u koraku 3 skripta |
| T9.5 — generisana referenca komandi se poklapa sa `data/scripts.csv` | **prošao** | `[T9.5]` × 2 u `test_reference.cpp`; **316** dokumentovanih komandi u 17 kategorija, od toga **46** označenih da ih u ovoj verziji nema |
| T9.6 — svaka prečica iz ugrađene mape nešto **radi** | **prošao** | svita `commands`; `[T9.6]` × 3 u `tests/coverage/test_command_coverage.cpp` nad sastavljenim demonskim stekom: **315** prečica — 238 implementirano, 64 najavljuje da ih nema, 13 namerno nevezano (N18). Dodat posle N99; uhvatio N101–N103 |
| `meson install --destdir /tmp/stage` | **prošao** | `.desktop` × 2 (oba prolaze `desktop-file-validate`), systemd unit, `sr` i `sr@latin` katalozi |
| Ručni test — šta korisnik **čuje** | **nije izvršen** | `tools/phase9-gui-check.sh --manual`; traži sesiju bez druge čitačke i slušanje |

Ručni test je jedino što nije izvršeno, i razlog je isti kao u fazama 3 i 7:
mašina ne može da izmeri šta korisnik čuje, a za ulaz tastature treba zaustaviti
Orcu koja na ovoj mašini drži `org.freedesktop.a11y.KeyboardMonitor` (N8).
**Automatski deo je izvršen u celosti**, uključujući živu proveru da otvaranje
prozora ne zamrzava jezgro — što je nalaz zbog kog ta provera postoji.

## Šta je izgrađeno

| Modul | Sadržaj |
|---|---|
| `alus::i18n` | gettext domen, `_()`/`N_()`/`C_()`/`P_()`, `substitute()`, restartabilan podsistem |
| `alus::i18n::data_strings` | N_() oznake za stringove koji žive u **generisanim podacima** (opisi komandi, kategorije, labele tastera) |
| `alus::gui::model` | 8 stranica, 40 grupa, **90 podešavanja**, svako sa svojom sekcijom i ključem — bez ijedne GTK linije |
| `alus::gui::gesture_model` | `getAllGestureMappings`, pet slojeva, dodavanje i dva načina uklanjanja |
| `alus::gui::Service` | prečice (T9.1), pokretanje prozora, i **govorno** prijavljivanje zašto ne može |
| `alus::gui::Window` + `gesture_dialog` | GTK4 + libadwaita; `AdwPreferencesDialog`, `AdwAlertDialog`, `GtkListView` |
| `alus-settings` | prozor kao **sopstveni proces** (N79), sa jednom instancom preko `org.alus.Settings` |
| `po/` | `alus.pot` (1102), `po/sr.po` i `po/sr@latin.po` (po **1102/1102 prevedeno**), `po/overrides.po` |
| `tools/import-nvda-po.py` | prevodi iz NVDA po msgid-u, sa transliteracijom u ćirilicu |
| `tools/update-potfiles.py` | `po/POTFILES.in` se **generiše** i proverava u testu |
| `tools/generate-command-reference.py` | `docs/commands.md` i `docs/commands.csv` iz `data/scripts.csv` |
| `data/desktop/` | autostart `.desktop`, pokretač podešavanja, systemd user unit |
| `alus-cli gui-info` / `settings` / `autostart` | dijagnostika: može li prozor, koji je jezik, šta se diže sa sesijom |

Srpski prevod nije pisan od nule. **738 od 1102 msgid-a je uzeto iz NVDA-inog
sopstvenog srpskog kataloga po msgid-u** (`tools/import-nvda-po.py`), jer
`00-VIZIJA.md` §7 to i zahteva: korisnik čija dokumentacija kaže „режим
прегледа" mora da čuje „режим прегледа". Preostalih **364** je napisano rukom u
`po/overrides.po`. Oba kataloga su **100% prevedena**.

## Odstupanja od plana

### O1 — `LC_NUMERIC` se vraća na `"C"` posle `setlocale(LC_ALL, "")`

Plan kaže „gettext infrastruktura" i ništa više. `setlocale(LC_ALL, "")` je
obavezan da bi katalog uopšte bio konsultovan, ali menja i `LC_NUMERIC` — a
ALUS upisuje `double` u `GKeyFile` (brzina, visina, `beepMinHz`). U srpskom
lokalu `printf("%f")` daje `1,500000`, i konfiguracija sa „1,5" se ne čita
natrag. `LC_NUMERIC` se zato odmah posle vraća na `"C"`, namerno
(`src/i18n/gettext.cpp`).

### O2 — `bind_textdomain_codeset(domain, "UTF-8")` je obavezan, ne opcion

Bez njega gettext prekodira katalog u skup znakova lokala. Korisnik čiji je
lokal `sr_RS` **bez** `.UTF-8` dobio bi mojibake na brajevom redu i u svakoj
GTK labeli. Jedna linija, i bez nje je ceo prevod neupotrebljiv na delu
sistema.

### O3 — ALUS koristi **samo** `%s`, dok NVDA koristi i `{ime}`

NVDA ima 299 msgid-a sa `{imenovanim}` čuvarima mesta. `i18n::substitute` je
pozicioni i prihvata jedan ili dva `%s`. Posledica: tih msgid-a se prevod **ne
nasleđuje** i piše se rukom u `po/overrides.po`. Mereno: od 3678 NVDA msgid-a,
299 ima `{ime}` a 3305 nema nijedan čuvar mesta, pa je nasleđivanje punih 90%
teksta sačuvano a konvencija ostala jedna. Pisanje dva sistema čuvara mesta u
`substitute` dobilo bi najviše onih msgid-a koje ALUS stvarno koristi, a to je
mali podskup od 299.

### O4 — `sr` je **ćirilica**, `sr@latin` je latinica

NVDA-in `sr` je latinica (`Language: sr_CS`, „Serbian (Latin)"). ALUS-ov `sr`
je ćirilica, i to nije ukus nego doslednost sa ostatkom projekta: Faza 2 je
napisala `data/symbols/sr.dic` ćirilicom, a Faza 7 je T7.1 merila na
`sr-Cyrl.ctb`. Latinski katalog sa ćiriličnim rečnikom simbola i ćiriličnom
brajevom tabelom bio bi tri pisma u jednom čitaču ekrana. Oba se isporučuju;
ćirilični se dobija **transliteracijom**, koja je za srpski striktna i
reverzibilna, i koja ne dira čuvare mesta, imena proizvoda i brajeve skraćenice.

### O5 — jedan `po/overrides.po`, pisan **latinicom**

Stringovi koje NVDA nema prevode se jednom, latinicom, i isti uvoznik ih
transliterira u ćirilični katalog. Alternativa je bila dva fajla za ručni
prevod, što znači 364 stringa pisana dvaput i dva mesta na kojima se mogu
raziđi.

### O6 — `msgfmt --check` nije u sviti `gui`, nego u sviti `external`

Plan navodi `msgfmt --check po/sr.po` kao izlazni kriterijum. On jeste
izvršen, ali u odvojenoj sviti: pod valgrind postavkom Memcheck prijavi
curenja u **samom GNU gettext-u** (455 bajtova u `string_list_concat`, i još),
pa bi `msgfmt` obarao valgrind prolaz. Nije ALUS-ovo da popravlja, a ni da
potiskuje po okviru — okviri su u tuđoj deljenoj biblioteci. `exclude_suites`
ne pomaže: eksplicitan `--suite` na komandnoj liniji ga nadjačava (**N82**).
Zato je isto proveravano **i u C++-u**, nad istim fajlovima
(`tests/gui/test_i18n.cpp`): svaki unos preveden, i svaki `msgstr` nosi iste
čuvare mesta kao njegov `msgid`. `--suite gui` je time potpun sam za sebe, a
`--suite external` je provera pravim alatom.

### O7 — prozor podešavanja je **sopstveni proces**, ne deo demona

Najveće odstupanje faze, i jedino izmereno a ne izabrano. Vidi **N79** i
`src/gui/main.cpp`. Plan to ne propisuje eksplicitno, ali `01-ARHITEKTURA.md`
§2 mapira `gui/` (wxPython) na `alus::gui`, a NVDA svoj GUI drži u procesu —
pa je ovo odstupanje od obrasca koji ceo projekat prenosi.

Šta to **nije** pokvarilo: dijalog gestova i dalje ima svoje podatke, jer su
četiri od pet slojeva gestova **fajlovi** (korisnički, jezički, drajverski,
ugrađeni) i katalog komandi je fajl. Peti sloj (šta vezuju app moduli i
plugin-i) je živ u demonu i ovde je prazan; dijalog to prikazuje.

Šta je koštalo: podešavanje izmenjeno u prozoru stiže u demon preko
`~/.config/alus/alus.ini` i `GFileMonitor`-a, pa je to **pun**
`resetConfiguration` a ne usko ponovno čitanje jedne sekcije. Iz fajla se ne
može znati koja se sekcija promenila, a nagađanje bi ostavilo podsistem sa
zastarelim podešavanjem.

### O8 — uska `applyConfiguration()` je postala javna na pet servisa

`speech`, `input`, `a11y`, `nav` i `braille` imali su privatni
`applyConfiguration()`. Sada je javan, i `speech::Service` ga je dobio (nije ga
imao). Razlog je bio prozor u procesu; posle O7 koristi ga samo
`resetConfiguration` posredno, ali ostaje javan jer je to tačan ugovor „ponovo
pročitaj svoju sekciju" i jer je `02-FAZE.md` pravilo 3 pročitano usko:
restartabilno u mestu, i to u najmanjem mestu koje će poslužiti.

### O9 — libadwaita 1.6 je deprecirala oblike koje plan pretpostavlja

`AdwPreferencesWindow` i `AdwMessageDialog` su deprecirani, a projekat se gradi
sa `-Werror`. Koriste se `AdwPreferencesDialog` i `AdwAlertDialog`. Posledica
koja nije očigledna: `AdwDialog` **nije** prozor — postaje samostalan prozor
samo kad se prikaže sa `adw_dialog_present(dialog, nullptr)`, a kad se zatvori
GTK ga uništi, pa se pokazivač mora obrisati u `closed` signalu ili drugo
otvaranje prikazuje oslobođenu memoriju (**N81**).

### O10 — GTK je u **jednom** izvršnom fajlu, ne u biblioteci

`libalus_gui` (model) se gradi bez GTK-a i linkuje ga demon;
`libalus_gui_widgets` ga ima i linkuje ga samo `alus-settings`. Nije stilska
odluka: dok je demon linkovao GTK, atk most ga je registrovao na magistrali
pristupačnosti kao GTK aplikaciju bez keš objekta, pa je svaki obilazak drveta
davao `GetItems ... /org/a11y/atspi/cache` upozorenje (**N80**).

### O11 — gest se u dijalogu **upisuje**, ne pritiska

NVDA stavlja dijalog u režim u kom se sledeći pritisak hvata umesto da se
izvrši. Na Waylandu to aplikaciji nije dostupno: ALUS vidi samo tastere za koje
mu je kompozitor već dao grab, a taster bez graba nikad ne stigne (N1, N18).
Zato se gest upisuje, onako kako se piše u `gestures.ini`, i proverava se protiv
tabela tastera pri unosu — `kb:alus+shift+t` se prihvata, `kb:alus+shiftt` se
odbija sa razlogom (**N83**). Hvatanje „pritisnite taster sada" moguće je samo
za tastere koje ALUS već grabuje, što je upravo skup koji korisnik ne treba da
vezuje.

### O12 — sedam stranica umesto petnaest panela, i prečice bez stranice **kažu** to

Generisana tabela komandi ima po jedan skript za svaki NVDA panel podešavanja.
ALUS ima 8 stranica. Prečice čiji panel ALUS **nema** (`alus+control+m` za miša,
`alus+control+u` za zvuk, `alus+control+p` za profile) mapirane su na **praznu**
stranicu i izgovaraju „у овој верзији нема таквих подешавања". Namerno, a ne kao
praznina koju treba zapuniti najbližom stranicom: `alus+control+m` koji otvori
podešavanja tastature zato što nema podešavanja miša bio bi gori od toga da
kaže da ih nema (§3.8). Test to proverava: nijedna prečica ne sme da otvori
pogrešnu stranicu, i nijedna ne sme da imenuje stranicu koja ne postoji.

### O13 — `--no-gui` je dodat demonu

Nije u planu. Potreban je zato što `daemon-smoke` test i sesija bez ekrana ne
smeju da pokrenu prozor, a `openPage` fork-uje proces.

### O14 — `alus-cli autostart` **ne** onemogućava drugu čitačku tiho

`03-RIZICI.md` R12 je eksplicitan da se to **ponudi**, ne uradi. `--enable`
ispiše šta treba pokrenuti i šta će to uraditi, i ne radi to sam. `meson
install` ne uključuje autostart uopšte: `.desktop` ide u
`$datadir/applications`, a ne u korisnikov `~/.config/autostart`.

### O15 — izmenjena je **jedna** linija nasleđenog `data/characters/sr.dic`

`x` je imao ćirilični mnemonik („икс") među latiničnim. Za govor je nebitno —
srpski glas čita oba pisma isto — ali `sr-Cyrl.ctb` ne, pa bi na brajevom redu
pod latinskim tasterom izašle pogrešne ćelije. Promenjeno u „Iks". Fajl inače
pokriva **oba pisma**, svako svojim mnemonicima, i to je bilo tačno od Faze 2.

## Nalazi

### N75 — najave su se gradile spajanjem fragmenata rečenice

Prvi prolaz prevoda je ovo otkrio, i to na petnaestak mesta:

```cpp
return _("with ") + rowCountText(rowCount) + " and " + columnCountText(columnCount);
append(sequence, std::to_string(indexInGroup) + _(" of ") + std::to_string(similarItems));
presenter.speakMessage(std::to_string(percent) + _(" percent"));
service_.message((forwards ? _("no next ") : "no previous ") + std::string(label));
```

Prevodilac koji dobije `"with "`, `" and "` i `" of "` kao odvojene stringove
**ne može** da napiše ispravnu rečenicu ni na jednom jeziku koji menja red reči
ili pada. Srpski traži da se padež imenice slaže sa predlogom, a to je moguće
samo ako su predlog i imenica u **jednom** stringu.

Pretvoreno u uzorke sa `%s`: `_("with %s and %s")`, `_("%s of %s")`,
`_("%s percent")`, `_("no next %s")`. `i18n::substitute` sa dva čuvara mesta
nalazi **oba u uzorku pre** nego što ubaci bilo šta — ime objekta je korisnički
podatak i može da sadrži `%s`, pa bi drugi prolaz ubacivao u vrednost.

Jedan slučaj se nije dao rešiti uzorkom: `no next %s` se puni imenom tipa
elementa, a to je imenica bilo kog roda („наслов" muški, „веза" ženski), pa se
nijedan pridev ne može složiti. Prevod je „нема следећег елемента: %s", što je
gramatično za svaki `%s`.

### N76 — množina se odlučivala u C++-u, a srpski ima tri forme

```cpp
return count == 1 ? formatOne(_("%s row"), count) : formatOne("%s rows", count);
return count == 1 ? _("with 1 item") : "with " + std::to_string(count) + " items";
```

Engleski ima dve forme, srpski tri: **1 ред, 2 реда, 5 редова**. Birati formu u
C++-u znači da je svaki slovenski prevod pogrešan na 2, 3 i 4 — i da prevodilac
to **ne može da ispravi**, jer do kataloga ne dolazi broj. Pretvoreno u `P_()`
(`ngettext`), a `po/overrides.po` nosi sve tri forme. Uzgred: druga grana u oba
primera **nije bila ni obeležena** za prevod, pa je i na engleskom bila jedini
string koji prevodilac nikad ne vidi.

### N77 — isti `msgid` sa dva značenja ne može biti jedan unos

„Speech" je i naslov stranice („Govor") i izbor kod traka napretka
(„Govorom", instrumental). `msgfmt` to vidi kao fatalnu grešku udvostručene
odrednice samo kad su oba u istom fajlu. Razdvojeno izmenom engleskog teksta
(`Speech only`) a ne gettext kontekstom, jer je razdvojeniji tekst jasniji i na
engleskom. Isto za „Tables" (brajeve tabele prevoda nasuprot tabelama u
dokumentu).

### N78 — `GestureMap::add` je stvarao tačne duplikate

Dijalog gestova je prvi pozivalac koji dodaje vezivanje koje je korisnik
**upisao**, a ne koje dolazi iz fajla ili iz ekstraktora. Isto vezivanje upisano
dvaput davalo je dva identična unosa: dva identična reda u dijalogu, a `remove`
vadi samo prvi — korisnik ukloni taster, ne čuje promenu, ukloni ga opet i tad
proradi. Ručno uređen `gestures.ini` sa ponovljenom linijom radio je isto i pre
nego što je dijalog postojao; dispečer to nikad nije primetio jer dva puta
nađen isti skript izvrši se jednom. Popravljeno **na izvoru**, u
`GestureMap::add`.

### N79 — prozor u procesu čitača ekrana se ne može pročitati, i zamrzne ga

Nalaz faze. Vidi uvod ovog dokumenta i `src/gui/main.cpp`. Brojevi:

| | u procesu demona | sopstveni proces |
|---|---|---|
| izgradnja prozora | 83 ms | 73 ms |
| prikazivanje | 122 ms | 122 ms |
| **jezgro stalo** | **7484 ms** | **0 ms** |
| `get_role` o prozoru | **istekao rok (500 ms), bez odgovora** | radi |
| ALUS vidi prozor u drvetu | ne | da, kao `APPLICATION "alus-settings"` |

Mera je ugrađena i **proverava se u koraku 6** `tools/phase9-gui-check.sh`, koji
digne demon, otvori prozor i pročita demonov sopstveni watchdog log — jer je to
jedino mesto na kom je zamrzavanje ikad bilo vidljivo.

### N80 — demon koji linkuje GTK se registruje na magistrali pristupačnosti

Dok je `libalus_gui` nosio i widget-e, demon je linkovao GTK, pa ga je atk most
registrovao kao GTK aplikaciju — bez keš objekta, jer demon nema prozor. Svaki
obilazak drveta je davao:

```
dbind-WARNING: AT-SPI: Error in GetItems, sender=:1.48,
               error=Објекат не постоји у путањи „/org/a11y/atspi/cache“
```

Čitač ekrana koji sebe prijavljuje kao aplikaciju koju treba čitati je i
besmislen i bučan. Razdvojeno u dve biblioteke (O10); posle toga `ldd
build/src/alus-daemon | grep -c libgtk` = **0**.

### N81 — `AdwDialog` se uništi kad se zatvori

`AdwPreferencesDialog` nije prozor; samostalan prozor je samo kad se prikaže sa
`adw_dialog_present(dialog, nullptr)`, i GTK ga uništi na zatvaranju. Čuvanje
pokazivača znači da drugo `alus+control+g` prikazuje oslobođenu memoriju — pad
u čitaču ekrana, ne u prozoru podešavanja. Pokazivač se briše u `closed`
signalu i prozor se gradi iznova; 73 ms, jednom, kad je čovek tražio prozor.

### N82 — meson `exclude_suites` nadjačava eksplicitan `--suite`

Postavka `valgrind` isključuje svite `live`, `catch2` i `external`, ali
valgrind prolaz imenuje svite eksplicitno, i tada isključivanje ne važi. Zato
`msgfmt` i `python3` nisu u sviti `gui` (O6), nego samo u `external`, a ono što
proveravaju proverava se i u C++-u nad istim fajlovima.

### N83 — na Waylandu dijalog gestova ne može da uhvati taster

ALUS vidi samo tastere za koje mu je kompozitor dao grab. Taster koji korisnik
želi da **veže** po definiciji nije grab-ovan, pa nikad ne stigne u dijalog.
NVDA-in režim hvatanja zato na Waylandu ne postoji, i gest se upisuje (O11).
Ovo je treći put da isto svojstvo platforme menja dizajn: N18 (ALUS ne može da
ubaci taster), podrazumevano isključen brajev unos, i sad ovo.

### N84 — `_` kao makro u instaliranom zaglavlju bi pokvario add-on-e

`include/alus/alus-plugin.h` je jedino zaglavlje koje ALUS instalira i §12.3
obećava da je samostalan C. Makro `_` u njemu pokvario bi svaki add-on koji
koristi `_` kao identifikator, a to je čest izbor. Makroi su zato u
`include/alus/i18n/i18n.hpp`, koje uključuju **samo** `.cpp` fajlovi — GNOME-ova
konvencija (`gi18n-lib.h` postoji odvojeno od `glib.h` iz istog razloga). Pravilo
čuva test: nijedno javno zaglavlje ne sme da uključi makro zaglavlje, i
`alus-plugin.h` ne sme da definiše `_`.

### N85 — stringovi u generisanim podacima ne stižu do prevodioca

`xgettext` čita izvorne fajlove, a `02-FAZE.md` §5.4 drži tabelu komandi u
`data/scripts.csv` **upravo zato** da ne bude ručno pisan kod. Posledica: 316
opisa komandi, 17 kategorija i 78 labela tastera bili su stringovi koje nijedan
prevodilac ne vidi, bez ikakve greške igde — build prolazi, `msgfmt` je
zadovoljan, i te komande se izgovaraju na engleskom zauvek.

Ekstraktor zato ispisuje iste stringove **drugi put**, kao
`src/i18n/data_strings.cpp` pun `N_()` oznaka. Dve generisane kopije jedne
istine su prihvatljive; dve koje se raziđu nisu, pa test proverava da svaki
opis, kategorija i labela iz podataka ima oznaku.

### N86 — opisi znakova se nisu instalirali

Nađeno čitanjem spiska fajlova koje `meson install` stvarno postavi, a ne
testom — jer test ne bi mogao: svita čita iz izvornog drveta, gde fajl postoji.

`alus::text::CharacterDescriptions` traži
`<datadir>/alus/characters/<jezik>.dic`, a `meson.build` je instalirao
`data/symbols` i `data/locale` ali **ne** `data/characters`. Posledica na
instaliranom ALUS-u: `numpad2` tri puta (opis znaka) padao je na **ugrađeni
engleski** set, na svakom jeziku, i nikad nije rekao zašto — jer fallback je
ispravno englesko ponašanje, ne greška. Defekt iz Faze 2, vidljiv samo iz
instalacije.

Jedna linija u `meson.build`. Provereno nad stažiranom instalacijom:

```
$ ls /tmp/stage/usr/local/share/alus/characters/
en.dic  sr.dic
```

### N87 — rani izlaz iz `main` je curio GOption stringove

`g_option_context_parse` duplicira svaki string sa komandne linije, a
`g_free` je bio samo na kraju `main`-a. Svaki rani izlaz je zato curio:
`--version` u sva tri izvršna fajla, „another instance is already running" u
demonu, i u novom `alus-settings` i nepostojeća stranica i predaja postojećem
prozoru. Mereno: 18 bajtova u `alus-settings`, 6 u demonu i u `alus-cli`.

Proces koji upravo izlazi nikom ne šteti sa 18 izgubljenih bajtova. Ali
**curenje koje detektor prijavi je curenje koje neko mora da triježi**, a
detektor je koristan samo dok je tih — isto obrazloženje zbog kog
`tests/valgrind.supp` postoji i zbog kog je `msgfmt` izvan valgrind prolaza
(O6). Oslobađanje je sada u jednoj lambdi kroz koju prolazi svaki `return`.

Nađeno zato što `settings-bad-page` test pod valgrind-om izlazi sa kodom 9
(valgrind) a ne 2 (loša stranica), a `should_fail: true` prihvata svaki
nenulti kod — pa bi prava greška ostala prikrivena. Test je ostao kakav je
(razlikovanje kodova pod `exe_wrapper`-om nije moguće), ali je put provera
ručno.

### N96 — druga čitačka odbija da se digne posle `pkill`, i sama kaže lek

Napisano je `tools/alus-switch.sh`, jer je svakodnevna upotreba ALUS-a uz
instaliranu drugu čitačku **uvek zamena u oba smera**: GNOME-ov menadžer
pristupačnosti dostavlja tastere tačno jednom imenu na magistrali, pa oba
čitača ne mogu zajedno.

Prvi test zamene je pao u jednom smeru, i to je dalo nalaz. Povratak na drugu
čitačku je prijavljivao da se nije digla, dok je ALUS uredno išao. Uzrok —
izmeren, ne pretpostavljen:

```
Већ је покренут један процес читача екрана за ову сесију.
Покрените наредбу „orca --replace“ да замените тај процес новим.
```

Ta čitačka radi kao **dva procesa**, pa `pkill -x` očisti ime procesa pre nego
što oba stvarno puste sesiju; sledeće pokretanje tada vidi zaostalu instancu i
odbije. Provereno da ALUS **nije** uzrok: sa ALUS-om koji radi, obično
pokretanje te čitačke **uspeva** (ogled u tri koraka).

Lek je ono što ona sama predlaže — `--replace` — čime pokretanje postaje
idempotentno. Uz to `stop_and_wait` sada posle gašenja imena čeka još sekundu
i potvrdi, jer „ime je nestalo" nije isto što i „proces je otišao".

**I defekt u samoj skripti, koji je ovaj nalaz odložio:** izlaz druge čitačke
je išao u `/dev/null`, pa je razlog pada bio nedostupan i tražio je `bash -x`.
Sada ide u `~/alus-switch-other.log` i ispisuje se na padu. Pravilo je isto kao
N91 u `FAZA6.md`: alat koji meri ne sme da baca dokaz.

Dva pravila po kojima je skripta napisana, jer ovde greška ima posebnu cenu:

1. **Svaki ishod se izgovara**, preko `spd-say` — koji govori sa
   speech-dispatcher-om neposredno i radi bez obzira na to koji je čitač gore i
   da li je ijedan. Samo ispis je bezvredan onome kome je skripta najpotrebnija.
2. **Pokretanje koje padne se vraća nazad** na čitač koji je radio. Bolje onaj
   koji si imao nego nijedan. Ta grana je izvršena pri prvom testu i odradila je
   posao — korisnik nije ostao bez govora.

Provereno: dve pune zamene u oba smera, svaki put ostane tačno jedan čitač i
prekidač `screen-reader-enabled` prati stanje (`false` za ALUS, `true` za drugu).

### N97 — „ALUS radi" nije isto što i „samo ALUS radi"

Posle instalacije `alus.desktop` se pojavi u meniju aplikacija, pa je prvo
pokretanje bilo odatle — i to je proizvelo stanje koje ni jedan alat dotad nije
umeo da prijavi: **oba čitača rade istovremeno.** Meni ne dira
`screen-reader-enabled`, pa je druga čitačka ostala gore, a ALUS se digao uz
nju. Oba su instalirala grabove na istu tastaturu.

```
running:          BOTH -- they are fighting for the keyboard
desktop switch:   screen-reader-enabled = true
```

Defekt je bio u `tools/alus-switch.sh`: rani izlaz je gledao **samo jedan**
čitač (`alus_running && exit 0`), pa u tom stanju nije radio ništa — a to je
upravo stanje iz kog treba izaći. Ispravljeno: rani izlaz traži da je jedan gore
**i** da drugi nije; kad su oba, zaustavi drugog i ne diže drugu kopiju prvog.

Druga polovina istog nalaza je u `status`-u. Ispisivao je `keyboard: free` dok
je ALUS držao tastaturu, jer `alus-cli input-info` izlazi sa 0 uz **sopstveni**
demon a sa 1 uz drugu čitačku. Taj ispis dakle odgovara na „može li ALUS da
uzme tastaturu", ne na „je li tastatura slobodna". Preformulisano u
`keyboard: available to ALUS` / `held by …`, jer je to jedina dijagnostika na
koju se slep korisnik oslanja i u njoj laž košta najviše.

### N98 — demon pokrenut iz menija ne piše u `~/alus.log`

`alus.desktop` ima `Exec=/usr/local/bin/alus-daemon`, bez `--log-file`. Izlaz
tada ide u standardnu grešku, a roditelj je `gnome-shell`, pa se log nalazi u
dnevniku sesije pod imenom ljuske:

```bash
journalctl --user -b | grep 'gnome-shell\[.*alus' | tail -40
journalctl --user -f | grep -i alus          # uživo
```

Nije defekt — dnevnik radi i ima sve, uključujući grabove, app sloj i module.
Ali je zamka pri ispravljanju grešaka: `tail -f ~/alus.log` tada pokazuje
**stari** log i izgleda kao da demon ne radi ništa. Zato, kad log treba na
jednom mestu, demon se pokreće sa `--log-file`, što `tools/alus-switch.sh` i
radi.

### N99 — `alus+q` je obećan u referenci, a nevezan je i neimplementiran

`data/scripts.csv` sadrži komandu `quit` sa podrazumevanim gestom `kb:ALUS+q` i
opisom „Quits ALUS!", i generisana referenca je verno prenosi:

```
docs/commands.md:257   | `quit` | ALUS+q | ALUS+q | Quits ALUS! |
```

Mereno nad instaliranim ALUS-om, i potvrđeno uživo pritiskom:

```
$ alus-cli gesture kb:alus+q
command:   (none — this gesture is unbound)
```

`"quit"` se ne pojavljuje **nigde** u `src/`: komanda nema ni vezivanje ni
implementaciju. Taster je tih — ne kaže ni da ga nema.

**Zašto je ovo teže od obične nedostajuće funkcije.** Ostali načini da se ALUS
zaustavi su terminal (`pkill -TERM alus-daemon`), `gsettings` prekidač i
`tools/alus-switch.sh other`. Sva tri traže da korisnik **nađe terminal**. Ako
čitač progovori preko svega ili se zaglavi, `alus+q` je jedino što to ne traži —
i upravo tada ne radi.

**Zašto nijedan test to nije uhvatio, i to je treći put u istom obliku.** T9.5
proverava da se `docs/commands.md` **poklapa sa** `data/scripts.csv`. Poklapa se
savršeno — jer je iz njega **generisan**. Nijedan test ne proverava da komanda
iz tabele ima implementaciju ili vezivanje. Ista greška kao N91 (`FAZA6.md`) i
kao N97: **provera deli izvor sa onim što proverava.**

Odatle sledi kriterijum koji ovoj fazi nedostaje, i koji vredi više od same
ispravke: test koji nad **celom** tabelom od 390 skripta traži da svaki ima ili
implementaciju ili izričitu najavu da ga nema (O10 je taj obrazac već uveo za
tri dijaloga). To bi uhvatilo N99 i svaki sledeći takav.

Odluka koja stoji pre vezivanja: **traži li `alus+q` potvrdu.** NVDA pita
„Exit NVDA?". Čitač koji se gasi na jedan omašen taster je svoja vrsta
nesreće, a dijalog potvrde na Waylandu ima ograničenje iz N83. Nije rešeno.

**Dopuna, 8. 11. 2026.** Zaključak stoji — `quit` nema ni vezivanje ni
implementaciju — ali **dokaz koji je gore naveden ne stoji.** `alus-cli
gesture` sastavlja samo ulazni sloj, pa odgovara „unbound" i za komande koje
su implementirane (N100). Zaključak je potvrđen drugim putem: `"quit"` se ne
pojavljuje u `src/`, i T9.6 (vidi niže) ga je prijavio iz sastavljenog
demonskog steka. Taster od tada **govori** da komande nema
(`data/commands-status.ini`), a vezivanje i dalje čeka odluku o potvrdi.

### N100 — `alus-cli gesture` odgovara „unbound" i za komande koje postoje

Dijagnostika koja je dala dokaz za N99 je nepouzdana, i to u opasnom smeru:
prijavljuje **nevezano** za komandu koja radi.

```
$ alus-cli gesture kb:alus+tab      # reportCurrentFocus, implementiran u a11y::ObjectCommands
command:   (none — this gesture is unbound)
$ alus-cli gesture kb:h             # nextHeading, implementiran u nav::BrowseMode
command:   (none — this gesture is unbound)
$ alus-cli gesture kb:alus+1        # toggleInputHelp, implementiran u input::InputCommands
command:   toggleInputHelp
```

Uzrok je u `src/cli/main.cpp`: za `gesture`, `input-info` i `list-commands`
pravi se **samo** `input::Service`. Pretraga u deset koraka radi tačno, ali
kontekst sadrži jednu jedinu grupu komandi od šest koje demon sastavlja
(`a11y`, `nav`, `braille`, `app`, `gui` i ulazni sloj). Sve što nije u ulaznom
sloju — dakle 233 od 238 vezanih tastera — izgleda nevezano.

**Posledica za način rada, ne samo za alat.** Pet od dvanaest komandi
`alus-cli`-ja postoji da bi se nešto **izmerilo** umesto da se pretpostavi.
Alat koji u tom poslu laže je gori od nepostojećeg, jer se njegovom izlazu
veruje. Ovo je ispravljeno **ne** u alatu nego u testu: T9.6 sastavlja ceo
stek. Sam `alus-cli gesture` i dalje odgovara samo za ulazni sloj i to mu
treba zapisati u ispis — nije rešeno.

### N101 — `alus+space` nije radio: prečica iz mape nije dosezala objekat

Prebacivanje između režima pregleda i režima fokusa je najčešće pritiskan
taster u čitaču nad veb stranom. Bio je **tih**.

Komanda je implementirana (`src/nav/browse_mode.cpp:562`,
`toggleVirtualBufferPassThrough`), i mapa je ima:

```
data/gestures-builtin.ini:269   [globalCommands.GlobalCommands]
                                toggleVirtualBufferPassThrough = kb:ALUS+space
```

Ali odeljak mape imenuje NVDA **klasu**, a korak 6 pretrage prihvata unos iz
mape samo ako objekat odgovara na to ime (`ScriptableObject::answersTo`).
`browseModeClassChain()` je imao pet imena i nijedno nije bilo
`globalCommands.GlobalCommands` — jer upstream tu komandu drži na
`GlobalCommands` odakle poseže za presretačem fokusa, a ALUS je, ispravno,
preselio **implementaciju** na presretač. Preseljenje implementacije bez
preseljenja imena klase daje ključ koji nijedan korak pretrage ne može da
nađe: korak 6 odbija lokaciju, korak 10 nema skriptu.

Ispravka je jedno ime u lancu, uz `virtualBuffers.VirtualBuffer` iz istog
razloga (`alus+f5`). Ne zaklanja ništa: korak 6 prihvata samo ime skripte koje
**taj** objekat poseduje, a `toggleVirtualBufferPassThrough` je jedina komanda
iz tog odeljka koju režim pregleda poseduje (provereno nad celom mapom).

**Zašto nijedan test to nije uhvatio.** `tests/input/test_catalog.cpp` ima
`CHECK(boundTo("kb:ALUS+space", "toggleVirtualBufferPassThrough"))` — ali to
proverava **mapu**, a ne da iko tu komandu ima na tom mestu. T6.9 proverava da
presretač ima skriptu po imenu, a ne da je prečica dosegne. Dva testa, oba
prolaze, a taster ne radi: ni jedan ni drugi ne spajaju mapu sa objektom, i to
spajanje je upravo ono što je bilo pokvareno.

### N102 — režim pregleda nikada nije dobio katalog: 180 komanda bez opisa

`BrowseMode` ima parametar `const script::Catalog* catalog` sa podrazumevanim
`nullptr`, u telu je stajalo `(void)catalog;`, a `chooseForDocument` — jedini
način na koji se presretač uopšte pravi — nije prosleđivao ništa. Posledica:
**svih 180 komanda** režima pregleda radilo je bez ikakvih metapodataka.

Šta to konkretno znači:

| | |
|---|---|
| pomoć za unos | ne ume da imenuje komandu; `h` i `k` su bez opisa |
| dijalog gestova | **ne prikazuje ih** — NVDA i ALUS kriju skriptu bez opisa (T9.3) |
| kategorije | sve 180 bez kategorije, pa ne mogu da se grupišu |

Izmereno posle prosleđivanja kataloga: 180 komanda, **121 sa opisom** (ostalih
59 upstream i ne dokumentuje, kao i 74 komande koje referenca zato izostavlja).

Nijedan test to nije uhvatio jer `tests/nav/test_browse_mode.cpp` sam pravi
presretač i proverava **ponašanje** komanda, ne njihove metapodatke. Regresioni
test je zato u `tests/coverage/`, gde se gradi pravi demonski stek.

### N103 — zapeta je u mapi gestova i razdvajač i taster

`data/gestures-builtin.ini` razdvaja više gestova jedne komande zapetom:

```
activatePosition = kb:enter, kb:numpadEnter, kb:space
```

A dve komande su vezane **na samu zapetu**:

```
movePastEndOfContainer = kb:,
moveToStartOfContainer = kb:shift+,
```

`GestureMap::splitComma` je delila na svakoj zapeti, pa su te dve linije
postajale `kb:` i `kb:shift+` — identifikatori koje nijedna tastatura ne može
da proizvede. Obe komande su bile nedostižne, u generisanoj mapi koju dobija
**svaki** korisnik, i to tiho: učitavanje nije prijavilo grešku jer je linija
sintaksno ispravna.

Ispravka: zapeta je razdvajač samo ako posle nje sledi **drugi gest** — a svaki
gest počinje izvorom i `:` ili `(` (`kb:`, `kb(laptop):`, `br(brlapi):`).
Nađeno tako što je T9.6 odbio da ih prizna za prečice tastature.

### N104 — šta je iz svega ovoga izgrađeno: T9.6 i `commands-status.ini`

Kriterijum koji je N99 zahtevao je izgrađen, i uhvatio je N100–N103 u jednom
prolazu. Dva dela:

**1. `data/commands-status.ini` — treći izvor, i jedini koji nije generisan.**
`scripts.csv` i `gestures-builtin.ini` dolaze iz upstream izvora i znaju šta
komanda **jeste**; nijedan od njih ne može da zna da li je ALUS **napisao**.
Taj fajl to kaže, u dva odeljka:

| odeljak | ponašanje tastera | zašto |
|---|---|---|
| `[announce]` (49) | taster se grabuje i **izgovori** „није у овој верзији" | O10: nedovršena funkcija koja se čuje nije pokvarena funkcija |
| `[platform]` (3) | taster se **ne grabuje** | pripada aplikaciji, a ALUS ne može da ga pošalje natrag (N18) |

`[platform]` su `tab`, `shiftTab` i `passThrough`. Najava na njima bila bi gora
od tišine: korisnik bi izgubio taster a dobio rečenicu. Zato test za njih
tvrdi **obrnuto** — da ih pretraga ne nalazi — da ih niko ne „ispravi"
grabovanjem.

Fajl je nosiv u oba smera: `script::bindAnnouncements` iz njega vezuje najave
pri pokretanju, a test iz njega čita ugovor. Jedan izvor, dva korisnika.

**2. `tests/coverage/test_command_coverage.cpp` — T9.6.** Sastavlja **demonov
stek** (`src/daemon/main.cpp`, isti servisi, isti red, isto ožičenje) nad
lažnom pristupačnom platformom, pa za svaku prečicu iz ugrađene mape pita
pretragu u deset koraka da li iko odgovara — u dva konteksta koja tim
tasterima i pripadaju: fokus u veb dokumentu (režim pregleda) i fokus u polju
za unos (komande kareta, koje veže **ponašanje** `EditableText`, a ne nijedan
objekat komanda).

Izmereno, 8. 11. 2026.:

```
315 prečica tastature u data/gestures-builtin.ini
    238  implementirano
     64  najavljuje da ga nema u ovoj verziji
     13  namerno nevezano (N18)
```

Kad komanda nije dosežna, test razlikuje dve stvari koje imaju različite
ispravke — **UNIMPLEMENTED** (ništa je ne poseduje) i **UNREACHABLE** (nešto je
poseduje, ali pretraga ne može da dođe) — i u drugom slučaju ispisuje imena
klasa na koja vlasnik odgovara. Upravo je taj ispis imenovao N101.

**Čuvar koji ne deli izvor sa onim što proverava.** Treći slučaj u fajlu čita
`src/` — izvor koji ništa drugo u ovoj proveri ne čita — i traži dve stvari:
da nijedna komanda iz `[announce]` nije u međuvremenu implementirana (ustajala
linija bi zaklonila pravu komandu), i da **svaki** fajl u `src/` koji veže
komande bude u spisku koji fikstura pokriva. Drugo je važnije: fikstura koja
zaboravi objekat prijavljuje svaku njegovu prečicu kao implementiranu a da
ništa nije pitala — tiha greška, ista vrsta kao N91, N97 i N99.

**Test je proveren tako što su greške vraćene.** Sve četiri prijavljuje:

| vraćena greška | šta test kaže |
|---|---|
| uklonjen `dateTime` iz `[announce]` | `UNIMPLEMENTED AND SILENT: 'dateTime' … kb:alus+f12` |
| `toggleInputHelp` upisan u `[announce]` | „bound in src/input/commands.cpp but … still lists it under [announce]" |
| uklonjen `globalCommands.GlobalCommands` iz lanca | `UNREACHABLE: 'toggleVirtualBufferPassThrough' … kb:alus+space` |
| nov fajl sa `bindScript(` | „binds commands and is not in this test's list" |

Prvi pokušaj dijagnoze je pritom i sam bio pokvaren: `UNREACHABLE` je
prijavljivalo `UNIMPLEMENTED`, jer je vlasnika tražilo u kontekstu koji je
`resolve` ostavio (polje za unos), gde režima pregleda nema. Nađeno tek kad je
greška vraćena — što je i razlog zašto se greške vraćaju.

**Cena najava je u grabovanim tasterima, i izmerena je.** Grab set se računa
tako što se pretraga pita da li bi gest **sada** našao skriptu (N61), pa svaka
najava uzima taster od aplikacije. Mereno nad `--no-input` demonom, gde je
jedini kontekst ulazni sloj:

| | grabova |
|---|---|
| bez `commands-status.ini` | 12 |
| sa njim (43 najave u globalnom sloju) | **100** |

Od 49 najavljenih prečica, **42 nose ALUS modifikator** i ne mogu da se sudare
ni sa čim. Ostalih sedam: `p` i `shift+p` su **samo u režimu pregleda** (korak
6), gde ALUS već grabuje svako slovo; a `numpadDivide`, `numpadMultiply`,
`shift+numpadDivide`, `shift+numpadMultiply` i `shift+numpad2` su na numeričkoj
tastaturi, koju ALUS u desktop rasporedu **već drži** — numpad 1–9, `numpadPlus`,
`numpadEnter` i `numpadDelete` su vezani za pregledni kursor od Faze 5. Nijedan
taster nije uzet aplikaciji van sloja koji je ALUS-ov i pre ovoga.

**Dokumentaciona polovina N99.** `tools/generate-command-reference.py` sada
čita isti fajl i označava svaki takav red u `docs/commands.md`
(**НЕМА У ОВОЈ ВЕРЗИЈИ** / **НЕМА НА WAYLAND-У**), a T9.5 to proverava — 46
redova. Referenca više ne obećava komandu koja ne postoji, što je bila prva
polovina N99.

### N105 — `alus+q` vezan, i meni preseljen na `alus+a`; za to je trebao treći generisani ulaz

Dve odluke korisnika, 8. 11. 2026.: `alus+q` **mora da zatvara** (najava nije
dovoljna), a meni ide na **`alus+a`**, ne na `alus+n`.

**`quit`.** Jedino što poseduje `core::Loop` je `daemonMain`, pa komanda stiže
do njega kao hook (`GestureManager::Hooks::quit`) — isti obrazac kao
`cancelSpeech` i ostalih devet. `InputCommands::quit` izgovori „излазим" pa
pozove hook; kad hooka nema (`alus-cli`, test) **kaže** „одавде не могу да
изађем" i ne radi ništa tiho.

**I čeka da se reč izgovori.** Sintetizator je drugi proces: `loop.quit()` u
trenutku pritiska zatvara vezu u sredini izgovora, pa slep korisnik ne čuje da
li je taster uopšte radio — a sledeće što čuje je tišina, što zvuči isto kao
pad. Zato demon pita svakih 100 ms da li je govorni red prazan, sa kapom od 3 s
da sintetizator koji ne prijavi indeks ne može da drži ALUS u životu (isti
razlog zbog kog `SpeechManager` ima `lostIndexGrace`).

**Prečica je trebala treći ulaz u generisanu mapu.** `gestures-builtin.ini` i
kolone gestova u `scripts.csv` se generišu iz upstream izvora, pa izmena rukom
nestaje pri sledećem pokretanju ekstraktora — tiho, jer ništa ne pada. A nov
**sloj** mape ne bi pomogao: slojevi se **naslažu**, ne zamenjuju, pa bi stari
taster i dalje radio i referenca bi imenovala pogrešan. Jedini način da se
taster zaista **premesti** je da se promeni ulaz generatora:

`data/gestures-alus.ini` — ALUS-ove sopstvene prečice, primenjuje ih
`tools/extract-commands.py` **pre** oba pisca, pa mapa, CSV i referenca kažu
istu stvar. Fajl se ne instalira: on je ulaz prevođenja, a ne podatak koji
demon čita.

**Provera sudara, i greška u prvoj verziji te provere.** Generator odbija
prečicu koja se sudara sa drugom komandom u istom odeljku, jer dve komande na
jednom tasteru rešava „koji objekat korak 10 prvi dohvati" — što nije ponašanje
koje je iko izabrao. Prva verzija je upoređivala **nizove**, pa je propustila
upravo slučaj zbog kog je i pisana: `kb:ALUS+a` važi u **oba** rasporeda i
sudara se sa `kb(laptop):ALUS+a` (`sayAll`), a ta dva niza ne liče. Nađeno tako
što je sudar napisan namerno. Sada se upoređuje po (raspored, taster), sa
sortiranim modifikatorima:

```
$ python3 tools/extract-commands.py …        # sa showGui = kb:ALUS+a
gestures-alus.ini: [globalCommands.GlobalCommands] showGui = kb:ALUS+a
collides with sayAll on kb:ALUS+a — … Pick another key, or move the other
command here too.
```

Zato je upisano `showGui = kb(desktop):ALUS+a, kb(laptop):ALUS+n`: desktop
dobija `a`, laptop zadržava upstream-ov `n` (tamo je `a` već `sayAll`).

**`showGui` je i implementiran.** Upstream njime otvara svoj **meni**; ALUS
menija nema — ima prozor podešavanja, što je ista stvar jedan nivo niže. Taster
otvara prvu stranicu prozora umesto da kaže da takvog panela nema: korisnik je
tražio „ALUS-ova podešavanja" i to i dobija.

Posle oba: **316** prečica — 241 implementirano, 62 najavljuje da ih nema, 13
namerno nevezano. Grabova 98.

### N106 — 76 srpskih reči je u ćiriličnom katalogu bilo na latinici, a pet u nijednom pismu

Nađeno dok je dodavan prevod za „излазим": importer ga je ostavio kao
`izlazim`.

**Uzrok.** `tools/import-nvda-po.py` pravi `po/sr.po` (ćirilica) i
`po/sr@latin.po` (latinica) iz jednog rukom pisanog latiničnog izvora. Nešto
mora da **ostane** na latinici — skraćenice koje upstream bira za brajev red,
gde je jedinica **ćelija** i cela reč ne staje. Koje su to, odlučivao je
**oblik teksta**: mala ASCII slova, bez razmaka, najviše sedam. Taj test ne
može da razlikuje `cmnt` od `meni` — oba su četiri mala slova.

Izmereno: **76 običnih reči** je tako ostalo na latinici u ćiriličnom
katalogu, a među njima su reči koje čitač ekrana izgovara najčešće:

| msgid | bilo | sada |
|---|---|---|
| `window` | prozor | прозор |
| `button` | dugme | дугме |
| `menu` | meni | мени |
| `heading` | naslov | наслов |
| `table` | tabela | табела |
| `list` | lista | листа |
| `graphic` | slika | слика |
| `link` | link | линк |

Sve su bile **prevedene**, pa T9.4 nije imao šta da prijavi: proveravao je da
nema praznog `msgstr`-a, ne da je prevod u pismu koje katalog tvrdi.

**I druga, gora polovina.** Pet prevoda je ispalo u **nijednom** pismu, jer
transliteracija prolazi slovo po slovo i ostavlja slova kojih u srpskoj
latinici nema:

```
"windows"   -> "wиндоwс"        ("w" nema ćirilični ekvivalent)
"Waylandu"  -> "Wаyланду"       (KEEP zna „Wayland", ne i padež)
"rhvoice"   -> "рхвоице"
"speech-dispatcher-a" -> "спеецх-диспатцхер-а"
"changedContext" -> "цхангедЦонтеxт"
```

**Dve ispravke, obe bez heuristike.**

1. **Spisak umesto oblika.** `ABBREVIATION_MSGIDS` imenuje 82 msgid-a iz
   `src/braille/labels.cpp` i 7 skraćenica iz `properties.cpp` (koji sadrži i
   **reči**, zato spisak ide po msgid-u a ne po fajlu). Importer ga proverava
   protiv `.pot`-a pri svakom pokretanju i **pada** ako imenuje string koji
   više ne postoji — jer bi preimenovana oznaka tiho ispala sa spiska i
   postala besmislica na brajevom redu.
2. **Pravilo koje gasi celu klasu:** srpska latinica nema `q`, `w`, `x` ni
   `y`. Reč koja sadrži jedno od njih **nije srpska** i ostaje nedirnuta.
   Ispravlja `windows`, `Waylandu`, `changedContext` i svaki sledeći takav bez
   dopisivanja u spisak. Uz to su u `KEEP` dodata imena koja su mu falila
   (`speech-dispatcher`, `rhvoice`, `GUI`, `ATK`, `UNO`, `VCL`, `Gecko`,
   `Blink`, `ICU`, `UPower`).

**Test, i njegova granularnost je bila pogrešna dva puta pre ovog.** Prva
verzija je tvrdila „nijedan prevod ne sadrži ASCII slovo" — pala je na 129
tačnih redova, jer ćirilični tekst **sme** da nosi ime proizvoda („Приказује
ALUS подешавања"). Druga je proveravala svaku **reč** protiv spiska — pala je
na 60 brajevih skraćenica čije srpske oblike sam pogađao. Tačna granularnost
je **unos**:

| provera | tvrdnja |
|---|---|
| 1 | svaki prevod **bez ijednog ćiriličnog slova** mora da bude imenovan u spisku (83 ih je: skraćenice i imena) |
| 2 | nijedna reč ne sme da ima latinično slovo **posle** ćiriličnog — `speech-dispatcher-а` i `BrlAPI-ја` su u redu, `wиндоwс` nije |

Nijedna ne ponavlja pravilo importera; obe čitaju samo isporučeni tekst i
jedan kratak deklarisan spisak. Provereno vraćanjem obe greške: prva prijavi
`msgid "window" … "prozor"`, druga `"цхангедЦонтеxт"`.

## Merenja

```
$ bash tools/phase9-gui-check.sh
==> 1. Is the GUI built, and can it open here
  alus-settings:     /home/dare/ALUS/build/src/alus-settings
  can open here:     yes
  pages:             general, speech, braille, keyboard, presentation, review,
                     formatting, gestures
  language:          sr_RS        catalogue: loaded        installed: sr, sr@latin
==> 2. The catalogues
    ok    po/sr.po valid: 1102 преведене поруке.
    ok    po/sr@latin.po valid: 1102 преведене поруке.
    ok    po/sr.po: nothing untranslated
==> 3. Does the translated screen reader actually speak Serbian
    ok    the command table is in Serbian Cyrillic
    ok    sr@latin is a separate catalogue and loads
==> 4. The command reference
    ok    docs/commands.md matches data/scripts.csv (T9.5)
    ok    po/POTFILES.in lists every translatable source
==> 5. The installation
    ok    alus.desktop is a valid desktop entry
    ok    alus-settings.desktop is a valid desktop entry
    ok    alus.service passes systemd-analyze verify
==> 6. THE FINDING: does opening the window freeze the screen reader
    ok    the core did not stall while the settings window opened
    ok    no AT-SPI call about the window timed out
    ok    ALUS sees the settings window as an application of its own
phase 9: every automatic check passed
```

Prevod, uživo, bez ikakvog mock-a:

```
$ ./build/src/alus-cli --language sr list-commands | head -3
toggleNativeAppSelectionMode  Режим претраживања  kb:ALUS+shift+f10
                              Укључује и искључује режим уграђеног избора
nextAnnotation                Режим претраживања  kb:a
                              премешта се на наредну напомену
```

Pokrivenost nasleđenog prevoda, mereno uvoznikom:

```
$ python3 tools/import-nvda-po.py
po/sr.po: 1102/1102 translated (100%) — 738 from upstream,
          364 from overrides.po (35 matched after rewriting the product name),
          0 left to translate
```

## Predaja

Faza 9 je poslednja u `02-FAZE.md`. Šta ostaje otvoreno nije njena nedovršenost
nego lista za v1.0, i deli se na tri vrste.

**1. Traži okruženje, ne kod** — tri kriterijuma iz ranijih faza i dalje:

```bash
# T5.3 — japanska i tajlandska segmentacija
sudo apt install libicu-dev && meson setup --wipe build && meson compile -C build
meson test -C build --suite text

# T6.7 — milisekunde quick-nav-a; traži pokrenut Firefox ili Chromium
./build/src/alus-cli browse-info

# Faza 7, ručni test — traži brajev red ili brltty u virtualnom modu (N64)
brltty -b no -d /dev/null -x no -n &
bash tools/phase7-braille-check.sh --manual
```

**2. Traži čoveka koji sluša** — ručni testovi faza 3, 5, 6, 7, 8 i 9. Planovi
su napisani i izvršni; ono što mašina ne može je da potvrdi da je ono što se
čuje razumljivo:

```bash
bash tools/phase3-input-check.sh --manual   # ulaz, ulazna pomoć
bash tools/phase5-text-check.sh --manual    # Text Editor i terminal
bash tools/phase6-browse-check.sh --manual  # Wikipedia, tabele, Writer
bash tools/phase7-braille-check.sh --manual # brajev red
bash tools/phase8-app-check.sh --manual     # Writer, Calc, VS Code, Nautilus
bash tools/phase9-gui-check.sh --manual     # prečice, dijalog gestova, srpski
```

**3. Traži odluku, ne rad** — tri stvari koje faza 9 ostavlja svesno:

- **Peti sloj gestova u dijalogu je prazan.** Šta vezuju app moduli i plugin-i
  živi u demonu, a dijalog je drugi proces (O7). Ili se čita preko magistrale,
  ili ostaje prazan sa linijom u dijalogu. Za v1.0 je prazan.
- **Uređivači rečnika i dijalog profila nisu u GUI-ju.** Prečice postoje i
  kažu da ih nema (O12). `[app]` sekcija i `createProfile`/`mapTrigger` iz
  Faze 8 su tu; nedostaje samo stranica.
- **Add-on manager nema dugme.** `alus-cli app-info` je model (predaja Faze 8,
  tačka 3); GUI treba stranicu koja poziva `PluginHost::markForRemoval`.

## Stanje na kom se nastavlja

```bash
meson setup build && meson compile -C build
meson test -C build                      # 19 testova, 18 prošlo + 1 očekivani pad
meson test -C build --suite gui          # 8474 asertacije — izlazni kriterijum Faze 9
meson test -C build --suite external     # msgfmt i generatori, pravim alatima
meson test -C build --setup=valgrind --suite core --suite config --suite speech \
         --suite input --suite a11y --suite text --suite nav --suite browsemode \
         --suite braille --suite app --suite gui
                                         # 13/13, 0 grešaka, 0 izgubljenih bajtova
bash tools/phase9-gui-check.sh           # mašina: katalozi, referenca, instalacija, N79
bash tools/phase9-gui-check.sh --manual  # + plan ručnog testa
./build/src/alus-cli gui-info            # može li prozor, koji je jezik
./build/src/alus-cli autostart           # šta se diže sa sesijom
meson test -C build --suite commands    # T9.6: svaka prečica iz mape nešto radi
tools/alus-switch.sh status             # koji čitač ima tastaturu sada
tools/alus-switch.sh alus               # ALUS uzima tastaturu (govorno javlja)
tools/alus-switch.sh other              # vrati je drugoj čitačkoj
./build/src/alus-settings --page braille # prozor podešavanja
```

Regenerisanje svega što je generisano:

```bash
python3 tools/extract-commands.py --source ./NVDA --out data/ \
        --strings-out src/i18n/data_strings.cpp
python3 tools/extract-controltypes.py --source ./NVDA --out .
python3 tools/update-potfiles.py
meson compile -C build alus-pot          # po/alus.pot
python3 tools/import-nvda-po.py          # po/sr.po, po/sr@latin.po
python3 tools/generate-command-reference.py
```
