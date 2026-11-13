# ALUS — dokumentacija plana

Inženjerski plan za **ALUS** (**A**ccess **L**inux **U**sing **S**peech), NVDA-kompatibilan čitač ekrana za Linux u C++20.

Čitati po redu:

| Dokument | Sadržaj | Linija |
|---|---|---|
| [`00-VIZIJA.md`](00-VIZIJA.md) | Ciljevi, **ne-ciljevi za v1.0** (obavezujući), licenca, rečnik pojmova, merila uspeha | 117 |
| [`01-ARHITEKTURA.md`](01-ARHITEKTURA.md) | Tehnološki stek, mapiranje NVDA → ALUS → Linux API (svih 45 modula), threading model, svaki podsistem | 1029 |
| [`02-FAZE.md`](02-FAZE.md) | Faze 0–9, svaka sa **izvršivim** izlaznim kriterijumom i testovima | 506 |
| [`03-RIZICI.md`](03-RIZICI.md) | 16 rizika sa uzrokom, merom i detektorom; plus 11 rizika **razrešenih istraživanjem** | 450 |

Izveštaji o izvršenim fazama:

| Dokument | Faza | Sadržaj |
|---|---|---|
| [`../spikes/RESULTS.md`](../spikes/RESULTS.md) | 0 | tri spike-a, tri odluke, nalazi N1–N19 |
| [`FAZA2.md`](FAZA2.md) | 2 | govor: status T2.1–T2.11, šest odstupanja, nalazi N20–N25 |
| [`FAZA3.md`](FAZA3.md) | 3 | ulaz i skriptovi: status T3.1–T3.10, četrnaest odstupanja, nalazi N26–N35, **dijagnoza N19** |
| [`FAZA4.md`](FAZA4.md) | 4 | objektni model: status T4.1–T4.9, sedamnaest odstupanja, nalazi N36–N44, merenje na živoj sesiji |
| [`FAZA5.md`](FAZA5.md) | 5 | tekst i review: status T5.1–T5.9, petnaest odstupanja, nalazi N45–N51, **T5.3 delimično** (nema `libicu-dev`) |
| [`FAZA6.md`](FAZA6.md) | 6 | browse mode: status T6.1–T6.10, šesnaest odstupanja, nalazi N52–N61, **T6.7 delimično** (nema pokrenut pregledač) |
| [`FAZA7.md`](FAZA7.md) | 7 | brajev red: status T7.1–T7.8 **svih osam prošlo**, devet odstupanja, nalazi N62–N65, **ručni test nije izvršen** (nema `brltty`) |
| [`FAZA8.md`](FAZA8.md) | 8 | app moduli i ekstenzibilnost: status T8.1–T8.7 **svih sedam prošlo**, petnaest odstupanja, nalazi N66–N74, **sva četiri modula prepisana posle živog merenja** |
| [`FAZA9.md`](FAZA9.md) | 9 | GUI i lokalizacija: status T9.1–T9.5 **svih pet prošlo**, petnaest odstupanja, nalazi N75–N87, **prozor je sopstveni proces** (N79) |
| [`FAZA10.md`](FAZA10.md) | 10 | komande koje su se najavljivale a nisu bile napisane: **29 napisano** (`[announce]` 47 → 18), osam odstupanja, nalazi N107–N116, **GTK 4 se čitao kao onemogućen** (N111) i **browse mode u Chrome-u** (N112–N116), oba nađena živim merenjem |

## Kako je plan nastao

Plan je izveden iz dva izvora, a ne iz pretpostavki:

1. **Detaljna analiza NVDA izvornog koda** u `../NVDA` (189k linija Pythona, commit `8475635`, 2026-11-02) — jezgro (`core.py`, `queueHandler.py`, `eventHandler.py`, `baseObject.py`, `watchdog.py`), ulaz (`inputCore.py`, `keyboardHandler.py`, `scriptHandler.py`, `globalCommands.py`), govor (`speech/`), objektni i tekstualni model (`NVDAObjects/`, `textInfos/`, `browseMode.py`, `compoundDocuments.py`), brajev red (`braille/`).
2. **Merenja i introspekcija na ciljnom sistemu** (Debian 13, GNOME 48.7, Wayland, AT-SPI 2.56.2) — latencija AT-SPI poziva, D-Bus interfejsi kompozitora, SSIP mogućnosti, dostupnost zavisnosti.

Zato **svaka platformska tvrdnja u dokumentima ima uz sebe komandu kojom se proverava.** Verzije se menjaju; tvrdnje moraju da ostanu proverljive.

## Brza provera okruženja

