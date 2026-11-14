# Faza 8 — App moduli i ekstenzibilnost

Izveštaj o izvršenoj fazi. Status svakog izlaznog kriterijuma, odstupanja od
plana sa razlogom, i nalazi koje je faza proizvela.

**Jedna komanda:**

```bash
meson test -C build --suite app
```

Rezultat: **439 asertacija, 42 test slučaja, zeleno.** Pod valgrind-om:
**12/12 svita, 0 grešaka, 0 izgubljenih bajtova** — i ovde to nije formalnost,
jer faza `dlopen`-uje tuđi kod i mora da ga `dlclose`-uje bez da odnese
registraciju sa sobom.

Faza je izmerena **na živim aplikacijama**, ne samo na mock platformi, i to je
promenilo kod: **sva četiri app modula su prepisana posle merenja**, jer je
drvo koje aplikacije stvarno izlažu drugačije od onog koji plan pretpostavlja
(N66–N71). Jedno od tih merenja je našlo defekt u Fazi 4 koji bi zamrzao čitač
ekrana u LibreOffice Calc-u (**N68**).

## Status izlaznih kriterijuma

| Kriterijum | Status | Gde se meri |
|---|---|---|
| T8.1 — app modul se učita po imenu izvršnog fajla i po toolkit-u | **prošao** | `[T8.1]` × 2 u `test_lookup.cpp`; obe polovine odvojeno |
| T8.2 — `chooseOverlays`: `clsList.remove` i `prepend` rade | **prošao** | `[T8.2]`, dve sekcije; remove proverava da je modul **video** predlog platforme |
| T8.3 — `event_<name>` koji ne pozove `next()` pojede događaj | **prošao** | `[T8.3]`, obe strane: pojeden ⇒ ništa se ne izgovori, propušten ⇒ izgovori se |
| T8.4 — `atomicProfileSwitch`: prebacivanje rekonstruiše profil 1×, ne N× | **prošao** | `[T8.4]` × 2; mereno brojačem: 40 prebacivanja = **40** rekonstrukcija, 100 fokusa u istoj aplikaciji = **0** |
| T8.5 — ABI verzija se proverava, nekompatibilan plugin se odbije sa logom | **prošao** | `[T8.5]` × 2, nad **stvarnim** `.so` fajlovima; `init` se ne poziva |
| T8.6 — plugin može da doda skript, overlay i event handler | **prošao** | `[T8.6]` × 2, četiri sekcije, nad C plugin-om prevedenim kao C |
| T8.7 — plugin koji se deinstalira dok je učitan se obriše pri sledećem startu | **prošao** | `[T8.7]` × 2: i add-on direktorijum i goli `.so` |
| Ručni test (Writer, Calc, VS Code, Nautilus) | **delimično izvršen** | izmereno drvo sve tri instalirane aplikacije (vidi dole); VS Code **nije instaliran**; „šta korisnik čuje" je `tools/phase8-app-check.sh --manual` |

Ručni test je jedino što nije izvršeno u celosti, i razlog je mašina:

```
$ bash tools/phase8-app-check.sh
   OK   6 module rules registered
   OK   the desktop was read and every application resolved to a module
        gnome-terminal (pid 3324) runs as gnome-terminal-server
        soffice: installed at /usr/bin/soffice, not running
        nautilus: installed at /usr/bin/nautilus, not running
        konsole: not installed
        code: not installed
   OK   a plugin builds from alus-plugin.h alone (cc (Debian 14.2.0-19) 14.2.0)
   OK   the entry point alus_plugin_v1 is exported
   OK   no plugin directories: a stock ALUS with no add-ons
   OK   meson test --suite app passed
   OK   every suite still passes
```

Ono što se **moglo** izmeriti bez korisnika — šta aplikacija izlaže — izmereno
je, jednom po aplikaciji, i rezultat je u N66–N71. Ono što ne može jeste da li
je najava koju modul pravi *prava*; to je `--manual` plan.

## Živo merenje: šta aplikacije stvarno izlažu

Mereno preko stvarnog AT-SPI busa, na Debian 13 / GNOME 48.7, komandom iz
`tools/` i `alus-cli app-info`:

