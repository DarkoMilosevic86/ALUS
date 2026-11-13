# ALUS — Arhitektura

> Dokument 2 od 4. Pretpostavlja `00-VIZIJA.md`.
>
> Svaka platformska tvrdnja u ovom dokumentu ima uz sebe komandu za proveru. Verzije se menjaju — tvrdnje moraju da ostanu proverljive.

---

## 0. Osnovna teza

NVDA je 20 godina rešavao probleme koji su **isti na svakoj platformi**: kako ne blokirati kad aplikacija zablokira, kako ne poplaviti govor pod lavinom događaja, kako sinhronizovati karetu sa zvukom, kako ne pitati platformu isto deset puta u jednoj sekundi. Rešenja su u kodu i **platformski su neutralna**.

ALUS zato:
- **prenosi NVDA-ovu arhitekturu jezgra praktično neizmenjenu** (pump, red, keš, limiter događaja, speech manager, watchdog),
- **zamenjuje samo platformski sloj** (Windows a11y API-ji → AT-SPI2, WASAPI → PipeWire/Pulse, wx → GTK4),
- **odbacuje ono što je postojalo samo zbog Windows-a** (in-process injekcija, virtualni baferi, display model).

Referenca za ceo ovaj dokument: `reference/nvda/projectDocs/design/technicalDesignOverview.md`.

---

## 1. Tehnološki stek

| Sloj | Izbor | Verzija na ciljnom sistemu | pkg-config |
|---|---|---|---|
| Jezik | C++20 (g++ 14.2 / clang) | `<coroutine>`, `<format>`, `<ranges>` rade | — |
| Build | Meson + Ninja | 1.7.0 / 1.12.1 | — |
| Testovi | Catch2 3.x | 3.7.1 | `catch2-with-main` |
| Event loop | GLib `GMainLoop` | 2.84.4 | `glib-2.0 gobject-2.0 gio-2.0` |
| Pristupačnost | libatspi | 2.56.2 | `atspi-2` |
| Govor | libspeechd (SSIP) | 0.12.0 | `speech-dispatcher` |
| Brajev prevod | liblouis | 3.33.0 | `liblouis` |
| Brajev red | libbrlapi | 6.7 | `libbrlapi` |
| Audio (tonovi, wav) | libpulse-simple → PipeWire | 17.0 / 1.4.2 | `libpulse-simple` |
| Tasteri (imena) | X11 keysymdef.h | — | konstante prepisane u `alus/input/keysyms.hpp` |
| Tasteri (keysym ↔ znak) | libxkbcommon | 1.7.0 | `xkbcommon` |
| GUI (Faza 9) | GTK4 + libadwaita | 4.18.6 | `gtk4` |
| Lokalizacija | gettext | — | — |

**GUI ne stoji u procesu čitača ekrana, i to je jedina stavka iz ovog
dokumenta koju je merenje oborilo.** NVDA svoj wx GUI drži u procesu i ovaj
dokument je to preuzeo; na AT-SPI-u ne radi. ALUS čita radnu površinu
**sinhronim** pozivima iz glavne petlje, a poziv o objektu odgovara se iz
procesa koji objekat poseduje — pa kad je objekat ALUS-ov sopstveni prozor, to
je ista petlja, blokirana unutar poziva koji čeka samu sebe. Izmereno: jezgro
staje **7,5 s**, a prozor se **ne može pročitati** (`get_role failed: Did not
receive a reply`). Zato je prozor `alus-settings`, odvojen izvršni fajl, i GTK
je linkovan u njega a ne u demon. Vidi `claude/FAZA9.md` N79 i N80.

**GMainLoop nije opcija, nego posledica:** `libatspi` je GObject biblioteka i zahteva `GMainContext` za dostavu D-Bus signala. Dobra vest: `GMainLoop` je skoro savršen ekvivalent wx petlje koju NVDA koristi (`wx.CallAfter` ↔ `g_idle_add`, `wx.CallLater` ↔ `g_timeout_add`, `wx.Timer` ↔ `GSource`).

> **Dve izmene steka iz Faze 3** — vidi `FAZA3.md` O1 i O2.
> 1. **`libxkbcommon` je dodat.** Gest `kb(laptop):alus+š` mora da postane
>    keysym koji kompozitor poredi sa grab-om, a to je *legacy* keysym
>    `XK_scaron` (0x1b9), ne Unicode forma 0x1000161. `xkb_utf32_to_keysym` to
>    radi kanonski, i to je ista biblioteka koju kompozitor koristi.
> 2. **`x11proto-dev` **nije** zavisnost.** Potrebne keysym konstante su
>    prepisane u `include/alus/input/keysyms.hpp` (protokolne konstante,
>    nepromenjene decenijama), da Wayland-only sistem ne vuče X11 zaglavlja.

Provera stека:
```bash
pkg-config --modversion atspi-2 speech-dispatcher liblouis libbrlapi glib-2.0 xkbcommon
g++ -std=c++20 -fsyntax-only -x c++ - <<< '#include <coroutine>
#include <format>
#include <ranges>
int main(){}'
```

---

## 2. Mapiranje modula: NVDA → ALUS → Linux

| NVDA | ALUS modul | Linux API | Napomena |
|---|---|---|---|
| `core.py` main loop + `CorePump` | `alus::core::Loop`, `PumpRequest` | `GMainLoop`, `g_idle_add`, `g_timeout_add` | §3.1 |
| `queueHandler.py` + generatori | `alus::core::Queue`, `TaskRegistry` | — | §3.2, C++20 coroutines |
| `eventHandler.py` | `alus::core::EventDispatcher` | — | §3.3 |
| `IAccessibleHandler/orderedWinEventLimiter.py` | `alus::a11y::EventLimiter` | — | §3.4 — **obavezno** |
| `baseObject.py` AutoPropertyObject | `alus::core::Cached<T>` | — | §3.5 |
| `baseObject.py` ScriptableObject | `alus::script::Scriptable` | — | §5.3 |
| `extensionPoints/` | `alus::core::{Action,Filter,Decider,Chain}` | — | §3.6 |
| `watchdog.py` | `alus::core::Watchdog` | D-Bus timeouts | §3.7 |
| `logHandler.py` | `alus::core::Log` | — | §3.8 |
| `config/` | `alus::config` | `GKeyFile` | §3.9 |
| `inputCore.py` | `alus::input::GestureManager` | — | §5.1 |
| `keyboardHandler.py` | `alus::input::Keyboard` | **`AtspiDevice`**, `keysymdef.h` | §5.2 |
| `scriptHandler.py` + `@script` | `alus::script::Registry` | — | §5.3 |
| `vkCodes.py` + `keyLabels.py` | `alus::input::KeyNames` | `keysymdef.h` | §5.2 |
| `mouseHandler.py` | `alus::input::Mouse` | `AtspiDevice::generate_mouse_event`, `Component` | — |
| `speech/speech.py` | `alus::speech::Presenter` | — | §4.2 |
| `speech/manager.py` | `alus::speech::SpeechManager` | — | §4.1 — **najteži deo** |
| `speech/commands.py` + `priorities.py` | `alus::speech::{Command,Priority}` | — | §4.3 |
| `speech/sayAll.py` | `alus::speech::SayAll` | — | coroutine |
| `speech/speechWithoutPauses.py` | `alus::speech::SentenceBuffer` | — | §4.4 |
| `synthDriverHandler.py` + `synthDrivers/` | `alus::speech::ISynth`, `SsipSynth`, `EspeakDirectSynth` | `libspeechd`, opc. `libespeak-ng` | §4.5 |
| `nvwave.py` + `tones.py` | `alus::audio::WavePlayer`, `Tones` | `libpulse-simple` | §4.6 |
| `IAccessibleHandler`/`UIAHandler`/`JABHandler` | `alus::a11y::Registry` (**jedan**, ne četiri) | `libatspi` | §6.1 |
| `NVDAObjects/__init__.py` | `alus::a11y::Object` + `OverlayRegistry` | `Accessible`, `Component`, `Action`, `Value`, `Selection` | §6.2 |
| `NVDAObjects/behaviors.py` | `alus::a11y::behaviors::*` | — | §6.3 — platformski neutralno |
| `api.py` | `alus::a11y::Api` (globalno stanje) | — | §6.4 |
| `controlTypes/` | `alus::a11y::{Role,State,OutputReason}` | `AtspiRole`, `AtspiStateType` | §6.5 |
| `textInfos/__init__.py` | `alus::text::TextRange` (interfejs) | — | §7.1 |
| `textInfos/offsets.py` | `alus::text::OffsetsTextRange` | `AtspiText` | §7.2 |
| `textUtils/` | `alus::text::{OffsetConverter,WordSeg}` | ICU (sistemski `libicu`) | §7.3 — **pažnja: kodiranje** |
| `review.py` | `alus::nav::Review` | — | §8.1 |
| `cursorManager.py` | `alus::nav::CursorManager` | — | §8.2 |
| `editableText.py` | `alus::nav::EditableText` | `AtspiEditableText` | §8.3 |
| `documentBase.py` | `alus::nav::DocumentWithTableNavigation` | `AtspiTable`, `AtspiTableCell` | §8.4 |
| `treeInterceptorHandler.py` | `alus::nav::TreeInterceptor` | — | §8.5 |
| `browseMode.py` | `alus::nav::BrowseMode` | **`AtspiCollection`** | §9 |
| `compoundDocuments.py` | `alus::nav::CompoundDocument` | — | §9.3 — **ključni port** |
| `virtualBuffers/` | — **ne portuje se** | — | §9.1 |
| `displayModel.py` | — **ne portuje se**; zamena: flat object review | `Component::get_extents` | §10 |
| `characterProcessing.py` | `alus::text::Symbols` | — | §11.1 |
| `speechDictHandler/` | `alus::speech::Dictionaries` | — | §11.1 |
| `appModuleHandler.py` + `appModules/` | `alus::app::{Module,Registry}` | `/proc/<pid>/exe`, `Application::get_toolkit_name` | §12 |
| `globalPluginHandler.py` + `addonHandler/` | `alus::plugin` (stabilan C ABI) | `dlopen` | §12.3 |
| `gui/` (wxPython) | `alus::gui` | GTK4 + libadwaita, **u sopstvenom procesu** | Faza 9 |
| `NVDAHelper`, `comInterfaces`, `oleacc`, `winBindings`, `XMLFormatting`, `textUtils/uniscribe` | — **briše se** | — | Windows-only |

---

## 3. Infrastruktura jezgra

### 3.1 Petlja i pump — prenosi se doslovno

NVDA **nema sopstveni main loop**. Petlja je `wx.MainLoop()`, a NVDA-ov rad vozi `CorePump(wx.Timer)` koji se „kucne" na zahtev (`core.py:990-1049`). `requestPump(immediate)` je **koalescer**:

