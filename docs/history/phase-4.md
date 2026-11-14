# Faza 4 — Objektni model: rezultati

> Izveštaj o izvršenoj fazi. Prati `phase-plan.md` → *Faza 4 — Objektni model*.
> Datum: 2026-11-05. Prethodne faze: 1 (jezgro), 2 (govor), 3 (ulaz), zelene.

## Status: ZAVRŠENA — svih devet kriterijuma prošlo, merenje na živom sistemu izvršeno

```bash
python3 tools/extract-controltypes.py --source ./NVDA   # 156 rola, 53 stanja
meson test -C build --suite a11y          # 98 slučajeva, 686 tvrdnji, zeleno
meson test -C build                       # 7 paketa, sve zeleno
meson test -C build --setup=valgrind --suite a11y --suite core   # 0 grešaka
meson test -C build --suite live          # libatspi protiv prave sesije
tools/phase4-object-check.sh              # šest automatskih koraka, svi prošli
./build/src/alus-cli a11y-info            # „object model: WORKING"
```

Objektni model je napisan, povezan u demon i izmeren na ciljnom sistemu
(Debian 13, GNOME 48.7, Wayland, AT-SPI 2.56.2). Merenje je otkrilo **dva
defekta koja bi čitač ekrana učinila nepotrebno nemim** i jedan koji bi ga
oborio na izlasku; sva tri su ispravljena i svaki ima test. Vidi „Nalazi".

**Ovo je tačka na kojoj ALUS prvi put „radi":** `alus-cli a11y-info` na živoj
sesiji nalazi aktivan prozor, fokusirani objekat i njegov lanac predaka, a
demon sa uključenim objektnim modelom dobija 279 AT-SPI događaja u 10 s i
najavljuje promene fokusa kroz govorni sloj Faze 2.

---

## Izlazni kriterijumi

| Kriterijum | Status | Gde se proverava |
|---|---|---|
| T4.1 — `Object` property-ji za GTK4 dugme | ✅ | `tests/a11y/test_object.cpp` |
| T4.2 — 30 čitanja u ciklusu = 1 batch | ✅ **izmereno** | isto; i uživo (`test_live_a11y.cpp`) |
| T4.3 — 1000 `ChildrenChanged` ⇒ ≤10 po aplikaciji | ✅ | `tests/a11y/test_events.cpp` |
| T4.4 — 20 focus događaja ⇒ 1 (najnoviji) | ✅ | isto |
| T4.5 — `Window:Activate` briše čekajući focus | ✅ | isto |
| T4.6 — svaki `AtspiRole` ima ALUS `Role` | ✅ | `tests/a11y/test_mapping.cpp` |
| T4.7 — `xml-roles=banner` ⇒ landmark; `level=2` ⇒ naslov 2 | ✅ | isto |
| T4.8 — `getFocusDifferenceLevel` | ✅ | `tests/a11y/test_api.cpp` |
| T4.9 — `setFocusObject` odbija mrtav objekat | ✅ | isto |
| Ručni test (alt+tab, tab, alus+tab, numpad navigacija) | ⏳ **čeka čoveka** | `tools/phase4-object-check.sh --manual` |

Ručni test je jedini korak koji mašina ne može da odradi: proverava **šta
korisnik čuje**. Skript ispisuje plan koraka i šta se posle traži u logu.
Sve što se moglo automatizovati — nalaženje fokusa, lanac predaka, navigacija,
aktivacija, govorni tekst za svaki objekat — pokriveno je testovima koji rade
nad mock platformom, plus `live` paketom koji radi nad pravom.

### Mock platforma je na granici `Backend`-a, ne D-Bus server

Plan je tražio „mock AT-SPI server u testovima". `tests/a11y/mock_backend.hpp`
implementira `alus::a11y::Backend` — ceo interfejs preko koga objektni model
vidi AT-SPI (`include/alus/a11y/backend.hpp`). Razlog je merenje, ne lakoća:

* **svaki platformski poziv se broji, po objektu i po property-ju** — bez toga
  T4.2 nije merenje nego tvrdnja o obliku koda; D-Bus mock bi brojao poruke,
  ali ne bi razlikovao promašaj keša od pogotka;
* **objekat se ubija u tačnom trenutku** (`kill()`), a T4.9 i vraćanje na
  stariji focus događaj bez toga nisu testovi nego kockanje;
* **nema sesijske magistrale, registry demona ni radne površine**, pa je paket
  isti na mašini programera i u CI-ju.

Ono što mock ne pokriva — ponašanje `libatspi`-ja, dostavu događaja, keš
biblioteke i to **da li pravi toolkit govori ono što tabele pretpostavljaju** —
pokriva `tests/a11y/test_live_a11y.cpp` (paket `live`) i
`tools/phase4-object-check.sh`. Tri od pet nalaza ove faze dolaze odatle.

---

## Merenje na živom sistemu — IZVRŠENO

`tools/phase4-object-check.sh`, 2026-11-05 u 20:03, GNOME 48.7 / Wayland,
14 aplikacija na magistrali (gnome-shell, Nautilus, Terminal, Chrome, Orca…):

| Šta | Izmereno | Zašto je važno |
|---|---|---|
| `atspi_init` + pretplata | 38 tipova događaja, 0 odbijenih | §6.1 lista prolazi u celosti |
| šest property-ja jednog objekta, hladno | **7 poziva, 448 µs** | cena jednog objekta po ciklusu |
| **60 čitanja istih property-ja, toplo** | **0 poziva, 1 µs** | T4.2 uživo: ceo keš model |
| obilazak 44 objekta, 4 nivoa duboko | 25,4 ms, 1451 poziv (578 µs/objekat) | zato je svaki obilazak ograničen budžetom |
| događaji u 10 s | **279 stiglo, 263 isporučeno** | limiter radi i na pravoj sesiji |
| nalaženje fokusa bez događaja | `focus: Terminal (TERMINAL), 8 predaka` | `alus+tab` odgovara od prve sekunde |
| demon sa objektnim modelom | 34–46 pump ciklusa u 3 s, čisto gašenje | integracija sva četiri podsistema |
| role sa `UNKNOWN` na živoj površini | **0 od 6 prozora** | tabela mapiranja pokriva pravi desktop |

Jedan broj traži pažnju u Fazi 5: **578 µs po objektu** pri obilasku drveta.
To nije keš promašaj nego cena stvaranja objekta (role + atributi + stanja +
deca ≈ 33 poziva). Quick-nav iz §9.2 zato **ne sme** da obilazi drvo objekat
po objekat — `Collection` interfejs postoji upravo zbog toga i Faza 6 ga
koristi, kako je spike S3 i pokazao.

---

## Odstupanja od plana, sa razlogom

### O1 — prezentacija objekata živi u `a11y`, ne u `speech::Presenter`

Plan kaže „`alus::speech::Presenter` (dopuna): `getObjectSpeech`,
`getPropertiesSpeech`, role/state labele". Te funkcije su napisane u celosti,
ali kao `alus::a11y::getObjectSpeech` / `getPropertiesSpeech`
(`src/a11y/object_speech.cpp`), zato što bi dodatak u `speech::Presenter`
značio da govorni sloj zna šta je `a11y::Object` — a objektni model već zna
šta je govor. To je kružna zavisnost između dve biblioteke.

Smer je umesto toga jednosmeran, isto kao u Fazi 3 (ulaz ne linkuje govor):
objektni model govori **samo** kroz `Registry::PresentationHooks`, a jedan
jedini fajl (`installSpeechPresentation`) ih povezuje sa `speech::Presenter`.
Posledica koja se isplatila odmah: svaki test u `tests/a11y` tvrdi **šta bi
bilo izgovoreno**, bez sintetizatora, zvučne kartice i speech-dispatcher-a.

