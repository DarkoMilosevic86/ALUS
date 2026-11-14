# Faza 6 — browse mode

Izveštaj o izvršenoj fazi. Status svakog izlaznog kriterijuma, odstupanja od
plana sa razlogom, i nalazi koje je faza proizvela.

**Jedna komanda:**

```bash
meson test -C build --suite browsemode
```

Rezultat: **393 asertacije, 14 test slučajeva, zeleno.** Pod valgrind-om: 0
grešaka, 0 izgubljenih bajtova.

## Status izlaznih kriterijuma

| Kriterijum | Status | Gde se meri |
|---|---|---|
| T6.1 — `ControlFieldSynth`: GTK/web objekat ⇒ `ControlField` sa tačnim role/states/name/level | **prošao** | `[T6.1]`, 8 sekcija |
| T6.2 — `AncestorChainDiff`: ugnježdena lista u tabeli ⇒ tačan `controlStart`/`controlEnd` tok | **prošao** | `[T6.2]`, 5 sekcija |
| T6.3 — `commonFieldCount` kontinuitet: 100 linija ne ponavlja „lista sa 5 stavki" | **prošao** | `[T6.3]`, mereno na 100 stavki |
| T6.4 — `uniqueID` stabilan preko AT-SPI (bus name, object path) | **prošao** | `[T6.4]`, 4 sekcije |
| T6.5 — QuickNav: svaki tip iz §9.2 nađe tačan element | **prošao** | `[T6.5]`, 22+ pravila i 9 pretraga |
| T6.6 — `sortBy=FLOW` daje redosled **čitanja**, ne drveta | **prošao** | `[T6.6]`, lebdeća traka |
| T6.7 — quick-nav latencija p95 < 20 ms | **prošao** | `[T6.7]` hermetično meri **cenu poziva**; milisekunde izmerene u Chrome 157 (7. 11. 2026.) i Firefox/Gecko 153.6 (8. 11. 2026.): p95 **0,11 ms** u oba (O11). Merenje je otkrilo četiri defekta, N88–N90 i N94. |
| T6.8 — auto focus mode: tab u `<input>` ⇒ focus mode; escape ⇒ browse mode | **prošao** | `[T6.8]`, 3 sekcije |
| T6.9 — single-letter nav ne radi u focus mode-u | **prošao** | `[T6.9]`, preko `passThrough` i grabova (N61) |
| T6.10 — table nav: `control+alt+strelice`, zaglavlja jednom | **prošao** | `[T6.10]`, 4 sekcije |