```cpp
enum class PumpRequest { None, Delayed, Immediate };
constexpr int kPumpMaxDelayMs = 10;   // NVDA: PUMP_MAX_DELAY

// requestPump: N zahteva između dva pumpanja → najviše jedno pumpanje.
// Immediate nadograđuje Delayed koji već čeka.
void Loop::requestPump(bool immediate) {
    if (pending_ == PumpRequest::None ||
        (immediate && pending_ == PumpRequest::Delayed)) {
        pending_ = immediate ? PumpRequest::Immediate : PumpRequest::Delayed;
        queueRequest();          // na glavnoj niti: direktno; inače g_idle_add
    }
}
```

Jedan pump ciklus (`core.py:1015-1048`) — **red operacija je deo ugovora**:

```cpp
void Loop::tick() {
    assert(!isPumping_);          // pump NIJE re-entrant
    isPumping_ = true;
    pending_ = PumpRequest::None;
    watchdog_.alive();

    a11y_.drainEvents();          // platformski događaji PRVO → u red
    queue_.pumpAll();             // koraci coroutine-a, pa flush reda
    mouse_.pumpAll();
    braille_.pumpAll();

    Cached<>::invalidateAll();    // keš živi TAČNO jedan ciklus
    watchdog_.asleep();
    isPumping_ = false;

    // re-entry se ODLAŽE preko reda, nikad direktno (stack overflow)
    if (pending_ == PumpRequest::Immediate) queueRequest();
    else if (pending_ == PumpRequest::Delayed) processRequest();
}
```

Tri invarijante koje se **ne smeju** promeniti:
1. Platformski događaji se cede **pre** reda, da se u istom ciklusu konvertuju i izvrše.
2. `Cached<>::invalidateAll()` na kraju svakog ciklusa definiše životni vek keša.
3. Pump nije re-entrant; re-entry ide kroz red.

`PUMP_MAX_DELAY = 10 ms` **nije arbitraran** — postoji da bi se događaji grupisali. Ne smanjivati bez merenja.

### 3.2 Red i coroutine-i

Jedan **FIFO** red. Prioritet se *ne* izražava kroz više redova, nego kroz `immediate` zastavicu koja menja raspoređivanje pumpa (`queueHandler.py:43-53`).

```cpp
void Queue::flush() {
    // NVDA: range(qsize()+1) — funkcija koja se sama re-queue-uje
    // NE MOŽE da zaglavi pump. Ograničenje je obavezno.
    const size_t bound = q_.size() + 1;
    for (size_t i = 0; i < bound && !q_.empty(); ++i) {
        auto fn = std::move(q_.front()); q_.pop();
        watchdog_.alive();                   // heartbeat po itemu
        try { fn(); } catch (...) { log_.exception(); }
    }
}
```

NVDA-ovi registrovani generatori (`queueHandler.registerGeneratorObject`) — kooperativni multitasking za say-all i učitavanje bafera — postaju **C++20 coroutine-i**, koraknuti jednom po ciklusu:

```cpp
struct Task { struct promise_type { /* suspend_always na svakom co_yield */ }; };
// TaskRegistry::pumpAll(): jedan resume() po tasku po ciklusu;
// dok ima živih taskova → requestPump() (samoodrživ tik).
```

Ovo je jedno mesto gde je C++20 **bolji** od NVDA-ovog Pythona: `co_await` na „sledeći pump ciklus" je tipski bezbedan i bez alokacije po koraku.

### 3.3 Dispatch događaja — lanac sa eksplicitnom kontinuacijom

Najvredniji extensibility pattern u NVDA (`eventHandler.py:140-166`). Nivoi 1–3 dobijaju `(obj, nextHandler)` i **odlučuju** da li nastavljaju; nivo 4 je terminalan.

```
1. svaki global plugin        → handler(obj, next)
2. obj.appModule             → handler(obj, next)
3. obj.treeInterceptor       → handler(obj, next)   [samo ako isReady ili ignoreIsReady]
4. objekat sam               → handler()            [bez next — terminalno]
```

Uz to, brojači čekajućih događaja (`_pendingEventCountsBy{Name,Obj,NameAndObj}`) i predikat `isPendingEvents(name, obj)` — „da li da uopšte radim ovaj skup posla, ili će ga noviji događaj pregaziti". Koristi se kroz ceo govorni i brajev sloj. **Portovati sa brojačima, ne bez njih.**

`gainFocus` ide sa `immediate=true`. `doPreGainFocus` jedan `gainFocus` može da razvije u `foreground` + N `focusEntered` događaja, izvršenih sinhrono.

### 3.4 Limiter događaja — zašto NVDA ostaje responzivan

`OrderedWinEventLimiter` sedi između OS callback niti i pumpa. **AT-SPI poplavljuje isto tako kao winEvents** (Chromium pri učitavanju strane, LibreOffice pri skrolovanju tabele), pa je ovo **deo Faze 1, ne optimizacija „za kasnije"**.

Pravila (prvi stepen, `addEvent`):
- dedup sa **pomeranjem u vremenu** — duplikat dobija nov redni broj, ne drugi red u queue-u;
- `ChildrenChanged:Add` poništava čekajući `Remove` za isti objekat i obrnuto;
- `Window:Activate` **briše** čekajući `StateChanged:Focused` za isti objekat;
- od menu događaja zadrži **samo poslednji**;
- **najviše 10 događaja po izvornoj aplikaciji po ciklusu**, osim za trenutno fokusiran objekat;
- od focus događaja zadrži **samo poslednja 4**.

Drugi stepen (`pumpAll`): emituj **samo jedan** focus/foreground događaj — probaj najnoviji, pa fallback na starije dok jedan ne uspe.

Mapiranje pravila na AT-SPI imena događaja je u Fazi 4; izvorna lista NVDA pravila je u `reference/nvda/source/IAccessibleHandler/orderedWinEventLimiter.py`.

### 3.5 `Cached<T>` — ceo performansni model

NVDA-ov `AutoPropertyObject` kešira property-je, a keš se prazni na kraju svakog pump ciklusa (`baseObject.py:121-178` + `core.py:1035`). To je **jedini razlog** zašto je u redu da govor, brajev red i vizija u istom ciklusu svaki pitaju objekat za `name`, `role`, `states` — platforma se udari jednom.

Na AT-SPI-u ovo vredi **više** nego na Windows-u: focus događaj traži 20–50 property-ja; bez keša to je 20–50 D-Bus poziva (~1–3 ms), sa kešom 1 batch.

```cpp
template <class T> class Cached {
    mutable std::optional<T> value_;
    std::function<T()> getter_;
public:
    const T& get() const { if (!value_) value_ = getter_(); return *value_; }
    void invalidate() { value_.reset(); }
    // Svaka instanca se registruje u per-objekat listu; Object::invalidateCache()
    // je prazni; Loop::tick() poziva invalidateAll() na kraju ciklusa.
};
```

**Dodatni lever specifičan za AT-SPI:** `org.a11y.atspi.Registry.RegisterEvent(s event, as properties, s app_bus_name)` — argument `properties` traži da se navedeni propertiji **uključe u payload signala**. Focus događaj može da stigne sa `name`/`role`/`states` bez ijednog round-tripa. Provera:
```bash
gdbus introspect --address "unix:path=/run/user/$(id -u)/at-spi/bus" \
  --dest org.a11y.atspi.Registry --object-path /org/a11y/atspi/registry
```

### 3.6 Extension points

Pet vrsta (`extensionPoints/__init__.py`), sve preko registra sa **slabim referencama**:

| Tip | Semantika |
|---|---|
| `Action::notify(args…)` | pozovi sve; na izuzetak loguj i nastavi |
| `Filter<T>::apply(value, args…)` | lanac: izlaz jednog je ulaz sledećeg |
| `Decider::decide(args…)` | prekini na prvom `false` |
| `AccumulatingDecider::decide(args…)` | pokreni **sve**, vrati `!default` ako je bilo koji rekao tako |
| `Chain<T>::iter(args…)` | generator koji spaja sve iterable |

Dve lekcije iz NVDA koje se moraju preneti:
1. **Pozivalac mora da drži handler živim.** Slabe reference znače da lambde i lokalni closure-i nestaju bez traga. NVDA konsekventno registruje metode dugoživećih singletona.
2. **Iteracija ide preko snapshot-a** (`list(self._handlers.values())`), jer handler sme da (de)registruje handlere dok se poziva.

### 3.7 Watchdog

Kad aplikacija zablokira na a11y pozivu, ALUS ne sme da zablokira sa njom.

- `alive()` na **svakom** pump ciklusu, **svakom** itemu reda i **svakom** koraku coroutine-a; `asleep()` na kraju pumpa (jezgro sme da bude mirno neograničeno).
- Kad je jezgro zaglavljeno: **tasteri prolaze kroz** — `isAttemptingRecovery` provera je prva stvar u `GestureManager::execute` (`inputCore.py:548-552`).
- Na Linuxu je **lakše** nego u NVDA: `CoCancelCall` nema ekvivalent, ali D-Bus pozivi imaju native timeout. Struktura ostaje: heartbeat + nadzorna nit + „pokreni ovaj poziv na worker-u sa timeout-om i napusti ga".

### 3.8 Logovanje

Jedan NVDA detalj je **funkcionalno** važan, ne kozmetički: `log.exception` **degradira očekivane izuzetke na debug nivo** (`logHandler.py:333-383`) — nevažeći handle, timeout, objekat umro. Razlog: greške puštaju zvuk greške. Čitač ekrana koji cvrkuće svaki put kad aplikacija tajmautuje a11y poziv je neupotrebljiv.

Na Linuxu lista očekivanih: `org.freedesktop.DBus.Error.NoReply`, `.ServiceUnknown`, `.UnknownObject`, `.Timeout`, i AT-SPI `defunct` objekti.

### 3.9 Konfiguracija

`GKeyFile` (ini) — isti format kao NVDA-ov configobj, pa su `gestures.ini` fajlovi **direktno kompatibilni**. Putanje po XDG:

```
~/.config/alus/alus.ini          glavna konfiguracija
~/.config/alus/gestures.ini      korisnički preklopi tastera (format identičan NVDA)
~/.config/alus/profiles/*.ini    konfiguracioni profili
~/.config/alus/symbols-<lang>.dic
<datadir>/alus/locale/<lang>/{gestures.ini,symbols.dic,characterDescriptions.dic}
```

Profili i trigeri (app-specifični profil, say-all profil) se prenose iz `config/` — potrebni su, jer `SpeechManager` ima latch za prebacivanje profila u sredini govora (§4.1).

**Svaki podsistem mora da bude restartabilan u procesu**, kao NVDA-ov `resetConfiguration` (`core.py:315-419`). To je zahtev dizajna, ne naknadna misao.