| Aplikacija | Accessible name | Toolkit | `/proc/<pid>/exe` |
|---|---|---|---|
| GNOME Terminal | `org.gnome.Terminal` | `gtk 3.24.49` | `gnome-terminal-server` |
| LibreOffice | `soffice` | **`gtk 3.24.49`** | `soffice.bin` |
| Nautilus | `org.gnome.Nautilus` | **`GTK 4.18.6`** | `nautilus` |
| Google Chrome | `Google Chrome` | **`Chromium 1.0`** | `chrome` |
| Orca | `orca` | `gtk 3.24.49` | `python3.13` |

**Nijedno od tih pet imena nije ono što bi korisnik pretpostavio**, i to je
ceo razlog zašto ključ iz §12.1 ima tri komponente. Orca je dobar primer:
izvršni fajl je `python3.13`, pa bi pravilo po izvršnom fajlu uhvatilo svaki
Python program na sistemu — zato je *accessible name* sekundarni ključ.

Drveta su merena do dubine na kojoj modul radi:

```
LibreOffice Writer
  frame/panel/panel/root pane/status bar          13 dece, dubina 4
  frame/.../document text                         [Collection!]  (N73)
LibreOffice Calc
  frame/.../document spreadsheet/table            deca = 2147483647  (N68)
  frame/.../document spreadsheet/table/table cell  bez `table row`   (N69)
Nautilus (list view)
  scroll pane/tree table "Преглед садржаја"
    [0] table row  (zaglavlje; deca su `filler`)
    [1] list       (anonimna, drži redove)
        table row "ALUS"        <- DATOTEKA
          table cell "ALUS ALUS"      ime, DUPLIRANO  (N70)
          table cell "14 ставки"
          table cell "Данас, у 9∶05"
GNOME Terminal
  frame/filler/page tab list/page tab/panel/filler/terminal   role=terminal
```

## Šta je izgrađeno

| Modul | Linija (.cpp + .hpp) | Napomena |
|---|---|---|
| `alus::app::AppKey` + `keyFor` + alias tabela | 126 + 87 | ključ iz §12.1; `/proc/<pid>/exe`, fallback na accessible name |
| `alus::app::Module` | 238 + 260 | svih **pet** mehanizama iz §12.2 + tri generalizovana hook-a |
| `alus::app::Registry` | 253 + 155 | pravila sa specifičnošću, running table po pid-u, koreografija `handleAppSwitch` |
| `alus::app::PluginHost` + `PluginScripts` + `NamedBehavior` | 686 + 205 | `dlopen`, provera ABI-ja, dvofazna deinstalacija, host vtable |
| `alus/alus-plugin.h` | — + 207 | **jedini** header koji add-on uključuje, i jedini koji se instalira; čist C |
| `alus::app::Service` | 484 + 153 | ožičenje u svih sedam šavova (vidi `service.hpp`) |
| `alus::app::AppCommands` | 73 + 56 | `reportStatusLine` (`alus+end` iz kataloga) |
| app moduli: libreoffice / terminal / electron / nautilus | 685 ukupno | svaki posle **živog merenja** |
| testovi | 1985 | T8.1–T8.7 + četiri **stvarna** C plugin-a |

Ukupno: **2545 linija `.cpp`, 1161 `.hpp`, 1985 testa.**

### Dodate stvari u ranijim slojevima

Faza 8 je sloj iznad svih ostalih, pa je morala da otvori šavove u njima. Svi
su **opcioni** — svaki test Faza 4–7 radi sa nijednim instaliranim:

| Gde | Šta | Zašto |
|---|---|---|
| `config::Manager::AtomicSwitch` | RAII transakcija profila | T8.4; bez nje alt-tab košta N rekonstrukcija |
| `a11y::Registry::setObjectInitHook` | mehanizam 2 (`event_NVDAObject_init`) | trenutak posle behavior-a, pre prvog pitanja |
| `a11y::Registry::setFocusEventFilter` | `shouldAllowFocusEvent` | lažan focus se **ne obradi uopšte** |
| `a11y::Registry::lastQueuedFocusObject` | detekcija duplikata | postavlja se pri **queue**-u, ne pri dispatch-u |
| `a11y::Registry::setContainerOverride` | override `container` | poziva ga `Object::container()` |
| `nav::TreeInterceptorRegistry::addVeto` | „ovaj objekat NE sme da se presretne" | chooser koji odbije samo prosledi pitanje dalje |
| `input::GestureManager::appMap()` | četvrti globalni sloj mape gestova | ključevi modula i plugin-a, rebindable u `gestures.ini` |
| `a11y::Object::children()` | **kMaxChildren = 8192** | N68 — bez njega Calc zamrzne čitač ekrana |