### O2 — `Role`, `State` i platformski enumi se **generišu**

`tools/extract-controltypes.py` (695 linija) čita NVDA-ov
`source/controlTypes/role.py` i `state.py` i `atspi-constants.h`, i generiše
`include/alus/a11y/{role,state,platform}.hpp` + tabele imena i labela. Isti
razlog kao O3 u Fazi 3: 390 imena od kojih svako mora da bude identično
izvoru. Greška u labeli role je reč koju slep korisnik čuje pogrešno.

**Mapiranje se ne generiše** — ono je odluka po roli i stoji u
`src/a11y/role_map.cpp` i `state_map.cpp`, sa razlogom uz svaki netrivijalan
red, a T4.6 pada ako ijedna AT-SPI rola nije pokrivena.

### O3 — labele su kompajlirane, ne podaci

Faza 3 je opise komandi stavila u `data/scripts.csv` (O4 te faze). Role i
stanja idu u kod: 209 kratkih stringova koji su temelj svakog izgovora
(„dugme", „polje za unos", „označeno"). Čitač ekrana koji ne može da izgovori
„dugme" zato što `data/` nije na mestu nije upotrebljiv ni za dijagnostiku.
Faza 9 ih izvlači u gettext katalog, gde im je i mesto.

### O4 — `DispatchContext::globalCommands` je sada **lista**

NVDA ima jedan `GlobalCommands` objekat. ALUS istu komandnu tablu sastavlja po
podsistemu koji je vlasnik komande: Faza 3 dala je `input::InputCommands`
(5 komandi), Faza 4 daje `a11y::ObjectCommands` (13 komandi), a oba odgovaraju
na `globalCommands.GlobalCommands` i time dobijaju NVDA-ove tastere i opise iz
generisane tabele. Deseti korak lookup-a zato prolazi kroz **sve** takve
objekte, u redu registracije. Promena je tri linije u `dispatch.cpp` i nijedna
u ugovoru koji testovi Faze 3 štite.

### O5 — `Role::Application` i `Role::DesktopPane` su **layout**

NVDA-ov lanac predaka fokusa na Windows-u ne sadrži ni radnu površinu ni
objekat aplikacije; na AT-SPI ga sadrži **uvek** (desktop → application →
window → …). Bez ove dve role u listi layout objekata svaka promena fokusa
počinjala bi sa „main desktop pane, Text Editor". Prozor ispod njih kaže sve
što korisniku treba.

### O6 — `positionInfo` ima **listu rola**, jer AT-SPI nema `groupPosition`

NVDA čita `IAccessible2::groupPosition`, koji **toolkit** izračuna i ostavi na
nuli kad ne znači ništa. AT-SPI to ne nudi: postoje samo atributi
`posinset`/`setsize` (koje Gecko i Chromium postavljaju, a GTK ne). Rezervno
računanje iz `indexInParent`/`childCount` zato važi samo za role gde grupna
pozicija nešto znači (stavke liste i drveta, kartice, stavke menija, radio
dugmad, redovi tabele). Bez te liste svako dugme u prozoru bilo bi „20 od 20".

### O7 — limiter nema menu događaje

`EventLimiterConfig::menuNames` je prazna lista. Na Windows-u NVDA od
`EVENT_SYSTEM_MENUSTART` i rodbine zadržava samo poslednji; AT-SPI takve
događaje ne emituje — meni se najavljuje običnim focus događajima na svojim
stavkama, pa taj posao rade pravila za fokus. Zapisano u `events.hpp` da se
ne traži kao propust.

### O8 — objekti žive **ceo ciklus**, i `invalidateAllCaches` je prepravljen

Faza 1 je zapisala da `invalidate()` „ne sme ni da stvara ni da uništava
scope-ove" i zato iterirala registar scope-ova direktno. Faza 4 donosi prvi
legitiman slučaj u kome se to dešava: `a11y::Object` kešira roditelja i decu
kao `shared_ptr` na druge objekte, a svaki od njih ima svoj `CacheScope`.
Dve mere, obe potrebne (vidi N40):

1. `Registry` drži svaki objekat kog je ciklus pomenuo i pušta ga na **početku**
   sledećeg ciklusa (`releaseCycleObjects`), pa invalidacija nikad ne ispušta
   poslednju referencu;
2. `invalidateAllCaches()` iterira **po indeksu** i ponovo čita vektor, pa
   preživljava i slučaj kad se scope ipak uništi (gašenje, test, toolkit koji
   prijavi objekat kao sopstvenog pretka).

### O9 — `pushGrabs()` je idempotentan

Faza 3 je u predaji tražila `refreshGrabs()` pri promeni fokusa (O6 te faze).
Dosledno sprovedeno, to je jedan `SetKeyGrabs` D-Bus poziv i jedna log linija
**po kontroli** pri tab-ovanju kroz dijalog. `pushGrabs()` zato pamti šta je
instalirano i ne šalje nepromenjen skup.

### O10 — `shouldReportTextContent` je `false` do Faze 5

NVDA-ov `getObjectSpeech` za objekat sa navigabilnim tekstom čita **tekst**
umesto vrednosti (`getTextInfoSpeech`). Tekstualni sloj je Faza 5, pa dok ga
nema ALUS izgovara vrednost. Mesto je obeleženo u `object_speech.cpp`.

### O11 — sricanje je `CharacterMode`, ne ceo `speakSpelling`

„Pritisni dvaput da sriče" radi: `PresentationHooks::spell` šalje tekst sa
`CharacterModeCommand` i `SymbolLevel::All`. NVDA-ov `speakSpelling` dodaje
opise znakova i povišen ton za velika slova — to je Faza 5 (ona ima obradu
znakova), pa treći pritisak za sada sriče isto kao drugi.

### O12 — od `review_*` komandi ušla je samo `review_activate`

Ostale traže `TextRange`. `alus+numpadEnter` je u ručnom testu Faze 4, pa je
implementirana: hoda uz drvo dok ne nađe objekat sa akcijom, kako NVDA radi.

### O13 — app moduli i tree interceptor-i su rupe sa kukom

`DispatchContext.appModule` i `.treeInterceptor` se pune iz
`Object::appModule()` i `Object::treeInterceptor()`; prvi pita
`Registry::setAppModuleProvider` (Faza 8), drugi `Object::setTreeInterceptor`
(Faza 6). Do tada su null, `sleepMode` je `Awake`, i lookup to već podnosi —
Faza 3 ga je testirala za svih deset koraka.

### O14 — `properties` u pretplati je kratka lista, i to je merenje (N38)

### O15 — `atspi_exit()` se ne zove

Isti razlog kao N32 u Fazi 3: `libatspi` pri gašenju ruši D-Bus vezu koju
njegova radna nit još koristi. Veza odlazi sa procesom.

### O16 — `cellCoordsText` dolazi samo iz atributa

AT-SPI nema „tekst koordinate ćelije"; LibreOffice ume da ga stavi u atribut
`coords-text`, i to se čita. Inače prezentacioni sloj kaže „red 3 kolona 2",
što je ono što NVDA radi kad toolkit ne nudi svoju formulaciju.

### O17 — izmerene veličine rečnika nisu one iz plana (N42)

---

## Nalazi (nastavak numeracije iz Faze 3)

### N36 — `atspi_set_main_context` **mora** posle `atspi_init`, inače nema ni jednog događaja

Najozbiljniji nalaz faze, zato što **ne proizvodi nikakvu grešku**.
`atspi_init()` na kraju veže magistralu pristupačnosti na
`g_main_context_default()`:

```c
/* at-spi2-core-2.56.2, atspi/atspi-misc.c:1070 */
atspi_dbus_connection_setup_with_g_main (bus, g_main_context_default ());
```

Kontekst postavljen **pre** `atspi_init`-a se time tiho odbacuje. Posledica
izmerena na živom sistemu: pretplata uspeva na svih 38 tipova, `a11y-info`
prijavljuje da sve radi, a u 3 s stigne **0 događaja**. Čitač ekrana je nem i
ništa u logu to ne kaže.

Ispravka je redosled (`atspi_backend.cpp::initialize`): `set_timeout`,
`atspi_init`, pa `set_main_context`. Isti fajl (`atspi-misc.c:374`) veže i
svaku kasnije otvorenu vezu ka aplikaciji na **podrazumevani** kontekst, što
jedan poziv ne može da sustigne — zato demonska petlja radi na podrazumevanom
kontekstu (Faza 1 je `LoopConfig::context` ostavila null baš zbog toga), a
privatni kontekst prosleđuju samo testovi, svesno.

Posle ispravke: **279 događaja u 10 s** na istoj sesiji.

### N37 — prazan AT-SPI state set znači „ne prijavljujem stanja", ne „sve je netačno"

Izmereno (`python3 -c 'Atspi...get_state_set()'`):

```
desktop frame  []
application    []                                        ← sve aplikacije
window         [enabled focusable focused sensitive showing visible]
```

Objekat radne površine i **svi** objekti aplikacija prijavljuju prazan skup, a
svaki pravi widget pun. Inverzije iz `state_map.cpp` (odsustvo `ENABLED` ⇒
`UNAVAILABLE`, `VISIBLE` ⇒ `INVISIBLE`, `SHOWING` ⇒ `OFFSCREEN`) su zato
uslovljene nepraznim skupom. Bez toga `alus-cli a11y-tree` prijavljuje celu
radnu površinu kao nedostupnu — tako je i otkriveno.

### N38 — AT-SPI uz događaj može da pošalje samo **property-je**, i to samo za trajanje callback-a

§6.1 traži `RegisterEvent(event, properties, …)`. Mera je manja nego što plan
pretpostavlja, iz dva razloga u izvornom kodu:

* aplikacijska strana nudi kao property-je samo `Name`, `Description`,
  `Locale`, `Parent`, `ChildCount`, `Attributes`, `AccessibleId`, `HelpText`
  (+ `Component.ScreenExtents` i slično po interfejsu) —
  `atk-adaptor/adaptors/accessible-adaptor.c:608`. **`Role` i `State` su
  metode (`GetRole`, `GetState`), ne property-ji, i ne mogu da putuju sa
  događajem**;
* `libatspi` od poslatog u svoj keš stavlja samo `interfaces`, `Attributes` i
  `Component.ScreenExtents` (`atspi-misc.c:2059`), i to **važi samo dok traje
  callback** (`atspi-event-listener.c:584`).

Zato je lista kratka (`Attributes`, `Component.ScreenExtents`), a ono što se
njome dobija uzima se **u samom callback-u**, i to samo za focus događaje
(`Registry::primeFromEvent`) — tamo gde živi budžet latencije. Sve ostalo nosi
`Cached<T>`, koji je i tako pravi odgovor (T4.2: 0 poziva na 60 čitanja).

### N39 — AT-SPI nema `groupPosition`

Vidi O6. Posledica je lista rola; bez nje svako dugme dobija „20 od 20".

### N40 — keš koji drži `shared_ptr` na vlasnike keša obara ugovor Faze 1

`Object` kešira roditelja i decu kao `ObjectPtr`. Dve stvari su ispale iz toga,
obe izmerene na pravom sistemu i obe sa testom:

1. **Uništavanje scope-a tokom `invalidateAllCaches()`** — iteracija
   `for (CacheScope* scope : registry())` nad vektorom koji se menja. Ispravka:
   iteracija po indeksu + objekti koji žive ceo ciklus (O8).
2. **Prsten `roditelj ↔ deca` preživi gašenje.** `Registry::stop()` je puštao
   svoje reference, a prsten je ostajao živ kao smeće — **sa visećom
   referencom na uništeni `Registry` u sebi**, jer `~Object` traži od registra
   backend da vrati platformsku referencu. To je use-after-free sa odloženim
   dejstvom, u demonu pri izlasku. Ispravka: `Registry::releaseObjects()`
   prvo invalidira keš svakog živog objekta (preseče prsten), pa pusti.

### N44 — referenca unutar keširane strukture je referenca koju nema kome da vrati

Nađeno pregledom koda, ne testom, i zato zapisano: `TableCell::get_table` vraća
**novu referencu**, a prvi oblik `CellInfo` ju je nosio kao `Ref` polje. Pošto
je `CellInfo` keširan po ciklusu, nijedan vlasnik je nije puštao — korisnik
koji pređe preko sto ćelija izgubi sto accessible-a. Mock to ne vidi (njegove
reference su brojač koji test sam postavlja), a valgrind ne vidi reference
`libatspi`-ja.

Mera je pravilo, ne zakrpa: **u strukturama koje `Backend` vraća nema
referenci.** Tabela ćelije se traži posebno (`Backend::tableOf`) i postaje
`ObjectPtr`, koji je vlasnik za trajanje ciklusa. `tableId()` je i dalje samo
identitet za poređenje („ista tabela"), ali sad iza njega stoji živ objekat.

### N41 — ime toolkit-a nije kanonski napisano

Na istoj sesiji: `gtk` (gsd-*, Terminal, Orca), `GTK` (Nautilus), `clutter`
(gnome-shell), `Chromium` (Chrome). Pošto je `(toolkit_name, AtspiRole)`
**primarni ključ** za izbor overlay behavior-a i app modula (§6.2), poređenje
mora da ignoriše veličinu slova. Zapisano za Fazu 8, koja piše app module.

### N42 — AT-SPI 2.56.2 ima 131 rolu i 44 stanja, ne 133 i 45

`ATSPI_ROLE_COUNT` je `(130 + 1)`. Brojevi iz plana (§6.5) uključuju
`ATSPI_ROLE_LAST_DEFINED` i alias `ATSPI_ROLE_PUSH_BUTTON = ATSPI_ROLE_BUTTON`,
što nisu role. Isto za stanja (`ATSPI_STATE_LAST_DEFINED`). NVDA-ova strana je
tačna: **156 rola i 53 stanja**. Mapiranje je dakle 131 → 156 i 44 → 53, a
`test_mapping.cpp` te brojeve tvrdi, pa promena u AT-SPI pada kao test.

### N43 — platformski događaj mora da **traži pump**

`EventLimiter::addEvent` samo primi događaj; ciklus ne nastaje sam. Bez
`loop_.requestPump()` u `Registry::onPlatformEvent` demon je u 3 s imao
**4 pump ciklusa** (samo tajmeri), a sa njim **46**. Promena fokusa mišem bila
bi najavljena kad se nešto drugo desi — ili nikad. Invarijanta 1 iz §3.1,
sprovedena: focus traži `immediate`, ostalo grupisani pump.

---

## Šta je napravljeno

| Modul | Linija | Napomena |
|---|---|---|
| `alus::a11y::Backend` (interfejs) | 292 | granica platforme; ugovor greške („mrtav objekat nije izuzetak") |
| `alus::a11y::AtspiBackend` | 148 + 1055 | `libatspi`, ograničen timeout, naš kontekst (N36), 38 pretplata |
| `alus::a11y::Object` | 278 + 867 | ceo property set §6.2, sve `Cached<T>`, dispatch kroz behavior-e |
| `alus::a11y::Registry` | 222 + 464 | pretplata, limiter, prevod imena, keš objekata, `doPreGainFocus` |
| `alus::a11y::Api` | 103 + 201 | focus + lanac predaka, `focusDifferenceLevel`, navigator, bool setteri |
| `alus::a11y::OverlayRegistry`, `Behavior` | 93 + 110 | tri faze izbora, `remove`/`prepend` kao NVDA-ov `clsList` |
| `alus::a11y::behaviors::*` | 100 + 379 | ProgressBar, Dialog, FocusableUnfocusableContainer, ToolTip, Notification |
| mapiranje role i stanja | 122 + 820 | 131→156, 44→53, profinjavanje preko `xml-roles`/`level`/`tag` |
| `Role`, `State`, `PlatformRole/State/Relation` | 598 | **generisano** iz NVDA i `atspi-constants.h` |
| `alus::a11y::getObjectSpeech` i rodbina | 165 + 762 | `getPropertiesSpeech` red po redu, `processAndLabelStates` |
| `alus::a11y::ObjectCommands` | 70 + 300 | 13 komandi objektne navigacije, NVDA-ovi tasteri i opisi |
| `alus::a11y::Settings` | 92 + 134 | NVDA-ovi ključevi i podrazumevane vrednosti |
| `alus::a11y::Service` | 127 + 222 | restartabilan podsistem; spaja govor, ulaz i jezgro |
| `alus-cli a11y-info`, `a11y-tree` | 130 | dijagnostika koja samo čita |
| `tools/extract-controltypes.py` | 695 | generator tabela |
| `tools/phase4-object-check.sh` | 146 | merenje na živom sistemu + plan ručnog testa |
| mock platforma | 563 | `Backend` nad ručno sastavljenim drvetom, sa brojačima |
| testovi | 3236 | 98 slučajeva, 686 tvrdnji, + `live` paket |

Ukupno: **8782 linije** koda i zaglavlja, **3798** testova i mock-a.

Promene u ranijim fazama, sve male i sve objašnjene gore: `DispatchContext`
(O4), `ScriptableObject::setScriptClasses` (lanac klasa zavisi od izabranih
behavior-a), `input::Service::setContextContributor` (predaja Faze 3, tačka 2),
`invalidateAllCaches` (O8), `pushGrabs` (O9), demon (`--no-a11y`, dve kuke).

---

## Predaja Fazi 5

1. **`Object::hasNavigableText()` i `isTextEmpty()` postoje i čekaju
   `TextRange`.** `getObjectSpeech` ima `shouldReportTextContent = false` na
   jednom mestu (O10); kad Faza 5 doda tekstualni sloj, to postaje prava
   provera i objekti sa tekstom počinju da čitaju tekst umesto vrednosti.
2. **`PresentationHooks::spell` je privremeno** (O11). Faza 5 ga zamenjuje
   pravim `speakSpelling` putem (opisi znakova, ton za velika slova).
3. **Review pozicija je trenutno navigator objekat.** `Api::navigator()` pada
   na fokus kad nije postavljen; `setNavigatorObject` već emituje
   `becomeNavigatorObject` sa `isFocus`, što review kursoru treba da bi pratio
   (ili ne pratio) fokus.
4. **578 µs po objektu pri obilasku drveta** (merenje gore). Say-all i review
   po paragrafima ne smeju da obilaze drvo objekat po objekat.
5. **`isSayAllRunning` kuka u `input::GestureManager::Hooks` je još prazna** —
   jedina preostala iz predaje Faze 3.
6. **Događaji koje Faza 4 dispatch-uje a nijedan objekat još ne koristi**:
   `caret`, `textChange`, `textSelectionChange`, `childrenAdded/Removed`,
   `documentLoadComplete`, `pageChange`, `reorder`, `mouseButton`. Imena su
   fiksirana u `events.hpp`, prevod sa AT-SPI testiran; Faza 5 i 6 samo
   dodaju rukovaoce.
