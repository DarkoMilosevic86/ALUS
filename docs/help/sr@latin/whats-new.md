# Šta je novo u programu ALUS

## 2026.1.0 beta

Prva javna verzija programa ALUS.

### Najvažnije

- Čitač ekrana za GNOME radnu površinu na Wayland-u, sa komandama, tasterima
  i pojmovima koje slepi korisnici poznaju: fokus, kursor pregleda, objektna navigacija, režim
  pretraživanja, režim fokusa, čitaj sve, pomoć za unos.
- **ALUS meni** (`ALUS+A`, laptop `ALUS+N`): podešavanja, komande tastature,
  pomoć, doniranje, čuvanje i vraćanje podešavanja, izlaz.
- **Pomoć na engleskom, srpskoj ćirilici i srpskoj latinici**: ovo uputstvo,
  Šta je novo i brzi pregled komandi, napravljen od tastera koje zaista
  koristite.
- **Veb**: slova za brzu navigaciju čitaju element na koji stanu;
  tabele, liste i orijentiri čitaju svoj prvi red; lista elemenata
  (`ALUS+F7`) otvara se tamo gde ste; raspored ekrana (`ALUS+V`) pamti se i
  posle ponovnog pokretanja.
- **Režim fokusa**: `ALUS+Space` ga uključuje i isključuje;
  `Escape` izlazi samo iz režima fokusa u koji je ALUS ušao sam, pa i dalje
  izlazi iz prikaza preko celog ekrana na YouTube-u.
- **Thunderbird**: poruke se čitaju u režimu pretraživanja.
- **Govor** preko programa speech-dispatcher (preporučuje se espeak-ng, rade
  i RHVoice i drugi); podešavanja glasa menjaju se i bez otvaranja prozora.
- **Brajev red** preko programa brltty i biblioteke liblouis, uključujući
  srpske tabele.
- **Komande koje na Wayland-u ne mogu da rade** -- na primer pomeranje
  pokazivača miša -- kažu zašto, umesto da ćute.
- **Komande za ostavu**: čitanje ostave (`ALUS+C`) i kopiranje onoga što je
  ALUS poslednje izgovorio (`ALUS+Control+X`).
- **Bezbedna zamena**: `alus-switch alus` menja Orcu programom ALUS i
  obrnuto, izgovara svaki korak i vraća Orcu ako ALUS ne uspe da se pokrene.
  ALUS sam uključuje pristupačnost aplikacija kada je prekidač radne površine
  isključen.

### Poznata ograničenja

- Provereno je samo na GNOME-u na Wayland-u; druge radne površine možda rade.
- Na Wayland-u pokazivač miša ne može da se pomera niti da klikne; tasteri za
  klik umesto toga deluju na navigacioni objekat.
- ALUS je beta verzija: probleme prijavite na stranici projekta,
  https://github.com/DarkoMilosevic86/ALUS.