## Odstupanja od plana

**O1 — `init` dobija vtable kao drugi argument.** §12.3 piše
`int (*init)(AlusHost* host)`, sa pozivima „kroz `AlusHost`". Implementirano je
`int (*init)(AlusHost*, const AlusHostApi*)`. Razlog: da `AlusHost` ostane
**neproziran** (plugin nikada ne dereferencira), a da se ABI širi dodavanjem
članova u `AlusHostApi` čiju veličinu plugin proverava preko `api->size`.
Alternativa — članovi u `AlusHost` — znači da svaki novi poziv menja layout
strukture koju plugin vidi, što je upravo ono što ABI verzija 1 ne sme.

**O2 — plugin ne može da da C++ `Behavior`.** Može da doda **ime** u overlay
listu (`behaviors_prepend`), što ga stavlja u script class chain objekta — i
to je ono za šta NVDA-ovi global plugin-i overlay klase zaista koriste:
da sekcija mape gestova i skup skriptova važe za te objekte i nijedne druge.
Property override-i ostaju u C++-u, u ovom procesu, gde je pogrešan odgovor
bug a ne lom ABI-ja.

**O3 — veto nad tree interceptor-om je jedan, na nivou sloja.** Plan kaže
„blokiraj skup tree interceptor" u app modulu. Prva verzija je instalirala
veto po modulu, sa `processId` u closure-u; to curi po jedan closure na svaki
pokrenut Electron proces i nadživi modul koji ga je postavio. Zamenjeno
`Module::blocksTreeInterceptor()` i **jednim** vetom u `app::Service` koji
pita modul vlasnika objekta.

**O4 — četvrti sloj globalne mape gestova.** Tri postojeća sloja (user,
locale, braille driver) su zauzeta, a ključevi app modula i plugin-a moraju
biti **niži prioritet od korisnikovog `gestures.ini`** i **viši od generisanog
builtin-a**. Dodat je `GestureManager::appMap()` na to mesto. Alternativa —
ubacivanje u builtin mapu — učinila bi da korisnik ne može da rebind-uje
ključ app modula, što je mehanizam 4 iz §12.2.

**O5 — `atomicProfileSwitch` je RAII, ne callback.** Koreografija iz §12.1
(`loseFocus` → trigeri → `gainFocus`) mora da se **izvrši unutra**, a ne da se
preda kao funkcija, jer modul u `appGainFocus()` legitimno menja konfiguraciju.
`Manager::AtomicSwitch` je reentrantan i notifikuje najviše jednom.

**O6 — projekat sada deklariše i jezik C, i instalira jedan header.**
`project('alus', 'cpp', 'c')`. Nijedna linija ALUS-a nije u C-u; C postoji da
bi se četiri test plugin-a prevela **kao C, protiv samo
`alus/alus-plugin.h`**. Time obećanje „add-on treba samo C prevodilac i jedan
header" postaje svojstvo build-a a ne tvrdnja u komentaru.

Uz to, `install_headers('include/alus/alus-plugin.h')`: header koji se ne
instalira čini to obećanje neodrživim. Proveren je i tako — plugin se prevodi
protiv **instaliranog** header-a, bez izvornog drveta:

```
$ meson install -C build --destdir /tmp/stage
$ cc -shared -fPIC -std=c11 -Wall -Wextra \
     -I /tmp/stage/usr/local/include -o ext.so ext.c
$ nm -D --defined-only ext.so | grep alus_plugin_v1
000000000000110c T alus_plugin_v1
```

Nijedan drugi header se ne instalira, i to je odluka: ostatak `include/alus/`
je unutrašnji, C++20, i menja se sa svakom fazom. Instalirati ga značilo bi
pretvoriti svaku unutrašnju odluku u obavezu kompatibilnosti.

**O7 — generički modul postoji za svaku aplikaciju.** Plan to ne kaže
eksplicitno; NVDA to radi i mora se raditi, jer profil trigger (`app:<ime>`),
sleep mode i `statusBar` svi zavise od toga da modul **postoji**, a 95%
aplikacija nema svoje pravilo. `Module::isGeneric()` ih razlikuje.