Pored toga, zatvorene su **sve sedam obaveza** koje je Faza 5 ostavila
(`phase-5.md`, „Predaja Fazi 6"):

| Obaveza Faze 5 | Gde je zatvorena |
|---|---|
| 1. `getTextWithFields` nad drvetom dokumenta | `nav::AncestorChainDiff`, `DocumentTextRange::getTextWithFields` |
| 2. `getControlFieldSpeech` je rupa koja čeka (O14) | `a11y::getControlFieldSpeech`, 300 linija, + ožičenje u `getTextRangeSpeech` |
| 3. `Review::documentPosition()` vraća null | vraća root proxy opseg; „Document review" se pojavljuje u ciklusu modova |
| 4. U+FFFC se ne razrešava (N47) | `AncestorChainDiff::enter` postavlja `content` = ime objekta |
| 5. `setTreeInterceptor` i `passThrough` su kuke koje se ne pune | `TreeInterceptorRegistry` ih puni; `BrowseMode::setPassThrough` ih menja |
| 6. Quick-nav **ne sme** da koristi `OffsetsTextRange` | `QuickNav` ide isključivo preko `Collection`; nijedan offset se ne računa |
| 7. Pravilo N49 važi i dalje | kursor je `text::Bookmark`, nikad opseg; valgrind: 0 izgubljenih bajtova |

## Šta je izgrađeno

| Modul | Linija | Napomena |
|---|---|---|
| `alus::nav::ControlFieldSynth` + `normaliseControlField` | 327 + 107 | port `compoundDocuments._getControlFieldForObject` i koncepta `_normalizeControlField` |
| `alus::nav::DocumentLeaves` | u `document_range` | obilazak listova dokumenta; osnov svega ostalog |
| `alus::nav::AncestorChainDiff` | u `document_range` | **port `TreeCompoundTextInfo.getTextWithFields`** — diff lanaca predaka |
| `alus::nav::DocumentTextRange` | 961 + 231 | compound opseg; pozicija je (list, bajt offset) |
| `alus::nav::TreeInterceptor` + `TreeInterceptorRegistry` | 258 + 160 | životni ciklus, `isReady`, `passThrough`, `RootProxyTextRange` |
| `alus::nav::CursorManager` | 261 + 132 | virtualni caret, anchor logika selekcije, pretraga |
| `alus::nav::QuickNav` | 530 + 146 | tabela §9.2: 37 tipova, 22+ `MatchRule` |
| `alus::nav::BrowseMode` | 760 + 163 | `passThrough`, auto focus mode, elements list, tabela navigacija |
| `a11y::getControlFieldSpeech` | ~300 u `text_speech.cpp` | port `speech.getControlFieldSpeech` |
| `a11y::Backend::matchesFrom` / `matches` | u `backend`/`atspi_backend` | `Collection` kao platformska granica (O13) |
| testovi | 937 + 178 | T6.1–T6.10 nad mock platformom koja **stvarno** odgovara `Collection` |

## Odstupanja od plana

### O1 — `AtspiDocumentTextRange : OffsetsTextRange` nije moguć; opseg je compound

§9.4 propisuje opseg nad **korenom dokumenta** kao `OffsetsTextRange`, na
osnovu tvrdnje §9.1 da „Gecko i Chromium izlažu telo dokumenta kao
`AtspiText`". **Faza 5 je tu tvrdnju izmerila i ne važi na ovoj radnoj
površini:**

- **N47**: tekst fokusiranog Chrome dokumenta je `￼￼￼` — tri znaka, sva tri
  U+FFFC. Koren **ne sadrži telo**, sadrži listu svoje dece.
- **N45**: Chromium ne odgovara ni `LINE` ni `WORD` ni `CHARACTER`
  granularnost, pa bi svaka granica linije nad takvim korenom bila pretraga
  kroz ceo tekst — preko D-Bus-a, po pritisku tastera.

Zato je opseg **compound**: pozicija je `(list, bajt offset u tom listu)`, a
rad po jedinicama se delegira na `a11y::ObjectTextRange`, koji je Faza 5 već
izmerila nad četiri toolkit-a. To je NVDA-in sopstveni `TreeCompoundTextInfo`
dizajn.

**Posledica je dobra vest:** §9.4 traži dve klase —
`GeckoChromiumDocument` za pregledače i `CompoundDocument` za Writer. **One
su jedna klasa**, jer je opseg compound za oba, a jedina prava razlika je
quick-nav — i ta razlika ne traži ni liniju koda: LibreOffice ne izlaže
`Collection`, pa `matchesFrom` odgovori prazno i korisnik čuje „no next
heading", što je **istina**.

### O2 — `getTextWithFields` radi pravi diff; NVDA-in compound ne radi

NVDA-in `TreeCompoundTextInfo.getTextWithFields` (`:331-368`) emituje **samo
početni lanac** predaka plus ugnježdene objekte. Između dva uzastopna lista
ne emituje ništa — pa se u NVDA-inom compound dokumentu prelaz iz jedne liste
u drugu ne najavljuje.

§9.3 traži **diff lanaca predaka uzastopnih leaf TextInfo-a**, i to je ono što
je ALUS uradio. Rezultat je tok koji je **uravnotežen** (`controlStart` ==
`controlEnd`, mereno u T6.2) i koji odgovara onome što virtualni baferi
proizvode — dakle bolje od izvora porta, i to je bio zahtev plana.

### O3 — AT-SPI nema `LINKED` stanje, pa NVDA-in test za named link ne radi

NVDA prepoznaje `<a name="x">` (odredište, ne link) testom
`State.LINKED not in states`. **AT-SPI to stanje ne postoji**: ALUS ga
sintetiše iz same `Link` uloge (`src/a11y/state_map.cpp:160`), pa bi test
uvek bio netačan i funkcija bi bila mrtav kod.

Test je zamenjen onim što na AT-SPI stvarno razlikuje to dvoje: **nema šta da
se aktivira** — ni akcije, ni fokusabilnosti. To je i razlika koju korisnik
može da čuje: link najavljen za kotvu ga pošalje da traži nešto što se ne
može pritisnuti. Vidi N53.

### O4 — AT-SPI nema `clickable` atribut; pravilo je `Action` interfejs

NVDA-ov `nextClickable` čita atribut `clickable` koji njegov sopstveni
in-process bafer sintetiše iz DOM event handler-a. AT-SPI takav atribut ne
izlaže, a `a11y::State::Clickable` je ALUS-ovo izvedeno stanje (iz uloge), pa
ni ono ne može da bude `MatchRule`.

Ono što AT-SPI **izlaže** je `Action` interfejs, a element sa akcijom **jeste**
element koji korisnik može da aktivira. To je uže od NVDA (div sa `onclick` i
bez akcije se promaši) i **nikad netačno** — što je ispravan smer za
navigacioni taster. Vidi N54.

### O5 — pet NVDA klasa je jedan objekat sa pet imena

NVDA-ine `BrowseModeTreeInterceptor`, `BrowseModeDocumentTreeInterceptor`,
`CursorManager`, `DocumentWithTableNavigation` i `TextInfoTreeInterceptor` su
Python nasledni lanac, **ne pet objekata**. `script::ScriptableObject` je ono
što lookup nalazi i što sekcija gesture mape adresira, pa je `BrowseMode`
jedan objekat koji **odgovara na svih pet imena** (`scriptClasses()`).

Zato generisana komandna tabela važi bez ijedne pretipkane linije: 110
`browseMode` redova, 35 `cursorManager`, 13 `documentBase`.

### O6 — quick-nav i kursor skriptovi se vezuju iz tabele, u petlji

37 tipova × 2 smera = **74 skripta**, plus 24 kursorska. Ispisati ih je 98
prilika da se veže pogrešan taster, a katalog ih već sve zna. Zato
`bindQuickNavScripts` i `bindCursorScripts` čitaju tabelu i zovu
`bindScript(ime, lambda)`.

To je §5.4 („ekstrakcija komandne tabele — automatski, ne rukom") primenjeno
na Fazu 6.

### O7 — `FieldNormalizer` nije klasa nego funkcija

Plan imenuje modul `alus::nav::FieldNormalizer`. Pošto sintetizator već
proizvodi kanonske ključeve, normalizator nema stanje — pa je slobodna
funkcija `normaliseControlField(ControlField&)` koju **sintetizator zove nad
sopstvenim izlazom.** To je ono što „jedno mesto koje odlučuje" iz §9.3 čini
istinom a ne željom, i ostavlja drugog pozivaoca za Fazu 8 (app modul koji
donosi svoje atribute).

### O8 — `isBlock` dolazi iz uloge, jer `display: block` ne postoji na AT-SPI

NVDA-in `isBlock` dolazi iz atributa koji njegov bafer čita iz DOM-a. AT-SPI
nema takav atribut. Pošteni odgovor je **uloga**, i to je odgovor koji dva
pozivaoca i traže: `roleText` polje se čita kao kontejner a ne kao jedna
linija, i say-all cepa iskaz na granici bloka.

### O9 — `table-id` je platformska referenca tabele, ne `1 # FIXME`

NVDA-in `_getControlFieldForObject` ima doslovno `field["table-id"] = 1  #
FIXME` — jednu konstantu za svaku tabelu u dokumentu, pa prezentacioni sloj ne
može da razlikuje dve tabele jednu za drugom. ALUS koristi `Ref` tabele, koja
**jeste** identitet (§6.2). Zato „izlazim iz tabele, ulazim u tabelu" radi.

### O10 — tri dijaloga su Faza 9; tasteri to **kažu**

Dijalog za pretragu (`alus+control+f`), elements list dijalog (`alus+f7`) i
clipboard (`control+c`) traže GUI i Wayland data source — Faza 9. Ono što
Faza 6 duguje je da **sadržaj** bude tačan, i to je ono što testovi mere:
`elementsList()` vraća listu, `cursor_.selection()` vraća opseg.

Tasteri do tada **izgovore da ih nema** umesto da ne urade ništa. To je
razlika između nedovršene i pokvarene funkcije, i razlika koju slep korisnik
može da uoči.

### O11 — T6.7 meri **cenu poziva**; milisekunde su izmerene kasnije

Kriterijum traži p95 < 20 ms u Firefox-u i Chromium-u. Hermetičan test ne može
da izmeri milisekunde pregledača koji ne radi; ono što **može** da izmeri je
ono što te milisekunde omogućava: **jedan D-Bus poziv po pritisku tastera.**
Mereno: 1 poziv za jedan pritisak, 101 poziv za 100 pritisaka.

**Izmereno 7. 11. 2026.**, Google Chrome 157.0.8103.38, AT-SPI 2.56.2, nad
člankom „КК Вршац" na Википедији (333 linka, 11 naslova, 5 tabela):

| put | nađeno | p95 | kriterijum |
|---|---|---|---|
| `matchesFrom`, `count=1` — jedan pritisak `k` | 1 | **0,11 ms** | prošao (< 20 ms) |
| `matchesFrom`, `count=1` — jedan pritisak `h` | 1 | **0,15 ms** | prošao |
| `matchesFrom`, `count=1` — jedan pritisak `t` | 1 | **0,23 ms** | prošao |
| `matches` — cela lista linkova | 333 | **2,03 ms** | prošao |
| `matches` — cela lista naslova | 11 | **1,82 ms** | prošao |

Isto, kroz `alus-cli browse-info` nad istim člankom (435 listova, 12 561 bajt
teksta, polja uravnotežena 699/699), svih osam tipova iz §9.2:

| taster | tip | nađeno | p95 |
|---|---|---|---|
| `h` | naslov | 11 | 1,91 ms |
| `k` | link | 20 (granica alata) | 0,29 ms |
| `f` | polje obrasca | 19 | 3,24 ms |
| `l` | lista | 20 (granica) | 1,24 ms |
| `t` | tabela | 5 | 1,12 ms |
| `d` | landmark | 11 | 1,35 ms |
| `b` | dugme | 10 | 1,13 ms |
| `g` | grafika | 20 (granica) | 0,83 ms |

Rezerva je oko stostruka, i to je očekivano: `Collection` radi u procesu
pregledača, pa jedan poziv zamenjuje hiljade.

**Merenje nije potvrdilo plan — oborilo ga je.** Prvi pokušaj je našao **0
elemenata svakog tipa**, a ne 333. Tri odvojena defekta, svaki dovoljan sam za
sebe da quick navigation ne nađe ništa ni na jednoj stvarnoj veb strani:
N88 (`traverse`), N89 (`MATCH_INVALID`), N90 (`FLOW` u `GetMatches`). Svih 397
podtestova browse mode-a je pritom prolazilo. Zašto — N91.

Merenje se ponavlja ovako, i sada je **test**, a ne ručni korak:

```bash
meson test -C build --suite live            # a11y-live sadrži T6.7
./build/tests/test-a11y-live "[T6.7]"       # samo ovaj kriterijum
```

Test pristojno preskače kad nema otvorene stranice, i **pada** kad je stranica
otvorena a Collection ne nalazi ništa. Razlika je N91.

### O12 — `alus+f5` nije vezan

§9.1: `script_refreshBuffer` nema smisla kad bafer održava pregledač. Nije
vezan, i to je odluka a ne propust.

### O13 — `Backend` je dobio `Collection`; plan to nije naveo

Plan nabraja module Faze 6 ali ne pominje izmenu platformske granice.
`matchesFrom` i `matches` su dodati u `a11y::Backend`, zajedno sa
`MatchRule`, `MatchType`, `SortOrder` i `TreeTraversal`. Bez toga §9.2 nema
gde da se izvrši.

Jedna odluka u toj implementaciji je vredna reči: **koren bez `Collection`
interfejsa vraća prazno, ne obilazak drveta.** Obilazak bi bio „radi uvek", ali
bi cena bila hiljadu D-Bus poziva po tasteru — tačno ono čega se §9.1 oslobodio.

### O14 — mock platforma implementira `Collection` **stvarno**

Stub koji uvek odgovara „nema poklapanja" pustio bi da svih 22 pravila §9.2
budu pogrešna a da test to ne primeti. Zato `tests/a11y/mock_backend.hpp`
evaluira pravilo sa AT-SPI ANY/ALL/NONE semantikom nad podrvetom spljoštenim u
redosledu dokumenta, plus `MockNode::flowIndex` — čime FLOW **može** da se
razlikuje od CANONICAL i T6.6 postaje merenje a ne tvrdnja.

### O15 — ćelijske koordinate su 1-bazirane na izvoru

AT-SPI računa redove i kolone od 0; svaka reč koju korisnik čuje računa od 1.
Pretvaranje je u `ControlFieldSynth`, jednom, a ne u prezentaciji na tri mesta.

### O16 — uloga naslova u `ControlField`-u je `Heading1`, ne `Heading`

§6.5 (atributsko profinjenje) pretvori naslov sa `level=1` u sopstvenu ulogu
**pre** nego što ga kontrolno polje vidi. To je ispravno — prezentacija treba
da kaže „heading level 1" — ali je iznenađenje za svakog ko piše test ili
pravilo. Quick-nav pravila su netaknuta, jer `MatchRule` koristi
`PlatformRole` (AT-SPI-jevu), ne ALUS-ovu profinjenu. Vidi N59.

## Nalazi

### N52 — #12808 garda je u compound opsegu potrebna **dvaput**

Faza 5 ima gardu za „neke implementacije se pomere jednu jedinicu dalje nego
što treba i slete na ekskluzivni kraj teksta". U compound opsegu to nije
kozmetika nego **jedini način da se pređe u sledeći list**.

`OffsetsTextRange::move(Line, 1)` nad poslednjom linijom lista **vrati uspeh** i
slete na `storyLength()` — gde nema jedinice. Compound opseg koji to prihvati
zalepi se na kraj **svakog paragrafa** i odgovori „edge of document" u sredini
strane.

Mereno: pre garde je `downArrow` sa naslova odgovorio praznu liniju umesto
sledećeg paragrafa; posle garde test `the document range crosses leaves` prolazi
sa 30 asertacija. Garda je: ako je pomeraj naprednji i novi offset ≥ dužina
lista, **pomeraja nije bilo** — prelazi se u sledeći list.

Ovo je najverovatniji izvor tihih bugova u celoj Fazi 6, i zato je zapisan prvi.

### N53 — AT-SPI nema `LINKED`; ALUS ga sintetiše iz uloge

`src/a11y/state_map.cpp:160` dodaje `State::Linked` svakom objektu sa `Link`
ulogom, jer prezentacioni sloj to stanje traži. Posledica: **svaki test koji
pita `has(State::Linked)` da bi razlikovao linkove je tautologija.** Vidi O3.

Ovo važi i za Fazu 7 (brajev red) — ako brajev red razlikuje posećene linkove
po `LINKED`, razlikovaće ih netačno.

### N54 — AT-SPI nema `clickable`, i to nije rupa u ALUS-u nego u platformi

Nijedan atribut i nijedno stanje na AT-SPI ne kaže „ovaj element ima DOM
`onclick`". NVDA to zna jer mu kod radi **u procesu pregledača**. Na Linuxu je
najbliža istina `Action` interfejs. Zapisano da se ne traži kao regresija.

### N55 — stek lanca predaka mora da prati lanac **uključujući elemente bez polja**

Prva verzija `AncestorChainDiff` je u stek stavljala samo one pretke koji
proizvedu polje (paragraf i named link destination ne proizvode). Poređenje
`open_[i].object` sa `chain[i]` je onda **tiho skliznulo** čim dokument sadrži
paragraf — a simptom je `controlEnd` za pogrešnu kontrolu, dakle korisnik
kome se kaže da je izašao iz tabele u kojoj još stoji.

Nađeno čitanjem koda pre nego što je test postojao; mera je `OpenField::emitted`
i stek koji prati lanac jedan-za-jedan. **Ovo je bug koji ne pada, ne loguje i
ne curi** — samo laže, i zato je klasa izdvojena da bi T6.2 mogao da je gleda
direktno.

### N56 — kategorija prezentacije ćelije zavisi od lanca predaka

`ControlField::getPresentationCategory` vrati `Cell` samo kad je **tabela u
lancu predaka** (`textInfos/__init__.py:146`); sa praznim lancem ista ćelija je
`Layout` i **ne kaže ništa**.

To je pravilo koje layout tabelu čini tihom, i to je razlog zašto ugnježđenost
toka iz T6.2 **nosi teret** a nije dekoracija: tok sa pogrešnim ugnježđenjem
prođe svaki test o tekstu i ućuti svaku tabelu.

Nađeno tako što je prvi napisani test za T6.10 predao prazan lanac i dobio
tišinu.

### N57 — `PresentationMemory` je procesno globalna

`a11y::presentationMemory()` je `static` — i to je ispravno, jer kretanje duž
reda ne sme da ponavlja broj reda. Posledica za testove: test koji tvrdi nešto
o **prvoj** ćeliji mora da zove `resetPresentationMemory()`, inače ga prethodni
test učini zelenim ili crvenim bez veze sa kodom.

### N58 — GI vezivanja skidaju prefiks sa `Collection` enum-a

`AtspiCollectionSortOrder` u Pythonu ima članove `CANONICAL` i `FLOW`, ne
`SORT_ORDER_CANONICAL`; `AtspiCollectionMatchType` ima `ANY` i `ALL`, ne
`MATCH_ANY`. C imena nose prefiks, GI imena ne.

Zabeleženo zato što je prva verzija `tools/phase6-browse-check.sh` zbog toga
prijavila da ova radna površina **nema** `Collection` — a ima ga, kompletnog:

```
methods: get_matches, get_matches_from, get_matches_to
orders:  CANONICAL, FLOW, INVALID, LAST_DEFINED, REVERSE_CANONICAL,
         REVERSE_FLOW, REVERSE_TAB, TAB
match:   ALL, ANY, EMPTY, INVALID, LAST_DEFINED, NONE
```

**Dijagnostika koja laže je gora od dijagnostike koje nema**, i zato je ovo
nalaz a ne ispravka u tišini.

### N59 — profinjenje uloge se dešava pre kontrolnog polja

Vidi O16. `Role::Heading1` a ne `Role::Heading`.

### N61 — grabovi se osvežavaju **pre** događaja koji pravi interceptor

`Registry::handleGainFocus` zove `focusChanged_()` → `refreshGrabs()`, i **tek
onda** `dispatcher_.executeEvent(gainFocus)` — a to je događaj na kom se tree
interceptor pravi. Dakle grab set izračunat tamo **još ne zna** da skriptovi
dokumenta postoje.

Posledica bi bila: **prva veb strana koju korisnik fokusira ne odgovara ni na
jedno quick-nav slovo, a druga odgovara.** To je najzbunjujući otkaz koji
čitač ekrana može da ima — ne pada, ne loguje, i izgleda kao da je slučajan.

Mera je `nav::Service::refreshGrabs()`, pozvan sa dva mesta:

1. kad `handleInterceptorEvent` napravi ili promeni tekući interceptor;
2. iz `BrowseMode::setPassThrough` — i to je ono što **focus mode čini
   stvarnim**: `computeGrabs` pita desetostepeni lookup da li bi gest našao
   skript, `passThrough` čini odgovor „ne" za svako slovo, pa se slova
   **otpuštaju aplikaciji** umesto da se testiraju unutar svakog od 74
   quick-nav skripta. T6.9 je zato svojstvo lookup-a, ne svojstvo skriptova.

Nađeno čitanjem putanje događaja posle nego što su svi testovi bili zeleni —
nijedan hermetičan test to ne vidi, jer mock backend nema grabove. Zato je
`tools/phase6-browse-check.sh` jedino mesto gde se broj grabova stvarno meri.

### N60 — `get_matches_to` je nazad, ne obrnut sort

libatspi ima tri funkcije: `get_matches` (sve), `get_matches_from` (napred od
objekta) i `get_matches_to` (**nazad** od objekta). `shift+h` je
`get_matches_to`, ne `get_matches_from` sa `REVERSE_FLOW` — a razlika je što
`REVERSE_FLOW` okreće **ceo rezultat**, pa bi „prethodni naslov" bio poslednji
naslov u dokumentu.

Takođe: `get_matches_to` ima argument `limit_scope` koji `get_matches_from`
nema. `FALSE` znači „celo podrvo kolekcije", što je ono što browse mode traži.

**Dopuna od 7. 11. 2026.:** ovaj nalaz je bio tačan ali nepotpun, i ta
nepotpunost je postala N88. `limit_scope` nije jedini logički argument tih
funkcija — postoji i `traverse`, posle `count`, i on je onaj koji stvarno
odlučuje da li se u podrvo **ulazi**.

### N88 — `traverse = FALSE` je quick navigation ostavljao bez ikakvog rezultata

Sva tri poziva u `AtspiBackend` prosleđivala su `FALSE` kao poslednji logički
argument:

```c
GArray *atspi_collection_get_matches      (…, gint count, gboolean traverse, GError **);
GArray *atspi_collection_get_matches_from (…, gint count, gboolean traverse, GError **);
GArray *atspi_collection_get_matches_to   (…, gboolean limit_scope, gint count,
                                           gboolean traverse, GError **);
```

Kod je taj argument čitao kao `limit_scope` (N60 ga spominje, i komentar u
izvoru ga je tako i imenovao), a to je `traverse`. Sa `FALSE`, libatspi
razmatra **samo neposrednu decu** objekta. Veb dokument ima jedno dete —
`section` — pa je odgovor uvek bio prazan niz.

Mereno nad istim člankom, isti poziv, samo taj argument:

| `traverse` | nađeno linkova | trajanje |
|---|---|---|
| `FALSE` | **0** | 0,09 ms |
| `TRUE` | **333** | 2,70 ms |

Onih 0,09 ms je bila cena **prazne pretrage**. Prvi izveštaj `browse-info`-a je
pokazivao „p95 0,15 ms, 0 found" i to je izgledalo kao da kriterijum prolazi.

### N89 — `MATCH_INVALID` ne znači „kriterijum se ne koristi"

Posle ispravke N88 **ni jedan ni drugi put nije radio**: `matchesFrom` je
počeo da troši pravo vreme (1,0–1,6 ms umesto 0,1 — dakle pretraga je stvarno
išla u podrvo) ali je još vraćao 0, a `matches` se nije ni pomerio. Dakle N88
je bio nužan, ne dovoljan.

Uzrok je bio `MatchRule` čiji `stateMatch` kod nije postavio: ALUS ga je
prevodio u `ATSPI_Collection_MATCH_INVALID`, sa značenjem „ovaj kriterijum ne
koristim".

Nijedan provajder to tako ne implementira. Mereno, `role=Link`,
`sortBy=CANONICAL`, nad 333 linka:

| `stateMatch` | nađeno |
|---|---|
| `MATCH_INVALID` | **0** (u 1,82 ms — pretraga se izvršila i vratila prazno) |
| `MATCH_ALL` | 333 |
| `MATCH_ANY` | 333 |
| `MATCH_NONE` | 333 |

`attributeMatch` i `interfaceMatch` nisu menjali ništa; samo stanje. Orca nikad
ne pošalje `MATCH_INVALID`, pa nijedan pregledač nije testiran na njega.

Lek: **prazan skup sa `MATCH_ALL`** znači „ima sva stanja iz praznog skupa",
što je vakuumski tačno i dakle potpuno isti kriterijum — samo napisan onako
kako provajderi zaista implementiraju. Pretvaranje je u `MatchRuleScope`,
jednom, i samo kad je odgovarajući skup prazan: neprazan skup zadržava ono što
je pozivalac tražio.

### N90 — `GetMatches` sa `sortBy=FLOW` u Chrome-u vraća prazno

T6.6 je izmerio, i pravilno, da je FLOW redosled **čitanja** a CANONICAL
redosled **drveta**, i da quick navigation mora da koristi FLOW. Nad stvarnim
Chrome-om, `GetMatches` FLOW ne implementira:

| sortBy | `GetMatches` (lista) | `GetMatchesFrom` (pritisak) |
|---|---|---|
| `CANONICAL` | 333 u 2,0 ms | 1 u 0,09 ms |
| `REVERSE_CANONICAL` | 333 u 2,5 ms | — |
| `FLOW` | **0 u 0,07 ms** | 1 u 0,08 ms |
| `TAB` | **0 u 0,07 ms** | 1 u 0,07 ms |

Rupa je u **jednoj metodi**, ne u provajderu: isti Chrome poštuje FLOW u
`GetMatchesFrom`.

**Izmereno i u Firefox-u 8. 11. 2026.** (Gecko 153.6.0, članak „Нешто између",
111 linkova) — ponaša se **identično**:

| sortBy | `GetMatches` | `GetMatchesFrom` (count=1) |
|---|---|---|
| `CANONICAL` | 111 u 0,23 ms | 1 u 0,16 ms |
| `REVERSE_CANONICAL` | 111 u 0,27 ms | 1 u 0,08 ms |
| `FLOW` | **0 u 0,12 ms** | 1 u 0,15 ms |
| `TAB` | **0 u 0,14 ms** | 1 u 0,10 ms |
| `REVERSE_FLOW` / `REVERSE_TAB` | **0** | 1 |

**I u LibreOffice Writer-u, 8. 11. 2026.** (soffice, `document text`, 7
naslova stila `Heading 1`/`Heading 2`), `role=HEADING`:

| sortBy | `GetMatches` | `GetMatchesFrom` (count=1) |
|---|---|---|
| `CANONICAL` | 7 u 0,12 ms | 1 u 0,17 ms |
| `REVERSE_CANONICAL` | 7 u 0,15 ms | 1 u 0,10 ms |
| `FLOW` | **0 u 0,14 ms** | 1 u 0,16 ms |
| `TAB` | **0 u 0,14 ms** | 1 u 0,13 ms |
| `REVERSE_FLOW` / `REVERSE_TAB` | **0** | 1 |

Dakle **tri nezavisna provajdera — Blink, Gecko i VCL/UNO — daju isti odgovor**,
i to menja zaključak: na Linux-u lista elemenata **nikad** ne dobije red
čitanja preko `GetMatches`. Nije reč o jednom pregledaču nego o tome da tu
mogućnost ne implementira niko ko je dostupan. Red čitanja je ostvariv samo
kroz `GetMatchesFrom` — a to je, na sreću, upravo put kojim ide pritisak
tastera, koji je ono što korisnik oseti. T6.6 stoji za quick navigation; za
listu elemenata ne stoji ni kod jednog izmerenog provajdera.

Neizmereno ostaje WebKitGTK (GNOME Web) i Qt/KDE aplikacije. To je **jedini**
razlog zašto ovo nije napisano kao svojstvo AT-SPI-ja po sebi.

N88 i N89 su takođe provereni u Firefox-u i identični su: `traverse=FALSE` →
**1** link (ne 0, jer Firefox-ov koren ima 11 dece od kojih je jedno link) a
`TRUE` → **111**; `MATCH_INVALID` → **0**, svaki drugi tip → **111**.

U Writer-u je **N89 potvrđen i treći put** (`MATCH_INVALID` → 0, svaki drugi
tip → 7), a **N88 se tamo ne vidi**: `traverse=FALSE` i `TRUE` daju isto
(7), jer je Writer-ovo drvo dokumenta **plitko** — naslovi i odlomci su
neposredna deca korena, pa nema podrva u koje bi se ulazilo. To je vredno
zapisati zato što znači da defekt klase N88 **može da se uhvati samo
pregledačem**: na kancelarijskom dokumentu je nevidljiv.

Lek: `matches()` traži red čitanja, a ako je odgovor prazan, pita ponovo u redu
drveta. Alternativa je reći slepom korisniku da stranica nema linkova.

**Odluka, posle trećeg provajdera.** Dok je bio izmeren samo Chrome, rezerva je
ostavljena da se aktivira svaki put (dva poziva: prazan 0,07–0,12 ms, pa pravi
0,2–2,5 ms), uz uslov zapisan ovde: *ako merenje pokaže da FLOW ne daje
nijedan, ovo treba da postane jedan poziv i §9.1 da to zapiše.*

LibreOffice Writer je bio treći i odgovor je isti, pa je uslov ispunjen i
**izvršen**: `QuickNav::all` sada traži `SortOrder::Canonical`, dakle **jedan
poziv**. Rezerva u `AtspiBackend::matches` ostaje za svakog pozivaoca koji još
traži FLOW — i živi test je izvršava, jer bi inače bila mrtav kod — pa je
vraćanje na FLOW izmena jedne reči ako WebKitGTK ili Qt pokažu da ga imaju.
`architecture.md` §9.1 tačka 2 i §9.2 sada to i kažu.

Pritisak tastera ovo ne plaća: `GetMatchesFrom` poštuje FLOW, pa tamo ostaje
jedan poziv — što je invarijanta koju hermetični `[T6.7]` i meri.

### N91 — defekt je živeo u jedinom sloju koji hermetični testovi zamenjuju

Sva tri defekta su bila u prevodu iz ALUS-ovog `MatchRule` u
`AtspiMatchRule` — dakle u `AtspiBackend`. O14 beleži da mock platforma
`Collection` implementira **stvarno**, i to je bila dobra odluka: zbog nje su
svih 22 pravila §9.2 proverena. Ali mock implementira `matches`/`matchesFrom`
**direktno**, pa prevod koji je bio pokvaren mock nikad ne izvrši. Rezultat:
397 podtestova browse mode-a prolazi, a funkcija ne radi ni na jednoj stranici.

Zato je T6.7 sada živi test (`tests/a11y/test_live_a11y.cpp`), a ne ručni
korak.

**I druga polovina nalaza, koja je skuplja od prve.** Prva verzija tog testa
je odlučivala „ima li otvorene stranice" **pozivom koji testira** — ako
`matches()` vrati 0 linkova, zaključi da nema sadržaja i pristojno preskoči.
Sa vraćenim defektom test je **preskočio, ne pao**:

```
SKIPPED: the biggest document open has 0 links: open a content page …
```

To je ista rupa kroz koju se defekt i provukao, samo jedan sloj iznad. Garda
koja odlučuje da li merenje ima smisla **ne sme da deli kod sa onim što meri**.
Sada taj broj daje obilazak drveta (`walkCount`, bez ijednog `Collection`
poziva), pa je „stranica ima 333 linka a Collection nalazi 0" **pad**, a
„nijedna stranica nije otvorena" preskakanje. Provereno u oba smera:

| stanje koda | ishod testa |
|---|---|
| ispravljen | prolazi — 333 po obilasku, 333 po `Collection`, p95 0,11 ms |
| sa vraćenim defektom | **pada** — `CHECK( found.size() >= walkedLinks / 2 )`, `0 >= 166` |

### N92 — `browse-info` je tražio fokus u dokumentu, a fokus je na okviru

Alat je merio **dokument u lancu fokusa**. Dok korisnik ne klikne u telo
stranice, fokus je `FRAME "… – Google Chrome"`, pa je alat odgovarao
`document: NONE in the focus chain` i vraćao 1 — i to je bio jedini razlog
zašto je T6.7 mesecima izgledao nemerljivo. Čovek je morao da zna da klikne.

Sada, kad u lancu fokusa nema dokumenta, meri **najveću otvorenu stranicu** i
to kaže u samom izlazu (`NOT the focused object`). Rangiranje je po **veličini
podrveta**, ne po broju dece: koren veb dokumenta ima tačno jedno dete (telo
`section`), pa bi brojanje dece stavilo Messenger-ov prozorčić iznad članka
Википедије — što je prvo i uradilo.

### N93 — sonda u `tools/phase6-browse-check.sh` imala je ista dva defekta

Korak 3 je ispisivao `CANONICAL: []` i `FLOW: []` i onda `OK   measured`.
Sopstvena Python sonda skripte, napisana nezavisno od C++ koda, imala je
**oba** defekta: `MATCH_INVALID` (N89) i `get_matches(..., 10, False)` (N88).
Dva nezavisna pisanja istog pogrešnog razumevanja su bolji dokaz da je
dokumentacija libatspi-ja o tim argumentima loša nego jedno.

Ispravljeno, i ocena je sada iskrena: prazno u oba redosleda znači „stranica
nema naslova, nema šta da se uporedi"; CANONICAL puno a FLOW prazno znači
**WARN** sa objašnjenjem iz N90, a ne `OK`. Mereno posle ispravke:
CANONICAL vraća 10 srpskih naslova članka, FLOW ništa.

### N94 — Firefox vraća prazno kad je krajnji ofset preko kraja teksta

`browse-info` je za Firefox ispisivao `own text: 11 characters, 0 of them
U+FFFC` — što je izgledalo kao da Firefox obara N47, odnosno kao da koren
dokumenta u Firefox-u **ima** sopstveni tekst a ne čuvare mesta. Nije.

Alat je tražio `get_text(0, 120)` nad dokumentom od 11 znakova:

| pregledač | `get_text(0, n)` | `get_text(0, 120)` |
|---|---|---|
| Firefox | 11 znakova, 33 bajta | **0 znakova** |
| Chrome | 1 znak, 3 bajta | 1 znak, 3 bajta |

Chrome odseca prekoračenje i odgovori; Firefox vrati prazan string. Posle
ispravke (`std::min(characters, 120)`): **11 znakova, svih 11 U+FFFC** — po
jedan na svako od 11 dece, dakle N47 stoji i u Firefox-u.

Provereno da pravi kod ovo ne radi: svi pozivaoci `textRange` u `src/nav`,
`src/text` i `src/a11y` koriste `(0, -1)` — AT-SPI-jevu konvenciju za „do
kraja" — ili `(0, characterCount())`. Fiksni ofset je bio samo u dijagnostici.
Backend namerno **ne** odseca sam: to bi tražilo `characterCount()` pre svakog
čitanja teksta, dakle jedan D-Bus poziv više na najtoplijem putu u čitaču
ekrana.

### N95 — šta Writer izlaže, a šta ne: `h` radi, `k` ne

Mereno nad `document text`-om u LibreOffice Writer-u:

| uloga iz §9.2 | Writer | napomena |
|---|---|---|
| `heading` | **7** | sa `paragraph-style` = `Heading 1`/`Heading 2` u tekstualnim atributima |
| `paragraph` | 18 | |
| `link` | **0** | a dokument **ima** 24 linka |
| `table`, `list` | 0 | **nije izmereno**, vidi niže |

Naslovi dakle rade: `h` i `shift+h` u Writer-u nalaze ono što treba, i to je
dobra vest jer je bila neizvesna.

**Linkovi ne rade, i to je pravo svojstvo platforme.** Writer ih ne izlaže kao
objekte sa ulogom `link` nego kao **hipertekst unutar odlomka** — isti odlomak
odgovara `hyperlinks: 1` preko `Hypertext` interfejsa, a u celom drvetu okvira
nema ni jedne uloge `link`. Pošto quick navigation po §9.2 traži **uloge** kroz
`Collection`, taster `k` u Writer-u ne nalazi ništa. Lek bi bio drugi put za
linkove u kancelarijskim dokumentima (obilazak odlomaka i njihovog
`Hypertext`-a), što je posao za fazu koja se bavi kancelarijskim paketom, ne za
ovu — ali taster tada mora da **kaže** da ih ne nalazi, kao u O10.

**Dve nule koje NISU merenje, i zašto to piše ovde.** `table` i `list` su
nule iz mojih grešaka, ne iz Writer-a:

* `list` — drugi probni dokument ga uopšte nije sadržao (ispustio sam blok
  `text:list` pri prepravljanju);
* `table` — dokument ga sadrži, ali Writer izlaže **samo vidljivi deo**: od 8
  naslova i 24 odlomka u fajlu, drvo pokazuje 7 i 18, a tabela je ispod
  prevoja. Nula je artefakt vidnog polja.

Zapisano kao nemereno a ne kao nalaz, jer je ovo drugi put u istom merenju da
je moj probni dokument proizveo lažan nalaz. Prvi put sam na osnovu dokumenta
bez deklarisanih stilova zaključio da Writer **uopšte** ne izlaže naslove — svih
27 dece je bilo `paragraph`. Uzrok: `text:style-name="Heading_20_1"` bez
odgovarajućeg `style:style` u `office:styles` LibreOffice tiho razreši u
`Default Paragraph Style`. Provera koja je to otkrila je bio tekstualni atribut
`paragraph-style`, koji je za „naslov" pisao `Default Paragraph Style`.

Pravilo koje iz toga sledi, i koje je isto pravilo kao N91: **kad merenje nad
spoljnim programom da nulu, prvo treba dokazati da ulaz nije prazan.**

## Šta ostaje da se izmeri sa čovekom

Jedino što mašina nije mogla: **šta korisnik čuje**, i **koliko to traje u
pravom pregledaču**.

```bash
# Stopiraj Orcu prvo (drži KeyboardMonitor — N8), pa:
./build/src/alus-daemon --log-level info --log-file ~/alus.log

# Dijagnostika nad fokusiranim dokumentom: Collection, U+FFFC, listovi,
# uravnoteženost toka polja, i p95 po tipu quick-nav-a (T6.7)
./build/src/alus-cli browse-info

# Sve automatske provere plus plan ručnog testa
bash tools/phase6-browse-check.sh --manual
```

Plan ručnog testa (`--manual`) pokriva Wikipedia članak u **oba** pregledača,
tabelu, i LibreOffice Writer — gde se očekuje da čitanje radi a quick-nav
odgovori „no next heading", jer Writer ne izlaže `Collection`.

Pa onda log, koji kaže šta se desilo a ne šta mislite da se desilo:

```bash
grep -E 'tree interceptor'    ~/alus.log   # koji dokumenti su presretnuti
grep -E 'pass through'        ~/alus.log   # svaka promena moda, i zašto
grep -E 'collection'          ~/alus.log   # dokument bez Collection
grep -E 'not reached walking' ~/alus.log   # obilazak listova koji je odustao: bug
```

## Predaja Fazi 7

Faza 7 je brajev red (`liblouis`, `brlapi`). Šta joj Faza 6 ostavlja:

1. **`text::TextWithFields` je sad pravi tok**, i brajev red ga čita isto kao
   govor. `braille.py`-jev `getControlFieldBraille` je drugi potrošač
   `ControlField`-a, i polja su već kanonska (O7).
2. **`DocumentTextRange::boundingRects()` radi po listovima** — brajevo
   rutiranje kursora traži pravougaonik po liniji, i compound opseg ga sastavlja
   iz opsega svakog lista.
3. **N53 važi i za brajev red**: ne razlikuj linkove po `State::Linked`, jer ga
   ima svaki link. Posećenost je `State::Visited`.
4. **`CursorManager` je mesto gde brajev red upisuje kursor.** `caret()` je
   `text::Bookmark`, a ne opseg — pravilo N49 važi i za brajev red, koji živi
   isto toliko dugo koliko i objekat.
5. **Tri dijaloga iz O10 su i brajevu redu potrebna** (pretraga, elements
   list). Ako Faza 7 uvede bilo kakav UI sloj, O10 se zatvara zajedno.

## Stanje na kom se nastavlja

```bash
meson setup build && meson compile -C build
meson test -C build                      # 10 svita, zeleno
meson test -C build --suite browsemode   # 393 asertacije — izlazni kriterijum Faze 6
meson test -C build --setup=valgrind --suite core --suite config --suite speech \
         --suite input --suite a11y --suite text --suite nav --suite browsemode
                                         # 10/10, 0 grešaka, 0 izgubljenih bajtova
```
