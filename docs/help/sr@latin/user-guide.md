# Uputstvo za korisnike programa ALUS

Verzija 2026.1.0 beta

## Sadržaj

- [Uvod](#uvod)
- [Sistemski zahtevi](#sistemski-zahtevi)
- [Instalacija](#instalacija)
- [Pokretanje i zaustavljanje](#pokretanje-i-zaustavljanje)
- [Prvi koraci](#prvi-koraci)
- [ALUS meni](#alus-meni)
- [Čitanje](#čitanje)
- [Objektna navigacija i kursor pregleda](#objektna-navigacija-i-kursor-pregleda)
- [Veb stranice i pošta: režim pretraživanja](#veb-stranice-i-pošta-režim-pretraživanja)
- [Kucanje i uređivanje: režim fokusa](#kucanje-i-uređivanje-režim-fokusa)
- [Aplikacije](#aplikacije)
- [Govor](#govor)
- [Brajev red](#brajev-red)
- [Podešavanja](#podešavanja)
- [Promena tastera](#promena-tastera)
- [Jezik](#jezik)
- [Rešavanje problema](#rešavanje-problema)
- [Pomoć i podrška](#pomoć-i-podrška)

## Uvod

ALUS (Access Linux Using Speech) je čitač ekrana za Linux. Ono što je na
ekranu čita sintetizovanim govorom i na brajevom redu, tako da slepi i
slabovidi mogu da koriste računar pomoću tastature.

ALUS radi sa GNOME radnom površinom na Wayland-u, veb pregledačima (Firefox,
Chrome, Chromium), poštom (Thunderbird), paketom LibreOffice, upravljačem
datoteka, terminalima i većinom GTK, Qt i Electron aplikacija.

Ako ste koristili NVDA na Windows-u, već poznajete veći deo programa ALUS:
komande, njihovi tasteri i pojmovi -- fokus, kursor pregleda, navigacioni
objekat, režim pretraživanja, režim fokusa, čitaj sve -- isti su. Ovo
uputstvo objašnjava i ono što je posebno za Linux.

ALUS je slobodan softver, objavljen pod GNU opštom javnom licencom, verzija 2
ili novija.

## Sistemski zahtevi

- Linux distribucija sa GNOME radnom površinom na Wayland-u (razvijano i
  provereno na Debian-u 13 sa GNOME 48).
- speech-dispatcher sa bar jednim sintetizatorom govora: preporučuje se
  espeak-ng; rade i RHVoice i drugi.
- Magistrala pristupačnosti (at-spi2-core), koju ima svaka GNOME instalacija.
- Za brajev red: brajev red koji podržava brltty, i liblouis.

## Instalacija

ALUS se gradi iz izvornog koda i instalira pomoću alata Meson. Spisak paketa
koje prvo treba instalirati nalazi se u datotekama `docs/building.md` i
`docs/installing.md` u izvornom kodu. Ukratko:

```
meson setup build --prefix=/usr/local --buildtype=release
ninja -C build
sudo meson install -C build
```

Instalacija ništa ne menja u vašoj sesiji: ALUS se ne pokreće sam, a čitač
ekrana radne površine (Orca) radi dalje dok drugačije ne odlučite.

## Pokretanje i zaustavljanje

Tastaturu u jednom trenutku može da koristi samo jedan čitač ekrana. Da biste
isprobali ALUS dok radi Orca, otvorite terminal i upišite:

```
alus-switch alus nolog
```

Skripta zaustavlja Orcu, pokreće ALUS i naglas izgovara svaki korak. `nolog`
znači da se ništa ne upisuje u datoteku dnevnika. Da biste tastaturu vratili
Orci:

```
alus-switch other
```

Važno: **ne** isključujte Orcu prečicom `Alt+Super+S` pre pokretanja programa
ALUS. Ta prečica isključuje i prekidač čitača ekrana radne površine, pa pregled
aktivnosti (taster `Super`) posle toga ništa ne najavljuje. Skripta sama
zaustavlja Orcu i ne dira prekidač.

Da bi se ALUS pokretao pri svakoj prijavi: `alus-cli autostart --enable`. Da
biste to isključili: `alus-cli autostart --disable`.

ALUS zatvarate tasterima `ALUS+Q` ili stavkom Izlaz u ALUS meniju. ALUS pre
zatvaranja kaže „izlazim“. Ako ste ALUS pokrenuli skriptom `alus-switch`,
umesto toga koristite `alus-switch other`, da bi se Orca vratila.

## Prvi koraci

### ALUS taster

ALUS komande se pritiskaju zajedno sa **ALUS tasterom**. Podrazumevano su ALUS
tasteri i `Insert` i `Insert` na numeričkoj tastaturi; `Caps Lock` se može
dodati u Podešavanjima → Tastatura. U ovom uputstvu ALUS taster se piše
`ALUS`, pa `ALUS+T` znači: držite Insert i pritisnite T.

Dva brza pritiska ALUS tastera šalju aplikaciji pravi Insert.

### Rasporedi tastature

ALUS ima **desktop** raspored, koji koristi numeričku tastaturu, i **laptop**
raspored, koji umesto nje koristi slova. Birate ga u Podešavanjima →
Tastatura → Raspored. Komande na numeričkoj tastaturi rade kada je Num Lock
isključen.

### Prve komande koje treba naučiti

| Tasteri | Šta radi |
|---|---|
| `ALUS+A` (laptop: `ALUS+N`) | otvara ALUS meni |
| `ALUS+1` | pomoć za unos: svaki taster kaže šta radi umesto da to uradi; ponovni pritisak je isključuje |
| `Control` | prekida govor |
| `Shift` | pauzira govor; ponovni pritisak ga nastavlja |
| `ALUS+Tab` | kaže šta je u fokusu |
| `ALUS+T` | kaže naslov prozora |
| `ALUS+Strelica gore` (laptop: `ALUS+L`) | čita trenutni red |
| `ALUS+Strelica dole` (laptop: `ALUS+A`) | čitaj sve: čita odavde do kraja |
| `ALUS+F12` | vreme; dva pritiska za datum |
| `ALUS+Q` | zatvara ALUS |

Dvostruki pritisak komande za izveštavanje sriče ono što je rečeno; trostruki
sriče sa opisima slova („a, Avala“).

Komande koje na Linux-u nisu dostupne ipak odgovaraju na pritisak: kažu zašto,
tako da nijedan taster nije nem.

## ALUS meni

Pritisnite `ALUS+A` (laptop: `ALUS+N`) da otvorite ALUS meni. Krećete se
strelicama gore i dole, a birate tasterom Enter. Svaka stavka ima i slovo koje
je odmah bira. `Escape` zatvara meni.

| Stavka | Šta radi |
|---|---|
| Podešavanja… | otvara prozor podešavanja |
| Komande tastature… | prikazuje svaku komandu i njene tastere, i omogućava da ih promenite |
| Pomoć | otvara podmeni: Uputstvo za korisnike, Brzi pregled komandi, Šta je novo, O programu ALUS |
| Doniraj… | kako da podržite ALUS donacijom |
| Sačuvaj podešavanja | upisuje trenutna podešavanja na disk |
| Vrati sačuvana podešavanja | poništava izmene od poslednjeg čuvanja |
| Vrati fabrička podešavanja | vraća svako podešavanje na početnu vrednost |
| Izlaz | zatvara ALUS |

Podmeni Pomoć otvarate strelicom desno ili tasterom Enter, a iz njega se
vraćate strelicom levo ili tasterom Escape. Uputstvo i Šta je novo otvaraju se
u veb pregledaču. **Brzi pregled komandi** navodi svaku komandu sa tasterima
koji je upravo sada pokreću, uključujući tastere koje ste sami dodelili.

## Čitanje

| Tasteri | Šta čita |
|---|---|
| `ALUS+Strelica gore` (laptop: `ALUS+L`) | trenutni red |
| `ALUS+Strelica dole` (laptop: `ALUS+A`) | čitaj sve, od kursora do kraja |
| `ALUS+B` | ceo prozor u prvom planu |
| `ALUS+End` (laptop: `ALUS+Shift+End`) | statusnu traku |
| `ALUS+Shift+F` | formatiranje na mestu kursora |
| `ALUS+C` | tekst u ostavi; dva pritiska: sriče ga |
| `ALUS+Control+X` | kopira u ostavu ono što je ALUS poslednje izgovorio |
| `ALUS+S` | režim govora: govor, zvučni signali, isključen, na zahtev |
| `ALUS+P` | koliko interpunkcije se izgovara: nimalo, nešto, većina, sve |
| `ALUS+2` / `ALUS+3` | izgovor otkucanih znakova / reči, uključen ili isključen |

Tokom čitanja svega kursor prati čitanje. Pritisnite `Control` da prekinete:
kursor ostaje tamo gde je čitanje stalo.

## Objektna navigacija i kursor pregleda

Objektna navigacija omogućava da istražite sve što je na ekranu, a ne samo ono
što je u fokusu. Objekat na kome se nalazite je **navigacioni objekat**.

| Tasteri (desktop) | Tasteri (laptop) | Šta radi |
|---|---|---|
| `ALUS+Numerički 5` | `ALUS+Shift+O` | izveštava o navigacionom objektu |
| `ALUS+Numerički 8` | `ALUS+Shift+Strelica gore` | ide na objekat koji ga sadrži |
| `ALUS+Numerički 2` | `ALUS+Shift+Strelica dole` | ide na njegov prvi podobjekat |
| `ALUS+Numerički 4` / `6` | `ALUS+Shift+Strelica levo` / `desno` | prethodni / sledeći objekat |
| `ALUS+Numerički minus` | `ALUS+Backspace` | nazad na fokus |
| `Numerička kosa crta` | `ALUS+[` | levi klik na navigacioni objekat |
| `Numerička zvezdica` | `ALUS+]` | desni klik (kontekstni meni) na navigacioni objekat |

**Kursor pregleda** čita tekst navigacionog objekta bez pomeranja kursora:

| Tasteri (desktop) | Tasteri (laptop) | Šta radi |
|---|---|---|
| `Numerički 7` / `8` / `9` | `ALUS+Strelica gore` / `ALUS+Shift+.` / `ALUS+Strelica dole` | prethodni / trenutni / sledeći red |
| `Numerički 4` / `5` / `6` | | prethodna / trenutna / sledeća reč |
| `Numerički 1` / `2` / `3` | | prethodni / trenutni / sledeći znak |
| `Numerički plus` | `ALUS+Shift+A` | čitaj sve od kursora pregleda |

Na Wayland-u ALUS ne može da pomera pokazivač miša niti da pritiska taster
miša. Zato tasteri za klik deluju direktno na navigacioni objekat: levi klik
pritiska dugme, prati vezu ili menja polje za potvrdu; desni klik otvara
kontekstni meni kada aplikacija to dozvoljava, a inače ALUS predlaže
`Shift+F10` ili taster menija.

## Veb stranice i pošta: režim pretraživanja

Na veb stranicama i u porukama pošte ALUS koristi **režim pretraživanja**:
dokument čitate strelicama kao tekst, a pojedinačna slova skaču na sledeći
element određene vrste.

| Taster | Sledeći | Taster | Sledeći |
|---|---|---|---|
| `H` | naslov | `K` | veza |
| `1` do `6` | naslov tog nivoa | `F` | polje obrasca |
| `T` | tabela | `L` | lista |
| `I` | stavka liste | `B` | dugme |
| `E` | polje za unos | `X` | polje za potvrdu |
| `R` | radio dugme | `C` | kombinovani okvir |
| `G` | slika | `D` | orijentir |
| `Q` | citat | `S` | razdelnik |

Sa tasterom `Shift` idete na prethodni. Skok čita samo element na koji stane;
tabela, lista ili orijentir čitaju svoj prvi red. `ALUS+Shift+Space` isključuje
i uključuje skokove slovima, za stranice koje imaju sopstvene prečice.

### Lista elemenata

`ALUS+F7` otvara listu elemenata: veze, naslove, polja obrasca, dugmad ili
orijentire stranice. Vrstu birate radio dugmićima, listu sužavate kucanjem u
polje za filtriranje, a Enter aktivira element ili Pomeri se postavlja
kursor na njega. Escape zatvara listu. Lista se otvara na elementu na kome se
nalazite na stranici, a naslovi su prikazani kao stablo sa nivoima.

### Tabele

`Control+Alt` sa strelicama pomera od ćelije do ćelije. ALUS izveštava o redu
i koloni, i o zaglavljima kada se promene.

### Raspored ekrana

`ALUS+V` uključuje i isključuje raspored ekrana. Kada je uključen
(podrazumevano), red je ono što ekran prikazuje u jednom redu: oznaka pored
svog polja, dva dugmeta jedno pored drugog, veze unutar rečenice. Kada je
isključen, svaka veza i svako polje su poseban red. Vrlo dugi redovi se dele na
oko 100 znakova. Izbor se odmah čuva i ostaje i posle ponovnog pokretanja.

### Ostali tasteri režima pretraživanja

| Tasteri | Šta radi |
|---|---|
| `Enter` | aktivira element na kursoru (prati vezu, pritiska dugme) |
| `ALUS+F5` | osvežava dokument |
| `ALUS+F2` | sledeći taster prosleđuje stranici (na primer `K` za pauzu na YouTube-u) |
| `,` / `Shift+,` | pomera iza kraja / na početak trenutnog okvira |

## Kucanje i uređivanje: režim fokusa

Kada uđete u polje za unos, ALUS sam prelazi u **režim fokusa**, pa ono što
kucate ide aplikaciji. `ALUS+Space` ručno prebacuje između režima
pretraživanja i režima fokusa.

`Escape` izlazi iz režima fokusa samo kada je ALUS u njega ušao sam. Režim
fokusa koji ste uključili tasterima `ALUS+Space` isključujete ponovo tasterima
`ALUS+Space`; tada `Escape` ide stranici, na primer za izlazak iz prikaza preko
celog ekrana na YouTube-u.

Dokumenti koje uređujete -- u programu LibreOffice Writer ili uređivaču teksta
-- uvek su u režimu fokusa: kursor pripada aplikaciji, a ALUS ga prati.

## Aplikacije

- **Veb pregledači** (Firefox, Chrome, Chromium): režim pretraživanja, kao što
  je gore opisano.
- **Thunderbird**: poruke se čitaju u režimu pretraživanja; `ALUS+Strelica
  dole` čita poruku od početka, a `Escape` je zatvara.
- **GNOME radna površina**: gornja traka, pregled aktivnosti (`Super`),
  obaveštenja i podešavanja.
- **Datoteke (Nautilus)**: lista datoteka prati izbor; kucanje prvih slova
  imena skače na datoteku.
- **LibreOffice**: dokumenti programa Writer čitaju se kao tekst, a Calc
  izveštava o ćelijama.
- **Terminali** (GNOME Terminal, Konsole i drugi): novi ispis se čita čim se
  pojavi.
- **Electron aplikacije** i većina GTK i Qt aplikacija.

`ALUS+Shift+S` (laptop: `ALUS+Shift+Z`) uspavljuje ALUS za trenutnu
aplikaciju, za programe koji sami govore.

## Govor

ALUS govori preko programa speech-dispatcher. U Podešavanjima → Govor birate
izlazni modul (espeak-ng, RHVoice i drugi), glas tog modula, i brzinu, visinu,
jačinu i intonaciju.

Bez otvaranja podešavanja: `ALUS+Control+Strelica levo/desno` bira podešavanje
glasa, a `ALUS+Control+Strelica gore/dole` ga menja.

ALUS sam menja jezik glasa kada tekst kaže na kom je jeziku (Podešavanja →
Govor → Prati jezik teksta).

## Brajev red

ALUS upravlja brajevim redovima preko programa brltty. Instalirajte i
podesite brltty za svoj brajev red, zatim izaberite brltty u Podešavanjima →
Brajev red → Red, a na istoj stranici i tabele prevođenja. Srpske
tabele, ćirilične i latinične, deo su biblioteke liblouis.

## Podešavanja

Podešavanja otvarate iz ALUS menija ili tasterima `ALUS+Control+G`. Prozor je
poseban program i čita se kao svaka druga aplikacija. Izmene važe kada
pritisnete OK; Otkaži ili Escape ih odbacuju. Glas možete isprobati pre nego
što ga sačuvate.

| Stranica | Šta podešava |
|---|---|
| Opšte | jezik programa ALUS, nivo dnevnika |
| Govor | sintetizator, glas, brzinu, visinu, jačinu, interpunkciju, promenu jezika |
| Brajev red | brajev red, tabele prevođenja, kursor, prelom reči |
| Tastatura | raspored, ALUS tastere, izgovor kucanja, šta prekida govor |
| Prikaz objekata | opise, prečice, informacije o položaju, opise alata, obaveštenja |
| Kursor pregleda | šta kursor pregleda prati, navigaciju kroz dokument, raspored ekrana |
| Formatiranje dokumenta | koje formatiranje i struktura se najavljuju tokom čitanja |
| Gestovi | svaku komandu i njene tastere |

Podešavanja se čuvaju u datoteci `~/.config/alus/alus.ini`.

## Promena tastera

Svakoj komandi možete dodeliti drugi taster, ili joj ukloniti taster, u
prozoru Komande tastature (ALUS meni → Komande tastature…). Pronađite komandu,
izaberite Dodaj i pritisnite novi taster. Tasteri se čuvaju u datoteci
`~/.config/alus/gestures.ini`, u istom formatu kao kod NVDA, pa datoteka iz
NVDA radi bez izmena.

## Jezik

Jezik programa ALUS ne zavisi od jezika radne površine: možete imati englesku
radnu površinu i srpski čitač ekrana. U Podešavanjima → Opšte → Jezik birate
engleski, srpski (ćirilica) ili srpski (latinica). Izmena važi kada pritisnete
OK, bez ponovnog pokretanja. Ovo uputstvo postoji na istim jezicima.

## Rešavanje problema

**ALUS govori, ali nijedan taster ne radi.** Tastaturu drži drugi čitač
ekrana. Pokrenite `alus-cli input-info` da vidite koji, i koristite
`alus-switch alus`.

**ALUS ništa ne govori.** Proverite speech-dispatcher komandom `spd-say
zdravo`. Ako i ona ćuti, problem je u programu speech-dispatcher ili u zvuku,
a ne u programu ALUS.

**Neka aplikacija ćuti.** Zatvorite je i ponovo otvorite: neke aplikacije
uključuju pristupačnost samo pri pokretanju. `alus-cli a11y-tree 3
ime-aplikacije` pokazuje šta ALUS u njoj vidi.

**Pregled aktivnosti na tasteru Super ništa ne govori.** Prekidač čitača
ekrana radne površine je isključen. Pokrenite ALUS sa `alus-switch alus` dok
Orca još radi, bez prečice `Alt+Super+S`.

**ALUS na trenutak prestane da reaguje.** Kada se neka aplikacija zaglavi,
ALUS-ov nadzornik to primeti za pola sekunde, prepusti tastaturu aplikacijama
i ponovo je preuzme kada se ALUS oporavi.

**Prijava problema.** Snimite dnevnik dok ponavljate problem:

```
alus-switch measure 300
```

ALUS radi pet minuta sa detaljnim dnevnikom, a zatim se Orca sama vraća.
Pošaljite dnevnik (`alus-measure-…log` u ličnoj fascikli) sa kratkim opisom
onoga što ste uradili i onoga što ste čuli.

## Pomoć i podrška

Stranica projekta je https://github.com/DarkoMilosevic86/ALUS; tamo možete
prijaviti probleme i pratiti razvoj.

ALUS je besplatan, a pravi ga jedan čovek. Ako vam pomaže, njegov razvoj
možete podržati donacijom preko PayPal-a ili Google Pay-a, oboje na adresu
daremc86@gmail.com. ALUS meni → Doniraj… otvara PayPal i kopira adresu za
Google Pay.

Autorska prava (C) 2026 Darko Milošević. ALUS je objavljen pod GNU opštom
javnom licencom, verzija 2 ili novija.