**O8 — `statusBar`/`getStatusBarText` imaju pravu podrazumevanu
implementaciju,** NVDA-ovo pravilo (poslednje dete prozora sa rolom
`StatusBar`, tekst = imena njegove dece), a ne „vrati ništa". Uz to je
`reportStatusLine` iz kataloga ožičen, pa `alus+end` radi — i to je prvi
mehanizam 5 koji korisnik čuje.

**O9 — treći pritisak `alus+end` ne kopira u clipboard.** Clipboard je posao
Faze 9 (traži Wayland data source). Rečeno naglas, kao i u
`a11y/commands.cpp` i `nav/text_commands.cpp`, a ne prećutano.

**O10 — moduli za Firefox i Chromium nisu napisani,** što plan i predviđa
(„verovatno nije potreban"). Potvrđeno dvaput: Faza 6 je izmerila da jedan
`BrowseMode` pokriva oba, a Electron pravilo **odbija** pregledače po imenu
izvršnog fajla, pa oni dobijaju generički modul i zadržavaju browse mode.

**O11 — LibreOffice modul ne donosi `CompoundDocument`.** Faza 6 je izmerila
da jedan `BrowseMode` pokriva i Writer, jer role mapa sve šest AT-SPI
document rola presliкava na `Role::Document`. Druga kopija tog koda bila bi
druga stvar koja može da se raziđe.

**O12 — `kMaxChildren` je promena u Fazi 4, napravljena u Fazi 8.** Nije
opciono i nije moglo da čeka: vidi N68.

**O13 — moduli za `konsole` i `xfce4-terminal` nisu izmereni.** Nisu
instalirani na mašini. Pravilo je napisano da bude ispravno u oba slučaja: u
procesu koji **jeste** terminal emulator, fokusirani objekat koji izlaže Text
i prijavljuje generičku rolu dobija `Terminal` behavior. Greška u bezbednom
smeru košta jedan behavior više; greška u drugom smeru je terminal koji ne
najavljuje ništa što ispiše, a to korisnik ne može da zaobiđe.

**O14 — VS Code nije instaliran,** pa je Electron modul izmeren samo
negativno: Google Chrome prijavljuje isti toolkit string i mora da dobije
generički modul, što je `alus-cli app-info` potvrdio (`chrome "Google Chrome"
(chromium 1.0) -> generic`). Da pravilo matchuje po toolkit-u a fabrika odbija
pregledač je zato izmereno; da li VS Code zaista zamrzava bez vetoa nije.

**O15 — jedan flake pod valgrind-om, ne iz ove faze.** Pri prvom punom
valgrind prolazu T5.8 (`sayAll().stop()` < 50 ms) izmerio je 54,8 ms, jer je
mašina u tom trenutku zatvarala LibreOffice i Nautilus. Ponovljen prolaz na
praznoj mašini: **12/12 zeleno**. Zabeleženo jer je to merenje zidnog vremena
unutar valgrind-a i biće flake i ubuduće pod teretom.

## Nalazi

**N66 — LibreOffice je `gtk`, ne `gail`.** Plan (§12.1) i prva verzija modula
pretpostavljaju `gail`. Izmereno: `toolkit=gtk 3.24.49`. Da je pravilo bilo
keyed na `(soffice.bin, gail)` ne bi se učitalo nikad; da je bilo keyed samo
na toolkit `gail` uhvatilo bi — ništa, a na `gtk` svaku GTK aplikaciju na
radnoj površini. Zato LibreOffice pravilo gleda **samo izvršni fajl**.

**N67 — polja status bara nemaju ime; sadržaj je u Text interfejsu.** Svih 13
dece status bara su `label` sa praznim `name`, praznim `value` i praznim
`description`. Sadržaj se čita samo kroz `Text`:

```
[1] "Page 1 of 1"   [2] "0 words, 0 characters"   [4] "Default Page Style"
[5] "Serbian Cyrillic (Serbia)"   [12] "100%"   + sedam praznih
```

`Module::statusBarText` (koji spaja imena i vrednosti) vraća **prazan string**
za LibreOffice. Modul zato override-uje i čitanje i preskakanje praznih polja
— bez preskakanja korisnik čuje „Page 1 of 1, , , , 100%".

**N68 — Calc prijavljuje 2147483647 dece, i to je bio zamrzavanje.**
`INT_MAX`, cela adresabilna mreža, ne ćelije u kojima nešto postoji.
`Object::children()` je radio `reserve(count)` i petljao do `count`: jedan
poziv je **32 GB alokacije i dve milijarde D-Bus round-trip-ova**. Čitač
ekrana se tu ne usporava — stane. Dohvat je ograničen na `kMaxChildren = 8192`
u `src/a11y/object.cpp`, sa jednom linijom u logu po ciklusu; `childCount()`
i dalje prijavljuje ono što je toolkit rekao, jer tabela se čita preko `Table`
interfejsa (po koordinati), a lista dece je za korisnički interfejs. Test:
`tests/a11y/test_object.cpp`, „a toolkit that claims two billion children is
capped". **Ovo je defekt Faze 4 koji bi flat screen review u Calc-u zamrzao, i
nijedan test Faze 4–7 ga ne bi našao, jer nijedan mock ne laže o broju dece.**

**N69 — Calc nema `table row`.** Hijerarhija je `table → table cell`. Primer
iz §12.2 („container ćelije je njena tabela, ne red") **nije potreban za
Calc** — roditelj već jeste tabela. Override je zadržan za oblik u kom red
postoji (tabele u Writer-u) i tiho ne odgovara kad ga nema.

**N70 — datoteka u Nautilus-u je `table row`, a ime joj je duplirano.**
Nautilus 48 na GTK 4.18.6: datoteka je `table row` unutar **anonimne** `list`
unutar `tree table`; detalj je u **imenima ćelija**; prva ćelija sadrži ime
reda **dva puta** (`"ALUS ALUS"` za red `"ALUS"`). Tri posledice: pravilo
keyed na `ListItem` (GTK3 pretpostavka iz plana) ne radi ništa; preskakanje
ćelije čije ime je **jednako** imenu reda ne preskače je; i zaglavlje tabele
je takođe `table row`, pa se razlikuje po tome što su mu deca `filler` a ne
`table cell`.

**N71 — pregledač i Electron aplikacija su isti toolkit string.** Google
Chrome: `toolkit=Chromium 1.0`. VS Code prijavljuje isto. Moraju se tretirati
**suprotno**: pregledač zadržava browse mode, Electron aplikacija ne sme da ga
ima. Zato Electron pravilo matchuje **toolkit** (to je polovina T8.1 koju
izvršni fajl ne može), a fabrika **odbija** po imenu izvršnog fajla; tada
nastupa generički modul. Lista pregledača je kratka i to je namerno: nova
Electron aplikacija radi bez izmene, nov pregledač traži jednu liniju.

**N72 — `comm` u `/proc` je skraćen na 15 znakova,** pa `pgrep -x
gnome-terminal-server` ne nalazi ništa (`comm` je `gnome-terminal-`). Ime
izvršnog fajla na koje se pravilo kači **ne može** da se nađe exact match-om
na ime procesa; `tools/phase8-app-check.sh` koristi `pgrep -f` i filtrira po
`readlink /proc/<pid>/exe`.

**N73 — LibreOffice Writer **oglašava** `Collection`; Nautilus na GTK4 ne.**
Izmereno je **tačno jedno**: lista interfejsa.

```
Writer:   document text  [Accessible,Collection,Component,Selection]
Nautilus: nijedan objekat u celom drvetu ne oglašava Collection
```

`phase-6.md` (red 82) kaže kategorično: „LibreOffice ne izlaže `Collection`, pa
`matchesFrom` odgovori prazno i korisnik čuje 'no next heading', što je
**istina**." Prva polovina te tvrdnje je **netačna za ovu verziju**.

Šta ovim merenjem **nije** dokazano: da `GetMatchesFrom` zaista odgovara
korisno. Interfejs koji se oglašava i interfejs koji radi nisu isto, i LibreOffice
je zatvoren pre nego što je taj poziv izmeren. Zato nijedna odluka Faze 6 nije
promenjena — ali zaključak Faze 6 više ne stoji na merenju, nego na
pretpostavci, i to je upisano i u njen rizik: `tools/phase6-browse-check.sh`
treba pokrenuti nad **pokrenutim** Writer-om, a tek rezultat tog poziva kaže
da li quick-nav u Writer-u može da radi.

**N74 — app sloj se budi posle fokusa, i to je morao da uhvati.** Nađeno
pokretanjem demona nad **živom sesijom**, ne testom. Podsistemi se dižu po
redu i ovaj je poslednji, pa je objektni model već uradio `Registry::start()`
— koji nađe početni fokus i pošalje njegov `foreground` događaj kroz lanac u
koji app sloj **još nije ušao**. Simptom: aplikacija u kojoj korisnik **već
jeste** nikada ne dobije svoj app switch — njen profil trigger se ne upali,
`appGainFocus` se ne pozove, i `alus+end` odgovara „no focused application" dok
korisnik ne alt-tab-uje negde i vrati se.

Mera: `app::Service::initialize()` na kraju uzme `api().focus()` i uradi
prebacivanje sam. Ista putanja je i SIGHUP — rekonfigurisan app sloj mora da
stigne na fokus koji **postoji**, ne na sledeći — pa jedan test pokriva oba
(`"the app layer picks up the focus that already exists"`). Posle mere, nad
živom sesijom:

```
DEBUG registry.cpp:214  app switch: (none) -> gjs-console
INFO  service.cpp:190   app layer: 6 module rules, 1 modules running, 0 plugins,
                        focus in gjs-console
DEBUG registry.cpp:214  app switch: gjs-console -> (none)      (pri gašenju)
```

Nijedan test sa mock platformom ovo ne bi našao, jer test fixture pravi servis
pre nego što fokus postoji — što je obrnuto od reda u demonu.

## Predaja Fazi 9

Faza 9 je GUI i lokalizacija. Šta joj Faza 8 ostavlja:

1. **`getAllGestureMappings` je već moguć.** Dijalog gestova iz T9.2 treba
   sve četiri globalne mape plus builtin; četvrta (`appMap`) sadrži ključeve
   app modula i plugin-a sa `info->location` koji je **ime klase koje korisnik
   upisuje u `gestures.ini`** (`appModules.soffice.AppModule`,
   `plugins.<ime>`). Ništa se ne mora dodavati.
2. **Skript bez opisa se već ne vidi** (`ScriptInfo::isVisibleInGestureDialog`),
   a svaki skript koji app modul ili plugin registruje **ima** opis, jer ga
   `register_script` traži.
3. **`alus-cli app-info` je model za GUI stranu „add-on manager".** Isti tri
   koraka: direktorijumi, šta je u njima, šta je markirano za brisanje. GUI
   dodaje samo dugme koje poziva `PluginHost::markForRemoval`.
4. **`[app]` sekcija je mesto za podešavanja po aplikaciji,** jer profil
   trigger `app:<ime>` postoji za svaku aplikaciju (O7). Dijalog „podešavanja
   samo za ovu aplikaciju" je `createProfile` + `mapTrigger`, bez novog koda.
5. **Prevod obuhvata i app module.** Svi stringovi u
   `src/app/modules/*.cpp` nose `// Translators:` komentar na mestu gde se
   izgovaraju, pa ih `xgettext` vidi; `productName()` override-i (`"LibreOffice"`,
   `"Files"`) **nisu** za prevod i nemaju ga.

Tri kriterijuma iz ranijih faza i dalje čekaju okruženje, ne kod:

```bash
# T5.3 — japanska i tajlandska segmentacija
sudo apt install libicu-dev && meson setup --wipe build && meson compile -C build

# T6.7 — milisekunde quick-nav-a; traži pokrenut Firefox ili Chromium
./build/src/alus-cli browse-info
# i, po N73, isto nad pokrenutim LibreOffice Writer-om

# Faza 7, ručni test — traži brajev red ili brltty u virtualnom modu (N64)
brltty -b no -d /dev/null -x no -n &
bash tools/phase7-braille-check.sh --manual
```

## Stanje na kom se nastavlja

```bash
meson setup build && meson compile -C build
meson test -C build                      # 12 svita, zeleno
meson test -C build --suite app          # 439 asertacija — izlazni kriterijum Faze 8
meson test -C build --setup=valgrind --suite core --suite config --suite speech \
         --suite input --suite a11y --suite text --suite nav --suite browsemode \
         --suite braille --suite app
                                         # 12/12, 0 grešaka, 0 izgubljenih bajtova
tools/phase8-app-check.sh                # mašina: ključevi, ABI, plugin direktorijumi
tools/phase8-app-check.sh --manual       # + plan ručnog testa
./build/src/alus-cli app-info            # koji modul dobija svaka pokrenuta aplikacija
```
