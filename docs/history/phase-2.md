# Faza 2 — Govor: rezultati

> Izveštaj o izvršenoj fazi. Prati `phase-plan.md` → *Faza 2 — Govor*.
> Datum: 2026-11-04. Prethodna faza: Faza 1 (jezgro), zelena.

## Status: završena

```bash
meson test -C build                   # podrazumevano: core + config + daemon + speech
meson test -C build --suite core      # 379 + 102 podtestova, zeleno (Faza 1, neokrnjeno)
meson test -C build --suite speech    # 121 slučajeva, 20897 tvrdnji, zeleno
meson test -C build --suite live      # T2.11 — traži speech-dispatcher i govori naglas,
                                      # zato se traži izričito (vidi `add_test_setup('quick')`)
```

Svi deliverable-i iz plana postoje, sa dve izmene statusa koje su obrazložene
niže (`EspeakDirectSynth` → v1.1, `ICU regex` → `GRegex`/PCRE2).

---

## Izlazni kriterijumi

| Test | Šta proverava | Status |
|---|---|---|
| **T2.1** | `ui::message("zdravo")` se izgovori | **PASS** — `test_presenter.cpp` |
| **T2.2** | `NOW` prekine `NORMAL`; `NORMAL` se nastavi posle | **PASS** — `test_manager.cpp` |
| **T2.3** | `NOW` ne prekine prethodni `NOW` | **PASS** |
| **T2.4** | `isIndexABeforeB` tačan preko granice 9999→1 | **PASS** — plus provera antisimetrije nad celim prostorom |
| **T2.5** | `CallbackCommand` se izvrši na glavnoj niti, u ispravnom trenutku | **PASS** — proverava se i `std::thread::id` |
| **T2.6** | Zastarela najava fokusa se izbaci iz reda pre izgovaranja | **PASS** — 5 najava u redu, 4 zastarele ⇒ izgovori se 1 |
| **T2.7** | Posle prekida se brzina/visina vrate | **PASS** — i obrnuto: `isDefault` se **ne** reprodukuje |
| **T2.8** | 10 linija bez tačke ⇒ 1 iskaz, ne 10 | **PASS** — `test_sentence_buffer.cpp` |
| **T2.9** | `alus+p` nivoi menjaju izgovor interpunkcije | **PASS** — `test_symbols.cpp`, sva 4 nivoa + `preserve` |
| **T2.10** | Rečnik: unos sa dijakritikom i granicom reči radi | **PASS** — i **dekomponovani** oblik (`c` + U+0301), što je prava zamka |
| **T2.11** | p95 `speak`→zvuk < 50 ms | **PASS uz podeljen budžet** — vidi O3 i N23 |

```bash
./build/src/alus-cli speak "Проба говора на српском" --rate 20   # radi
./build/src/alus-cli bench-speech --iterations 100               # radi
```

---

## Odstupanja od plana, sa razlogom

### O1 — `EspeakDirectSynth` nije implementiran u Fazi 2

Plan ga je već prebacio u „planiran za v1.1" (`RESULTS.md`, ispravka 6 za
`phase-plan.md`), jer S2 pokazuje da SSIP prolazi sve prolazne budžete. Ono što
Faza 2 duguje tom planu je **dokaz da apstrakcija nosi dva implementatora**, i
taj dokaz postoji: `ISynth` ima `SsipSynth` i `NullSynth`, a `NullSynth` nije
atrapa — on nosi `--no-speech` režim demona i ceo testni pogon menadžera.
`WavePlayer::feed(data, size, onDone)` je implementiran **sa callback-om po
bloku**, što je jedini deo koji `EspeakDirectSynth` ne može da doda naknadno
bez prepravke (§4.6).

### O2 — tekstualni sloj koristi `GRegex` (PCRE2), ne ICU

`architecture.md` §11.1 traži „ICU regex, ne `std::regex`". Zahtev iza te
rečenice je **`\w` koje ne isključuje kombinujuće znakove** — bez toga unosi
rečnika pucaju na svakom jeziku sa dijakriticima, uključujući srpski. Taj
zahtev je ispunjen, ali drugim alatom, iz razloga koji se nije video kad je
plan pisan:

1. **Obrasci su podaci, ne kod.** Složeni simboli u `symbols.dic` su pisani u
   Python `re` sintaksi — `(?P<ime>…)` grupe i `(?P=ime)` povratne reference —
   i te datoteke dolaze iz NVDA locale podataka. PCRE2 prima tu sintaksu
   doslovno; **ICU `(?P<…>)` uopšte ne podržava.** Izbor ICU bi značio ručno
   prepisivanje obrazaca za svaki jezik, što je mašina za tihe greške.