---

## 4. Govor

### 4.1 `SpeechManager` — najteže izborena logika u NVDA

Portuje se kao celina (`speech/manager.py`, 773 linije). Ključni mehanizmi:

**Indeksi i modularno poređenje.** Indeksi ciklično 1..9999. Cancel može da se utrkuje sa index notifikacijom, pa poređenje „da li je indeks A pre indeksa B" koristi **polu-prostor prozor** — isti trik kao TCP sequence numbers:

```cpp
constexpr int kMaxIndex = 9999;
constexpr int kWrapMagnitude = kMaxIndex / 2;   // 4999
bool isIndexABeforeB(int a, int b) {
    return a != b && ((a < b && kWrapMagnitude >= b - a) ||
                      (b < a && kWrapMagnitude <  a - b));
}
```

**Prepisivanje sekvence** (`_processSpeechSequence`): sekvenca se **cepa na svakom callback-u** (callback → dodeli indeks, emituj `IndexCommand`, zapamti `index → callback`), a `_ensureEndUtterance` garantuje `IndexCommand` na kraju **svakog** iskaza. Taj završni indeks je način na koji se detektuje da je iskaz završen.

**Prioriteti.** `NOW` prekida niže prioritete ali **ne** prethodno postavljeni `NOW`. Prekinuti red se **nastavlja** posle. Parametri (`rate`/`pitch`/`volume`/`lang`) se pamte u `ParamChangeTracker` i **ponovo emituju** kad se prekinuti red nastavi — inače se glas „zaglavi" na pogrešnoj brzini.

**Otkazivanje zastarelih najava.** `_CancellableSpeechCommand` se veže na govor nastao zbog promene fokusa. Pre izgovaranja se proverava da li je još relevantan (je li to još fokusirani objekat / predak fokusa / foreground). Ako nije — **izbacuje se iz reda pre nego što se izgovori**. Ovo je direktno vidljivo ponašanje: bez njega, brzo kretanje kroz listu izgovara pet starih imena pre trenutnog.

**Latch za profile.** Svaka promena konfiguracionog profila zahteva da sintetizator **prvo završi** (`_shouldPushWhenDoneSpeaking`) — inače bi iskaz počeo na glasu A i završio na glasu B.

**Nitna granica.** Index/done notifikacije dolaze sa SSIP reader niti i **odmah** se marshaluju na glavnu nit (`g_idle_add`). Celo stanje `SpeechManager`-a je isključivo na glavnoj niti. Dodatno: `if (synth != currentSynth()) return;` — notifikacija od drajvera koji je već zamenjen se odbacuje.

### 4.2 Prezentacioni sloj

`speech/speech.py` (3198 linija) je **platformski neutralan** i portuje se logikom, ne doslovno. Normalizacioni tok `speak()` ima **obavezan red operacija** (`speech.py:1126-1233`):

```
filter_speechSequence → validacija tipova → prioritet → prazno? → speech viewer
→ pre_speech → režim govora (off/beeps/talk/onDemand) → ako pauzirano: cancel
→ jezički prolaz (ubaci LangChangeCommand gde se jezik menja)
→ tekstualni prolaz (dictionaries → simboli → whitespace → normalizacija)
→ CHUNK_SEPARATOR ("  ") između delova, osim u karakternom modu
→ SpeechManager::speak()
```

`CHUNK_SEPARATOR` su **dva razmaka** — postoji da se susedni brojevi ne spoje u jedan u francuskom i nemačkom. Mali detalj, vidljiv bug ako se izostavi.

### 4.3 Komande govora → SSML

| ALUS komanda (iz NVDA) | SSIP / SSML |
|---|---|
| `IndexCommand(n)` | `<mark name="n"/>` → notifikacija `700 INDEX MARK` |
| `BreakCommand(ms)` | `<break time="Nms"/>` |
| `PitchCommand`, `RateCommand`, `VolumeCommand` | `<prosody pitch= rate= volume=>` (koristi `.multiplier` kao procenat) |
| `LangChangeCommand(lang)` | `<voice xml:lang="…">` |
| `CharacterModeCommand(bool)` | `SET SELF SPELLING on/off` ili `char()` |
| `PhonemeCommand(ipa, text)` | `<phoneme ph="…">text</phoneme>` |
| `EndUtteranceCommand` | interno (cepanje iskaza), ne ide sintetizatoru |
| `BeepCommand`, `WaveFileCommand` | **ne** preko SSIP-a → `alus::audio` |
| `CallbackCommand` | interno: izvršava se **na glavnoj niti** pri dolasku indeksa; mora da se vrati brzo |
| `_CancellableSpeechCommand` | interno, nikad ne dolazi do sintetizatora |
| `ConfigProfileTriggerCommand` | interno, sa latch-om iz §4.1 |

Prozodijske komande iz NVDA imaju *offset XOR multiplier* semantiku relativnu na **korisnički konfigurisanu vrednost** (`commands.py:249-344`): `PitchCommand(offset=30)` znači „+30 na ono što je korisnik postavio". Promena visine na velikim slovima je upravo to.

### 4.4 `SentenceBuffer` — zašto say-all zvuči kontinuirano

`speechWithoutPauses` drži tekst **do granice rečenice** (regex na `[.!?]` sa opcionim zatvorenim citatom) i tek tad ga predaje sintetizatoru. Bez toga sintetizator ubacuje pauzu iskaza na **svakoj liniji**, i say-all zvuči isprekidano.

Detalj koji se lako propusti: **poslednji `LangChangeCommand` pre mesta cepanja se kopira u zadržani fragment**, inače se zadržani deo izgovori na pogrešnom jeziku.

### 4.5 `ISynth` — dva backend-a

```cpp
class ISynth {
public:
    virtual void speak(const SpeechSequence&) = 0;
    virtual void cancel() = 0;
    virtual void pause(bool) = 0;
    virtual std::set<CommandType> supportedCommands() const = 0;   // IndexCommand OBAVEZAN
    virtual std::vector<VoiceInfo> availableVoices() const = 0;
    // rate/pitch/volume/inflection: 0–100 na ALUS nivou, preslikano na backend
    Signal<int> synthIndexReached;     // → marshal na glavnu nit
    Signal<>    synthDoneSpeaking;     // → marshal na glavnu nit
};
```

**`SsipSynth` (podrazumevani).** Inicijalizacija:
```
SET SELF CLIENT_NAME "alus:core:main"
SET SELF SSML_MODE on
SET SELF NOTIFICATION index_marks on
SET SELF NOTIFICATION end on
SET SELF NOTIFICATION cancel on
SET SELF NOTIFICATION begin on
```
`index_marks` → `synthIndexReached`, `end` → `synthDoneSpeaking`. **Jedan iskaz u letu**, jedan SSIP prioritet; ALUS drži red, ne speech-dispatcher (vidi `03-RIZICI.md` R4).

> **Ispravke koje je Faza 0 izmerila** (`spikes/RESULTS.md`, implementirano u Fazi 2):
>
> 1. **`begin` i `end` nisu audio događaji.** `begin` stiže ~7.5 ms *pre* prvog
>    sample-a (N10), `end` ~294 ms *posle* poslednjeg (N12). Ulančavanje ide
>    isključivo preko `IndexCommand`-a. `end` se koristi samo kao latch za
>    profile i kao **nadzornik izgubljene oznake** (N22 u `FAZA2.md`).
> 2. **Oznaka se nikad ne stavlja odmah posle završne interpunkcije** (N15/N16,
>    rizik R14): espeak-ng je tiho izgubi. `SsipSynth` tada **cepa iskaz na
>    odvojene SSIP poruke** tako da je svaka oznaka na kraju svoje poruke —
>    jedini oblik koji je dokazano preživeo. Poruke idu jedna za drugom, pa
>    server sam ulančava i ALUS ne čeka povratni put.
> 3. **`SpeechManager` ima rokove.** Izostala oznaka ne sme da bude mrtva
>    petlja: posle `end`-a sledi kratak rok, pa `DEBUG_WARNING` i nastavak.
> 4. **Pomeraj oznake je postojan, ne šum** (−40 ms, N13), i zavisi od audio
>    uređaja: `ISynth::indexMarkOffset()`. Ko sinhronizuje nešto sa zvukom
>    (govorni kursor, praćenje reči) mora da ga kompenzuje.
> 5. **Vremenski budžeti su svojstvo MODULA, ne SSIP-a** (N17). `rhvoice`
>    promašuje svaki; `ISynth::timingIsGuaranteed()` to prijavljuje, a ALUS
>    kaže u logu kad radi sa neizmerenim modulom.

Provera da modul podržava indeksiranje:
```bash
grep -i indexing /etc/speech-dispatcher/modules/espeak-ng.conf    # EspeakIndexing 1
spd-say -L                                                        # dostupni glasovi
```

**`EspeakDirectSynth` (planiran za v1.1).** Direktno `libespeak-ng` u `AUDIO_OUTPUT_SYNCHRONOUS` modu + sopstveni `WavePlayer`. ALUS tad **poseduje audio tok**, pa su prekid i sinhronizacija karete u say-all-u uzorkovno tačni — isto kao NVDA. Postoji zato da tenzija iz `03-RIZICI.md` R4 ima izlaz, i da `ISynth` apstrakcija bude dokazana sa dva stvarna implementatora, a ne jednim.

### 4.6 Audio

`WavePlayer` mora da ima **`feed(data, size, onDone)` sa callback-om po bloku** — to je mehanizam kojim NVDA sinhronizuje indekse sa zvukom. Plus `sync`, `idle`, `stop`, `pause`, `setVolume` po kanalu.

v1.0: `libpulse-simple` (radi i na PipeWire kroz `pipewire-pulse`). Kasnije: nativni PipeWire za nižu latenciju. Audio ducking: per-stream cork ili volume preko PipeWire/Pulse; zadržati NVDA-ov ref-count + 150 ms odloženo otpuštanje.

---

## 5. Ulaz i komande

### 5.1 Gesture sistem — prenosi se 1:1

To je ono što korisnici znaju. Nema improvizacije.

**Identifikator:** `source:keys`, npr. `kb:alus+shift+t`, `kb(laptop):alus+a`, `br(alva.bc680):t1+t2`, `bk:dot7+dot8`.

**Normalizacija** (`inputCore.normalizeGestureIdentifier`): lowercase, pa **sortiranje `+`-delova po abecedi**. Bez ovoga `alus+shift+t` i `shift+alus+t` nisu isti gest. `+` se nikad ne pojavljuje kao ime tastera — literalni `+` se zove `plus`.

