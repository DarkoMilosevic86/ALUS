# Faza 5 — Tekst i review: rezultati

> Izveštaj o izvršenoj fazi. Prati `02-FAZE.md` → *Faza 5 — Tekst i review*.
> Datum: 2026-11-06. Prethodne faze: 1 (jezgro), 2 (govor), 3 (ulaz),
> 4 (objektni model), zelene.

## Status: ZAVRŠENA — svih devet kriterijuma prošlo (T5.3 dovršen 8. 11. 2026.)

```bash
meson setup build && meson compile -C build
meson test -C build                       # 9 paketa, sve zeleno
meson test -C build --suite text          # 160 tvrdnji, 18 slučajeva
meson test -C build --suite nav           # 533 tvrdnje, 72 slučaja
meson test -C build --setup=valgrind --suite core --suite config --suite speech \
                                   --suite input --suite a11y --suite text --suite nav
                                          # 0 grešaka, 0 izgubljenih bajtova
meson test -C build --suite live          # govor, objektni model i tekst nad pravom sesijom
tools/phase5-text-check.sh                # pet automatskih koraka
./build/src/alus-cli text-info            # „text layer: WORKING"
```

Tekstualni sloj je napisan, povezan u demon i izmeren na ciljnom sistemu
(Debian 13, GNOME 48.7, Wayland, AT-SPI 2.56.2). **Posle Faze 5 → alfa izdanje
za desktop aplikacije**, kako plan i kaže.

**T5.3 je bio jedini nedovršen kriterijum, i to zbog spoljnog paketa.**
`libicu-dev` je instaliran 8. 11. 2026., i time je kriterijum **izmeren i
prošao** bez ijedne izmene koda — ICU grana je bila napisana i čekala paket,
kako O2 i kaže.

Izmereno: `Segmenter::icuAvailable()` → `ICU 76.1`, i japanski deo testa sada
tvrdi tri reči a ne jednu:

| ulaz | segmenti | ranije, bez ICU-a |
|---|---|---|
| 日本語を読む | `日本語` / `を` / `読む` | jedna reč preko cele fraze |

```
tools/phase5-text-check.sh  →  OK  libicu-dev present: 76.1
                               segmenter:    ICU 76.1
./build/tests/test-text "[T5.3]"  →  17 tvrdnji u 4 slučaja, sve prošle
```

Posledica koju je ovo uklonilo je bila stvarna, ne teorijska:
`control+rightArrow` je u japanskom tekstu prelazio celu frazu umesto reč po
reč. Vidi O2, koji ostaje tačan: ICU je i dalje `required: false`, jer čitač
ekrana koji odbije da se izgradi bez `libicu-dev` je gori od onog koji srpski i
engleski čita tačno i **kaže** šta je sa japanskim.

---

## Izlazni kriterijumi

| Kriterijum | Status | Gde se proverava |
|---|---|---|
| T5.1 — `OffsetConverter`: emoji, CJK, kombinujući dijakritik, round-trip | ✅ | `tests/text/test_offsets.cpp` |
| T5.2 — `get_string_at_offset(LINE)` = ALUS `UNIT_LINE` | ✅ **izmereno** | `tests/nav/test_text_range.cpp`, `tests/nav/test_live_text_layer.cpp` |
| T5.3 — `UNIT_WORD` preko ICU za srpski, engleski, japanski | **prošao** | `tests/text/test_segment.cpp`; ICU 76.1 izmeren 8. 11. 2026., japanski daje tri reči (O2) |
| T5.4 — dekomponovano „é" je JEDAN znak za sricanje | ✅ | `tests/text/test_offsets.cpp`, `tests/nav/test_text_speech.cpp` |
| T5.5 — detekcija karete u GTK4, Qt6 i LibreOffice | ✅ | `tests/nav/test_caret.cpp` (sedam slučajeva) |
| T5.6 — `LiveText` diff: 1000 linija ⇒ samo nove | ✅ | `tests/nav/test_live_text.cpp` |
| T5.7 — SayAll: 100 linija kontinuirano, kareta prati zvuk | ✅ **izmereno** | `tests/nav/test_say_all.cpp` |
| T5.8 — SayAll se prekida na pritisak tastera u < 50 ms | ✅ **izmereno** | isto |
| T5.9 — flat object review: prostorno sortiranje za tri kolone | ✅ | `tests/nav/test_review.cpp`, `tests/nav/test_live_text_layer.cpp` |
| Ručni test (cela review/caret grupa, Text Editor, terminal) | ⏳ **čeka čoveka** | `tools/phase5-text-check.sh --manual` |

