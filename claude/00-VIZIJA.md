# ALUS — Vizija i opseg

> Dokument 1 od 4. Čita se prvi. Definiše **šta** ALUS jeste, za koga, i — podjednako važno — **šta nije**.
>
> | Dokument | Sadržaj |
> |---|---|
> | `00-VIZIJA.md` | ovaj dokument: ciljevi, ne-ciljevi, licenca, rečnik |
> | `01-ARHITEKTURA.md` | moduli, threading, mapiranje NVDA → ALUS → Linux API |
> | `02-FAZE.md` | fazni plan sa izvršnim izlaznim kriterijumima |
> | `03-RIZICI.md` | registar rizika sa merama |

---

## 1. Šta je ALUS

**ALUS** = **A**ccess **L**inux **U**sing **S**peech.

Ime kodira primarnu modalnost: govor je podrazumevani i uvek prisutan izlaz. Brajev red je tvrdi zahtev (Z7), ali je drugi izlazni kanal, ne prvi — zato je govorni podsistem Faza 2 a brajev Faza 7, i zato `speech-dispatcher` nije implementaciona odluka nego deo identiteta projekta.

ALUS je čitač ekrana za Linux, napisan u C++20, koji:

1. **Koristi NVDA komandni set.** Isti tasteri, ista imena skriptova, isti desktop/laptop rasporedi, isti `gestures.ini` format. Korisnik koji prelazi sa Windows-a ne uči ništa novo.
2. **Govori preko speech-dispatcher-a.** Standardni Linux govorni sloj; radi sa espeak-ng, RHVoice, Piper i svim ostalim SSIP modulima koje korisnik već ima.
3. **Radi na svim desktopima.** GNOME Wayland prvi, pa KDE Plasma i X11, kroz isti apstraktni ulazni sloj.
4. **Brzo odgovara.** Budžet: **od pritiska tastera do prvog zvuka < 50 ms**. Ovo je merljiv, testiran zahtev, ne aspiracija.

## 2. Za koga

Primarno: slepi i slabovidi korisnici Linuxa koji dolaze sa NVDA i kojima je Orca prespora, nedovoljno predvidiva ili ima pogrešne komande. Sekundarno: programeri koji žele da pišu app module i dodatke bez poznavanja celog jezgra.

## 3. Zašto ne Orca

Orca je jedini ozbiljan čitač ekrana za Linux i radi — ali:

- **Komande su drugačije.** Orca ima svoj taster (Insert/CapsLock), svoj flat review model i svoje komande. Prelazak sa NVDA je ponovno učenje, i to je glavni praktični razlog zašto ljudi ostaju na Windows-u.
- **Arhitektura, ne jezik, je uzrok sporosti.** Merenje na ciljnom sistemu: AT-SPI property round-trip je **0.053–0.064 ms**, a `Cache.GetItems` vraća **1837 objekata u 31 ms** (~17 µs/objekat). IPC nije usko grlo. Sporost dolazi od nedostatka keširanja po ciklusu, nedostatka rate-limiting-a događaja i sinhronog upitivanja.
- NVDA je 20 godina rešavao te probleme i rešenja su **dokumentovana u kodu** (`core.CorePump`, `baseObject.AutoPropertyObject`, `OrderedWinEventLimiter`, `SpeechManager`). ALUS ne izmišlja arhitekturu — prenosi dokazanu.

ALUS nije kritika Orce i ne cilja da je zameni za postojeće korisnike. Cilja NVDA korisnike na Linuxu.

## 4. Tvrdi zahtevi

| # | Zahtev | Kako se proverava |
|---|---|---|
| Z1 | NVDA komandni set, desktop i laptop raspored | generisana tabela komandi se poredi sa NVDA korisničkim vodičem |
| Z2 | ALUS taster = Insert / numpad Insert / Caps Lock, po bitmasci | `alus+t` radi; dupli Insert propušta pravi Insert |
| Z3 | `gestures.ini` istog formata, tri sloja (user → locale → braille driver) | korisnikov `gestures.ini` iz NVDA se učita bez izmene |
| Z4 | speech-dispatcher kao podrazumevani govorni izlaz | radi sa korisnikovim postojećim SSIP konfiguracijama |
| Z5 | Pritisak → prvi zvuk < 50 ms; prekid govora < 30 ms | merni test u CI |
| Z6 | GNOME Wayland, KDE Wayland, X11 | isti build, bez uslovne kompilacije u jezgru |
| Z7 | Brajev red preko BrlAPI, prevod preko liblouis | tekst na redu, cursor routing pomera caret |
| Z8 | Lokalizacija, srpski uključen | `sr` prevod + `sr-Cyrl` brajeva tabela + srpski preklopi tastera |

## 5. Ne-ciljevi za v1.0 — **ova lista je obavezujuća**

NVDA ima ~189.000 linija izvornog koda. ALUS v1.0 mora da bude dramatično manji ili nikad ne izlazi. Sledeće se **svesno izostavlja** iz v1.0:

| Izostavljeno | Zašto | Kada |
|---|---|---|
| **Screen review preko display modela** | NVDA hooks-uje GDI pozive u tuđim procesima. Na Waylandu ne možeš čak ni da pročitaš tuđu površinu. | Zamenjeno: *flat object review* (v1.0) — prostorno sortirani `presType_content` objekti. Vidi `01-ARHITEKTURA.md` §6. |
| **OCR / content recognition** | Zahteva portal screencast + tesseract pipeline | v1.1, kao *poslednji* fallback posle AT-SPI i flat review-a |
| **Magnifier** (21 komanda) | Odvojen problem; GNOME već ima lupu | nikad, ili kao zaseban projekat |
| **Screen curtain** | Wayland ekvivalent nije jasan | v1.2 |
| **Touch screen podrška** (`ts:` gestovi) | Mali broj korisnika na Linuxu | v1.2 |
| **Remote Access** (8 komanda) | Velik, samostalan podsistem | v1.2 |
| **Java Access Bridge** | AT-SPI pokriva Java preko `java-atk-wrapper` | ne treba |
| **Add-on store** | Prvo treba stabilan add-on API | v1.1 |
| **Math (MathML interakcija)** | Zahteva MathCAT ili ekvivalent | v1.1 |
| **Vision enhancement provideri** | Vizualne funkcije nisu fokus | nikad |
| **GUI za podešavanja** | v1.0 ima TOML/ini config + CLI | Faza 9 |

Sve izostavljeno iz v1.0 mora da ostane **arhitekturno moguće** — zato su `ISynth`, input backend i review modovi apstrakcije od početka, a ne posle.

## 6. Licenca

**ALUS se objavljuje pod GNU GPL verzija 2 ili novija (GPL-2.0-or-later).**

Obrazloženje: NVDA je GPL-2.0-or-later. ALUS **ne kopira NVDA kod** — C++ implementacija je nova. Ali ALUS **preuzima izvedene radove**:

- komandne tabele i imena skriptova (`globalCommands.py`, `browseMode.addQuickNav`),
- `symbols.dic` i `characterDescriptions.dic` po jeziku,
- tekstove opisa komandi iz korisničkog vodiča,
- strukturu `gestures.ini` formata.

GPL-2.0-or-later uklanja svaku pravnu nedoumicu i kompatibilan je sa svim zavisnostima: `libatspi` (LGPL-2.1+), `liblouis` (LGPL-3.0+ / GPL-3.0+ za tabele), `libbrlapi` (LGPL-2.1+), `glib` (LGPL-2.1+), `libspeechd` (LGPL-2.1+), GTK4 (LGPL-2.1+).

Svaki fajl nosi SPDX zaglavlje: `SPDX-License-Identifier: GPL-2.0-or-later`.

## 7. Rečnik — isti pojmovi kao NVDA

Kod, dokumentacija i korisnička pomoć moraju da govore **isti jezik kao NVDA**, inače korisnici i dokumentacija ne mogu da se prevode jedno u drugo.

| Pojam | Značenje |
|---|---|
| **caret** (sistemski kursor) | kursor koji aplikacija pomera strelicama; ALUS ga prati, ne poseduje |
| **review cursor** (kursor pregleda) | nezavisan kursor koji korisnik pomera numpadom; ne pomera caret |
| **navigator object** | objekat na kom je review cursor; osnovica objektne navigacije |
| **review mode** | `object` / `document` / `screen` — šta review cursor obilazi |
| **script** | funkcija vezana za ulazni gest; sinonim: komanda |
| **gesture** (gest) | jedan ulazni događaj: `kb:alus+t`, `br(alva):t1`, `bk:dot7` |
| **ALUS taster** | Insert, numpad Insert i/ili Caps Lock; nije pravi modifikator. U gestu se piše `alus` (`kb:alus+t`); mapa uvezena od drugog čitača može da ga piše drugačije i to ime se prihvata pri čitanju — `FAZA3.md` O14 |
| **browse mode** (režim pregleda) | čitanje dokumenta kao ravnog teksta sa quick-nav tasterima |
| **focus mode** (režim fokusa) | tasteri idu aplikaciji; automatski u polja za unos |
| **pass-through** | `alus+space`; prebacivanje browse ↔ focus |
| **tree interceptor** | presretač koji hvata događaje i skriptove za celo podrvo objekata |
| **overlay class** | klasa koja se dinamički ušiva u nasledni lanac objekta |
| **app module** | prilagođenje specifično za jednu aplikaciju |
| **say all** | kontinuirano čitanje od karete/review cursora do kraja |
| **tether** | da li brajev red prati fokus ili review cursor |
| **input help** (pomoć pri unosu) | `alus+1`; svaki taster izgovori šta radi, bez izvršavanja |

## 8. Merila uspeha v1.0

ALUS v1.0 je gotov kad NVDA korisnik može, bez čitanja dokumentacije:

1. da se prijavi na sistem i navigira GNOME Shell-om i Nautilus-om,
2. da pročita i uredi tekst u GNOME Text Editor-u i LibreOffice Writer-u,
3. da pročita web stranu u Firefox-u ili Chromium-u browse mode-om, koristeći `h`, `k`, `t`, `f`, `1`–`9` i `alus+f7`,
4. da radi u terminalu (gnome-terminal / konsole) sa najavom novog izlaza,
5. da koristi brajev red ako ga ima,
6. da sve ovo radi **brže nego Orca**, merljivo.