**Prioritet identifikatora:** gest vraća **uređenu** listu; prvi pogodak pobeđuje. Za tastaturu: `("kb(<layout>):<keys>", "kb:<keys>")` — **raspored-specifično uvek pobeđuje raspored-agnostično**.

**Tri sloja mape**, u prioritetu:
```
1. ~/.config/alus/gestures.ini            (korisnik)
2. <datadir>/alus/locale/<lang>/gestures.ini  (lokal)
3. gestureMap trenutnog brajevog drajvera
```
…pa tek onda built-in class bindings. Format identičan NVDA, uključujući `None = <gest>` za **odvezivanje** i `kb:<keyname>` kao ime skripta za emulaciju tastera.

Srpski lokal već postoji u NVDA (`source/locale/sr/gestures.ini`) i remapira `zoomIn`/`zoomOut` i laptop mouse/flow komande na `š`/`đ` — direktno upotrebljiv.

**Dispatch skripta — 10 koraka** (`scriptHandler._yieldObjectsForFindScript`):
```
 1. gesture.scriptableObject        (npr. brajev drajver za br: gestove)
 2. svaki global plugin
 3. focus.appModule
 4. brajev red (ako je Scriptable)
 5. aktivni vision provideri
 6. focus.treeInterceptor           [filter: passThrough off ILI ignoreTreeInterceptorPassThrough]
 7. focus objekat
 8. preci fokusa, od najbližeg     [filter: samo ako script.canPropagate]
 9. configProfileActivationCommands
10. globalCommands                  (poslednja instanca)
```
Korak 6 sa svojim filterom je **tačno ono** što čini da jednoslovni quick-nav radi u browse mode-u a propada kroz u focus mode-u.

Unutar svakog objekta: **globalne mape pobeđuju built-in class bindings**.

### 5.2 Tastatura

> **Izvedeno u Fazi 3 **bez** `AtspiDevice`** — vidi `FAZA3.md` O1, N26, N27.
>
> `atspi/atspi-device-a11y-manager.c` zove kompozitor sa
> `g_dbus_proxy_call_sync(..., timeout = -1, ...)` i **odbacuje `GError`**, a
> `SetKeyGrabs` izdaje iz `g_timeout_add` callback-a, to jest iz glavne petlje
> klijenta. To je **uzrok N19** (petlja blokirana bez roka dok grab-ovi stoje)
> i **uzrok N7** (`AccessDenied` se ne vidi). Oba su u biblioteci.
>
> ALUS zato govori direktno sa `org.freedesktop.a11y.KeyboardMonitor`:
> `A11yManagerBackend`, svaki poziv asinhron sa konačnim rokom, svaki odgovor
> pročitan, `emergencyRelease()` radi sa watchdog niti. X11 i legacy putevi
> ostaju **drugi `IInputBackend`**; Z6 se ispunjava izborom backend-a u
> vremenu izvršavanja, ne uslovnom kompilacijom.
>
> Tri činjenice o interfejsu koje se ne vide iz dokumentacije (N27):
> prvi argument `SetKeyGrabs`-a su **keysym-ovi**, ne indeksi modifikatora;
> virtualni modifikator (15–31) je **klijentska fikcija**; `state` u
> `KeyEvent`-u **ne sadrži** taj bit, nego ga klijent sam prati.
>
> I posledica koju plan nije imao (O6): kompozitor potrošnju traži **unaprede**,
> pa je skup grab-ova **kontekstno zavisan** — grabuje se tačno onaj gest koji
> bi *sada* našao skript. Inače bi `h`, `k` i cifre (browse mode) bili
> grabovani globalno i kucanje bi prestalo da radi svuda.

**Backend: `AtspiDevice`** — `libatspi` 2.56 već apstrahuje tri platforme:

| Backend | Kad | Mehanizam |
|---|---|---|
| `AtspiDeviceA11yManager` | GNOME ≥48, KDE ≥6.3.6, Wayland | `org.freedesktop.a11y.KeyboardMonitor` |
| `AtspiDeviceX11` | X11 sesija | XInput2 / XRecord |
| `AtspiDeviceLegacy` | stari AT-SPI | `DeviceEventController` |

API koji ALUS koristi:
```c
atspi_device_new()                        // auto-izbor backend-a
atspi_device_set_app_id(dev, "org.gnome.Orca")   // vidi 03-RIZICI.md R1
atspi_device_add_key_watcher(dev, cb)     // posmatraj SVE (echo znakova)
atspi_device_add_key_grab(dev, keydef, cb)// potroši konkretnu kombinaciju
atspi_device_remove_key_grab(dev, id)
atspi_device_grab_keyboard(dev)           // sve (input help / learn mode)
atspi_device_ungrab_keyboard(dev)
atspi_device_map_keysym_modifier(dev, keysym)    // → virtualni modifikator 15..31
atspi_device_unmap_keysym_modifier(dev, vmod)
atspi_device_get_locked_modifiers(dev)
atspi_device_generate_mouse_event(...)
```

**`map_keysym_modifier` je rešenje za ALUS taster.** Insert / numpad Insert / Caps Lock se mapiraju u virtualni modifikator (indeks 15–31, **a vraća se MASKA `1 << indeks`** — nalaz N9), pa grab `{keysym=XK_t, modifiers=ALUS_VMOD|SHIFT}` daje tačno NVDA semantiku — a sprovodi je kompozitor. Ne implementira se ručno.

> **Faza 3, N28:** svaki ALUS taster dobija **svoj** virtualni modifikator
> (mereno: `0x8000` za `Insert`, `0x10000` za numpad `Insert`). Gest `alus+t`
> se zato grabuje **jednom po ALUS tasteru**; jedan grab sa spojenom maskom
> znači „drži oba tastera" i nikad se ne poklapa.

Provera interfejsa i kontrole pristupa:
```bash
gdbus introspect --session --dest org.freedesktop.a11y.Manager \
  --object-path /org/freedesktop/a11y/Manager
busctl --user list | grep -i keyboardmonitor      # ko trenutno drži ime
```

**Imena tastera** — `keysymdef.h` → NVDA imena. Numpad sa NumLock-om **isključenim** daje imenovane `KP_*` keysym-ove, pa je razlika numpad-vs-navigacija čista na Linuxu (na Windows-u NVDA to rešava `(vkCode, extended)` parom — nam ne treba):

```
XK_KP_Home→numpad7   XK_KP_Up→numpad8     XK_KP_Prior→numpad9
XK_KP_Left→numpad4   XK_KP_Begin→numpad5  XK_KP_Right→numpad6
XK_KP_End→numpad1    XK_KP_Down→numpad2   XK_KP_Next→numpad3
XK_KP_Insert→numpadInsert   XK_KP_Delete→numpadDelete   XK_KP_Enter→numpadEnter
XK_KP_Divide→numpadDivide   XK_KP_Multiply→numpadMultiply
XK_KP_Subtract→numpadMinus  XK_KP_Add→numpadPlus
XK_KP_0..9→numLockNumpad0..9
XK_Insert→insert  XK_Home→home  XK_Up→upArrow  ...
```

**Obavezna ponašanja iz NVDA:**

| Ponašanje | Zašto |
|---|---|
| **`trappedKeys`** — ako se pojede key-down, mora se pojesti i key-up | inače aplikacije vide neuravnoteženo stanje tastera |
| ALUS taster se **nikad** ne propušta OS-u, osim dvostrukim pritiskom | dupli Insert u roku `multiPressTimeout` → pravi Insert |
| `multiPressTimeout` = 500 ms | i za dupli pritisak skripta i za propuštanje ALUS tastera |
| **Ponavljanje je po skriptu, ne po tasteru** | `lastScriptRepeatCount()` → 0/1/2/3; resetuje se na nevezanom gestu |
| Ulazna pomoć ne sme da pojede svoj prekidač | `toggleInputHelp` se **izvršava** čak i u input help-u |
| Echo znakova / reči | `speakTypedCharacters` sa bafером reči; terminali ga redom stavljaju u red i prazne na promenu teksta |

### 5.3 Skriptovi

```cpp
// Ekvivalent NVDA @script dekoratora.
ALUS_SCRIPT(reportCurrentFocus,
    .description  = N_("Reports the object with focus. If pressed twice, spells "
                       "the information. Pressing three times spells it using "
                       "character descriptions."),
    .category     = ScriptCategory::Focus,
    .gestures     = { "kb:alus+tab" },
    .speakOnDemand = true);
```

Atributi iz NVDA koji se prenose: `description` (to je ono što input help izgovara **i** što se vidi u dijalogu gestova), `category`, `gestures`, `canPropagate`, `bypassInputHelp`, `allowInSleepMode`, `resumeSayAllMode`, `speakOnDemand`.

`isScriptWaiting()` se proverava kroz ceo govorni kod da bi se dug govor prekinuo kad je novi taster u redu. **Bez ovoga responzivnost „pritisni taster da prekineš" je pogrešna.**

### 5.4 Ekstrakcija komandne tabele — automatski, ne rukom

`globalCommands.py` je 5714 linija sa ~300 skriptova u 21 kategoriji; `browseMode.addQuickNav` registruje quick-nav **programski**. Ručna transkripcija bi uvela greške.

```
tools/extract-commands.py
  → uveze NVDA module (globalCommands, browseMode, cursorManager,
     documentBase, editableText, virtualBuffers, braille)
  → pročita __gestures dict-ove i @script argumente
  → ispiše:
       data/gestures-builtin.ini   built-in bindings za ALUS
       data/scripts.csv            ime, opis, kategorija, desktop gest,
                                   laptop gest, speakOnDemand, resumeSayAllMode
```

Rezultat je **izvor istine** za ALUS i proverljiv protiv NVDA korisničkog vodiča (`user_docs/en/userGuide.md` + `source/keyCommandsDoc.py`, koji generiše referencu komandi iz `<!-- KC:… -->` komentara).

Napomena o rasporedima: gest zapisan kao `kb:X` je aktivan u **oba** rasporeda. Samo 12 globalnih skriptova ima stvarno desktop-ekskluzivan binding. Numpad komande moraju da nastave da rade i u laptop rasporedu.