2. GLib je ionako zavisnost jezgra, pa ovo ne košta ni jednu novu biblioteku.
3. `\w` je napisano eksplicitno kao `[\p{L}\p{M}\p{N}_]`
   (`alus::text::kWordCharacterClass`) umesto da se veruje bilo čijem `\w`-u.
   **Ponašanje je zahtev; način na koji ga motor piše nije.**

ICU **i dalje treba u Fazi 5**, za segmentaciju reči i rečenica, što PCRE2 ne
ume. Ova odluka to ne dira. `libicu-dev` zato nije instaliran.

### O3 — T2.11: budžet je podeljen na dva merenja, oba poštena

Plan traži „isti test kao S2, sada u CI". Faza 0 je utvrdila (nalaz N14) da se
stvarni zvuk može meriti **samo** hvatanjem monitora sink-a preko `pw-record`.
Taj aparat je `spikes/s2-speech.c` i tamo ostaje — čitač ekrana ne sme da
izraste audio tap da bi testirao sam sebe. Posledica:

- **`spikes/s2-speech.c` vlasnik je budžeta „`speak()` → prvi čujni sample"**:
  8.71 ms p50 / 11.50 ms p95 na ciljnom sistemu, pri budžetu 50 ms. Pokreće se
  ponovo kad se menja audio stek.
- **`tests/speech/test_live_speech.cpp` vlasnik je SSIP putanje** — onoga na
  šta ALUS-ov kod može da utiče. Meri `speak()` → prva oznaka i → poslednja
  oznaka, i obara build ako prva pređe 50 ms ili ako ijedan iskaz ne stigne do
  kraja (rizik R14).

### O4 — `index_mark` pomeraj se ne meri pri dizanju

`RESULTS.md` (ispravka 4 za `architecture.md`) traži da `SsipSynth` **izmeri**
pomeraj od −40 ms pri prvom dizanju, jer zavisi od audio uređaja. Merenje
zahteva audio tap (isti razlog kao O3), pa je izmerena vrednost **podrazumevana
konfiguraciona vrednost** (`speech/indexMarkOffsetMs = -40`), dostupna preko
`ISynth::indexMarkOffset()`, a korisnik može da je prepiše sopstvenim merenjem.
Govorni kursor (Faza 5) je prvi potrošač te vrednosti; do tada ništa ne zavisi
od njene tačnosti.

### O5 — `libpulse-dev` nije instaliran na ovoj mašini

`alus::audio` je potpuno implementiran i testiran preko `ISink` apstrakcije
(`MemorySink` u testovima), a `PulseSink` se kompajlira iza
`-DALUS_HAVE_PULSE`. Bez paketa ALUS **govori normalno** (govor ide kroz
speech-dispatcher), samo su sopstveni tonovi i zvuci tihi, i to se prijavljuje
na `DEBUG_WARNING` nivou — očekivana degradacija, ne greška (§3.8).

```bash
sudo apt install libpulse-dev && meson setup --reconfigure build   # tonovi ožive
```

### O6 — odloženo u kasnije faze, po planu

`speakSpelling` i opisi znakova (`characterDescriptions.dic`) pripadaju Fazi 5;
pune locale datoteke Fazi 9. Faza 2 nosi `data/symbols/en.dic` i `sr.dic`
pisane za ovu fazu, plus **ugrađeni engleski minimum** u binarnom fajlu, da
instalacija bez podataka i dalje izgovara interpunkciju.

---

## Nalazi (nastavak numeracije iz Faze 0)

### N20 — `libspeechd` sam oslobađa ime index oznake. Dupli `free`.

`SPDCallbackIM` dobija `char* index_mark`. Dokumentacija ne kaže čije je
vlasništvo. Oslobađanje u callback-u **ruši proces na reader niti**:

```
Invalid free() ... at free (vg_replace_malloc.c:989)
   by ??? (in /usr/lib/x86_64-linux-gnu/libspeechd.so.2.6.0)
Address is 0 bytes inside a block of size 2 free'd
   by alus::speech::onIndexMarkCallback (ssip_synth.cpp:49)
```

Otkriveno valgrind-om nad `--suite live`, ne unit testovima: javlja se samo sa
pravim serverom. **Mera: callback samo čita ime.** Zabeleženo u kodu, jer je
sledeći čitalac u iskušenju da „popravi curenje".

> Pouka šira od ovog buga: `--suite live` nije luksuz. Tri od četiri problema
> koje je Faza 2 našla u tuđem kodu vide se samo protiv stvarnog servera.

