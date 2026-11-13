# Merenje: ALUS 0.1.0 prema Orci 48.1

Protokol i rezultati. Mašina: Debian 13, GNOME 48, **Wayland**, AT-SPI 2.56.2,
speech-dispatcher sa rhvoice (glas `Marija`, `hr`) i espeak-ng.

## 0. Bezbednost: kako se Orca vraća

ALUS i Orca **ne mogu zajedno** da drže tastaturu. Oba registruju D-Bus ime
`org.gnome.Orca.KeyboardMonitor` kod GNOME-ovog a11y menadžera, pa jedan od
njih ne dobija nijedan taster (to je nalaz N8 iz Faze 0, ovde potvrđen na
pokrenutoj sesiji). Zato merenje ide **naizmenično**, i zato ovaj odeljak stoji
prvi.

Četiri nezavisna načina da se Orca vrati, od najmanje do najviše truda:

1. **Samo čekaj.** ALUS je pokrenut sa `--exit-after 300`, pa se sam ugasi
   posle pet minuta.
2. **`alus+q`** — Insert+q. Gasi ALUS odmah, i to izgovori (*„izlazim"*).
3. **Automatski čuvar.** Pre gašenja Orce pokrenut je odvojen proces koji
   posle 330 s vraća Orcu **bez obzira na sve** — i ako ovaj terminal umre, i
   ako ALUS zablokira, i ako sesija sa Claude-om prestane da odgovara.
4. **Ručno**, u ovom terminalu:

   ```
   ! orca --replace &
   ```

   (`!` ispred komande je pokreće u ovoj sesiji.)

ALUS taster je **Insert** ili **numpad Insert** (CapsLock *nije* ALUS taster po
podrazumevanom podešavanju). Raspored je `desktop`.

## 1. Šta pritisnuti, po redu

Prvih pet su ono zbog čega je faza 10 postojala — podešavanje sinteze bez
prozora. Ostalo su nove komande.

| # | Taster | Šta mora da se čuje |
|---|---|---|
| 1 | `Insert+control+→` (4×) | imena podešavanja u prstenu: **Brzina**, **Visina**, **Modulacija**, **Jačina**, pa ponovo Brzina. Uz svako i trenutna vrednost |
| 2 | `Insert+control+↑` / `↓` | nova vrednost (korak 5), i govor se **odmah** menja tom brzinom |
| 3 | `Insert+control+pageUp` / `pageDown` | korak 20 |
| 4 | `Insert+control+c` | *„Podešavanja su sačuvana"* |
| 5 | `Insert+control+r` 1× / 3× | *„Podešavanja su primenjena"* / *„Podešavanja su vraćena na fabrička"* |
| 6 | `Insert+s` | kroz režime govora: on-demand, isključen, pištanje, govor |
| 7 | `Insert+p` | nivo interpunkcije: neki, većina, svi, nijedan |
| 8 | `Insert+f12` 1× / 2× | vreme / datum, na srpskom |
| 9 | `Insert+shift+b` | *„Nema sistemske baterije"* (ova mašina je desktop) |
| 10 | `Insert+x` | ponovi poslednje izgovoreno; drugi pritisak srica |
| 11 | `Insert+t`, `Insert+tab` | naslov prozora, pa šta ima fokus |
| 12 | `Insert+f1` | *„Razvojne informacije su upisane u dnevnik"* |
| 13 | u pregledaču: `h`, `p`, `shift+p` | naslov, pa pasusi članka — `p` mora da **preskoči** menije i redove linkova |
| 14 | `Insert+shift+space`, pa `h` | navigacija jednim slovom se gasi; `h` posle toga **upisuje h** |
| 15 | `Insert+q` | *„izlazim"* (ili pusti da istekne 300 s) |

Ne mora sve. Prvih pet i 8–10 su dovoljni da merenje ima sadržaj.

## 2. Šta se meri iz dnevnika

ALUS se pokreće sa `--log-level debug`, pa dnevnik nosi i trenutak kad je
taster stigao i trenutak kad je govor predat sintetizatoru. Odatle:

* **koji skript je odgovorio na koji taster** (i da li je ijedan ostao tih),
* **kašnjenje taster → predaja govora**, koje je ALUS-ov sopstveni deo puta,
* broj grabova koje je kompozitor prihvatio.

Ono što se **ne meri ovde**: kašnjenje do prvog zvuka. To je svojstvo
speech-dispatcher modula i isto je za oba čitača (`spikes/RESULTS.md` S2:
11,5 ms p95 za espeak-ng; rhvoice ne poštuje nijedan budžet, N17), pa ne
razlikuje ALUS od Orce i nije deo poređenja.

## 3. Rezultati

Prolaz: 2026-11-09, 10:28:14–10:33:14, ALUS sa `--log-level debug --exit-after
300`. Dnevnik: 1664 linije, 94 → 210 grabova, glas `Marija`.

### 3.1 Šta je korisnik uradio

Nije prošao tabelu iz odeljka 1 — otvorio je **prozor podešavanja**
(`alus+control+s`, dva puta) i 20 puta tabom, strelicama i enterom pokušao da
nešto promeni. To je bila bolja proba od tabele: išao je tačno onamo gde mu je
od početka bilo teško.

### 3.2 Glavni nalaz: N111

Od **207** najava u tom prozoru, **161** je sadržalo *„недоступно"*. Uzrok,
izmeren nezavisnom bibliotekom (Atspi/GI, mimo ALUS-ovog koda): **GTK 4
postavlja `SENSITIVE` na svaki objekat, a `ENABLED` ni na jedan**, a ALUS je
tražio oba. Orca gleda samo `SENSITIVE`. Popravljeno; detalji i sva tri koraka
dijagnoze su u `FAZA10.md`, N111.

| `[unavailable]` u `alus-settings` (199 objekata) | |
|---|---|
| pre popravke | svi |
| **posle** | **0** |

### 3.3 Merenja bez tastature

| Mera | Orca 48.1 | ALUS 0.1.0 | Kako |
|---|---|---|---|
| RSS posle starta | **72,7 MB** (8 s) | **18 MB** (6 s, objektni model + app sloj) | `ps -o rss` |
| Niti | 13 | 7 | `ps -o nlwp` |
| Komanda sa podrazumevanim tasterom | ~196–204 | 241, od toga **220 radi** | Orca: `KeyBinding(` sa ključem + tabela strukturne navigacije; ALUS: `data/gestures-builtin.ini` − `[announce]` |
| Brzina / visina / jačina / sinteza / interpunkcija s tastature | **0 od 8 vezano** (brzina ±, visina ±, jačina ±, sinteza, interpunkcija) | **svih 8 vezano** (prsten ×6, `alus+s`, `alus+p`) | Orca: `speech_and_verbosity_manager.py:178-225`, sve `KeyBinding("", ...)` |
| `[unavailable]` za osetljive kontrole GTK 4 | 0 | 0 (bilo: svi) | posle N111 |

Mera RSS-a je sada fer: Orca **startuje** na 72,7 MB, pa ranija vrednost od 77
MB posle 3,2 h nije bila rast.

### 3.4 Šta nije izmereno

* **Kašnjenje taster → govor.** Korisnik nije pritiskao komandne tastere iz
  tabele, a za navigaciju po prozoru ALUS nije beležio vreme predaje govora na
  nivou koji bi dozvolio pouzdanu meru. Ostaje za sledeći prolaz — i treba ga
  meriti **posle** N111, jer je pre njega svaka najava bila duža za jednu reč.
* **Veb stranica** (`p`, `h`, `k`). Pregledač nije bio otvoren.

### 3.5 Operativno: Orca se nije sama vratila

`gsettings set ... screen-reader-enabled true` posle gašenja ALUS-a **nije**
ponovo pokrenuo Orcu (pokrenula ju je gnome-session kao autostart, ne
gnome-shell preko podešavanja), i prvi `orca --replace` iz pozadine nije
preživeo. Orca je podignuta ručno sa `setsid orca`, i provereno je da drži
`org.gnome.Orca.KeyboardMonitor`. Za sledeći prolaz čuvar treba da koristi
`setsid orca` i da **proveri D-Bus ime**, ne samo proces.

## 4. Sledeći prolaz

1. Ponoviti prozor podešavanja: najave više ne smeju da sadrže *„недоступно"*
   osim za stvarno onemogućene kontrole.
2. Prsten (`Insert+control+strelice`) i `Insert+control+c` — prvih pet redova
   tabele iz odeljka 1.
3. Isto u Orci, istim redom, da poređenje ima dva stupca.
4. Pregledač: `h`, `p`, `shift+p` u istom članku.