> **Izmereno u Fazi 9, i dodaje četvrti fajl koji NIJE generisan.** Ekstraktor
> je izvor istine za to **šta komanda jeste** i **koji je taster vodi** — ali
> ne može da zna da li je ALUS tu komandu **napisao**. Dok to nije bilo
> zapisano nigde, 52 prečice iz generisane mape bile su **tihe**: taster se
> pritisne i ne uradi ništa, a referenca ih obećava jer je generisana iz iste
> tabele (`FAZA9.md` N99). Nijedan test to nije mogao da vidi, jer su i
> referenca i test čitali **isti** generisani izvor.
>
> Zato `data/commands-status.ini`, koji se održava **rukom**:
> `[announce]` su komande koje ALUS nema a taster to **izgovori**;
> `[platform]` su one koje se namerno **ne grabuju**, jer taster pripada
> aplikaciji a ALUS ne može da ga pošalje natrag (N18). Isti fajl nosi i
> ponašanje (`script::bindAnnouncements` pri pokretanju), i ugovor koji test
> T9.6 proverava nad **sastavljenim demonskim stekom**, i oznake u generisanoj
> referenci. Pravilo koje se iz ovoga izvlači, i koje je u Fazi 6 i 9 potvrđeno
> četiri puta: **provera ne sme da deli izvor sa onim što proverava.**
>
> I treći generisani ulaz, iz istog razloga: `data/gestures-alus.ini` nosi
> prečice koje ALUS bira **drugačije** od upstream-a. Primenjuje ih ekstraktor
> pre pisanja, pa ostaje jedna ugrađena mapa, jedan grab set i jedna
> referenca. Nov **sloj** mape to ne bi mogao: slojevi se naslažu, ne
> zamenjuju, pa bi stari taster ostao vezan (`FAZA9.md` N105).
>
> Dve posledice preseljenja implementacija iz NVDA-ovog `GlobalCommands` u
> podsistem koji ih poseduje: ime klase iz odeljka mape mora da se preseli
> **zajedno** s njom, inače korak 6 pretrage odbija unos i taster ne radi
> (N101, `alus+space`); i objekat koji drži skripte mora da dobije **katalog**,
> inače 180 komanda režima pregleda ostaje bez opisa i dijalog gestova ih krije
> (N102, T9.3).

---

## 6. Objektni model

### 6.1 Jedan API sloj, ne četiri