### N21 — `g_regex_escape_string` ne štiti `-`, a simboli idu u klasu znakova

Crtica nije specijalna u obrascu, pa je PCRE2 escape funkcija ne dira — ali
**jeste** specijalna unutar `[...]`, gde procesor simbola slaže sve
jednoznakovne simbole. Rečnik koji sadrži i `$` i `-` proizveo je
`[\$-\t]`, opseg u obrnutom redosledu, i **ceo locale nije uspeo da se
kompajlira**. Mera: `escapeForCharacterClass()` štiti svaki jednobajtni
ne-alfanumerički znak, a višebajtne UTF-8 znakove ostavlja doslovno (backslash
pred prvim bajtom bi ih pokvario). Uhvaćeno testom `T2.9`, pre pokretanja.

### N22 — `end` notifikacija je loša za ulančavanje, ali odličan nadzornik

Faza 0 je utvrdila da `end` kasni ~294 ms (N12) i zaključila: ne ulančavati na
njemu. Faza 2 dodaje drugu polovinu te misli: **baš zato što kasni, `end` je
tačno ono što treba za otkrivanje izgubljene oznake (R14).** Kad server kaže da
je završio, svaka oznaka tog iskaza je već morala da stigne; ako nije, oznaka
je izgubljena. `SpeechManager` tada čeka kratak rok (`lostIndexGraceMs`, 300 ms)
i nastavlja uz `DEBUG_WARNING`, umesto da zaneme zauvek.

Ovo je bolje od roka po oznaci, koji je plan tražio doslovno: rok po oznaci mora
da pogađa trajanje iskaza (koje se ne zna), a ovaj ne mora ništa da pogađa.
Postoji i apsolutni rok (`stallTimeoutMs`, 10 s) za backend koji ne kaže ništa.
Oba su testirana (`[r14]` oznaka u `test_manager.cpp`).

### N23 — izmereno na ovoj mašini, espeak-ng modul

| Metrika | p50 | p95 | n |
|---|---|---|---|
| `speak()` → prva index oznaka (SSIP povratni put) | 0.79 | 0.85 ms | 20 |
| `speak()` → poslednja oznaka („Проба") | 1082.6 | 1083.0 ms | 20 |
| iskazi koji nisu stigli do kraja | 0 | 0 | 20 |
| izgubljene oznake | 0 | 0 | 20 |

Prva oznaka stiže za **manje od milisekunde** — to je potvrda prijema SSIP
poruke za oznaku na **početku** poruke, ne početak zvuka (isti razred stvari
kao `begin`, nalaz N10). Zato je broj koristan kao regresioni čuvar ALUS-ove
putanje, a **ne** kao merenje zvuka. Za zvuk važi O3.

### N24 — korisnikov podrazumevani modul je `rhvoice`

Na ovoj mašini speech-dispatcher podrazumevano koristi `rhvoice`, modul za
koji N17 kaže da promašuje svaki vremenski budžet i ignoriše `<break>`.
`SsipSynth::timingIsGuaranteed()` vraća `false` i ALUS to **kaže u logu pri
dizanju**, umesto da ćuti i deluje sporo. `--suite live` sam prebacuje na
`espeak-ng`, jer meri ALUS-ov kod, a ne korisnikov izbor glasa.

### N25 — `spd_close()` uništava mutex dok reader nit još čeka na njemu

ThreadSanitizer nad `--suite live`:

```
WARNING: ThreadSanitizer: data race
  Write of size 1 by main thread: pthread_mutex_destroy ← spd_close
  Previous atomic read by thread T1: pthread_cond_wait ← libspeechd reader
SUMMARY: ... in spd_close
```

Oba učesnika su u `libspeechd`-u; ALUS samo poziva `spd_close()`, a drugog
API-ja za zatvaranje nema. Po standardu je to nedefinisano ponašanje, pa je
upisano kao **rizik R16** sa merom koja je moguća (otkaži govor pre zatvaranja,
nikad dve konekcije) i merom koja nije (prijaviti upstream).

> Vidi se **samo** pod TSAN-om i **samo** sa pravim serverom. Ni valgrind ni
> unit testovi ga ne nalaze.

---

## Provere koje su pokrenute

```bash
meson test -C build --suite core                      # Faza 1: 379 + 102, zeleno
meson test -C build --suite speech                    # 121 slučajeva, 20897 tvrdnji
meson test -C build --suite live                      # T2.11 (traži speech-dispatcher)
meson test -C build --setup=valgrind --suite speech   # ERROR SUMMARY: 0, izgubljeno 0 bajtova
./build-tsan/tests/test-speech                        # ThreadSanitizer: čisto
./build-tsan/tests/test-speech-live                   # 1 upozorenje, u libspeechd-u (N25)

# Definicija završenog za demon, sa govorom i bez njega:
valgrind --leak-check=full --suppressions=tests/valgrind.supp \
  ./build/src/alus-daemon --allow-multiple --exit-after 3
#   definitely/indirectly/possibly lost: 0 bajtova, ERROR SUMMARY: 0 grešaka
meson install -C build --destdir /tmp/stage --skip-subprojects

# Pravilo 3 (restartabilnost u procesu) za novi podsistem:
./build/src/alus-daemon --allow-multiple --exit-after 8 --log-level info &
kill -HUP %1    # veza ka speech-dispatcher-u se sruši i ponovo digne, bez restarta procesa
kill -TERM %1
```

Dve napomene o alatima:

- **PCRE2 JIT i Memcheck.** `G_REGEX_OPTIMIZE` uključuje JIT, čiji generisani
  kod čita subject u blokovima od 16 bajta i nema simbole za unwinder, pa
  Memcheck prijavljuje „invalid read" i „conditional jump on uninitialised
  value" sa bezimenim okvirima. Suppression po okviru je nemoguć (JIT kod nema
  objekat), pa `ALUS_REGEX_NO_JIT=1` isključuje JIT, a `add_test_setup
  ('valgrind')` ga postavlja. U produkciji JIT ostaje — simbolički regex radi
  nad svakim iskazom.