Sve komande ispod su pokrenute i potvrđene 2026-11-04 na ciljnom sistemu:

```bash
# Stek
pkg-config --modversion atspi-2 speech-dispatcher liblouis libbrlapi glib-2.0 xkbcommon
g++ -std=c++20 -fsyntax-only -x c++ - <<< '#include <coroutine>
#include <format>
#include <ranges>
int main(){}'

# Ulaz tastature na Wayland-u (najveći rizik — vidi 03-RIZICI.md R1)
gdbus introspect --session --dest org.freedesktop.a11y.Manager \
  --object-path /org/freedesktop/a11y/Manager
strings /usr/lib/x86_64-linux-gnu/libmutter-16.so.0.0.0 | grep KeyboardMonitor
busctl --user list | grep -i keyboardmonitor

# Ulaz: da li ALUS dobija tastaturu, i ako ne — zašto (Faza 3)
./build/src/alus-cli input-info
./build/src/alus-cli gesture 'kb:alus+1'

# Govor
strings /usr/lib/x86_64-linux-gnu/libspeechd.so.2 | grep "SET SELF NOTIFICATION"
grep -i indexing /etc/speech-dispatcher/modules/espeak-ng.conf

# Segmentacija (Faza 5): ICU je obavezan po 01-ARHITEKTURA.md §7.3, a build ga
# traži kao opcionu zavisnost sa glasnim fallback-om (FAZA5.md O2)
pkg-config --modversion icu-uc || echo 'nema libicu-dev: japanski i tajlandski nisu tačni'
./build/src/alus-cli text-info | head -1

# Browse mode (Faza 6): da li dokument izlaže Collection, i šta quick-nav košta
python3 -c "import gi; gi.require_version('Atspi','2.0'); from gi.repository import Atspi
print('collection:', [m for m in dir(Atspi.Collection) if 'match' in m])
print('sort:      ', [s for s in dir(Atspi.CollectionSortOrder) if s.isupper()])"
./build/src/alus-cli browse-info

# AT-SPI mogućnosti
python3 -c "import gi; gi.require_version('Atspi','2.0'); from gi.repository import Atspi
print('role/state:', len([r for r in dir(Atspi.Role) if r.isupper()]),
                      len([s for s in dir(Atspi.StateType) if s.isupper()]))
print('granularnost:', [g for g in dir(Atspi.TextGranularity) if g.isupper()])
print('collection:', [m for m in dir(Atspi.Collection) if 'match' in m])"

# GUI i lokalizacija (Faza 9)
pkg-config --modversion gtk4 libadwaita-1
which xgettext msgfmt msginit msgmerge
./build/src/alus-cli gui-info     # može li prozor, koji je jezik, koji katalozi
./build/src/alus-cli autostart    # šta se diže sa sesijom, i drži li drugi čitač taster

# Srpski
ls /usr/share/liblouis/tables/ | grep -E '^sr-'
cat reference/nvda/source/locale/sr/gestures.ini
./build/src/alus-cli --language sr list-commands | head   # mora biti ćirilica
```

## Dokle se stiglo

- **Faza 0** — završena. `spikes/RESULTS.md`: tri odluke i 19 nalaza; kod je bačen, brojevi su ostali.
- **Faza 1** (infrastruktura jezgra) — završena. `meson test -C build --suite core`.
- **Faza 2** (govor) — završena. `claude/FAZA2.md`, `meson test -C build --suite speech`.
- **Faza 3** (ulaz i skriptovi) — završena. `claude/FAZA3.md`,
  `meson test -C build --suite input`. N19 je dijagnostikovan i mera ugrađena;
  ulazni backend razgovara direktno sa `org.freedesktop.a11y.KeyboardMonitor`,
  a komandna tabela (440 skriptova, 319 vezivanja, 28 jezika) se **generiše**.
- **Faza 4** (objektni model) — završena. `claude/FAZA4.md`,
  `meson test -C build --suite a11y`. Tačka na kojoj ALUS prvi put „radi":
  fokus, lanac predaka, uloge i stanja, objektna navigacija. N36 i N43 su dva
  defekta koja bi čitač ekrana učinila nemim bez ijedne greške u logu.
- **Faza 5** (tekst i review) — završena. `claude/FAZA5.md`,
  `meson test -C build --suite text --suite nav`. **Ovo je alfa za desktop
  aplikacije**: linije, reči i znakovi, review kursor u tri moda, say-all koji
  pomera karetu sa zvukom, terminal koji najavljuje samo nov izlaz,
  formatiranje na `alus+f`. Jedan kriterijum (T5.3, japanska segmentacija) čeka
  `libicu-dev`; sve ostalo je zeleno i izmereno.