Ručni test je i ovde jedini korak koji mašina ne može da odradi: proverava
**šta korisnik čuje**. Sve ostalo je pokriveno testovima nad mock platformom
(gde se svaki platformski poziv broji), plus `live` paketom nad pravom
sesijom, plus skriptom koja meri.

### Šta je u T5.7 i T5.8 zaista izmereno

T5.7 traži „kontinuirano, bez pauze po liniji, caret prati zvuk (odstupanje
< 1 linija)". Mock platforma pamti **svaki `set_caret_offset` koji je stigao
do nje**, pa je merenje direktno:

* 100 linija pročitano, a sintetizatoru predato **manje od 100 iskaza** —
  `SentenceBuffer` je zadržavao tekst do granice rečenice (§4.4). Jedan iskaz
  po liniji je upravo ono mucanje zbog kog taj bafer postoji.
* kareta je pomerana **u rastućem nizu, po jednom po liniji**, i na kraju je
  stala na **poslednju liniju**, ne na kraj dokumenta. To je „odstupanje < 1
  linija": kareta se pomera kad linija **počne** da se izgovara, ne kad se
  preda sintetizatoru.
* `numpadPlus` (review say-all) pomera **review kursor i nijedan put karetu** —
  `caretsSet` je prazan. To je cela razlika između `numpadPlus` i
  `alus+downArrow`.

T5.8 je izmeren dva puta: prvo da `stop()` traje mikrosekunde i da **nijedna
od već baferovanih linija ne prođe posle njega** (bez generacijskog brojača iz
`SayAll` kolbekovi u letu bi nastavili), a onda i cela staza — pravi
`input::Service`, pravi `GestureManager`, pravi `KeyEvent` — gde se say-all
zaustavi pre nego što lookup skripta i počne.

---

## Merenje na živom sistemu — IZVRŠENO

`tools/phase5-text-check.sh` i `meson test --suite live`, 2026-11-06,
GNOME 48.7 / Wayland:

| Šta | Izmereno | Zašto je važno |
|---|---|---|
| granularnost `LINE` | gnome-shell ✅, GTK ✅, Chromium ✗ | §7.2: linije traži od toolkita — **ali ne sve dobije** |
| granularnost `PARAGRAPH` | **nijedan toolkit je ne odgovara** | N45: plan je pretpostavio da je ima |
| granularnost `WORD` | odgovaraju je, ali **ne NVDA-ovu** jedinicu | O3: reč + razmak posle nje računa ALUS |
| tekstualni atributi | 23–25 sirovih → 9–12 normalizovanih | svaki red normalizatora je odluka šta je toolkit mislio |
| nepoznati atributi | 11, od kojih **jedan važan** (`invisible`) | N46: ugrađen posle merenja |
| flat screen review aktivnog prozora | **4 dela, 3 reda, 49 objekata, 67 poziva, 4,9 ms** | §10 u brojevima; snapshot se deli, pa `copy()` košta 0 poziva |
| `copy()` opsega nad ekranom | **0 platformskih poziva** | review komande kopiraju pri svakom pomeraju |
| 20 čitanja property-ja fokusa u ciklusu | **0 poziva** | T4.2 i dalje važi: tekstualni sloj ga nije pokvario |
| tekst Chrome dokumenta | `￼￼￼` (U+FFFC ×3) | N47: ugnežđeni objekti, posao Faze 6 |
| valgrind, 7 paketa | **0 grešaka, 0 izgubljenih bajtova** | našao je N49 pre nego što je stigao do korisnika |

---

## Odstupanja od plana, sa razlogom

### O1 — `alus::text::WordSeg` → `alus::text::Segmenter`