- **`--skip-subprojects` pri instalaciji.** Catch2 subprojekat nema opciju da
  se ne instalira, pa bi `meson install` bez te zastavice ubacio Catch2
  header-e u prefiks. Pakovanje je posao Faze 9; dotad važi zastavica.

---

## Šta je napravljeno

| Modul | Fajl | Linija |
|---|---|---|
| `speech::Command` (cela hijerarhija) | `speech/commands.{hpp,cpp}` | 843 |
| `speech::Priority` | `speech/priority.hpp` | 36 |
| `speech::ISynth`, `NullSynth` | `speech/synth.{hpp,cpp}` | 360 |
| `speech::SsipSynth` + SSML graditelj | `speech/ssip_synth.{hpp,cpp}` | 922 |
| `speech::SpeechManager`, `ParamChangeTracker` | `speech/manager.{hpp,cpp}` | 1039 |
| `speech::SentenceBuffer` | `speech/sentence_buffer.{hpp,cpp}` | 286 |
| `speech::Presenter` | `speech/presenter.{hpp,cpp}` | 480 |
| `speech::Dictionaries` | `speech/dictionaries.{hpp,cpp}` | 613 |
| `speech::Service` (restartabilan podsistem) | `speech/service.{hpp,cpp}` | 283 |
| `text::Regex` (GRegex omotač) | `text/regex.{hpp,cpp}` | 501 |
| `text::Symbols` + ugrađeni podaci | `text/symbols*.{hpp,cpp}` | 1162 |
| `audio::WavePlayer`, sink-ovi, WAVE | `audio/wave_player.{hpp,cpp}` | 885 |
| `audio::Tones` | `audio/tones.{hpp,cpp}` | 124 |
| `ui::message` | `ui/message.{hpp,cpp}` | 76 |
| `alus-cli` | `cli/main.cpp` | 482 |
| **ukupno izvorni kod Faze 2** | | **8092** |
| Testovi Faze 2 | `tests/{speech,text,audio}/` | 2863 |
| Podaci | `data/symbols/{en,sr}.dic` | 196 |

`alus-daemon` sada drži **dva** restartabilna podsistema (log, govor), pa
`SIGHUP` ponovo diže i vezu ka speech-dispatcher-u, bez restarta procesa
(pravilo 3 iz `phase-plan.md`).

---

## Predaja Fazi 3

Faza 3 (ulaz i skriptovi) ne zavisi ni od čega iz Faze 2 osim od
`ui::message()`, koji postoji. Ono što joj Faza 2 ostavlja:

1. **`Presenter::setOnDemandPredicate()`** čeka skript sloj: `onDemand` režim
   govora je implementiran, ali bez Faze 3 nema ko da kaže šta je „na zahtev".
2. **`isScriptWaiting()` prekid govora** (T3.9) ima na čemu da radi:
   `Presenter::cancelSpeech()` i `SpeechManager::cancel()` su na mestu.
3. **Četiri nedokazana koraka S1-a** iz Faze 0 i dalje stoje kao prvi posao
   Faze 3 (`RESULTS.md`, „Šta još treba za zatvaranje Faze 0").