- **Faza 6** (browse mode) — završena. `claude/FAZA6.md`,
  `meson test -C build --suite browsemode`. **Ovo je beta**: dokument se čita
  kao dokument — `getTextWithFields` nad drvetom (§9.3, najvažniji nov kod u
  projektu), quick-nav preko `Collection` (jedan D-Bus poziv po tasteru),
  virtualni caret, auto focus mode, tabela navigacija. N52 je bug koji bi
  zalepio karetu na kraj svakog paragrafa; N55 je bug koji ne pada, ne loguje i
  ne curi — samo laže korisniku da je izašao iz tabele u kojoj stoji. Jedan
  kriterijum (T6.7, milisekunde quick-nav-a) čeka pokrenut pregledač.

- **Faza 8** (app moduli i ekstenzibilnost) — završena. `claude/FAZA8.md`,
  `meson test -C build --suite app`. Pet mehanizama prilagođenja iz §12.2, tri
  generalizovana hook-a, koreografija prebacivanja aplikacije u **jednoj**
  transakciji profila, i **stabilan C ABI** za add-on-e (`alus/alus-plugin.h`)
  sa `dlopen`-om i dvofaznom deinstalacijom. Četiri app modula, i **sva četiri
  su prepisana posle merenja na živim aplikacijama** — drvo koje LibreOffice i
  Nautilus stvarno izlažu nije ono koje plan pretpostavlja (N66–N71). Jedno od
  tih merenja je našlo **N68**: Calc prijavljuje 2147483647 dece, pa je jedan
  `children()` bio 32 GB alokacije i dve milijarde D-Bus poziva — defekt Faze 4
  koji bi zamrzao čitač ekrana i koji nijedan mock ne bi našao.

- **Faza 9** (GUI i lokalizacija) — završena. `claude/FAZA9.md`,
  `meson test -C build --suite gui`. **Ovo je poslednja faza iz `02-FAZE.md`.**
  Prozor podešavanja (8 stranica, 90 podešavanja), dijalog gestova nad celom
  komandnom tabelom, gettext sa `sr` i `sr@latin` katalogom — oba **100%
  prevedena**, od toga 738 od 1102 msgid-a **nasleđeno iz NVDA-inog srpskog po
  msgid-u** i transliterirano u ćirilicu. Generisana referenca komandi,
  autostart `.desktop`, systemd user unit.

  Faza je proizvela nalaz koji je promenio arhitekturu: **prozor podešavanja ne
  može da stoji u procesu čitača ekrana.** U demonu je jezgro stalo **7,5
  sekundi** i prozor nije mogao da se pročita ni jednom svojstvo — ALUS-ov
  sinhroni AT-SPI poziv o sopstvenom prozoru odgovara iz iste petlje koju taj
  poziv blokira (**N79**). Prozor je zato `alus-settings`, sopstveni proces, i
  mera se proverava uživo u `tools/phase9-gui-check.sh`. Prvi prolaz prevoda je
  našao još dva defekta koja nijedan test ne bi: najave su se gradile spajanjem
  fragmenata rečenice (**N75**) i množina se odlučivala u C++-u, a srpski ima
  tri forme (**N76**).

## Kako se ALUS proba

Posle Faze 6 ALUS je **beta za desktop i za veb**. Da bi se probao:

```bash
# 1. Orca mora da stane — drži org.freedesktop.a11y.KeyboardMonitor (N8)
#    i ALUS onda ne dobija ni jedan taster.
#    (U GNOME-u: Settings → Accessibility → Screen Reader, ili `pkill orca`.)

# 2. Proveri da li ALUS može da dobije tastaturu, PRE pokretanja
./build/src/alus-cli input-info

# 3. Pokreni
./build/src/alus-daemon --log-level info --log-file ~/alus.log

# 4. Podešavanja: alus+control+g, ili iz menija aplikacija, ili
./build/src/alus-settings --page speech

# 5. Na srpskom (ćirilica), bez menjanja jezika cele sesije:
./build/src/alus-daemon --language sr
#   ili trajno:  ~/.config/alus/alus.ini  ->  [general] language = sr
```