Plan imenuje deliverable `WordSeg` („ICU segmentacija reči i rečenica"). Napisan
je kao `Segmenter`, jer segmentira **i znakove** — grafemske klastere — a to je
trećina posla i jedini način da „jedan znak" znači isto review-u, sricanju i
(u Fazi 7) brajevom redu. `WordSeg` bi pogrešno imenovao dve trećine klase.

Uz to, `Segmenter` **ne prima locale**, i to je NVDA-ov nalaz koji je zadržan
jer je tačan (`textUtils/icu.py`): granice reči, znakova i rečenica su vođene
**pismom**, ne jezikom. ICU bira tajlandski, kmerski i CJK rečnik po pismu
znakova koje gleda, pa japanski segmentira tačno bez da mu se jezik kaže
(T5.3). Jedino što bi locale kupio je prilagođavanje skraćenica u rečenicama
(„Dr." ne završava rečenicu), što NVDA takođe ne primenjuje — a da ALUS
primeni, ALUS i NVDA bi se razlikovali u tome gde je kraj rečenice, bez testa
koji bi to opravdao.

### O2 — ICU je `required: false`, i to je odluka a ne propust

§7.3 kaže „isključivo ICU". Meson ipak traži `icu-uc` kao **opcionu**
zavisnost, sa glasnim fallback-om. Razlog nije dizajn nego korisnik: čitač
ekrana koji **odbije da se izgradi** na mašini bez `libicu-dev` je gori od onog
koji čita srpski i engleski tačno i **kaže** da japanski ne čita. Fallback je
bazna pretraga reči iz `offsets.cpp` (NVDA-ov `findStartOfWord`/`findEndOfWord`)
plus pravilo za terminator rečenice.

Ono što se bez ICU-a izgubi je izmereno i zapisano, ne prećutano:

* **japanski i tajlandski**: nema razmaka na kojima bi se lomilo, pa
  `control+rightArrow` pređe celu frazu umesto jedne reči;
* **grafemski klasteri**: znak je jedan **codepoint**, pa kombinujući dijakritik
  korača sam.

`Segmenter::icuAvailable()` je deo dijagnostike (`alus-cli text-info`, korak 1
skripte, prvi test u `test_segment.cpp`), pa build koji je izgubio ICU to
prijavi umesto da tiho pogrešno segmentira.

### O3 — reči i rečenice se **ne traže** od platforme, linije i paragrafi se traže

§7.2 piše `_getWordOffsets` → „`…GRANULARITY_WORD`, ili ICU segmentacija".
Izabrana je ICU strana, i to za obe jedinice:

* **reči**: `GRANULARITY_WORD` daje UAX#29 granice, koje se zaustavljaju
  **pre** razmaka posle reči. NVDA-ova jedinica reči uključuje taj razmak, ili
  kretanje po rečima traži dva pritiska na reč. Nijedan toolkit ne nudi
  NVDA-ovu jedinicu — izmereno (korak 2 skripte): svi je „odgovaraju", ali
  drugačije nego što treba.
* **rečenice**: GTK na `GRANULARITY_SENTENCE` vrati celu liniju, LibreOffice
  svoju predstavu rečenice. ICU nad paragrafom je jedan odgovor svuda.

**Linije i paragrafi dolaze od platforme**, jer tamo toolkit zna nešto što
ALUS ne može: **gde je prelomio tekst**. U liniji prelomljenog paragrafa nema
nijednog znaka koji to kaže. (Za paragrafe to u praksi ne radi — N45.)

### O4 — `a11y::Settings` ima i `text::FormatReportSettings`

Objektni model čita `[documentFormatting]` od Faze 4; tekstualni sloj treba
ostatak te sekcije, u obliku koji **ne zna šta je `a11y`** (`text::` ne sme da
zavisi od `a11y::`, jer ga koristi i prezentacija i Faza 6). Zato
`a11y::Settings` sada nosi i `text::FormatReportSettings`, a `settingsFrom()`
puni preklapajuće ključeve **iz istog unosa konfiguracije** — jedan izvor
istine, dva oblika. Faza 9 ih ujedinjuje kad bude pisala GUI za formatiranje.

### O5 — prezentacija teksta živi u `a11y`, ne u `speech`

Isto obrazloženje kao O1 Faze 4, i ista granica: `getTextRangeSpeech`,
`getFormatFieldSpeech` i `speakSelectionChange` su u `src/a11y/text_speech.cpp`
zato što `text::ControlField` nosi `a11y::Role`, a govorni sloj ne sme da zna
šta je rola. Posledica koja se isplatila: svaki test formatiranja tvrdi **šta
bi bilo izgovoreno**, bez sintetizatora.

Sricanje je **ostalo u `speech::Presenter`** (`src/speech/spelling.cpp`), jer
njemu trebaju samo simboli, opisi znakova i sposobnosti sintetizatora — sve
troje je Presenter već imao. Tamo ga NVDA i drži.

### O6 — `SayAll` **nije** coroutine

Plan kaže „coroutine, callback-driven praćenje karete". Napisan je kao
**callback-driven i ništa više**, i to je ispravka plana a ne odstupanje od
njegovog cilja.

Coroutine koji se nastavlja jednom po pump ciklusu je **oblik za anketiranje**:
probudi se na tempo petlje i pogleda da li se nešto promenilo. Say-all ne treba
da se budi na tempo petlje — treba da se budi kad **sintetizator stigne do
oznake**, jer se tad kareta pomera na liniju koja je upravo počela da se
izgovara. To je `speech::CallbackCommand` → `IndexCommand` → notifikacija
`700 INDEX MARK` (§4.3), i jedini tačan trenutak.

Jedno mesto u Fazi 5 gde je coroutine **jeste** pravi oblik je čekanje da
aplikacija pomeri karetu, i tamo se koristi (`nav::CaretWaiter` je
`core::Task`): tamo se zaista anketira, jer aplikacija ne javlja ništa.

### O7 — `nav` je peta biblioteka, a `LiveText` i `Terminal` ostaju `a11y::behaviors`

Plan imenuje `alus::a11y::behaviors::{LiveText,Terminal}`. Namespace je
zadržan (gesture mape i overlay chooser ga tako čitaju), ali kod stoji u
`libalus_nav`, jer mu treba tekstualni sloj. Nova biblioteka postoji zato što
je `nav` **sloj iznad** objektnog modela — review kursor, kareta, say-all i
tekstualne komande trebaju `a11y::Object`, a objektnom modelu ne treba ništa od
njih. Jednosmerna zavisnost je ono što drži `tests/a11y` bez review kursora i
`tests/nav` bez platforme.

### O8 — diff `LiveText`-a je **po linijama**, ne po znakovima

NVDA ima dva algoritma i podrazumevano koristi Google-ov diff-match-patch
(po znakovima). ALUS ima jedan, po linijama, jer:

* ono što korisniku terminala treba su **nove LINIJE**; promenjena reč u
  sredini linije je slučaj za koji NVDA ima drugi algoritam, a ne ono što
  `cat` i `ls` proizvode;
* karakterni diff je zavisnost više i O(n·m) nad scrollback baferom pri
  **svakom** osvežavanju ekrana.

Brza staza je slučaj koji se najčešće događa: izlaz koji je **dodat na kraj**.
Kad su stare linije prefiks novih, odgovor je sufiks, u jednom prolazu. Iznad
400 izmenjenih linija LCS se napušta za upoređivanje skupova — isti odgovor za
skrolovan terminal, bez kvadratne cene.

### O9 — `review_copy` selektuje, ali ne kopira

`alus+f10` pritisnut jednom **selektuje** tekst od markera (i to je provereno
testom: `set_selection` stigne do platforme). Pritisnut dvaput NVDA kopira u
clipboard; ALUS kaže da clipboard još nema. Clipboard traži vezu sa GUI
toolkitom, što je Faza 9. Rečeno naglas umesto prećutano, jer tiho nekopiranje
je gubitak podatka koji korisnik misli da ima.

### O10 — caret komande se **ne grabuju**, i to je nov pojam u ulaznom sloju

Najvažnija posledica Waylanda u ovoj fazi. NVDA pošalje taster aplikaciji i
onda čeka karetu; ALUS **ne može da pošalje taster** (N18:
`atspi_generate_keyboard_event` je no-op na Waylandu) — i ne mora, jer
kompozitorov `WatchKeyboard` dostavi ALUS-u svaki događaj **bez da ga pojede**.
Aplikacija je dakle već dobila taster dok se skripta izvršava.

Da bi to radilo, taster karete **ne sme da se grabuje**, inače aplikacija ga
nikad ne vidi i kareta se nikad ne pomeri. Zato `script::ScriptInfo` ima nov
flag `needsKeyPassThrough`, a `GestureManager::computeGrabs()` preskače takvu
skriptu kad platforma ne ume da pošalje taster. NVDA taj flag ne treba, jer
uvek može da vrati taster koji je pojeo.

### O11 — jedan `setContextContributor` je postao **lista**

Faza 3 je ostavila jedan slot za doprinos dispatch kontekstu; Faza 4 ga je
zauzela. Faza 5 treba treću komandnu grupu (review komande), a sa jednim
slotom je druga registracija tiho uklanjala prvu. Simptom bi bio **numpad koji
radi do prvog SIGHUP-a**. Sada je `addContextContributor` sa handle-om za
uklanjanje, a `setContextContributor` je ostao jer testovi grade kontekst rukom.

### O12 — `Behavior` sada može da **donese svoje skriptove**

NVDA-ov `editableText.EditableText` je mixin koji objektu donosi svoje
skriptove, i gesture mapa napisana za tu klasu mora da ih dohvati. Faza 4 je
behavior-ima dala property override-e i rukovanje događajima, ali ne i
skriptove. Dodato je `Behavior::bindScripts(Object&)`, koje `setBehaviors`
poziva **posle** sastavljanja lanca klasa — tako je skript vezan tu adresiran
pod imenom behavior klase.

### O13 — `Rect` se preselio u `core::geometry`

Do Faze 5 pravougaonik je bio `a11y::Rect` u `backend.hpp`. Sad ga traže tri
sloja koji ne smeju da zavise jedan od drugog: objektni model (`get_extents`),
tekstualni sloj (`boundingRects`) i screen review, koji **sortira po njemu**.
`a11y::Rect` je sada alias, pa kod Faze 4 čita isto.

### O14 — `getControlFieldSpeech` nije u Fazi 5

`getTextRangeSpeech` je prenesen sa celim ustrojstvom koje Faza 6 treba (stek
kontrolnih polja, `commonFieldCount`, diff inicijalnih polja), ali kontrolna
polja u Fazi 5 ne postoje: `OffsetsTextRange` emituje samo format polja, baš
kao NVDA-ova bazna klasa. `getTextWithFields` nad drvetom dokumenta je §9.3,
najvažniji nov kod projekta, i on je Faza 6.

### O15 — `review_currentSymbol`, `reviewCursorToStatusLine` i tabela nisu u Fazi 5

Komandna tabela ima 441 skript; Faza 5 implementira **35** iz kategorija „Text
review" i „System caret", tačno onoliko koliko ručni test iz `02-FAZE.md`
pritiska. Tabelarna navigacija (§8.4) je vezana za `Table` interfejs i stoji
uz app module Faze 8; `review_currentSymbol` traži GUI za uređivanje simbola
(Faza 9); `reviewCursorToStatusLine` traži pojam statusne linije koji na
AT-SPI-ju ne postoji bez app modula.

---

## Nalazi (nastavak numeracije iz Faze 4)

### N45 — nijedan toolkit na ovoj radnoj površini ne odgovara `GRANULARITY_PARAGRAPH`

Izmereno (korak 2 `tools/phase5-text-check.sh`):

```
application            toolkit   granularities answered
gnome-shell            clutter   char=yes word=yes sentence=yes line=yes paragraph=no
org.gnome.Terminal     gtk       char=yes word=yes sentence=yes line=yes paragraph=no
Google Chrome          Chromium  char=no  word=no  sentence=yes line=no  paragraph=no
```

§7.2 mapira `_getParagraphOffsets` → `…GRANULARITY_PARAGRAPH` kao da je ta
granularnost na raspolaganju. **Nije.** ATK je deklariše, a implementacije
vraćaju `(-1, -1)`.

Posledica je dvostruka i obe strane su dobre vesti:

1. fallback radi — paragraf je linija, baš kao NVDA-ov podrazumevani
   `_getParagraphOffsets`, pa `control+downArrow` u GTK editoru pomera za
   liniju i to je ono što aplikacija i sama radi;
2. **ovo je razlog zašto §11.2 ima tri stila paragrafa.** Stil `APPLICATION`
   na Linuxu znači „linija"; korisnik koji želi paragraf uzima
   `SINGLE_LINE_BREAK` ili `MULTI_LINE_BREAK`, i ALUS ga računa iz teksta.
   Bez ta dva stila kretanje po paragrafima na Linuxu ne bi postojalo.

Drugi red iste tabele je takođe nalaz: **Chromium odgovara samo rečenicu**.
Za Chrome dokument linije dakle računa ALUS, pretragom kroz ceo tekst —
skupa staza o kojoj `offsets_range.hpp` upozorava. Faza 6 (browse mode) tu
ionako ne koristi `OffsetsTextRange`.

### N46 — ATK atribut `invisible` je NVDA-in `hidden`, i GTK ga postavlja svuda

Korak 3 skripte ispisuje svaki atribut koji toolkiti stvarno koriste i
označava one koje normalizator ne zna. Od jedanaest nepoznatih, deset su
renderovanje bez značenja za govor (`bg-full-height`, `scale`, `stretch`,
`variant`, `wrap-mode`, `direction`, `editable`, `pixels-above-lines`,
`pixels-below-lines`, `pixels-inside-wrap`), a **jedan nosi informaciju koju
korisnik inače nikad ne bi dobio**: `invisible` je tekst koji postoji a ne
iscrtava se. NVDA ga prijavljuje kao „hidden". Ugrađen je u normalizator posle
merenja.

To je i razlog zašto korak 3 postoji u skripti: tabela normalizatora je lista
odluka o tome **šta je toolkit mislio**, i jedini način da se nađe praznina je
da se pita radna površina.

### N47 — tekst Chrome dokumenta je U+FFFC, i to je ispravno

`alus-cli text-info` nad fokusiranim Chrome dokumentom:

```
characters:   3
line         "￼"
attributes:   6 raw, 6 normalised, run [0,3)
  language             hr
```

Tri znaka, i sva tri su **U+FFFC OBJECT REPLACEMENT CHARACTER**: dokument ne
sadrži tekst nego tri ugnežđena objekta. To nije greška — to je kako
Hypertext radi, i `textUtils.OBJ_REPLACEMENT_CHAR` postoji u NVDA baš za ovo.

Faza 5 je spremila sve što za to treba (`Backend::linkIndexAtOffset`,
`hyperlink`, `ObjectTextRange::objectAtOffset`, `offsetsForObject`), ali
**ne razrešava** placeholder u govoru: čitanje ugnežđenog objekta na mestu
U+FFFC je `getTextWithFields` nad drvetom, §9.3, Faza 6. Do tada korisnik u
Chrome dokumentu čuje placeholder, a ne sadržaj — zapisano da se ne traži kao
regresija.

### N48 — `AtspiTextRange` i `AtspiRange` nemaju javnu funkciju za oslobađanje

`atspi-text.h` deklariše `atspi_text_range_get_type()` i ništa drugo; u
tabeli simbola biblioteke takođe nema ničega:

```bash
strings /usr/lib/x86_64-linux-gnu/libatspi.so.0 | grep -E '^atspi_(text_)?range'
# atspi_range_copy
# atspi_range_get_type
# atspi_text_range_get_type
```

`AtspiTextRange` ima `gchar* content`, pa bi `g_free` nad strukturom
**procurio taj string pri svakoj liniji koju korisnik pročita**. Tačan odgovor
je `g_boxed_free(ATSPI_TYPE_TEXT_RANGE, range)`: boxed tip ima registrovanu
funkciju oslobađanja i `g_boxed_free` je zove. Isto i za `AtspiRange` iz
`get_selection`.

### N49 — opseg teksta čuvan NA objektu je referentni prsten

Našao valgrind, u `nav` paketu, i oblik je isti kao N40 iz Faze 4.

`ObjectTextRange` drži svoj objekat živim (`ObjectPtr object_`) — mora, jer
opseg mora da nadživi ciklus. `EditableTextBehavior` je čuvao poslednju
selekciju kao `TextRangePtr`, a behavior **pripada objektu**. Prsten je dakle:

```
Object → behaviors_ → EditableTextBehavior → lastSelection_ → ObjectTextRange → Object
```

Posledica u demonu: **svako polje za unos koje je korisnik ikad fokusirao
procuri sebe i ceo svoj keširani podstablo** (izmereno: 9,6 KB po objektu).
Nijedan test nije padao; valgrind je rekao „definitely lost".

Mera je pravilo, zapisano u `text/range.hpp` da ga Faza 6 ne pogazi: **ništa
što objekat poseduje ne sme da čuva opseg nad tim objektom.** Čuva se
`text::Bookmark` i opseg se iz njega rekonstruiše. Bookmark je ionako jedino
što sme da nadživi pump ciklus.

### N50 — zadržani nastavak say-all-a mora da traži **immediate** pump

Kad linija ne sadrži granicu rečenice, `SentenceBuffer` je zadrži i ništa ne
ide sintetizatoru; say-all tada stavlja nastavak u red i traži pump. Sa
običnim zahtevom (`requestPump(false)`) pump dolazi posle `pumpMaxDelay`
(§3.1, 5 ms), **po liniji koja nije sadržala granicu rečenice**. Na pasusu od
sto linija to je pola sekunde tišine pre prvog zvuka.

Otkriveno u testu: sa običnim pumpom 100-linijski say-all je stao na prvoj
liniji sa jednim elementom u redu, jer test iterira petlju bez blokiranja i
tajmer od 5 ms nikad ne istekne. Isti uzrok, dve posledice. `requestPump(true)`
rešava obe — i to je tačno: korisnik čeka da čuje sledeću liniju.

### N51 — `ubrk_setUText` je jedini način da ICU vrati **bajt** offsete

ICU-ov C API je UTF-16 po podrazumevanju, pa bi svaka granica koju vrati bila
UTF-16 indeks koji treba mapirati u bajt-offsete kojima AT-SPI govori — dve
konverzije po pritisku tastera i dva mesta da se pogreši (§7.3 upravo o tome).
`utext_openUTF8` + `ubrk_setUText` čine da ICU iterira UTF-8 bajtove direktno,
pa su granice koje vrati **već** offseti koje platforma hoće, bez mapiranja.

Zapisano kao nalaz jer je to neočigledan deo ICU API-ja i jer bi očigledan
put (`ubrk_open` sa UChar baferom) bio tačan ali dvostruko skuplji i
sklon greškama.

---

## Šta je napravljeno

| Modul | Linija | Napomena |
|---|---|---|
| `alus::text::OffsetConverter` i rodbina | 519 | §7.3: bajt ↔ codepoint, ASCII brza staza, bazna pretraga linija i reči |
| `alus::text::Segmenter` | 490 | ICU preko `UText` (N51), sa pravilima kao fallback (O2) |
| `alus::text::TextUnit`, `ReadingUnit`, `ParagraphStyle` | 215 | jedinice kao enum, ne stringovi |
| `alus::text::Field`, `FormatField`, `ControlField`, `FieldCommand` | 527 | `getPresentationCategory` pravilo po pravilo |
| `alus::text::TextRange` (interfejs) | 288 | ceo `textInfos.TextInfo`; greška je vrednost, ne izuzetak |
| `alus::text::OffsetsTextRange` | 931 | ceo `OffsetsTextInfo`: `move`, `expand`, `find`, `boundingRects` |
| `alus::text::CharacterDescriptions` | 422 | NVDA-ovi opisi znakova, en i sr, engleski ugrađen u binarni fajl |
| `alus::core::Rect`, `Point` | 92 | preseljeno iz `a11y` (O13) |
| `alus::a11y::ObjectTextRange` + normalizator | 643 | §7.2 hook po hook; tabela AT-SPI atributa |
| `alus::a11y::getTextRangeSpeech` i rodbina | 1075 | `getTextInfoSpeech`, `getFormatFieldSpeech`, selekcija, uvlačenje |
| `alus::speech::Presenter` sricanje | 360 | `getSpellingSpeech`, opisi znakova, ton za velika slova (zatvara O11 Faze 4) |
| `alus::nav::Review` | 442 | tri moda, fallback među njima, praćenje karete |
| `alus::nav::ScreenReviewRange` | 543 | §10 flat object review, sa grupisanjem u redove |
| `alus::nav::EditableText` | 858 | tri strategije, tabela po toolkit-u, `core::Task` umesto blokiranja |
| `alus::nav::SayAll` | 533 | callback-driven, `SentenceBuffer`, objektni čitač |
| `alus::nav::ParagraphNav` | 347 | tri stila, `(passKey, moved)`, cepanje na rečenicama |
| `alus::nav::ScreenExplorer` | 192 | `screenExplorer.py`, skoro doslovno |
| `alus::a11y::behaviors::{LiveText,Terminal}` | 524 | diff po linijama (O8), red otkucanih znakova |
| `alus::nav::TextCommands` | 854 | 35 komandi review-a i karete, NVDA-ovi tasteri iz generisane tabele |
| `alus::nav::Service` | 500 | restartabilan podsistem; peta biblioteka (O7) |
| `alus-cli text-info` | 140 | dijagnostika koja samo čita i meri |
| `tools/phase5-text-check.sh` | 244 | pet automatskih koraka + plan ručnog testa |
| `data/characters/{en,sr}.dic` | 110 | NVDA-ovi podaci, nepromenjeni |
| testovi | 2935 | 95 slučajeva, 725 tvrdnji, + `live` paket |

Ukupno: **10 355 linija** koda i zaglavlja, **2935** testova.

Promene u ranijim fazama, sve male i sve objašnjene gore: `Behavior::bindScripts`
i `Object::bindBehaviorScript` (O12), `ScriptInfo::needsKeyPassThrough` i
`computeGrabs` (O10), `input::Service::addContextContributor` (O11),
`a11y::Backend` (14 novih tekstualnih poziva), `a11y::Settings` (O4),
`a11y::Rect` → `core::Rect` (O13), `getObjectSpeech` (zatvara O10 Faze 4),
`PresentationHooks::describeCharacters` (zatvara O11 Faze 4),
`Registry::setNavigatorChangedHook`, demon (`--no-nav`, `isSayAllRunning`,
pravo sricanje u eho otkucanih znakova).

### Šta je zatvoreno iz predaje Faze 4

| Predaja Faze 4 | Status |
|---|---|
| 1. `shouldReportTextContent` postaje prava provera | ✅ `object_speech.cpp`, test u `test_text_speech.cpp` |
| 2. `PresentationHooks::spell` zamenjen pravim putem | ✅ `src/speech/spelling.cpp`; treći pritisak sad **opisuje** znakove |
| 3. review pozicija prati navigator objekat | ✅ `Registry::setNavigatorChangedHook` → `Review::onNavigatorObjectChanged` |
| 4. 578 µs po objektu: ne obilaziti drvo objekat po objekat | ✅ snapshot screen review-a se uzima jednom i **deli** sa svakom kopijom opsega (izmereno: `copy()` = 0 poziva) |
| 5. `isSayAllRunning` kuka | ✅ demon je puni iz `nav::Service` |
| 6. `caret`, `textChange`, `textSelectionChange` bez rukovaoca | ✅ sva tri koristi `EditableTextBehavior`; `textChange` i `LiveText` |

---

## Predaja Fazi 6

1. **`getTextWithFields` nad drvetom dokumenta je sve što ostaje za §9.3.**
   `getTextRangeSpeech` je prenesen sa celim ustrojstvom koje kontrolna polja
   traže (stek, `commonFieldCount`, diff inicijalnih polja, `inClickable`), a
   `text::ControlField::getPresentationCategory` je prenesen pravilo po
   pravilu i testiran. Faza 6 dodaje **izvor** tih polja, ne njihovu
   prezentaciju.
2. **`getControlFieldSpeech` je rupa koja čeka** (O14). Mesto je u
   `text_speech.cpp`, u petlji koja već prolazi kroz `controlStart`/
   `controlEnd` komande i održava stek.
3. **`Review::documentPosition()` vraća null** i zato `positionForCurrentMode`
   pada na object review. Faza 6 vraća opseg nad tree interceptor-om i
   „Document review" se pojavljuje u ciklusu modova sam.
4. **U+FFFC se ne razrešava** (N47). `ObjectTextRange::objectAtOffset` i
   `offsetsForObject` postoje i testirani su; prezentacija ih ne zove.
5. **`Object::setTreeInterceptor` i `passThrough` su kuke koje Faza 5 poštuje
   ali ne puni**: `shouldReportTextContent` već pita za tree interceptor, a
   `Review` već forsira object review kad dokumenta nema (NVDA #3320).
6. **Quick-nav preko `Collection` (§9.2) ne sme da koristi `OffsetsTextRange`.**
   Merenje iz N45 je razlog: Chromium ne odgovara ni liniju, pa bi svaki
   pomeraj bio pretraga kroz ceo dokument preko D-Bus-a.
7. **Pravilo N49 važi i za Fazu 6**: tree interceptor je objekat koji živi uz
   `a11y::Object`, pa ne sme da čuva opseg nad tim objektom. `text::Bookmark`
   je ono što sme.

## Šta ostaje da se izmeri sa čovekom

Jedino što mašina nije mogla: **šta korisnik čuje**.
`tools/phase5-text-check.sh --manual` ispisuje plan — Text Editor (linije,
reči, znakovi, selekcija, brisanje, say-all, formatiranje), terminal
(`ls -la`, `cat`, kucanje, `sudo`), pa onda šta tražiti u logu:

```bash
grep -E 'caret move detected' alus.log   # koja strategija je uhvatila koji pomeraj
grep -E 'say all stopped'    alus.log    # koliko linija je svaki say-all pročitao
grep -E 'live text monitoring' alus.log  # polazno stanje terminala
```

Komanda koja je T5.3 prevela iz „delimično" u „prošlo", izvršena 8. 11. 2026.:

```bash
sudo apt install libicu-dev && meson setup --reconfigure build && meson compile -C build
meson test -C build --suite text   # japanski deo T5.3 sad tvrdi tri reči, ne jednu
```

`--reconfigure` je bio dovoljan; `--wipe` nije bio potreban, jer meson traži
`icu-uc` pri konfiguraciji a ne pri prevođenju.