> **Izvedeno u Fazi 4** — vidi `FAZA4.md`. Tri stvari su se pokazale drugačije
> od pretpostavke: `atspi_set_main_context` mora **posle** `atspi_init`-a ili
> ne stiže nijedan događaj (N36); `properties` u `RegisterEvent` ne može da
> nosi `Role` ni `State`, jer su to metode a ne property-ji, i važi samo za
> trajanje callback-a (N38); i platformski događaj mora sam da traži pump
> ciklus (N43). Sloj je iza interfejsa `alus::a11y::Backend`, pa se ceo
> objektni model testira bez magistrale (`FAZA4.md`, „Mock platforma").

NVDA ima `IAccessibleHandler` + `UIAHandler` + `JABHandler` + `displayModel`. ALUS ima **jedan**: `alus::a11y` nad `libatspi`.

Zadržava se NVDA-ov obrazac traženja najbolje API klase (`findBestAPIClass` / `getPossibleAPIClasses` / `kwargsFromSuper`) — ne zato što ALUS ima više API-ja, nego zato što taj obrazac rešava „iz ove reference napravi najspecifičniji objekat", što i dalje treba.

Pretplata na događaje: `Registry.RegisterEvent(event, properties, app_bus_name)`. Minimalni skup koji stvaran čitač ekrana koristi (verifikovano uživo nad Orcinim pretplatama — 38 tipova):
```
Object:StateChanged:{Focused,Selected,Checked,Pressed,Expanded,Showing,Busy,
                     Active,Sensitive,Indeterminate}
Object:TextCaretMoved   Object:TextChanged:{Insert,Delete}
Object:TextSelectionChanged   Object:TextAttributesChanged
Object:PropertyChange:{AccessibleName,AccessibleDescription,AccessibleValue}
Object:ChildrenChanged:{Add,Remove}   Object:ActiveDescendantChanged
Object:SelectionChanged   Object:ValueChanged   Object:AttributesChanged
Object:Announcement       (live regions)
Object:{Row,Column}Reordered
Window:{Activate,Deactivate,Create,Destroy}
Document:{LoadComplete,Reload,LoadStopped,PageChanged,AttributesChanged}
Mouse:Button
```

Provera:
```bash
A="unix:path=/run/user/$(id -u)/at-spi/bus"
gdbus call --address "$A" --dest org.a11y.atspi.Registry \
  --object-path /org/a11y/atspi/registry \
  --method org.a11y.atspi.Registry.GetRegisteredEvents
```

### 6.2 `alus::a11y::Object`

> **Izvedeno u Fazi 4.** Kompozicija behavior-a radi sa NVDA-ovim ugovorom u
> celosti (tri faze izbora, `remove`/`prepend`). Dve razlike koje AT-SPI
> nameće: `positionInfo` nema `groupPosition`, pa rezervno računanje važi samo
> za role gde grupna pozicija nešto znači (N39, O6), a objekti žive ceo pump
> ciklus zato što keš drži `shared_ptr` na druge objekte (N40, O8).

Property set se prenosi iz `NVDAObjects/__init__.py` praktično u celosti, sa AT-SPI izvorima:

| ALUS property | AT-SPI izvor |
|---|---|
| `name` | `Accessible::get_name`, uz `LABELLED_BY` relaciju |
| `role` | `Accessible::get_role` → mapiranje (§6.5) |
| `states` | `Accessible::get_state_set` → mapiranje |
| `value` | `Value::get_current_value` / `Text` / `Accessible` |
| `description` | `Accessible::get_description`, uz `DESCRIBED_BY` |
| `keyboardShortcut` | `Action::get_key_binding` |
| `parent` / `children` / `firstChild` / `next` / `previous` | `Accessible::get_parent`, `get_child_at_index`, `get_index_in_parent` |
| `location` | `Component::get_extents(ATSPI_COORD_TYPE_SCREEN)` |
| `isFocusable` / `hasFocus` | `FOCUSABLE` / `FOCUSED` stanje |
| `level` | `get_attributes()["level"]` (naslovi, drveta) |
| `positionInfo` | `get_index_in_parent` + `get_child_count` roditelja |
| `landmark` | `get_attributes()["xml-roles"]` |
| `flowTo` / `flowFrom` | `FLOWS_TO` / `FLOWS_FROM` relacija |
| `annotations` / `details` | `DETAILS` / `DETAILS_FOR` / `ERROR_MESSAGE` relacija |
| `cellCoordsText`, `rowNumber`, `columnNumber`, `rowSpan`, `columnSpan` | `TableCell::*` |
| `tableID`, `rowCount`, `columnCount` | `Table::*` |
| `processID` | `Application` → `/proc/<pid>` |
| `appModule` | `alus::app::Registry` (§12) |
| `actionCount` / `doAction` | `Action::get_n_actions`, `do_action` |
| `setFocus` | `Component::grab_focus` |
| `scrollIntoView` | `Component::scroll_to` |
| `treeInterceptor` | `alus::nav` (§8.5) |

Svaki je `Cached<T>` (§3.5).

**Overlay klase.** NVDA-ov `DynamicNVDAObjectType` sintetiše klasu po objektu kombinujući overlay klase koje su predložili API sloj, app modul i global plugin-i (`chooseNVDAObjectOverlayClasses(obj, clsList)`; `clsList.remove(...)` da se klasa ukine, `clsList[0:0] = (...)` da se preda na vrh). C++ ne može da gradi tipove u runtime-u, pa ALUS koristi **kompoziciju behavior objekata**:

```cpp
class Object {
    std::vector<std::unique_ptr<Behavior>> behaviors_;   // uređeno, prvi ima prioritet
    // Dispatch property/event/script ide kroz behaviors_ pa fallback na bazu.
};
// OverlayRegistry::choose(obj, std::vector<BehaviorFactory>& list)
//   — API sloj, pa app modul, pa global plugin-i; isti ugovor kao NVDA.
```

Ključ za izbor overlay klasa na Linuxu nije `windowClassName` (Windows pojam), nego:
```
(Application::get_toolkit_name, AtspiRole)  primarno
Accessible::get_attributes()                sekundarno (xml-roles, tag, id)
Accessible::get_interfaces()                tercijarno (ima li Table? Text? Document?)
```
Npr. LibreOffice: NVDA proverava `windowClassName in ("SALFRAME","SALTMPSUBFRAME")`; ALUS proverava `toolkit_name == "libreoffice"`. NVDA-ov `hasattr(obj, "IAccessibleTable2Object")` postaje `"org.a11y.atspi.Table" in get_interfaces()`.

### 6.3 Behaviors — platformski neutralno, prenosi se u celosti

`NVDAObjects/behaviors.py` je **najkorisniji fajl za port** jer ne zna ništa o Windows-u:

| Behavior | Čemu služi | AT-SPI napomena |
|---|---|---|
| `EditableText` | detekcija pomeraja karete, auto-select | §8.3 |
| `LiveText` | diff teksta i najava promena | `_getText()` radi direktno nad `AtspiText` |
| `Terminal` | `LiveText` + stavljanje otkucanih znakova u red | VTE/Konsole izlažu pravi `Text` interfejs |
| `KeyboardHandlerBasedTypedCharSupport` | echo znakova u konzoli | zamenjuje se događajima iz ulaznog sloja |
| `ProgressBar` | bip/govor pri napretku | `Value::get_current_value` |
| `Dialog` | sastavljanje teksta dijaloga iz dece | — |
| `RowWithFakeNavigation` | navigacija po ćelijama gde tabele nema | — |
| `CandidateItem` | IME kandidati | — |
| `FocusableUnfocusableContainer`, `ToolTip`, `Notification` | — | — |

**Važna razlika:** NVDA za terminale koristi `DisplayModelLiveText` (čita iz hook-ovanih GDI poziva). Na Linuxu je to **nepotrebno** — VTE (gnome-terminal, xfce4-terminal) i Konsole izlažu pravi `AtspiText` sa `TextChanged` događajima. Dakle `Terminal` behavior radi direktno, bez display modela. Algoritam diff-a (`diffHandler`: difflib vs. diff-match-patch) treba izmeriti po terminalu — VTE izlaže scrollback, Konsole možda samo vidljiv ekran.

### 6.4 Globalno stanje (`api.py`)

Prenosi se doslovno: focus objekat + **lanac predaka fokusa**, `getFocusDifferenceLevel()` (koliko se lanac promenio — osnovica za `focusEntered` događaje), navigator objekat, review pozicija, foreground objekat, mouse objekat, desktop objekat.

Ugovor koji se ne sme izgubiti: **setteri vraćaju `bool`**. `setFocusObject` sme da odbije promenu (npr. objekat je u međuvremenu umro), i pozivalac to mora da ispita.

### 6.5 Role i stanja

> **Izmereno u Fazi 4 na AT-SPI 2.56.2:** **131** rola i **44** stanja, ne 133
> i 45 — brojevi u tabeli ispod uključuju `*_LAST_DEFINED` i alias
> `ATSPI_ROLE_PUSH_BUTTON`, koji nisu vrednosti (N42). NVDA-ova strana je
> tačna (156 / 53), pa je mapiranje 131 → 156 i 44 → 53. Takođe: **prazan
> AT-SPI state set znači „ne prijavljujem stanja"** — tako ga prijavljuju
> radna površina i svi objekti aplikacija — pa inverzije (`ENABLED` ⇒
> `UNAVAILABLE` i ostale) važe samo za neprazan skup (N37).

| | AT-SPI 2.56 | NVDA | Posledica |
|---|---|---|---|
| Role | 133 | 156 | potrebna mapirajuća tabela + profinjavanje |
| State | 45 | 53 | isto |
| Relation | 24 | — | mapira se na NVDA relacione property-je |

Profinjavanje ide preko `Accessible::get_attributes()`, čiji su ključevi u Gecko-u i Chromium-u **isti kao IA2 atributi koje NVDA već koristi**: `xml-roles` (ARIA role → landmarks, `switch`, `region`), `level` (nivo naslova za `1`–`9`), `tag` (HTML tag → `blockquote`, `embed`, `figure`, `fieldset`), `id`.

Provera:
```bash
python3 -c "import gi; gi.require_version('Atspi','2.0'); from gi.repository import Atspi
print(len([r for r in dir(Atspi.Role) if r.isupper()]),
      len([s for s in dir(Atspi.StateType) if s.isupper()]))"
```

Lokalizovane labele role i stanja (`controlTypes/role.py`, `state.py`, uključujući **negativne** labele tipa „nije označeno") se prenose kao gettext stringovi — to je direktno korisnički vidljiv tekst i mora da bude identičan NVDA prevodu po jeziku.

---

## 7. Tekstualni sloj

### 7.1 `TextRange` — interfejs

Prenosi se `textInfos.TextInfo` u celosti: jedinice (`CHARACTER`, `WORD`, `LINE`, `SENTENCE`, `PARAGRAPH`, `STORY`, `READINGCHUNK`, `OFFSET`), `copy`, `move`, `expand`, `collapse`, `compareEndPoints`, `setEndPoint`, `getTextWithFields`, `find`, `updateCaret`, `updateSelection`, `bookmark`, `boundingRects`. Takođe `ControlField`, `FormatField`, `FieldCommand` i `ControlField::getPresentationCategory`.

### 7.2 `OffsetsTextRange` nad `AtspiText`

NVDA-ov `OffsetsTextInfo` se preslikava gotovo direktno:

| NVDA hook | AT-SPI |
|---|---|
| `_getStoryText` | `Text::get_text(0, get_character_count())` |
| `_getStoryLength` | `Text::get_character_count()` |
| `_getTextRange(s,e)` | `Text::get_text(s, e)` |
| `_getCaretOffset` | `Text::get_caret_offset()` |
| `_setCaretOffset` | `Text::set_caret_offset()` |
| `_getSelectionOffsets` | `Text::get_selection(0)` |
| `_setSelectionOffsets` | `Text::set_selection(0, s, e)` |
| `_getLineOffsets` | `Text::get_string_at_offset(o, ATSPI_TEXT_GRANULARITY_LINE)` → `(str, start, end)` |
| `_getWordOffsets` | `…GRANULARITY_WORD`, ili ICU segmentacija |
| `_getSentenceOffsets` | `…GRANULARITY_SENTENCE`, ili ICU |
| `_getParagraphOffsets` | `…GRANULARITY_PARAGRAPH` |
| `_getFormatFieldAndOffsets` | `Text::get_attribute_run(offset)` + normalizator |
| `_getOffsetFromPoint` | `Text::get_offset_at_point` |
| `_getBoundingRectFromOffset` | `Text::get_character_extents` / `get_range_extents` |
| `_getNVDAObjectFromOffset` | `Hypertext::get_link_index` → `get_link` → `Hyperlink::get_object` |
| `_getOffsetsFromNVDAObject` | `Hyperlink::get_start_index` / `get_end_index` |

`STORY`, `READINGCHUNK` i `OFFSET` su NVDA-specifične jedinice koje ALUS sintetiše sam.

Provera:
```bash
python3 -c "import gi; gi.require_version('Atspi','2.0'); from gi.repository import Atspi
print([g for g in dir(Atspi.TextGranularity) if g.isupper()])"
```

### 7.3 Kodiranje offseta — **najverovatniji izvor tihih bugova**

> **ODSTUPANJE, IZMERENO (FAZA10.md N113).** Prva tačka ispod je bila
> pogrešna, a na njoj je stajao ceo tekstualni sloj: **AT-SPI2 offseti su
> codepoint-i, ne UTF-8 bajtovi.** Izmereno na Chrome / AT-SPI 2.56.2
> nezavisnom bibliotekom (Atspi preko GObject introspection): „Prvi pasus je
> proza. Ima dve rečenice." javlja 38 znakova, ima 39 bajtova, i
> `get_text(31, 32)` je „č". Sa bajtovima pretpostavljenim, strelica desno na
> „Treći" je čitala „ći" i preskakala „i", a ćirilica je bila pogrešna u
> svakom slovu. `OffsetsTextRange` sada ima `offsetUnit()` (NVDA-in
> `encoding`): `ObjectTextRange` broji codepoint-e, pregled ekrana nad
> sopstvenim stringom ostaje na bajtovima, a konverzija se radi samo gde se
> lokalni UTF-8 string seče ili segmentira. Mock backend je brojao bajtove,
> pa se slagao sa greškom; sada broji codepoint-e kao platforma.

Tri sistema u igri:
- **AT-SPI2**: offseti su ~~UTF-8 bajtovi~~ **codepoint-i** (vidi odstupanje gore),
- **NVDA**: pretpostavlja **UTF-16 jedinice** (`WCHAR_ENCODING`),
- **interna reprezentacija**: codepoint-i / grafemi.

NVDA ovo rešava slojem `OffsetConverter` (`textUtils/__init__.py`) i — ključno — **`UTF8OffsetConverter` već postoji** (`textUtils/__init__.py:320`). ~~ALUS postavlja `OffsetsTextRange::encoding = "utf_8"` i registruje taj konverter.~~ ALUS-ov `ObjectTextRange` odgovara `offsetUnit() == Codepoints`, jer to AT-SPI zaista broji. Onda svaki `_get*Offsets` hook prima AT-SPI bajt-offsete direktno, a ostatak steka radi u logičkim indeksima.

**Ako se ovo pogreši, svaki emoji, CJK string i kombinujući dijakritik je tiho pokvaren**, i to se vidi samo u bug prijavama. Uraditi jednom, u Fazi 5, sa testovima.

Vezano: `unicodeNormalize` je nosiv u `getTextInfoSpeech` proveri „je li ovo jedan znak" — dekomponovano „é" su dva codepoint-a ali jedan grafem, i skraćenica za sricanje mora i tad da proradi.

Segmentacija reči i rečenica: NVDA na Windows-u koristi Uniscribe ili ICU. ALUS koristi **isključivo ICU** (sistemski `libicu`), što je bolje a ne gore — `textUtils/uniscribe.py` se briše.

---

## 8. Navigacija

### 8.1 Review modovi

`review.py` je 190 linija i prenosi se u celosti. Tri moda:

| Mod | Šta obilazi | Implementacija |
|---|---|---|
| `object` | tekst unutar navigator objekta | `Object::makeTextRange` |
| `document` | ceo dokument koji sadrži objekat | tree interceptor |
| `screen` | prostorni sadržaj ekrana | **flat object review** (§10) |

`reviewMode_next` / `reviewMode_previous` (`alus+numpad7` / `alus+numpad1`) cikliraju i prepozicioniraju review cursor na tačku navigator objekta.

### 8.2 `CursorManager` — virtualni caret

Za slučajeve gde ALUS **poseduje** kursor (browse mode, OCR rezultati): sve komande kretanja po liniji/reči/znaku/paragrafu/rečenici, selekcija sa `shift`, `control+a`, `control+c`, pretraga (`alus+control+f`, `alus+f3`, `alus+shift+f3`). Anchor logika selekcije se prenosi doslovno — lako je pogrešiti i dobiti selekciju koja „skače".

### 8.3 `EditableText` — detekcija pomeraja karete

`editableText.py` ima **tri strategije sa budžetom vremena** za `_hasCaretMoved`, jer Windows aplikacije lažu o caret događajima. **GTK, Qt, Electron, Java i LibreOffice lažu na *različite* načine.** Dakle:

- Strategije se **prenose**, ne pojednostavljuju.
- `caretMovementDetectionUsesEvents` ostaje override po klasi/behavior-u.
- `_useEvents_maxTimeoutSec` i `_caretMovementTimeoutMultiplier` se **podešavaju po toolkit-u** i to je očekivan posao Faze 5, ne znak greške.

### 8.4 Tabela navigacija

`control+alt+strelice` (ćelija), `control+alt+pageUp/Down/home/end` (red/kolona), `alus+control+alt+strelice` (čitaj red/kolonu). AT-SPI daje sve direktno: `Table::get_accessible_at(row,col)`, `get_row_at_index`, `get_column_at_index`, `TableCell::get_row_column_span`, `get_row_header_cells`, `get_column_header_cells`.

`tableUtils.HeaderCellTracker` se prenosi za tabelarne formate gde se zaglavlja *deklarišu* a ne otkrivaju po ćeliji (spreadsheet app moduli). Jedno pravilo iz NVDA koje treba zadržati: **kad stojiš *na* zaglavlju neke ose, zaglavlja te ose se ne najavljuju**.

### 8.5 `TreeInterceptor`

Prenosi se: životni ciklus, `isReady`, `passThrough` politika, `rootNVDAObject`, `RootProxyTextInfo`, kreiranje preko `Object::treeInterceptorClass`.

---

## 9. Browse mode

### 9.1 Zašto se virtualni baferi **ne** portuju

NVDA ubacuje C++ (`nvdaHelper`) u proces pregledača, obilazi drvo u procesu i serijalizuje rezultat u bafer koji NVDA indeksira celobrojnim offsetom. Bafer je **kopija** i mora eksplicitno da se osvežava (`alus+f5`).

Nijedan od razloga ne važi na Linuxu:

1. **Nema cross-process COM cene.** AT-SPI je D-Bus, ~50 µs po pozivu, sa batch-ovanjem.
2. **AT-SPI već ima servis pretrage.** `org.a11y.atspi.Collection` (`GetMatches`, `GetMatchesFrom`, `GetMatchesTo`) sa `MatchRule(states, attributes, roles, interfaces)` i redom `CANONICAL`/`FLOW`/`TAB`/`REVERSE_*` je **direktna zamena** za `VBuf_findNodeByAttributes`, uključujući semantiku `MATCH_ANY`/`ALL`/`NONE` koja odgovara NVDA-ovim alternativama/obaveznim/`None` konvencijama.

   > **Izmereno 7.–8. 11. 2026., i ova tačka je bila preoptimistična u dva detalja** (`FAZA6.md` N89, N90):
   >
   > * **`FLOW` u `GetMatches` ne implementira nijedan provajder.** Chrome 157 (Blink), Firefox 153 (Gecko) i LibreOffice Writer (VCL/UNO) vraćaju **prazan niz** za `FLOW` i `TAB`, a tačan odgovor za `CANONICAL`. Isti ti provajderi **poštuju** `FLOW` u `GetMatchesFrom`. Dakle red čitanja je dostupan pritisku tastera (§9.2), ali **ne** listi elemenata — ona ide u redu drveta. WebKitGTK i Qt nisu izmereni.
   > * **`MATCH_INVALID` nije „kriterijum se ne koristi".** Nijedan od tri provajdera to tako ne implementira: pravilo sa `MATCH_INVALID` za stanja ne poklapa **ništa**. Nekorišćen kriterijum se piše kao **prazan skup sa `MATCH_ALL`**, što je logički isti uslov. Orca nikad ne pošalje `MATCH_INVALID`, pa to niko i nije testirao.
3. **AT-SPI već ima ravan tekst.** Gecko i Chromium izlažu telo dokumenta kao `AtspiText` sa `get_string_at_offset(…, LINE)`, zamenskim znakom U+FFFC za ugrađene objekte i `Hypertext`/`Hyperlink` za mapiranje offseta na dete-objekat. **To *je* virtualni bafer — održava ga pregledač.**

Dakle: `script_refreshBuffer` (`alus+f5`) nema smisla i ili se briše ili postaje „pročitaj ponovo"; `prepare()` je no-op; `isReady` je „BUSY stanje korena je prošlo"; identitet čvora je AT-SPI `(bus name, object path)` — **stabilniji** od NVDA-ovog `(docHandle, ID)`.

### 9.2 Quick navigation preko `Collection`

```
_iterNodesByType(itemType, direction, pos)
  → Collection::GetMatchesFrom(currentObject, matchRule,
                               sortBy = FLOW, tree = INORDER, count, traverse)
```

Jedan D-Bus poziv po pritisku tastera. `MatchRule` tabela po tipu elementa se gradi po modelu `virtualBuffers/gecko_ia2._searchableAttribsForNodeType`, čiji je obrazac uvek **role ILI ARIA token ILI HTML tag**:

> **Izmereno:** `traverse` mora da bude `TRUE`. Sa `FALSE` libatspi razmatra samo **neposrednu decu** objekta, pa veb dokument — čiji koren ima jedno dete — ne vraća nikada ništa (`FAZA6.md` N88). Isti defekt je u Writer-u **nevidljiv**, jer je njegovo drvo dokumenta plitko.
>
> **Izmereno:** u LibreOffice Writer-u `h` radi (naslovi su uloga `heading`), a `k` **ne** — Writer linkove izlaže kao hipertekst unutar odlomka, ne kao objekte uloge `link`, pa ih pretraga po ulozi ne nalazi (`FAZA6.md` N95). Taster tada mora da **kaže** da ih ne nalazi.


| Taster | Tip | MatchRule (skica) |
|---|---|---|
| `h` | heading | `role=HEADING` |
| `1`–`9` | heading1–9 | `role=HEADING` + `attributes{level=N}` |
| `k` | link | `role=LINK` |
| `v` / `u` | visited / unvisited link | + `VISITED` stanje / negacija |
| `f` | form field | widget role-ovi bez `READ_ONLY`; ili editable combo/text |
| `i` | list item | `role=LIST_ITEM` |
| `l` | list | `role=LIST` |
| `t` | table | `role=TABLE` (+ bez `table-layout` ako je layout off) |
| `b` | button | `role=PUSH_BUTTON` |
| `x` | check box | `role=CHECK_BOX` ili `xml-roles` sadrži `switch` |
| `r` | radio button | `role=RADIO_BUTTON` |
| `c` | combo box | `role=COMBO_BOX` |
| `e` | edit | `role=ENTRY` |
| `g` | graphic | `role=IMAGE` |
| `q` | block quote | `role=BLOCK_QUOTE` ili `tag=blockquote` |
| `s` | separator | `role=SEPARATOR` |
| `m` | frame | `role=INTERNAL_FRAME` |
| `d` | landmark | `role=LANDMARK` ili `xml-roles` ∈ ARIA landmarks |
| `o` | embedded object | `tag ∈ {embed,object,applet,audio,video,figure}` ili `role ∈ {APPLICATION,DIALOG}` |
| `a` | annotation | `DETAILS` relacija / `role=COMMENT` |
| `w` | error | `attributes{invalid=spelling}` |
| `p` | text paragraph | `role=PARAGRAPH` sa tekstom |
| `n` | not-link-block | preskoči blok linkova (izvedeno, ne MatchRule) |

Obratiti pažnju na NVDA-ov detalj: kontejnerski tipovi (`grouping`, `region`) nose dodatni uslov `name != ""`, da se neimenovane grupe ne nude korisniku.

Provera:
```bash
python3 -c "import gi; gi.require_version('Atspi','2.0'); from gi.repository import Atspi
print([m for m in dir(Atspi.Collection) if 'match' in m])
print([s for s in dir(Atspi.CollectionSortOrder) if s.isupper()])"
```

### 9.3 `getTextWithFields` — **najvažniji novi kod u ALUS-u**

Ovo je jedini deo browse mode-a koji nema gotovu zamenu. AT-SPI daje **ravan tekst + atribute po offsetu**, ali ne daje NVDA-ov **ugnježdeni `controlStart`/`controlEnd` tok komandi** na kom počiva *sve* čitanje dokumenata.

Dobra vest: rešenje već postoji u NVDA, u platformski neutralnom obliku — `compoundDocuments.py`:

```
compoundDocuments._getControlFieldForObject(obj, ignoreEditableText=True)   # :164-211
    → SINTETIŠE ControlField iz role, states, name, description, level,
      table koordinata objekta

compoundDocuments.TreeCompoundTextInfo.getTextWithFields(formatConfig)      # :331-368
    → emituje controlStart / controlEnd / formatChange tok
      DIFF-ovanjem lanaca predaka uzastopnih leaf TextInfo-a
```

**Taj par je najvažnija stvar za port u celom browse mode-u.** Od njegove ispravnosti zavise:
- `commonFieldCount` kontinuitet (zato je potreban stabilan `uniqueID` — koristi AT-SPI `(bus name, object path)`),
- `isBlock` za cepanje iskaza u say-all-u,
- `table-*` ključevi za tabela navigaciju,
- cela najava „ulazim u listu / izlazim iz tabele".

Uz to se prenosi **koncept** `virtualBuffers._normalizeControlField` (`:393-448`): jedno mesto koje pretvara backend atribut-supu u kanonske ALUS ključeve (`table-id`, `table-rownumber`, `table-rowheadertext`, `isHidden`, `alwaysReportName`, `uniqueID`). Samoreferentna zaglavlja se preskaču (Chrome ih emituje).

`XMLFormatting.py` se **ne portuje** — postoji samo zato što C++ bafer serijalizuje u XML.

### 9.4 Oblik rešenja

```
AtspiDocumentTreeInterceptor : BrowseModeDocumentTreeInterceptor
    TextRange = AtspiDocumentTextRange : BrowseModeDocumentTextRange, OffsetsTextRange
        encoding = "utf_8"
        _get*Offsets            → §7.2
        getTextWithFields       → §9.3 (ancestor-chain diff)
    _iterNodesByType            → §9.2 (Collection)
    isAlive                     → koren nije DEFUNCT
    isReady                     → BUSY stanje korena je prošlo
    shouldPrepare               → false (nema šta da se gradi)
```

Za LibreOffice Writer: NVDA ne koristi virtualni bafer nego `CompoundDocument` (`soffice.py:610-620` dodeljuje `obj.treeInterceptorClass = SymphonyDocument`). ALUS isto — `CompoundDocument` nad AT-SPI, bez quick-nav-a, ili `CompoundDocument` + `BrowseMode` ako quick-nav treba.

**Za pregledače app modul ne treba.** NVDA nema `firefox.py` ni `chrome.py` — podrška je u objektnom sloju (`NVDAObjects/IAccessible/mozilla.py`, `chromium.py`). Na Linuxu Firefox i Chromium govore AT-SPI sa skoro identičnom semantikom (AT-SPI most se generiše iz istog internog drveta), pa **jedan `GeckoChromiumDocument` overlay** ključiran na `toolkit_name ∈ {gecko, Chromium}` pokriva oba.

---

## 10. Screen review bez display modela

NVDA-ov `displayModel.py` je zapis **svakog poziva za iscrtavanje teksta**, snimljen hook-ovanjem GDI/DirectWrite unutar ciljnog procesa, sa bounding rect-om po znaku. Na Linuxu to nije izvodljivo: GTK4/Qt6 iscrtavaju kroz Cairo/Skia/QPainter u klijentske bafere, a pod Wayland-om ne možeš ni da pročitaš tuđu površinu.

**Rešenje za v1.0: flat object review.** „Screen review" znači *ravan pregled objekata*:

```
1. uzmi aktivan toplevel prozor
2. obiđi sve potomke sa presentationType == content
3. sortiraj prostorno po (top, left) iz Component::get_extents(ATSPI_COORD_TYPE_SCREEN)
4. spoji njihov tekst u sintetički OffsetsTextRange
     _getStoryText            = spojeni tekst
     _getBoundingRectFromOffset = paralelni niz rect-ova
```

Jedino mesto koje se menja je `review.getScreenPosition` (`review.py:59-80`). Ovo daje **korisnički vidljivo ponašanje** screen review-a (prostorno, ravno, po prozoru) bez ijednog platformskog hook-a, i čuva `screenExplorer`, mouse tracking i (kasnije) touch eksploraciju besplatno.

`screenExplorer.py` (90 linija) se prenosi skoro doslovno — jedine platformski zavisne stvari su `objectFromPoint` → `Component::get_accessible_at_point` i `makeTextInfo(Point)` → `Text::get_offset_at_point`.

OCR (v1.1) je **poslednji** fallback, posle AT-SPI i posle flat review-a: `contentRecog` okvir se prenosi (čist je — recognizer prima pixel bafer + `RecogImageInfo`, vraća `LinesWordsResult`), backend je Tesseract, snimanje ekrana preko `xdg-desktop-portal` Screenshot/ScreenCast.

---

## 11. Simboli, rečnici, formatiranje

### 11.1 Obrada teksta

`characterProcessing.py` (953 linije) i `speechDictHandler/` su **platformski neutralni** i prenose se skoro neizmenjeno.

`SymbolLevel`: `NONE=0`, `SOME=100`, `MOST=200`, `ALL=300`, `CHAR=1000`, `UNCHANGED=-1`. Praznine su namerne (mesto za buduće nivoe). `alus+p` ciklira kroz njih.

Registar rečnika simbola, po redu primene:
```
1. cldr      locale/<lang>/cldr.dic      Unicode CLDR → imena emoji-ja; opciono
2. builtin   locale/<lang>/symbols.dic   OBAVEZAN, skriven iz GUI-ja;
                                         JEDINI koji sme složene (regex) simbole
3. add-on    <addon>/locale/<lang>/symbols-<name>.dic
4. user      ~/.config/alus/symbols-<lang>.dic   obavezan, bez regex-a
```
„Samo `builtin` sme regex" je namerno bezbednosno svojstvo — sačuvati ga.

Rečnici izgovora (`speechDictHandler`) rade u redu **builtin → default → voice → temp**, pre obrade simbola. Taj red je korisnički vidljiv.

Jedna zamka iz NVDA koju treba preneti: četiri tipa unosa sa granicom reči (`WORD`, `PART_OF_WORD`, `START_OF_WORD`, `END_OF_WORD`) **moraju** koristiti `regex` biblioteku sa `VERSION1` semantikom, jer standardni `re` **isključuje kombinujuće znakove iz `\w`**. Bez toga unosi rečnika pucaju na svakom jeziku sa dijakriticima.

> **Izvedeno u Fazi 2 preko `GRegex` (PCRE2), ne ICU** — vidi `FAZA2.md` O2.
> Zahtev je `\w` koje ne isključuje kombinujuće znakove, i on je ispunjen
> eksplicitnom klasom `[\p{L}\p{M}\p{N}_]` (`alus::text::kWordCharacterClass`).
> Razlog za PCRE2: **obrasci složenih simbola su podaci iz NVDA locale fajlova,
> pisani u Python `re` sintaksi** — `(?P<ime>…)` i `(?P=ime)` — koju PCRE2
> prima doslovno, a ICU `(?P<…>)` ne podržava uopšte. ICU i dalje ulazi u
> Fazi 5, za segmentaciju reči i rečenica, što PCRE2 ne ume.

### 11.2 Navigacija po paragrafima

`documentNavigation/` — tri stila iz `config`:
- `APPLICATION` — veruj aplikacijinoj jedinici paragrafa,
- `SINGLE_LINE_BREAK` — svaki prelom reda je nov paragraf,
- `MULTI_LINE_BREAK` — samo prazan red je nov paragraf (Markdown interpretacija).

Konvencija povratne vrednosti `(passKey, moved)` dozvoljava fallback na aplikacijino ponašanje — zadržati je.

---

## 12. App moduli i ekstenzibilnost

### 12.1 Registar app modula

NVDA ključira app module po **imenu izvršnog fajla** (`runningTable: dict[pid, AppModule]`, zaštićen RLock-om). Na Linuxu ključ je bogatiji i bolji:

```
primarno:    basename(readlink("/proc/<pid>/exe"))
sekundarno:  Application accessible name
diskriminator: Application::get_toolkit_name() + get_toolkit_version()
```

Alias tabela (NVDA-ov `EXECUTABLE_NAMES_TO_APP_MODS`) se prenosi kao koncept — `code-insiders` → `code`, itd.

Životni ciklus se prenosi doslovno, uključujući `handleAppSwitch` koreografiju: `event_appModule_loseFocus` na odlazeće, pa **sva promena trigera profila u jednoj atomskoj transakciji** (`atomicProfileSwitch`), pa `event_appModule_gainFocus`. Bez atomičnosti, prebacivanje aplikacija rekonstruiše profil N puta.

### 12.2 Pet mehanizama prilagođenja

Svi se prenose, sa istim imenima:

1. `chooseNVDAObjectOverlayClasses(obj, clsList)` — dodaj/ukini behavior; `clsList[0:0]` = na vrh.
2. `event_NVDAObject_init(obj)` — pokni atribute instance (npr. `obj.TextInfo = …`).
3. `event_<name>(obj, nextHandler)` — presretni događaj; **ne pozovati `nextHandler` = pojesti događaj**.
4. Gestovi i skriptovi — `AppModule` je `Scriptable`, bindings žive samo dok je aplikacija fokusirana.
5. Property override-i — `statusBar`, `getStatusBarText`, `productName`, `devInfo`.

Tri generalizovana hook-a koja NVDA-ovi app moduli najčešće koriste i koja ALUS mora da ima: **`shouldAllowFocusEvent`** (potisni lažan focus događaj), **`lastQueuedFocusObject`** (detekcija duplikata), i **override `container`** (preoblikuj lanac predaka fokusa).

### 12.3 Add-on API

v1.0: **stabilan C ABI preko `dlopen`.**

```c
// alus-plugin.h — ABI verzija se proverava pri učitavanju
typedef struct AlusPluginV1 {
    uint32_t    abi_version;        // ALUS_PLUGIN_ABI_V1
    const char* name;
    const char* min_alus_version;
    int  (*init)(AlusHost* host);
    void (*terminate)(void);
    void (*choose_overlays)(AlusObject* obj, AlusBehaviorList* list);
    /* event_* i script registracija kroz AlusHost pozive */
} AlusPluginV1;
```

Add-on direktorijumi, po NVDA modelu „ubrizgavanja putanje" (scratchpad ima prioritet nad sistemskim):
```
~/.local/share/alus/addons/<name>/   korisnički
<datadir>/alus/plugins/              sistemski
~/.local/share/alus/scratchpad/      razvoj (najviši prioritet)
```

Dvofazna instalacija/deinstalacija iz NVDA se prenosi — `.so` koji je učitan ne može da se izbriše, pa se markira i briše pri sledećem startu.

**Namerno odloženo:** Python add-on host. NVDA-ova add-on kultura je u Pythonu i to je velika vrednost, ali ugrađivanje CPython-a u C++ jezgro donosi GIL i drugi runtime. Razmatra se u v1.1, iza C ABI-ja, kao jedan plugin koji hostuje Python module.

---

## 13. Threading model — eksplicitno

| Nit | Šta radi |
|---|---|
| **glavna / `GMainLoop`** | AT-SPI događaji, dispatch gestova, skriptovi, **celo stanje `SpeechManager`-a**, **sva mutacija brajevih regiona**, `Cached<T>`, globalno stanje `api` |
| SSIP reader (u `libspeechd`) | čita notifikacije; **marshal preko `g_idle_add`** |
| Brajev I/O | `brlapi` poll; **samo upis ćelija**, sa single-slot zamenom |
| `GThreadPool` | veliki obilasci drveta, `liblouis` na dugim redovima, OCR |
| Watchdog | heartbeat, propuštanje tastera kad je jezgro zaglavljeno |

**Pravilo:** sve što dira stanje je na glavnoj niti. Zaključavanje je minimalno; umesto njega se koristi:
- **snapshot-pa-iteriraj** (handler liste, coroutine registry, lista indeksa u govoru),
- **single-slot zamena** umesto reda (brajev upis: ako stigne nov frame dok se stari piše, stari se **zamenjuje** — međukadrovi su bezvredni),
- **provera zastarelog izvora** (`if (synth != currentSynth()) return;`).

Potrebni lokovi: brojači čekajućih događaja (pišu se sa a11y niti), `queuedWrite` brajevog reda, `runningTable` app modula, zastavica isključivanja.

---

## 14. Brajev red

`braille/` se prenosi strukturno: `Region` (`TextRegion`, `ObjectRegion`, `TextInfoRegion`, `CursorRegion`), `BrailleBuffer` sa prozorom nad spojenim ćelijama, word wrap (`NONE` / `MARK_WORD_CUTS` / `AT_WORD_BOUNDARIES` / `AT_WORD_OR_SYLLABLE_BOUNDARIES`), tethering (`focus` / `review` / `auto`), `saveWindow()`/`restoreWindow()` kao `(region, offset)` par.

Dva mehanizma koalescenja koji **nisu** opcioni:
1. **`_regionsPendingUpdate`** — set regiona koji se obrađuje **jednom po pump ciklusu**. N pomeraja karete u jednom ciklusu = jedan update regiona i jedan upis na red.
2. **Single-slot upis** — §13.

Prevod: `liblouis`. Tri detalja iz `louisHelper.py` koja se lako promaše:
- `liblouis` **menja listu `typeform` na mestu** → prosleđivati kopiju,
- `dotsIO` mod znači da ćelije dolaze kao znakovi sa bitom `0x8000` → `cell & 0xFF`,
- NVDA registruje **sopstveni table resolver** koji ignoriše `LOUIS_TABLEPATH` da bi add-oni mogli da donose tabele; prenosi se nepromenjeno.

Red: `brlapi` (brltty), umesto NVDA-ovih ~40 drajvera za pojedinačne redove. To je veliko smanjenje obima i korisnici već imaju brltty podešen. `br(...)` gesture identifikatori i `gestureMap` po drajveru se čuvaju da korisnički `gestures.ini` ostane kompatibilan.

Brajev unos (`bk:` gestovi): `bk:dots`, `bk:dot7` (obriši), `bk:dot8` (enter), `bk:dot7+dot8` (prevedi), plus `braille_toggle<Modifier>` familija za emulaciju tastature sa brajevog reda.

---

## 15. Red izgradnje

Izveden iz zavisnosti, ne iz vidljivosti funkcija. Detaljan plan sa izlaznim kriterijumima je u `02-FAZE.md`.

```
 1. Cached<T>, Queue+coroutines, Loop+pump, extension points, Watchdog, Log, config
 2. Role/State/OutputReason, TextRange interfejs, OffsetConverter (UTF-8!)
 3. a11y::Object nad libatspi + Api globalno stanje + EventLimiter
 4. EventDispatcher lanac + Review + screenExplorer
 5. ISynth/SsipSynth + SpeechManager + Presenter + Symbols/Dictionaries
 6. input (AtspiDevice) + gesture mape + script registry + input help
 7. OffsetsTextRange nad AtspiText + EditableText behavior
 8. LiveText/Terminal behavior + diff
 9. getTextWithFields (ancestor-chain diff) + control/format field govor
10. TreeInterceptor + table nav + CursorManager + BrowseMode + Collection quick-nav
11. braille (liblouis + brlapi)
12. app moduli + plugin ABI
13. GUI + lokalizacija
```

**Tri stvari koje će najverovatnije ugristi**, u redu verovatnoće:
1. **Kodiranje offseta** (§7.3) — tri sistema, tihe greške, vidljive samo u bug prijavama.
2. **Sinteza `getTextWithFields`** (§9.3) — sve čitanje dokumenata zavisi od nje.
3. **Varijansa pouzdanosti događaja** (§8.3) — svaki toolkit laže drugačije; očekivati podešavanje po toolkit-u.