Ako kompozitor odbije tastaturu, **ALUS to izgovori** — ne prećuti. To je
odluka iz Faze 0 (N1, N7, N15, N18: četiri puta se desilo da čitač ekrana
krene, kaže „ALUS started" i više ne odgovori ni na jedan taster).

Dijagnostika, sva čita samo i bezbedna je dok ALUS ili Orca rade:

```bash
./build/src/alus-cli a11y-info      # radi li objektni model
./build/src/alus-cli text-info      # šta tekstualni sloj vidi, i šta košta
./build/src/alus-cli browse-info    # radi li browse mode, i šta quick-nav košta
./build/src/alus-cli app-info       # koji app modul dobija svaka pokrenuta aplikacija
./build/src/alus-cli gui-info       # može li prozor podešavanja, i koji je jezik
./build/src/alus-cli autostart      # diže li se ALUS sa sesijom, i ko mu otima taster
```

Planovi ručnog testa — jedino što mašina ne može da izmeri, **šta korisnik
čuje**:

```bash
bash tools/phase5-text-check.sh --manual   # Text Editor i terminal
bash tools/phase6-browse-check.sh --manual # Wikipedia u Firefox-u i Chromium-u,
                                           # tabela, LibreOffice Writer
bash tools/phase9-gui-check.sh --manual    # prečice podešavanja, dijalog gestova,
                                           # srpski, i čitanje samog prozora
```

## Sledeći korak

**Nijedan — `02-FAZE.md` je dovršen.** Faze 0–9 su izvršene i svaka ima
izvršive izlazne kriterijume koji prolaze.

Stanje na kom se nastavlja (provereno 2026-11-07):

```bash
meson setup build && meson compile -C build
meson test -C build                       # 19 testova, 18 prošlo + 1 očekivani pad
meson test -C build --suite gui           # 8474 asertacije — izlazni kriterijum Faze 9
meson test -C build --suite external      # msgfmt i generatori, pravim alatima
meson test -C build --setup=valgrind --suite core --suite config --suite speech \
         --suite input --suite a11y --suite text --suite nav --suite browsemode \
         --suite braille --suite app --suite gui
                                          # 13/13, 0 grešaka, 0 izgubljenih bajtova
meson test -C build --suite live          # govor, objektni model i tekst nad pravom sesijom
```

Šta ostaje do v1.0 nije nedovršena faza, nego tri vrste stvari — pun spisak je
u `FAZA9.md`, „Predaja":

1. **Traži okruženje, ne kod.** Tri kriterijuma iz ranijih faza (vidi niže).
2. **Traži čoveka koji sluša.** Šest planova ručnog testa je napisano i
   izvršno; mašina ne može da potvrdi da je ono što se čuje razumljivo.
3. **Traži odluku, ne rad.** Tri svesno ostavljene stvari: peti sloj gestova u
   dijalogu (app moduli i plugin-i — dijalog je drugi proces, `FAZA9.md` O7),
   uređivači rečnika i dijalog profila (prečice postoje i **kažu** da ih nema,
   O12), i dugme za add-on manager (`alus-cli app-info` je model).

Tri kriterijuma iz ranijih faza čekaju okruženje, ne kod:

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

Merenja koja se ponavljaju jednom komandom, kad god treba proveriti da na novom
sistemu stvarno radi:

```bash
SAFETY_CHECK=1 bash tools/phase3-input-check.sh   # ulaz: proba staze (Orca dole ~5 s)
bash tools/phase3-input-check.sh                  # ulaz: pravo merenje
bash tools/phase4-object-check.sh                 # objektni model
bash tools/phase5-text-check.sh                   # tekstualni sloj
bash tools/phase6-browse-check.sh                 # browse mode
bash tools/phase6-browse-check.sh --manual        # + plan ručnog testa
bash tools/phase7-braille-check.sh                # brajev red: liblouis, tabele, brltty
bash tools/phase7-braille-check.sh --manual       # + plan ručnog testa (traži brltty)
bash tools/phase8-app-check.sh                    # app moduli: ključevi, ABI, plugin-ovi
bash tools/phase8-app-check.sh --manual           # + plan ručnog testa
bash tools/phase9-gui-check.sh                    # GUI i prevod; korak 6 je nalaz N79
bash tools/phase9-gui-check.sh --manual           # + plan ručnog testa
```

Regenerisanje svega što je generisano iz NVDA-inog koda ili iz podataka:

```bash
python3 tools/extract-commands.py --source ./NVDA --out data/ \
        --strings-out src/i18n/data_strings.cpp   # komande, gestovi, labele, msgid-i
python3 tools/extract-controltypes.py --source ./NVDA --out .   # uloge i stanja
python3 tools/update-potfiles.py                  # po/POTFILES.in
meson compile -C build alus-pot                   # po/alus.pot
python3 tools/import-nvda-po.py                   # po/sr.po, po/sr@latin.po
python3 tools/generate-command-reference.py       # docs/commands.md, docs/commands.csv
```
