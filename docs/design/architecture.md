# ALUS — Architecture

> Design document 2 of 3. Assumes [`vision.md`](vision.md).
>
> Every platform claim in this document comes with a command that checks it.
> Versions change; the claims have to stay checkable.
>
> Section numbers are stable: code comments cite them (`architecture.md
> §4.5`). Where the implementation proved the original plan wrong, the
> section says so in a quoted **deviation** note, with the finding number
> (`N…`) from the phase reports in [`../history/`](../history/README.md).

---

## 0. The core thesis

NVDA spent 20 years solving problems that are **the same on every platform**:
how not to block when an application blocks, how not to flood speech under an
avalanche of events, how to keep the caret in step with the audio, how not to
ask the platform the same thing ten times in one second. The solutions are in
its code and they are **platform neutral**.

So ALUS:

- **carries NVDA's core architecture over practically unchanged** (pump,
  queue, cache, event limiter, speech manager, watchdog),
- **replaces only the platform layer** (Windows accessibility APIs → AT-SPI2,
  WASAPI → PipeWire/Pulse, wx → GTK 4),
- **drops what existed only because of Windows** (in-process injection,
  virtual buffers, the display model).

Reference for this whole document:
`reference/nvda/projectDocs/design/technicalDesignOverview.md`.

---

## 1. Technology stack

| Layer | Choice | Version on the target system | pkg-config |
|---|---|---|---|
| Language | C++20 (g++ 14.2 / clang) | `<coroutine>`, `<format>`, `<ranges>` work | — |
| Build | Meson + Ninja | 1.7.0 / 1.12.1 | — |
| Tests | Catch2 3.x | 3.7.1 (a wrap provides it when the system has none) | `catch2-with-main` |
| Event loop | GLib `GMainLoop` | 2.84.4 | `glib-2.0 gobject-2.0 gio-2.0` |
| Accessibility | libatspi | 2.56.2 | `atspi-2` |
| Speech | libspeechd (SSIP) | 0.12.0 | `speech-dispatcher` |
| Braille translation | liblouis | 3.33.0 | `liblouis` |
| Braille display | libbrlapi | 6.7 | (no .pc on Debian; found by header and library) |
| Audio (tones, wav) | libpulse-simple → PipeWire | 17.0 / 1.4.2 | `libpulse-simple` |
| Key names | X11 keysymdef.h | — | constants copied into `alus/input/keysyms.hpp` |
| Keys (keysym ↔ character) | libxkbcommon | 1.7.0 | `xkbcommon` |
| Segmentation | ICU | optional, loud fallback | `icu-uc` |
| GUI | GTK 4 + libadwaita | 4.18.6 | `gtk4`, `libadwaita-1` |
| Localisation | gettext | — | — |

**The GUI does not live in the screen reader's process, and that is the one
item in this document that measurement overturned.** NVDA keeps its wx GUI
in-process and this document originally did the same; on AT-SPI it does not
work. ALUS reads the desktop with **synchronous** calls from its main loop,
and a call about an object is answered by the process that owns the object.
When the object is ALUS's own window, that is the same loop, blocked inside a
call that waits for itself. Measured: the core stopped for **7.5 s**, and the
window **could not be read** (`get_role failed: Did not receive a reply`). So
the window is `alus-settings`, a separate executable, and GTK is linked into
it and not into the daemon. See [`phase-9.md`](../history/phase-9.md) N79 and
N80.

**GMainLoop is not a choice but a consequence:** `libatspi` is a GObject
library and needs a `GMainContext` to deliver D-Bus signals. The good news:
`GMainLoop` is an almost perfect equivalent of the wx loop NVDA uses
(`wx.CallAfter` ↔ `g_idle_add`, `wx.CallLater` ↔ `g_timeout_add`,
`wx.Timer` ↔ `GSource`).

> **Two stack changes from phase 3**, see [`phase-3.md`](../history/phase-3.md) O1 and O2.
> 1. **`libxkbcommon` was added.** The gesture `kb(laptop):alus+š` has to
>    become the keysym the compositor compares against the grab, and that is
>    the *legacy* keysym `XK_scaron` (0x1b9), not the Unicode form 0x1000161.
>    `xkb_utf32_to_keysym` does that canonically, and it is the same library
>    the compositor uses.
> 2. **`x11proto-dev` is not a dependency.** The keysym constants ALUS needs
>    are copied into `include/alus/input/keysyms.hpp` (protocol constants,
>    unchanged for decades), so a Wayland-only system does not pull in X11
>    headers.

Checking the stack:
```bash
pkg-config --modversion atspi-2 speech-dispatcher liblouis glib-2.0 xkbcommon
g++ -std=c++20 -fsyntax-only -x c++ - <<< '#include <coroutine>
#include <format>
#include <ranges>
int main(){}'
```

---

## 2. Module mapping: NVDA → ALUS → Linux

| NVDA | ALUS module | Linux API | Note |
|---|---|---|---|
| `core.py` main loop + `CorePump` | `alus::core::Loop`, `PumpRequest` | `GMainLoop`, `g_idle_add`, `g_timeout_add` | §3.1 |
| `queueHandler.py` + generators | `alus::core::Queue`, `TaskRegistry` | — | §3.2, C++20 coroutines |
| `eventHandler.py` | `alus::core::EventDispatcher` | — | §3.3 |
| `IAccessibleHandler/orderedWinEventLimiter.py` | `alus::a11y::EventLimiter` | — | §3.4, **mandatory** |
| `baseObject.py` AutoPropertyObject | `alus::core::Cached<T>` | — | §3.5 |
| `baseObject.py` ScriptableObject | `alus::script::Scriptable` | — | §5.3 |
| `extensionPoints/` | `alus::core::{Action,Filter,Decider,Chain}` | — | §3.6 |
| `watchdog.py` | `alus::core::Watchdog` | D-Bus timeouts | §3.7 |
| `logHandler.py` | `alus::core::Log` | — | §3.8 |
| `config/` | `alus::config` | `GKeyFile` | §3.9 |
| `inputCore.py` | `alus::input::GestureManager` | — | §5.1 |
| `keyboardHandler.py` | `alus::input::Keyboard` | `org.freedesktop.a11y.KeyboardMonitor`, `keysymdef.h` | §5.2 |
| `scriptHandler.py` + `@script` | `alus::script::Registry` | — | §5.3 |
| `vkCodes.py` + `keyLabels.py` | `alus::input::KeyNames` | `keysymdef.h` | §5.2 |
| `mouseHandler.py` | `alus::input::Mouse` | `Component` | — |
| `speech/speech.py` | `alus::speech::Presenter` | — | §4.2 |
| `speech/manager.py` | `alus::speech::SpeechManager` | — | §4.1, **the hardest part** |
| `speech/commands.py` + `priorities.py` | `alus::speech::{Command,Priority}` | — | §4.3 |
| `speech/sayAll.py` | `alus::nav::SayAll` | — | coroutine |
| `speech/speechWithoutPauses.py` | `alus::speech::SentenceBuffer` | — | §4.4 |
| `synthDriverHandler.py` + `synthDrivers/` | `alus::speech::ISynth`, `SsipSynth`, (`EspeakDirectSynth`, planned) | `libspeechd`, opt. `libespeak-ng` | §4.5 |
| `nvwave.py` + `tones.py` | `alus::audio::WavePlayer`, `Tones` | `libpulse-simple` | §4.6 |
| `IAccessibleHandler`/`UIAHandler`/`JABHandler` | `alus::a11y::Registry` (**one**, not four) | `libatspi` | §6.1 |
| `NVDAObjects/__init__.py` | `alus::a11y::Object` + `OverlayRegistry` | `Accessible`, `Component`, `Action`, `Value`, `Selection` | §6.2 |
| `NVDAObjects/behaviors.py` | `alus::a11y::behaviors::*` | — | §6.3, platform neutral |
| `api.py` | `alus::a11y::Api` (global state) | — | §6.4 |
| `controlTypes/` | `alus::a11y::{Role,State,OutputReason}` | `AtspiRole`, `AtspiStateType` | §6.5 |
| `textInfos/__init__.py` | `alus::text::TextRange` (interface) | — | §7.1 |
| `textInfos/offsets.py` | `alus::text::OffsetsTextRange` | `AtspiText` | §7.2 |
| `textUtils/` | `alus::text::{OffsetConverter,Segmenter}` | ICU (system `libicu`) | §7.3, **mind the encoding** |
| `review.py` | `alus::nav::Review` | — | §8.1 |
| `cursorManager.py` | `alus::nav::CursorManager` | — | §8.2 |
| `editableText.py` | `alus::nav::EditableText` | `AtspiEditableText` | §8.3 |
| `documentBase.py` | `alus::nav::DocumentWithTableNavigation` | `AtspiTable`, `AtspiTableCell` | §8.4 |
| `treeInterceptorHandler.py` | `alus::nav::TreeInterceptor` | — | §8.5 |
| `browseMode.py` | `alus::nav::BrowseMode` | **`AtspiCollection`** | §9 |
| `compoundDocuments.py` | `alus::nav::CompoundDocument` | — | §9.3, **the key port** |
| `virtualBuffers/` | — **not ported** | — | §9.1 |
| `displayModel.py` | — **not ported**; replaced by flat object review | `Component::get_extents` | §10 |
| `characterProcessing.py` | `alus::text::Symbols` | — | §11.1 |
| `speechDictHandler/` | `alus::speech::Dictionaries` | — | §11.1 |
| `appModuleHandler.py` + `appModules/` | `alus::app::{Module,Registry}` | `/proc/<pid>/exe`, `Application::get_toolkit_name` | §12 |
| `globalPluginHandler.py` + `addonHandler/` | `alus::app` plugins (stable C ABI) | `dlopen` | §12.3 |
| `gui/` (wxPython) | `alus::gui` | GTK 4 + libadwaita, **in its own process** | phase 9 |
| `NVDAHelper`, `comInterfaces`, `oleacc`, `winBindings`, `XMLFormatting`, `textUtils/uniscribe` | — **dropped** | — | Windows only |

---

## 3. Core infrastructure

### 3.1 The loop and the pump — carried over literally

NVDA **has no main loop of its own**. The loop is `wx.MainLoop()`, and NVDA's
work is driven by `CorePump(wx.Timer)`, which is "kicked" on request
(`core.py:990-1049`). `requestPump(immediate)` is a **coalescer**:

```cpp
enum class PumpRequest { None, Delayed, Immediate };
constexpr int kPumpMaxDelayMs = 10;   // NVDA: PUMP_MAX_DELAY

// requestPump: N requests between two pumps → at most one pump.
// Immediate upgrades a Delayed that is already waiting.
void Loop::requestPump(bool immediate) {
    if (pending_ == PumpRequest::None ||
        (immediate && pending_ == PumpRequest::Delayed)) {
        pending_ = immediate ? PumpRequest::Immediate : PumpRequest::Delayed;
        queueRequest();          // on the main thread: directly; otherwise g_idle_add
    }
}
```

One pump cycle (`core.py:1015-1048`). **The order of operations is part of
the contract**:

```cpp
void Loop::tick() {
    assert(!isPumping_);          // the pump is NOT re-entrant
    isPumping_ = true;
    pending_ = PumpRequest::None;
    watchdog_.alive();

    a11y_.drainEvents();          // platform events FIRST → into the queue
    queue_.pumpAll();             // coroutine steps, then flush the queue
    mouse_.pumpAll();
    braille_.pumpAll();

    Cached<>::invalidateAll();    // the cache lives EXACTLY one cycle
    watchdog_.asleep();
    isPumping_ = false;

    // re-entry is DEFERRED through the queue, never direct (stack overflow)
    if (pending_ == PumpRequest::Immediate) queueRequest();
    else if (pending_ == PumpRequest::Delayed) processRequest();
}
```

Three invariants that **must not** change:
1. Platform events are drained **before** the queue, so they are converted
   and executed in the same cycle.
2. `Cached<>::invalidateAll()` at the end of every cycle defines the
   lifetime of the cache.
3. The pump is not re-entrant; re-entry goes through the queue.

`PUMP_MAX_DELAY = 10 ms` **is not arbitrary**: it exists so events are
batched. Do not lower it without measuring.

### 3.2 The queue and coroutines

One **FIFO** queue. Priority is *not* expressed through several queues but
through the `immediate` flag, which changes how the pump is scheduled
(`queueHandler.py:43-53`).

```cpp
void Queue::flush() {
    // NVDA: range(qsize()+1) — a function that re-queues itself
    // CANNOT wedge the pump. The bound is mandatory.
    const size_t bound = q_.size() + 1;
    for (size_t i = 0; i < bound && !q_.empty(); ++i) {
        auto fn = std::move(q_.front()); q_.pop();
        watchdog_.alive();                   // heartbeat per item
        try { fn(); } catch (...) { log_.exception(); }
    }
}
```

NVDA's registered generators (`queueHandler.registerGeneratorObject`),
cooperative multitasking for say all and buffer loading, become **C++20
coroutines**, stepped once per cycle:

```cpp
struct Task { struct promise_type { /* suspend_always at every co_yield */ }; };
// TaskRegistry::pumpAll(): one resume() per task per cycle;
// while live tasks remain → requestPump() (a self-sustaining tick).
```

This is one place where C++20 is **better** than NVDA's Python: `co_await`
on "the next pump cycle" is type safe and allocates nothing per step.

### 3.3 Event dispatch — a chain with an explicit continuation

NVDA's most valuable extensibility pattern (`eventHandler.py:140-166`).
Levels 1–3 receive `(obj, nextHandler)` and **decide** whether to continue;
level 4 is terminal.

```
1. every global plugin        → handler(obj, next)
2. obj.appModule              → handler(obj, next)
3. obj.treeInterceptor        → handler(obj, next)   [only if isReady or ignoreIsReady]
4. the object itself          → handler()            [no next — terminal]
```

Alongside it, pending event counters (`_pendingEventCountsBy{Name,Obj,NameAndObj}`)
and the predicate `isPendingEvents(name, obj)`: "should I do this piece of
work at all, or will a newer event overwrite it?". It is used throughout the
speech and braille layers. **Port it with the counters, not without them.**

`gainFocus` is queued with `immediate=true`. `doPreGainFocus` can expand one
`gainFocus` into `foreground` + N `focusEntered` events, executed
synchronously.

### 3.4 The event limiter — why NVDA stays responsive

`OrderedWinEventLimiter` sits between the OS callback thread and the pump.
**AT-SPI floods just as winEvents do** (Chromium while loading a page,
LibreOffice while scrolling a sheet), so this is **part of phase 1, not an
optimisation for later**.

Rules (first stage, `addEvent`):
- deduplicate **by moving in time**: a duplicate gets a new sequence number,
  not a second entry in the queue;
- `ChildrenChanged:Add` cancels a pending `Remove` for the same object and
  vice versa;
- `Window:Activate` **deletes** a pending `StateChanged:Focused` for the same
  object;
- of menu events, keep **only the last**;
- **at most 10 events per source application per cycle**, except for the
  object that has focus;
- of focus events, keep **only the last 4**.

Second stage (`pumpAll`): emit **only one** focus/foreground event; try the
newest, then fall back to older ones until one succeeds.

The NVDA rules are listed in
`reference/nvda/source/IAccessibleHandler/orderedWinEventLimiter.py`; their
mapping onto AT-SPI event names was done in phase 4.

### 3.5 `Cached<T>` — the whole performance model

NVDA's `AutoPropertyObject` caches properties, and the cache is emptied at
the end of every pump cycle (`baseObject.py:121-178` + `core.py:1035`). That
is the **only reason** it is fine for speech, braille and vision to each ask
an object for `name`, `role` and `states` in the same cycle: the platform is
hit once.

On AT-SPI this is worth **more** than on Windows: a focus event needs 20–50
properties; without the cache that is 20–50 D-Bus calls (about 1–3 ms), with
the cache one batch.

```cpp
template <class T> class Cached {
    mutable std::optional<T> value_;
    std::function<T()> getter_;
public:
    const T& get() const { if (!value_) value_ = getter_(); return *value_; }
    void invalidate() { value_.reset(); }
    // Every instance registers in a per-object list; Object::invalidateCache()
    // empties it; Loop::tick() calls invalidateAll() at the end of the cycle.
};
```

> **Deviation, measured ([`phase-10.md`](../history/phase-10.md) N129).** Two
> facts are kept for an object's (or application's) whole lifetime instead of
> one cycle, because they cannot change while it exists: the **platform
> role** of an object, and the **application info** (name, toolkit, pid) per
> application bus name. Re-reading them every cycle was most of the ~300
> calls one arrow key cost in Nautilus. Everything else stays per cycle.

**An extra lever specific to AT-SPI:** `org.a11y.atspi.Registry.RegisterEvent(s
event, as properties, s app_bus_name)`. The `properties` argument asks for
the named properties to be **included in the signal payload**, so a focus
event can arrive with its properties and no round trip. Check:
```bash
gdbus introspect --address "unix:path=/run/user/$(id -u)/at-spi/bus" \
  --dest org.a11y.atspi.Registry --object-path /org/a11y/atspi/registry
```

### 3.6 Extension points

Five kinds (`extensionPoints/__init__.py`), all through a registry of
**weak references**:

| Type | Semantics |
|---|---|
| `Action::notify(args…)` | call all; on an exception log it and go on |
| `Filter<T>::apply(value, args…)` | a chain: the output of one is the input of the next |
| `Decider::decide(args…)` | stop at the first `false` |
| `AccumulatingDecider::decide(args…)` | run **all**, return `!default` if any said so |
| `Chain<T>::iter(args…)` | a generator joining all iterables |

Two lessons from NVDA that must be carried over:
1. **The caller has to keep the handler alive.** Weak references mean that
   lambdas and local closures vanish without a trace. NVDA consistently
   registers methods of long-lived singletons.
2. **Iteration goes over a snapshot** (`list(self._handlers.values())`),
   because a handler may (de)register handlers while it is being called.

### 3.7 Watchdog

When an application blocks on an accessibility call, ALUS must not block
with it.

- `alive()` on **every** pump cycle, **every** queue item and **every**
  coroutine step; `asleep()` at the end of the pump (the core may be idle
  indefinitely).
- While the core is stuck, **keys pass through**: the `isAttemptingRecovery`
  check is the first thing in `GestureManager::execute`
  (`inputCore.py:548-552`). On Wayland that means the watchdog thread
  releases every key grab (`emergencyRelease()`), and the main loop installs
  them again once it runs (finding N128).
- On Linux this is **easier** than in NVDA: `CoCancelCall` has no
  equivalent, but D-Bus calls have native timeouts. The structure stays:
  heartbeat + supervisor thread + "run this call on a worker with a timeout
  and abandon it".

### 3.8 Logging

One NVDA detail is **functionally** important, not cosmetic:
`log.exception` **downgrades expected exceptions to debug level**
(`logHandler.py:333-383`): an invalid handle, a timeout, a dead object. The
reason: errors play an error sound. A screen reader that chirps every time an
application times out an accessibility call is unusable.

On Linux the expected list is: `org.freedesktop.DBus.Error.NoReply`,
`.ServiceUnknown`, `.UnknownObject`, `.Timeout`, and AT-SPI `defunct`
objects.

### 3.9 Configuration

`GKeyFile` (ini), the same format as NVDA's configobj, so `gestures.ini`
files are **directly compatible**. Paths follow XDG:

```
~/.config/alus/alus.ini          the main configuration
~/.config/alus/gestures.ini      the user's key remaps (same format as NVDA)
~/.config/alus/profiles/*.ini    configuration profiles
~/.config/alus/symbols-<lang>.dic
<datadir>/alus/locale/<lang>/{gestures.ini,symbols.dic,characterDescriptions.dic}
```

Profiles and triggers (an application profile, a say-all profile) are
carried over from `config/`. They are needed because `SpeechManager` has a
latch for switching profiles in the middle of speech (§4.1).

**Every subsystem must be restartable in-process**, like NVDA's
`resetConfiguration` (`core.py:315-419`). That is a design requirement, not
an afterthought.

The configuration file reference for users is
[`../configuration.md`](../configuration.md).

---

## 4. Speech

### 4.1 `SpeechManager` — NVDA's hardest-won logic

Ported as a whole (`speech/manager.py`, 773 lines). The key mechanisms:

**Indexes and modular comparison.** Indexes cycle through 1..9999. A cancel
can race an index notification, so "is index A before index B" uses a
**half-space window**, the same trick as TCP sequence numbers:

```cpp
constexpr int kMaxIndex = 9999;
constexpr int kWrapMagnitude = kMaxIndex / 2;   // 4999
bool isIndexABeforeB(int a, int b) {
    return a != b && ((a < b && kWrapMagnitude >= b - a) ||
                      (b < a && kWrapMagnitude <  a - b));
}
```

**Rewriting the sequence** (`_processSpeechSequence`): the sequence is
**split at every callback** (callback → assign an index, emit an
`IndexCommand`, remember `index → callback`), and `_ensureEndUtterance`
guarantees an `IndexCommand` at the end of **every** utterance. That final
index is how the end of an utterance is detected.

**Priorities.** `NOW` interrupts lower priorities but **not** a `NOW` that was
queued earlier. The interrupted queue **resumes** afterwards. Parameters
(`rate`/`pitch`/`volume`/`lang`) are remembered in a `ParamChangeTracker` and
**emitted again** when the interrupted queue resumes; otherwise the voice
gets "stuck" at the wrong rate.

**Cancelling stale announcements.** A `_CancellableSpeechCommand` is attached
to speech caused by a focus change. Before it is spoken, ALUS checks whether
it is still relevant (is it still the focus / an ancestor of the focus / the
foreground). If not, it is **dropped from the queue before it is spoken**.
This is directly audible: without it, moving quickly through a list speaks
five old names before the current one.

**The profile latch.** Every configuration profile change requires the
synthesiser to **finish first** (`_shouldPushWhenDoneSpeaking`); otherwise an
utterance would start in voice A and end in voice B.

**The thread boundary.** Index/done notifications arrive on the SSIP reader
thread and are **immediately** marshalled onto the main thread
(`g_idle_add`). All `SpeechManager` state lives only on the main thread. In
addition: `if (synth != currentSynth()) return;`, so a notification from a
driver that has already been replaced is discarded.

### 4.2 The presentation layer

`speech/speech.py` (3198 lines) is **platform neutral** and is ported by its
logic, not literally. The normalisation flow of `speak()` has a **mandatory
order of operations** (`speech.py:1126-1233`):

```
filter_speechSequence → type validation → priority → empty? → speech viewer
→ pre_speech → speech mode (off/beeps/talk/onDemand) → if paused: cancel
→ language pass (insert LangChangeCommand where the language changes)
→ text pass (dictionaries → symbols → whitespace → normalisation)
→ CHUNK_SEPARATOR ("  ") between parts, except in character mode
→ SpeechManager::speak()
```

`CHUNK_SEPARATOR` is **two spaces**. It exists so adjacent numbers do not
merge into one in French and German. A small detail, an audible bug if left
out.

### 4.3 Speech commands → SSML

| ALUS command (from NVDA) | SSIP / SSML |
|---|---|
| `IndexCommand(n)` | `<mark name="n"/>` → notification `700 INDEX MARK` |
| `BreakCommand(ms)` | `<break time="Nms"/>` |
| `PitchCommand`, `RateCommand`, `VolumeCommand` | `<prosody pitch= rate= volume=>` (uses `.multiplier` as a percentage) |
| `LangChangeCommand(lang)` | `<voice xml:lang="…">` |
| `CharacterModeCommand(bool)` | `SET SELF SPELLING on/off` or `char()` |
| `PhonemeCommand(ipa, text)` | `<phoneme ph="…">text</phoneme>` |
| `EndUtteranceCommand` | internal (splits utterances), never reaches the synthesiser |
| `BeepCommand`, `WaveFileCommand` | **not** through SSIP → `alus::audio` |
| `CallbackCommand` | internal: runs **on the main thread** when its index arrives; must return quickly |
| `_CancellableSpeechCommand` | internal, never reaches the synthesiser |
| `ConfigProfileTriggerCommand` | internal, with the latch from §4.1 |

NVDA's prosody commands have *offset XOR multiplier* semantics relative to
the **value the user configured** (`commands.py:249-344`):
`PitchCommand(offset=30)` means "+30 on top of what the user set". The pitch
change on capital letters is exactly that.

### 4.4 `SentenceBuffer` — why say all sounds continuous

`speechWithoutPauses` holds text **up to a sentence boundary** (a regex on
`[.!?]` with an optional closing quote) and only then hands it to the
synthesiser. Without it the synthesiser inserts an utterance pause at
**every line**, and say all sounds choppy.

A detail that is easy to miss: **the last `LangChangeCommand` before the
split point is copied into the held fragment**; otherwise the held part is
spoken in the wrong language.

### 4.5 `ISynth` — two backends

```cpp
class ISynth {
public:
    virtual void speak(const SpeechSequence&) = 0;
    virtual void cancel() = 0;
    virtual void pause(bool) = 0;
    virtual std::set<CommandType> supportedCommands() const = 0;   // IndexCommand MANDATORY
    virtual std::vector<VoiceInfo> availableVoices() const = 0;
    // rate/pitch/volume/inflection: 0–100 at the ALUS level, mapped onto the backend
    Action<ISynth*, int> indexReached;    // → marshalled onto the main thread
    Action<ISynth*>      doneSpeaking;    // → marshalled onto the main thread
};
```

The real interface, with the contract changes measurement forced, is
`include/alus/speech/synth.hpp`.

**`SsipSynth` (the default).** Initialisation:
```
SET SELF CLIENT_NAME "alus:core:main"
SET SELF SSML_MODE on
SET SELF NOTIFICATION index_marks on
SET SELF NOTIFICATION end on
SET SELF NOTIFICATION cancel on
SET SELF NOTIFICATION begin on
```
`index_marks` → `indexReached`, `end` → `doneSpeaking`. **One utterance in
flight**, one SSIP priority; ALUS holds the queue, not speech-dispatcher (see
[`risks.md`](risks.md) R4).

> **Corrections phase 0 measured** (`spikes/RESULTS.md`, implemented in phase 2):
>
> 1. **`begin` and `end` are not audio events.** `begin` arrives ~7.5 ms
>    *before* the first sample (N10), `end` ~294 ms *after* the last (N12).
>    Chaining goes exclusively through `IndexCommand`. `end` is used only as
>    the profile latch and as the **watchdog for a lost mark** (N22 in
>    [`phase-2.md`](../history/phase-2.md)).
> 2. **A mark is never placed right after final punctuation** (N15/N16, risk
>    R14): espeak-ng silently loses it. `SsipSynth` then **splits the
>    utterance into separate SSIP messages** so every mark is at the end of
>    its message, the only shape proven to survive. The messages are sent
>    back to back, so the server chains them and ALUS does not wait for a
>    round trip.
> 3. **`SpeechManager` has deadlines.** A missing mark must not be a dead
>    loop: after `end` comes a short grace period, then a `DEBUG_WARNING` and
>    the queue goes on.
> 4. **The mark offset is constant, not noise** (−40 ms, N13), and depends on
>    the audio device: `ISynth::indexMarkOffset()`. Whoever synchronises
>    something with the audio (the speech cursor, word tracking) has to
>    compensate for it.
> 5. **Timing budgets are a property of the MODULE, not of SSIP** (N17).
>    `rhvoice` misses every one of them; `ISynth::timingIsGuaranteed()`
>    reports that, and ALUS says in the log when it runs with an unmeasured
>    module.

Checking that a module supports indexing:
```bash
grep -i indexing /etc/speech-dispatcher/modules/espeak-ng.conf    # EspeakIndexing 1
spd-say -L                                                        # available voices
```

**`EspeakDirectSynth` (planned for v1.1).** `libespeak-ng` directly in
`AUDIO_OUTPUT_SYNCHRONOUS` mode plus ALUS's own `WavePlayer`. ALUS then
**owns the audio stream**, so cancelling and caret synchronisation in say all
are sample accurate, as in NVDA. It exists so the tension in
[`risks.md`](risks.md) R4 has a way out, and so the `ISynth` abstraction is
proven by two real implementations rather than one.

### 4.6 Audio

`WavePlayer` must have **`feed(data, size, onDone)` with a per-block
callback**: that is the mechanism NVDA uses to synchronise indexes with the
audio. Plus `sync`, `idle`, `stop`, `pause`, and `setVolume` per channel.

v1.0: `libpulse-simple` (also works on PipeWire through `pipewire-pulse`).
Later: native PipeWire for lower latency. Audio ducking: per-stream cork or
volume through PipeWire/Pulse, keeping NVDA's reference count plus a 150 ms
delayed release.

---

## 5. Input and commands

### 5.1 The gesture system — carried over 1:1

This is what users know. No improvisation.

**Identifier:** `source:keys`, e.g. `kb:alus+shift+t`, `kb(laptop):alus+a`,
`br(alva.bc680):t1+t2`, `bk:dot7+dot8`.

**Normalisation** (`inputCore.normalizeGestureIdentifier`): lower case, then
**sort the `+`-separated parts alphabetically**. Without it `alus+shift+t`
and `shift+alus+t` are not the same gesture. `+` never appears as a key name;
a literal `+` is called `plus`.

**Identifier priority:** a gesture returns an **ordered** list; the first
match wins. For the keyboard: `("kb(<layout>):<keys>", "kb:<keys>")`, so
**layout-specific always beats layout-agnostic**.

**Three map layers**, in priority order:
```
1. ~/.config/alus/gestures.ini                 (user)
2. <datadir>/alus/locale/<lang>/gestures.ini   (locale)
3. the current braille driver's gestureMap
```
…and only then the built-in class bindings. The format is identical to
NVDA's, including `None = <gesture>` to **unbind** and `kb:<keyname>` as a
script name for key emulation.

The Serbian locale already exists in NVDA (`source/locale/sr/gestures.ini`)
and remaps `zoomIn`/`zoomOut` and the laptop mouse/flow commands onto `š`/`đ`;
it is directly usable.

**Script dispatch, 10 steps** (`scriptHandler._yieldObjectsForFindScript`):
```
 1. gesture.scriptableObject        (e.g. the braille driver for br: gestures)
 2. every global plugin
 3. focus.appModule
 4. the braille display (if Scriptable)
 5. active vision providers
 6. focus.treeInterceptor           [filter: passThrough off OR ignoreTreeInterceptorPassThrough]
 7. the focus object
 8. ancestors of the focus, nearest first   [filter: only if script.canPropagate]
 9. configProfileActivationCommands
10. globalCommands                  (the last resort)
```
Step 6 with its filter is **exactly** what makes single-letter quick
navigation work in browse mode and fall through in focus mode.

Within each object: **global maps beat built-in class bindings**.

### 5.2 The keyboard

> **Deviation: implemented in phase 3 _without_ `AtspiDevice`.** See
> [`phase-3.md`](../history/phase-3.md) O1, N26, N27.
>
> `atspi/atspi-device-a11y-manager.c` calls the compositor with
> `g_dbus_proxy_call_sync(..., timeout = -1, ...)` and **discards the
> `GError`**, and it issues `SetKeyGrabs` from a `g_timeout_add` callback,
> that is, from the client's main loop. That is **the cause of N19** (the
> loop blocked without a deadline while the grabs are in place) and **the
> cause of N7** (`AccessDenied` is never seen). Both are in the library.
>
> So ALUS talks to `org.freedesktop.a11y.KeyboardMonitor` directly:
> `A11yManagerBackend`, every call asynchronous with a finite deadline,
> every reply read, `emergencyRelease()` callable from the watchdog thread.
> X11 and the legacy paths remain **another `IInputBackend`**; H6 is met by
> choosing the backend at run time, not by conditional compilation.
>
> Three facts about the interface that the documentation does not show
> (N27): the first argument of `SetKeyGrabs` is **keysyms**, not modifier
> indexes; a virtual modifier (15–31) is a **client-side fiction**; and the
> `state` in a `KeyEvent` **does not contain** that bit; the client tracks
> it itself.
>
> And a consequence the plan did not have (O6): the compositor asks for
> consumption **in advance**, so the grab set is **context dependent**: ALUS
> grabs exactly the gestures that would find a script *right now*. Otherwise
> `h`, `k` and the digits (browse mode) would be grabbed globally and typing
> would stop working everywhere.

The original plan, kept for the reasoning: **backend `AtspiDevice`**, since
`libatspi` 2.56 already abstracts three platforms:

| Backend | When | Mechanism |
|---|---|---|
| `AtspiDeviceA11yManager` | GNOME ≥48, KDE ≥6.3.6, Wayland | `org.freedesktop.a11y.KeyboardMonitor` |
| `AtspiDeviceX11` | an X11 session | XInput2 / XRecord |
| `AtspiDeviceLegacy` | old AT-SPI | `DeviceEventController` |

**Mapping a keysym to a virtual modifier is the solution for the ALUS key.**
Insert / numpad Insert / Caps Lock are mapped to a virtual modifier (index
15–31, **and what comes back is the MASK `1 << index`**, finding N9), so a
grab `{keysym=XK_t, modifiers=ALUS_VMOD|SHIFT}` gives exactly NVDA's
semantics, and the compositor enforces it. It is not implemented by hand.

> **Phase 3, N28:** every ALUS key gets **its own** virtual modifier
> (measured: `0x8000` for `Insert`, `0x10000` for numpad `Insert`). The
> gesture `alus+t` is therefore grabbed **once per ALUS key**; one grab with
> the combined mask means "hold both keys" and never matches.

Checking the interface and its access control:
```bash
gdbus introspect --session --dest org.freedesktop.a11y.Manager \
  --object-path /org/freedesktop/a11y/Manager
busctl --user list | grep -i keyboardmonitor      # who holds the name now
alus-cli input-info                               # ALUS's own diagnosis
```

**Key names**: `keysymdef.h` → NVDA names. The numpad with Num Lock **off**
produces named `KP_*` keysyms, so numpad versus navigation keys is clean on
Linux (on Windows NVDA resolves it with a `(vkCode, extended)` pair; ALUS
does not need that):

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

**Mandatory behaviours from NVDA:**

| Behaviour | Why |
|---|---|
| **`trappedKeys`**: if a key-down is eaten, its key-up must be eaten too | otherwise applications see an unbalanced key state |
| The ALUS key is **never** passed to the system, except by a double press | a double Insert within `multiPressTimeout` → a real Insert |
| `multiPressTimeout` = 500 ms | both for a script's double press and for passing the ALUS key through |
| **Repetition is per script, not per key** | `lastScriptRepeatCount()` → 0/1/2/3; reset by an unbound gesture |
| Input help must not eat its own toggle | `toggleInputHelp` **runs** even in input help |
| Typed character / word echo | `speakTypedCharacters` with a word buffer; terminals queue it and flush it on a text change |

### 5.3 Scripts

```cpp
// The equivalent of NVDA's @script decorator.
ALUS_SCRIPT(reportCurrentFocus,
    .description  = N_("Reports the object with focus. If pressed twice, spells "
                       "the information. Pressing three times spells it using "
                       "character descriptions."),
    .category     = ScriptCategory::Focus,
    .gestures     = { "kb:alus+tab" },
    .speakOnDemand = true);
```

Attributes carried over from NVDA: `description` (what input help speaks
**and** what the gestures dialog shows), `category`, `gestures`,
`canPropagate`, `bypassInputHelp`, `allowInSleepMode`, `resumeSayAllMode`,
`speakOnDemand`.

`isScriptWaiting()` is checked throughout the speech code so long speech is
cut short when a new key is queued. **Without it, "press a key to interrupt"
responsiveness is wrong.**

### 5.4 Extracting the command table — automatic, not by hand

`globalCommands.py` is 5714 lines with about 300 scripts in 21 categories;
`browseMode.addQuickNav` registers quick navigation **programmatically**.
Transcribing it by hand would introduce errors.

```
tools/extract-commands.py
  → imports the NVDA modules (globalCommands, browseMode, cursorManager,
     documentBase, editableText, virtualBuffers, braille)
  → reads the __gestures dicts and the @script arguments
  → writes:
       data/gestures-builtin.ini   the built-in bindings for ALUS
       data/scripts.csv            name, description, category, desktop gesture,
                                   laptop gesture, speakOnDemand, resumeSayAllMode
```

The result is **the source of truth** for ALUS and can be checked against the
NVDA user guide (`user_docs/en/userGuide.md` + `source/keyCommandsDoc.py`,
which generates the command reference from `<!-- KC:… -->` comments).

A note on layouts: a gesture written as `kb:X` is active in **both** layouts.
Only 12 global scripts have a truly desktop-only binding. Numpad commands
must keep working in the laptop layout.

> **Measured in phase 9, and it adds a fourth file that is NOT generated.**
> The extractor is the source of truth for **what a command is** and **which
> key leads to it**, but it cannot know whether ALUS has **implemented** the
> command. While that was written down nowhere, 52 shortcuts from the
> generated map were **silent**: the key was pressed and nothing happened,
> and the reference promised them because it was generated from the same
> table ([`phase-9.md`](../history/phase-9.md) N99). No test could see it,
> because the reference and the test read **the same** generated source.
>
> Hence `data/commands-status.ini`, maintained **by hand**: `[announce]` are
> commands ALUS does not have, and the key **says so**; `[platform]` are
> those deliberately **not grabbed**, because the key belongs to the
> application and ALUS cannot send it back (N18). The same file drives
> behaviour (`script::bindAnnouncements` at startup), the contract test T9.6
> checks against the **assembled daemon stack**, and the markers in the
> generated reference. The rule this teaches, confirmed four times in phases
> 6 and 9: **a check must not share its source with what it checks.**
>
> And a third generated input, for the same reason: `data/gestures-alus.ini`
> carries the shortcuts ALUS chooses **differently** from upstream. The
> extractor applies them before writing, so there is still one built-in
> map, one grab set and one reference. A new map **layer** could not do
> that: layers stack, they do not replace, so the old key would stay bound
> ([`phase-9.md`](../history/phase-9.md) N105).
>
> Two consequences of moving implementations from NVDA's `GlobalCommands`
> into the subsystem that owns them: the class name in the map section has
> to move **with** them, or step 6 of the search rejects the entry and the
> key does nothing (N101, `alus+space`); and the object holding the scripts
> needs a **catalogue**, or 180 browse mode commands are left without a
> description and the gestures dialog hides them (N102, T9.3).

---

## 6. The object model

### 6.1 One API layer, not four

> **Implemented in phase 4**, see [`phase-4.md`](../history/phase-4.md).
> Three things turned out differently from the assumption:
> `atspi_set_main_context` has to come **after** `atspi_init`, or no event
> arrives at all (N36); `properties` in `RegisterEvent` cannot carry `Role`
> or `State`, because those are methods and not properties, and are only
> valid for the duration of the callback (N38); and a platform event has to
> request a pump cycle itself (N43). The layer sits behind the interface
> `alus::a11y::Backend`, so the whole object model is tested without a bus
> (`tests/a11y/mock_backend.hpp`).

NVDA has `IAccessibleHandler` + `UIAHandler` + `JABHandler` +
`displayModel`. ALUS has **one**: `alus::a11y` over `libatspi`.

NVDA's pattern for finding the best API class (`findBestAPIClass` /
`getPossibleAPIClasses` / `kwargsFromSuper`) is kept, not because ALUS has
several APIs, but because the pattern solves "make the most specific object
from this reference", which is still needed.

Event subscription: `Registry.RegisterEvent(event, properties,
app_bus_name)`. The minimal set a real screen reader uses (verified live
against Orca's subscriptions, 38 types):
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

Check:
```bash
A="unix:path=/run/user/$(id -u)/at-spi/bus"
gdbus call --address "$A" --dest org.a11y.atspi.Registry \
  --object-path /org/a11y/atspi/registry \
  --method org.a11y.atspi.Registry.GetRegisteredEvents
```

### 6.2 `alus::a11y::Object`

> **Implemented in phase 4.** Behaviour composition works with NVDA's
> contract in full (three selection stages, `remove`/`prepend`). Two
> differences AT-SPI imposes: `positionInfo` has no `groupPosition`, so the
> fallback computation applies only to roles where a group position means
> something (N39, O6); and objects live for the whole pump cycle because the
> cache holds `shared_ptr`s to other objects (N40, O8).

The property set is carried over from `NVDAObjects/__init__.py` almost in
full, with AT-SPI sources:

| ALUS property | AT-SPI source |
|---|---|
| `name` | `Accessible::get_name`, plus the `LABELLED_BY` relation |
| `role` | `Accessible::get_role` → mapping (§6.5) |
| `states` | `Accessible::get_state_set` → mapping |
| `value` | `Value::get_current_value` / `Text` / `Accessible` |
| `description` | `Accessible::get_description`, plus `DESCRIBED_BY` |
| `keyboardShortcut` | `Action::get_key_binding` |
| `parent` / `children` / `firstChild` / `next` / `previous` | `Accessible::get_parent`, `get_child_at_index`, `get_index_in_parent` |
| `location` | `Component::get_extents(ATSPI_COORD_TYPE_SCREEN)` |
| `isFocusable` / `hasFocus` | the `FOCUSABLE` / `FOCUSED` state |
| `level` | `get_attributes()["level"]` (headings, trees) |
| `positionInfo` | `get_index_in_parent` + the parent's `get_child_count` |
| `landmark` | `get_attributes()["xml-roles"]` |
| `flowTo` / `flowFrom` | the `FLOWS_TO` / `FLOWS_FROM` relation |
| `annotations` / `details` | the `DETAILS` / `DETAILS_FOR` / `ERROR_MESSAGE` relation |
| `cellCoordsText`, `rowNumber`, `columnNumber`, `rowSpan`, `columnSpan` | `TableCell::*` |
| `tableID`, `rowCount`, `columnCount` | `Table::*` |
| `processID` | `Application` → `/proc/<pid>` |
| `appModule` | `alus::app::Registry` (§12) |
| `actionCount` / `doAction` | `Action::get_n_actions`, `do_action` |
| `setFocus` | `Component::grab_focus` |
| `scrollIntoView` | `Component::scroll_to` |
| `treeInterceptor` | `alus::nav` (§8.5) |

Each one is a `Cached<T>` (§3.5).

**Overlay classes.** NVDA's `DynamicNVDAObjectType` synthesises a class per
object by combining the overlay classes proposed by the API layer, the app
module and the global plugins (`chooseNVDAObjectOverlayClasses(obj,
clsList)`; `clsList.remove(...)` to drop a class, `clsList[0:0] = (...)` to
put one at the top). C++ cannot build types at run time, so ALUS uses
**composition of behaviour objects**:

```cpp
class Object {
    std::vector<std::unique_ptr<Behavior>> behaviors_;   // ordered, the first wins
    // Property/event/script dispatch goes through behaviors_, then falls back to the base.
};
// OverlayRegistry::choose(obj, std::vector<BehaviorFactory>& list)
//   — the API layer, then the app module, then global plugins; the same contract as NVDA.
```

The key for choosing overlay classes on Linux is not `windowClassName` (a
Windows concept), but:
```
(Application::get_toolkit_name, AtspiRole)  primary
Accessible::get_attributes()                secondary (xml-roles, tag, id)
Accessible::get_interfaces()                tertiary (does it have Table? Text? Document?)
```
For example, for LibreOffice NVDA checks `windowClassName in
("SALFRAME","SALTMPSUBFRAME")`; ALUS checks `toolkit_name == "libreoffice"`.
NVDA's `hasattr(obj, "IAccessibleTable2Object")` becomes
`"org.a11y.atspi.Table" in get_interfaces()`.

### 6.3 Behaviours — platform neutral, carried over in full

`NVDAObjects/behaviors.py` is **the most useful file to port** because it
knows nothing about Windows:

| Behaviour | Purpose | AT-SPI note |
|---|---|---|
| `EditableText` | caret movement detection, auto-select | §8.3 |
| `LiveText` | text diffing and announcing changes | `_getText()` works directly on `AtspiText` |
| `Terminal` | `LiveText` + queueing typed characters | VTE/Konsole expose a real `Text` interface |
| `KeyboardHandlerBasedTypedCharSupport` | character echo in a console | replaced by events from the input layer |
| `ProgressBar` | beeps/speech on progress | `Value::get_current_value` |
| `Dialog` | composing a dialog's text from its children | — |
| `RowWithFakeNavigation` | cell navigation where there is no table | — |
| `CandidateItem` | IME candidates | — |
| `FocusableUnfocusableContainer`, `ToolTip`, `Notification` | — | — |

**An important difference:** for terminals NVDA uses `DisplayModelLiveText`
(reads from hooked GDI calls). On Linux that is **unnecessary**: VTE
(gnome-terminal, xfce4-terminal) and Konsole expose a real `AtspiText` with
`TextChanged` events. So the `Terminal` behaviour works directly, without a
display model.

### 6.4 Global state (`api.py`)

Carried over literally: the focus object + **the focus ancestor chain**,
`getFocusDifferenceLevel()` (how much of the chain changed, the basis for
`focusEntered` events), the navigator object, the review position, the
foreground object, the mouse object, the desktop object.

A contract that must not be lost: **the setters return `bool`**.
`setFocusObject` may refuse the change (for example, the object has died in
the meantime), and the caller has to check.

### 6.5 Roles and states

> **Measured in phase 4 on AT-SPI 2.56.2:** **131** roles and **44** states,
> not 133 and 45: the numbers in the table below include `*_LAST_DEFINED` and
> the alias `ATSPI_ROLE_PUSH_BUTTON`, which are not values (N42). NVDA's side
> is correct (156 / 53), so the mapping is 131 → 156 and 44 → 53. Also: **an
> empty AT-SPI state set means "I do not report states"**, which is how the
> desktop and every application object report it, so the inversions
> (`ENABLED` ⇒ `UNAVAILABLE` and the others) apply only to a non-empty set
> (N37).

| | AT-SPI 2.56 | NVDA | Consequence |
|---|---|---|---|
| Roles | 133 | 156 | a mapping table plus refinement is needed |
| States | 45 | 53 | the same |
| Relations | 24 | — | mapped onto NVDA's relation properties |

Refinement goes through `Accessible::get_attributes()`, whose keys in Gecko
and Chromium are **the same as the IA2 attributes NVDA already uses**:
`xml-roles` (ARIA role → landmarks, `switch`, `region`), `level` (heading
level for `1`–`9`), `tag` (HTML tag → `blockquote`, `embed`, `figure`,
`fieldset`), `id`.

Check:
```bash
python3 -c "import gi; gi.require_version('Atspi','2.0'); from gi.repository import Atspi
print(len([r for r in dir(Atspi.Role) if r.isupper()]),
      len([s for s in dir(Atspi.StateType) if s.isupper()]))"
```

Localised role and state labels (`controlTypes/role.py`, `state.py`,
including **negative** labels such as "not checked") are carried over as
gettext strings: they are text the user hears and must match the NVDA
translation for each language.

---

## 7. The text layer

### 7.1 `TextRange` — the interface

`textInfos.TextInfo` is carried over in full: the units (`CHARACTER`,
`WORD`, `LINE`, `SENTENCE`, `PARAGRAPH`, `STORY`, `READINGCHUNK`, `OFFSET`),
`copy`, `move`, `expand`, `collapse`, `compareEndPoints`, `setEndPoint`,
`getTextWithFields`, `find`, `updateCaret`, `updateSelection`, `bookmark`,
`boundingRects`. Also `ControlField`, `FormatField`, `FieldCommand` and
`ControlField::getPresentationCategory`.

### 7.2 `OffsetsTextRange` over `AtspiText`

NVDA's `OffsetsTextInfo` maps almost directly:

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
| `_getWordOffsets` | `…GRANULARITY_WORD`, or ICU segmentation |
| `_getSentenceOffsets` | `…GRANULARITY_SENTENCE`, or ICU |
| `_getParagraphOffsets` | `…GRANULARITY_PARAGRAPH` |
| `_getFormatFieldAndOffsets` | `Text::get_attribute_run(offset)` + a normaliser |
| `_getOffsetFromPoint` | `Text::get_offset_at_point` |
| `_getBoundingRectFromOffset` | `Text::get_character_extents` / `get_range_extents` |
| `_getNVDAObjectFromOffset` | `Hypertext::get_link_index` → `get_link` → `Hyperlink::get_object` |
| `_getOffsetsFromNVDAObject` | `Hyperlink::get_start_index` / `get_end_index` |

`STORY`, `READINGCHUNK` and `OFFSET` are NVDA-specific units that ALUS
synthesises itself.

### 7.3 Offset encoding — **the most likely source of silent bugs**

> **Deviation, measured ([`phase-10.md`](../history/phase-10.md) N113).** The
> original plan assumed AT-SPI2 offsets are UTF-8 bytes, and the whole text
> layer stood on that: **AT-SPI2 offsets are codepoints, not UTF-8 bytes.**
> Measured on Chrome / AT-SPI 2.56.2 with an independent library (Atspi
> through GObject introspection): "Prvi pasus je proza. Ima dve rečenice."
> reports 38 characters, has 39 bytes, and `get_text(31, 32)` is "č". With
> bytes assumed, the right arrow on "Treći" read "ći" and skipped "i", and
> Cyrillic was wrong in every letter. `OffsetsTextRange` now has
> `offsetUnit()` (NVDA's `encoding`): `ObjectTextRange` counts codepoints,
> screen review over its own string stays in bytes, and conversion happens
> only where a local UTF-8 string is cut or segmented. The mock backend
> counted bytes, so it agreed with the bug; it now counts codepoints as the
> platform does.

Three systems are involved:
- **AT-SPI2**: offsets are **codepoints** (see the deviation above),
- **NVDA**: assumes **UTF-16 code units** (`WCHAR_ENCODING`),
- **the internal representation**: codepoints / graphemes.

NVDA solves this with an `OffsetConverter` layer (`textUtils/__init__.py`).
ALUS's `ObjectTextRange` answers `offsetUnit() == Codepoints`, because that
is what AT-SPI really counts, and the rest of the stack works in logical
indexes.

**Get this wrong and every emoji, CJK string and combining diacritic is
silently broken**, visible only in bug reports.

Related: `unicodeNormalize` matters in the "is this one character" check of
`getTextInfoSpeech`: a decomposed "é" is two codepoints but one grapheme, and
the spelling shortcut has to work then too.

Word and sentence segmentation: NVDA on Windows uses Uniscribe or ICU. ALUS
uses **ICU only** (the system `libicu`), which is better, not worse;
`textUtils/uniscribe.py` is dropped. ICU is an optional build dependency with
a loud fallback ([`phase-5.md`](../history/phase-5.md) O2): without it,
Serbian and English are still segmented correctly, Japanese and Thai are not,
and `alus-cli text-info` says which build is running.

---

## 8. Navigation

### 8.1 Review modes

`review.py` is 190 lines and is carried over in full. Three modes:

| Mode | What it walks | Implementation |
|---|---|---|
| `object` | the text inside the navigator object | `Object::makeTextRange` |
| `document` | the whole document containing the object | the tree interceptor |
| `screen` | the spatial content of the screen | **flat object review** (§10) |

`reviewMode_next` / `reviewMode_previous` (`alus+numpad7` / `alus+numpad1`)
cycle and reposition the review cursor at the navigator object's point.

### 8.2 `CursorManager` — the virtual caret

For cases where ALUS **owns** the cursor (browse mode, OCR results): all
movement commands by line/word/character/paragraph/sentence, selection with
`shift`, `control+a`, `control+c`, search (`alus+control+f`, `alus+f3`,
`alus+shift+f3`). The selection anchor logic is carried over literally; it is
easy to get wrong and end up with a selection that "jumps".

### 8.3 `EditableText` — detecting caret movement

`editableText.py` has **three strategies with a time budget** for
`_hasCaretMoved`, because Windows applications lie about caret events. **GTK,
Qt, Electron, Java and LibreOffice lie in *different* ways.** So:

- The strategies are **carried over**, not simplified.
- `caretMovementDetectionUsesEvents` stays an override per class/behaviour.
- `_useEvents_maxTimeoutSec` and `_caretMovementTimeoutMultiplier` are
  **tuned per toolkit**, and that is expected work, not a sign of a mistake.

### 8.4 Table navigation

`control+alt+arrows` (cell), `control+alt+pageUp/Down/home/end` (row/column),
`alus+control+alt+arrows` (read a row/column). AT-SPI gives everything
directly: `Table::get_accessible_at(row,col)`, `get_row_at_index`,
`get_column_at_index`, `TableCell::get_row_column_span`,
`get_row_header_cells`, `get_column_header_cells`.

`tableUtils.HeaderCellTracker` is carried over for table formats where
headers are *declared* rather than discovered per cell (spreadsheet app
modules). One NVDA rule to keep: **when you stand _on_ a header of an axis,
that axis's headers are not announced**.

### 8.5 `TreeInterceptor`

Carried over: the lifecycle, `isReady`, the `passThrough` policy,
`rootNVDAObject`, `RootProxyTextInfo`, creation through
`Object::treeInterceptorClass`.

---

## 9. Browse mode

### 9.1 Why virtual buffers are **not** ported

NVDA injects C++ (`nvdaHelper`) into the browser process, walks the tree
in-process and serialises the result into a buffer that NVDA indexes by an
integer offset. The buffer is a **copy** and has to be refreshed explicitly
(`alus+f5`).

None of the reasons hold on Linux:

1. **There is no cross-process COM cost.** AT-SPI is D-Bus, about 50 µs per
   call, with batching.
2. **AT-SPI already has a search service.** `org.a11y.atspi.Collection`
   (`GetMatches`, `GetMatchesFrom`, `GetMatchesTo`) with `MatchRule(states,
   attributes, roles, interfaces)` and the orders
   `CANONICAL`/`FLOW`/`TAB`/`REVERSE_*` is a **direct replacement** for
   `VBuf_findNodeByAttributes`, including `MATCH_ANY`/`ALL`/`NONE` semantics
   that correspond to NVDA's alternatives/required/`None` conventions.

   > **Measured 7–8 Nov 2026; this point was too optimistic in two details**
   > ([`phase-6.md`](../history/phase-6.md) N89, N90):
   >
   > * **No provider implements `FLOW` in `GetMatches`.** Chrome 157 (Blink),
   >   Firefox 153 (Gecko) and LibreOffice Writer (VCL/UNO) return an **empty
   >   array** for `FLOW` and `TAB`, and the correct answer for `CANONICAL`.
   >   The same providers **honour** `FLOW` in `GetMatchesFrom`. So reading
   >   order is available to a key press (§9.2) but **not** to the elements
   >   list, which goes in tree order. WebKitGTK and Qt were not measured.
   > * **`MATCH_INVALID` does not mean "criterion unused".** None of the
   >   three providers implements it that way: a rule with `MATCH_INVALID`
   >   for states matches **nothing**. An unused criterion is written as **an
   >   empty set with `MATCH_ALL`**, which is logically the same condition.
   >   Orca never sends `MATCH_INVALID`, so nobody had tested it.
3. **AT-SPI already has flat text.** Gecko and Chromium expose the document
   body as `AtspiText` with `get_string_at_offset(…, LINE)`, the replacement
   character U+FFFC for embedded objects and `Hypertext`/`Hyperlink` for
   mapping offsets to child objects. **That _is_ the virtual buffer; the
   browser maintains it.**

So: `script_refreshBuffer` (`alus+f5`) makes no sense and either goes away or
becomes "read again"; `prepare()` is a no-op; `isReady` is "the root's BUSY
state has cleared"; a node's identity is the AT-SPI `(bus name, object
path)`, **more stable** than NVDA's `(docHandle, ID)`.

### 9.2 Quick navigation through `Collection`

```
_iterNodesByType(itemType, direction, pos)
  → Collection::GetMatchesFrom(currentObject, matchRule,
                               sortBy = FLOW, tree = INORDER, count, traverse)
```

One D-Bus call per key press. The `MatchRule` table per element type is
built on the model of `virtualBuffers/gecko_ia2._searchableAttribsForNodeType`,
whose pattern is always **role OR ARIA token OR HTML tag**:

> **Measured:** `traverse` must be `TRUE`. With `FALSE` libatspi considers
> only the object's **immediate children**, so a web document, whose root
> has one child, never returns anything ([`phase-6.md`](../history/phase-6.md)
> N88). The same defect is **invisible** in Writer, because its document tree
> is shallow.
>
> **Measured:** in LibreOffice Writer `h` works (headings have the role
> `heading`), but `k` does **not**: Writer exposes links as hypertext inside
> a paragraph, not as objects with the role `link`, so a search by role does
> not find them ([`phase-6.md`](../history/phase-6.md) N95). The key then has
> to **say** that it found none.

| Key | Type | MatchRule (sketch) |
|---|---|---|
| `h` | heading | `role=HEADING` |
| `1`–`9` | heading1–9 | `role=HEADING` + `attributes{level=N}` |
| `k` | link | `role=LINK` |
| `v` / `u` | visited / unvisited link | + the `VISITED` state / its negation |
| `f` | form field | widget roles without `READ_ONLY`; or an editable combo/text |
| `i` | list item | `role=LIST_ITEM` |
| `l` | list | `role=LIST` |
| `t` | table | `role=TABLE` (+ without `table-layout` if layout tables are off) |
| `b` | button | `role=PUSH_BUTTON` |
| `x` | check box | `role=CHECK_BOX` or `xml-roles` contains `switch` |
| `r` | radio button | `role=RADIO_BUTTON` |
| `c` | combo box | `role=COMBO_BOX` |
| `e` | edit | `role=ENTRY` |
| `g` | graphic | `role=IMAGE` |
| `q` | block quote | `role=BLOCK_QUOTE` or `tag=blockquote` |
| `s` | separator | `role=SEPARATOR` |
| `m` | frame | `role=INTERNAL_FRAME` |
| `d` | landmark | `role=LANDMARK` or `xml-roles` ∈ ARIA landmarks |
| `o` | embedded object | `tag ∈ {embed,object,applet,audio,video,figure}` or `role ∈ {APPLICATION,DIALOG}` |
| `a` | annotation | the `DETAILS` relation / `role=COMMENT` |
| `w` | error | `attributes{invalid=spelling}` |
| `p` | text paragraph | `role=PARAGRAPH` with text |
| `n` | not-link block | skip a block of links (derived, not a MatchRule) |

Mind NVDA's detail: container types (`grouping`, `region`) carry an extra
condition `name != ""`, so unnamed groups are not offered to the user.

Check:
```bash
python3 -c "import gi; gi.require_version('Atspi','2.0'); from gi.repository import Atspi
print([m for m in dir(Atspi.Collection) if 'match' in m])
print([s for s in dir(Atspi.CollectionSortOrder) if s.isupper()])"
alus-cli browse-info
```

### 9.3 `getTextWithFields` — **the most important new code in ALUS**

This is the only part of browse mode with no ready replacement. AT-SPI gives
**flat text + attributes per offset**, but not NVDA's **nested
`controlStart`/`controlEnd` command stream** that *all* document reading
rests on.

The good news: the solution already exists in NVDA, in a platform-neutral
form, `compoundDocuments.py`:

```
compoundDocuments._getControlFieldForObject(obj, ignoreEditableText=True)   # :164-211
    → SYNTHESISES a ControlField from the object's role, states, name,
      description, level and table coordinates

compoundDocuments.TreeCompoundTextInfo.getTextWithFields(formatConfig)      # :331-368
    → emits the controlStart / controlEnd / formatChange stream
      by DIFFING the ancestor chains of consecutive leaf TextInfos
```

**That pair is the most important thing to port in all of browse mode.** On
its correctness depend:
- `commonFieldCount` continuity (hence a stable `uniqueID`: the AT-SPI
  `(bus name, object path)`),
- `isBlock` for splitting utterances in say all,
- the `table-*` keys for table navigation,
- every "entering a list / leaving a table" announcement.

Alongside it, the **concept** of `virtualBuffers._normalizeControlField`
(`:393-448`) is carried over: one place that turns the backend's attribute
soup into canonical ALUS keys (`table-id`, `table-rownumber`,
`table-rowheadertext`, `isHidden`, `alwaysReportName`, `uniqueID`).
Self-referencing headers are skipped (Chrome emits them).

`XMLFormatting.py` is **not** ported; it exists only because the C++ buffer
serialises to XML.

### 9.4 The shape of the solution

```
AtspiDocumentTreeInterceptor : BrowseModeDocumentTreeInterceptor
    TextRange = AtspiDocumentTextRange : BrowseModeDocumentTextRange, OffsetsTextRange
        offsetUnit = Codepoints (§7.3)
        _get*Offsets            → §7.2
        getTextWithFields       → §9.3 (ancestor-chain diff)
    _iterNodesByType            → §9.2 (Collection)
    isAlive                     → the root is not DEFUNCT
    isReady                     → the root's BUSY state has cleared
    shouldPrepare               → false (nothing to build)
```

For LibreOffice Writer, NVDA uses no virtual buffer but a `CompoundDocument`
(`soffice.py:610-620` assigns `obj.treeInterceptorClass =
SymphonyDocument`). ALUS does the same. An **editable** Writer document is not
intercepted at all: the caret is the application's, and ALUS follows it.

**Browsers need no app module.** NVDA has no `firefox.py` or `chrome.py`;
support is in the object layer (`NVDAObjects/IAccessible/mozilla.py`,
`chromium.py`). On Linux Firefox and Chromium speak AT-SPI with almost
identical semantics (the AT-SPI bridge is generated from the same internal
tree), so **one overlay** keyed on `toolkit_name ∈ {gecko, Chromium}` covers
both.

---

## 10. Screen review without a display model

NVDA's `displayModel.py` is a record of **every text drawing call**, captured
by hooking GDI/DirectWrite inside the target process, with a bounding
rectangle per character. On Linux that is not feasible: GTK 4 and Qt 6 draw
through Cairo/Skia/QPainter into client buffers, and under Wayland you cannot
even read another client's surface.

**The v1.0 solution: flat object review.** "Screen review" means a *flat
review of objects*:

```
1. take the active top-level window
2. walk all descendants whose presentationType == content
3. sort them spatially by (top, left) from Component::get_extents(ATSPI_COORD_TYPE_SCREEN)
4. join their text into a synthetic OffsetsTextRange
     _getStoryText              = the joined text
     _getBoundingRectFromOffset = a parallel array of rectangles
```

The only place that changes is `review.getScreenPosition`
(`review.py:59-80`). This gives the **user-visible behaviour** of screen
review (spatial, flat, per window) without a single platform hook, and keeps
`screenExplorer`, mouse tracking and (later) touch exploration for free.

`screenExplorer.py` (90 lines) is carried over almost literally; the only
platform-dependent things are `objectFromPoint` →
`Component::get_accessible_at_point` and `makeTextInfo(Point)` →
`Text::get_offset_at_point`.

OCR (v1.1) is the **last** fallback, after AT-SPI and after flat review: the
`contentRecog` framework carries over (it is clean: a recogniser takes a
pixel buffer + `RecogImageInfo` and returns a `LinesWordsResult`), the
backend is Tesseract, and screen capture goes through the `xdg-desktop-portal`
Screenshot/ScreenCast interfaces.

---

## 11. Symbols, dictionaries, formatting

### 11.1 Text processing

`characterProcessing.py` (953 lines) and `speechDictHandler/` are **platform
neutral** and carried over almost unchanged.

`SymbolLevel`: `NONE=0`, `SOME=100`, `MOST=200`, `ALL=300`, `CHAR=1000`,
`UNCHANGED=-1`. The gaps are deliberate (room for future levels). `alus+p`
cycles through them.

The registry of symbol dictionaries, in order of application:
```
1. cldr      locale/<lang>/cldr.dic      Unicode CLDR → emoji names; optional
2. builtin   locale/<lang>/symbols.dic   MANDATORY, hidden from the GUI;
                                         the ONLY one allowed complex (regex) symbols
3. add-on    <addon>/locale/<lang>/symbols-<name>.dic
4. user      ~/.config/alus/symbols-<lang>.dic   mandatory, no regex
```
"Only `builtin` may use regex" is a deliberate safety property; keep it.

Pronunciation dictionaries (`speechDictHandler`) apply in the order
**builtin → default → voice → temp**, before symbol processing. That order is
user-visible.

One NVDA trap to carry over: the four entry types with a word boundary
(`WORD`, `PART_OF_WORD`, `START_OF_WORD`, `END_OF_WORD`) **must** use a regex
engine whose `\w` includes combining marks, because Python's standard `re`
**excludes combining characters from `\w`**. Without it dictionary entries
break in every language with diacritics.

> **Implemented in phase 2 with `GRegex` (PCRE2), not ICU**, see
> [`phase-2.md`](../history/phase-2.md) O2. The requirement is a `\w` that
> does not exclude combining characters, and it is met with the explicit
> class `[\p{L}\p{M}\p{N}_]` (`alus::text::kWordCharacterClass`). The reason
> for PCRE2: **complex symbol patterns are data from NVDA's locale files,
> written in Python `re` syntax** (`(?P<name>…)` and `(?P=name)`), which
> PCRE2 accepts literally and ICU does not support at all. ICU still came in
> with phase 5, for word and sentence segmentation, which PCRE2 cannot do.

### 11.2 Paragraph navigation

`documentNavigation/`: three styles from the configuration:
- `APPLICATION`: trust the application's paragraph unit,
- `SINGLE_LINE_BREAK`: every line break is a new paragraph,
- `MULTI_LINE_BREAK`: only an empty line starts a new paragraph (the
  Markdown interpretation).

The return convention `(passKey, moved)` allows falling back to the
application's own behaviour; keep it.

---

## 12. App modules and extensibility

### 12.1 The app module registry

NVDA keys app modules by **executable name** (`runningTable: dict[pid,
AppModule]`, guarded by an RLock). On Linux the key is richer and better:

```
primary:       basename(readlink("/proc/<pid>/exe"))
secondary:     the Application accessible's name
discriminator: Application::get_toolkit_name() + get_toolkit_version()
```

The alias table (NVDA's `EXECUTABLE_NAMES_TO_APP_MODS`) is carried over as a
concept: `code-insiders` → `code`, and so on.

The lifecycle is carried over literally, including the `handleAppSwitch`
choreography: `event_appModule_loseFocus` on the outgoing module, then **all
profile trigger changes in one atomic transaction** (`atomicProfileSwitch`),
then `event_appModule_gainFocus`. Without atomicity, switching applications
rebuilds the profile N times.

v1.0 ships these app modules (`src/app/modules/`): LibreOffice, GNOME Shell,
Nautilus, terminals (one module for several terminals) and Electron.
`alus-cli app-info` shows which module every running application gets.

### 12.2 Five customisation mechanisms

All carried over, with the same names:

1. `chooseNVDAObjectOverlayClasses(obj, clsList)`: add/remove a behaviour;
   `clsList[0:0]` = at the top.
2. `event_NVDAObject_init(obj)`: poke instance attributes (e.g.
   `obj.TextInfo = …`).
3. `event_<name>(obj, nextHandler)`: intercept an event; **not calling
   `nextHandler` = eating the event**.
4. Gestures and scripts: an `AppModule` is `Scriptable`; its bindings live
   only while the application has focus.
5. Property overrides: `statusBar`, `getStatusBarText`, `productName`,
   `devInfo`.

Three generalised hooks that NVDA's app modules use most and that ALUS must
have: **`shouldAllowFocusEvent`** (suppress a false focus event),
**`lastQueuedFocusObject`** (duplicate detection), and **overriding
`container`** (reshape the focus ancestor chain).

### 12.3 The add-on API

v1.0: **a stable C ABI through `dlopen`.** The header is
`include/alus/alus-plugin.h`, the only header ALUS installs. The guide for
add-on authors is [`../add-ons.md`](../add-ons.md).

```c
// alus-plugin.h — the ABI version is checked at load time
typedef struct AlusPluginV1 {
    uint32_t    abi_version;        // ALUS_PLUGIN_ABI_V1
    const char* name;
    const char* min_alus_version;
    int  (*init)(AlusHost* host, const AlusHostApi* api);
    void (*terminate)(void);
    void (*choose_overlays)(AlusObject* obj, AlusBehaviorList* list);
    /* event_* and script registration go through AlusHostApi calls */
} AlusPluginV1;
```

Add-on directories, on NVDA's "path injection" model (the scratchpad takes
priority over the system):
```
~/.local/share/alus/scratchpad/      development (highest priority)
~/.local/share/alus/addons/<name>/   the user's
<datadir>/alus/plugins/              system-wide
```

NVDA's two-phase install/uninstall is carried over: a loaded `.so` cannot be
deleted, so it is marked and deleted at the next start.

**Deliberately postponed:** a Python add-on host. NVDA's add-on culture is in
Python and that is of great value, but embedding CPython in a C++ core brings
the GIL and a second runtime. It is considered for v1.1, behind the C ABI, as
one plugin that hosts Python modules.

---

## 13. The threading model — explicitly

| Thread | What it does |
|---|---|
| **main / `GMainLoop`** | AT-SPI events, gesture dispatch, scripts, **all `SpeechManager` state**, **all mutation of braille regions**, `Cached<T>`, the global `api` state |
| SSIP reader (inside `libspeechd`) | reads notifications; **marshals through `g_idle_add`** |
| Braille I/O | `brlapi` polling; **cell writes only**, with single-slot replacement |
| `GThreadPool` | large tree walks, `liblouis` on long lines |
| Watchdog | heartbeat; releases the keys while the core is stuck |

**The rule:** everything that touches state is on the main thread. Locking
is minimal; instead ALUS uses:
- **snapshot-then-iterate** (handler lists, the coroutine registry, the list
  of speech indexes),
- **single-slot replacement** instead of a queue (braille writes: if a new
  frame arrives while the old one is being written, the old one is
  **replaced**; intermediate frames are worthless),
- **a stale-source check** (`if (synth != currentSynth()) return;`).

Locks that are needed: the pending event counters (written from the a11y
thread), the braille display's `queuedWrite`, the app module `runningTable`,
the shutdown flag.

---

## 14. Braille

`braille/` is carried over structurally: `Region` (`TextRegion`,
`ObjectRegion`, `TextInfoRegion`, `CursorRegion`), a `BrailleBuffer` with a
window over the joined cells, word wrap (`NONE` / `MARK_WORD_CUTS` /
`AT_WORD_BOUNDARIES` / `AT_WORD_OR_SYLLABLE_BOUNDARIES`), tethering
(`focus` / `review` / `auto`), and `saveWindow()`/`restoreWindow()` as a
`(region, offset)` pair.

Two coalescing mechanisms that are **not** optional:
1. **`_regionsPendingUpdate`**: a set of regions processed **once per pump
   cycle**. N caret moves in one cycle = one region update and one write to
   the display.
2. **Single-slot writes**: §13.

Translation: `liblouis`. Three details from `louisHelper.py` that are easy
to miss:
- `liblouis` **modifies the `typeform` list in place** → pass a copy,
- `dotsIO` mode means cells come back as characters with the bit `0x8000`
  → `cell & 0xFF`,
- NVDA registers **its own table resolver** that ignores `LOUIS_TABLEPATH`
  so add-ons can ship tables; carried over unchanged
  (`AlusHostApi::add_braille_table_source`).

The display: `brlapi` (brltty), instead of NVDA's ~40 drivers for individual
displays. That is a big reduction in scope, and users already have brltty
configured. `br(...)` gesture identifiers and per-driver `gestureMap`s are
kept so a user's `gestures.ini` stays compatible.

Braille input (`bk:` gestures): `bk:dots`, `bk:dot7` (erase), `bk:dot8`
(enter), `bk:dot7+dot8` (translate), plus the `braille_toggle<Modifier>`
family for emulating the keyboard from the braille display.

---

## 15. Build order

Derived from dependencies, not from how visible a feature is. The detailed
plan with exit criteria is [`../history/phase-plan.md`](../history/phase-plan.md).

```
 1. Cached<T>, Queue+coroutines, Loop+pump, extension points, Watchdog, Log, config
 2. Role/State/OutputReason, the TextRange interface, OffsetConverter
 3. a11y::Object over libatspi + Api global state + EventLimiter
 4. EventDispatcher chain + Review + screenExplorer
 5. ISynth/SsipSynth + SpeechManager + Presenter + Symbols/Dictionaries
 6. input (KeyboardMonitor) + gesture maps + script registry + input help
 7. OffsetsTextRange over AtspiText + EditableText behaviour
 8. LiveText/Terminal behaviour + diff
 9. getTextWithFields (ancestor-chain diff) + control/format field speech
10. TreeInterceptor + table nav + CursorManager + BrowseMode + Collection quick nav
11. braille (liblouis + brlapi)
12. app modules + plugin ABI
13. GUI + localisation
```

**Three things most likely to bite**, in order of likelihood:
1. **Offset encoding** (§7.3): three systems, silent errors, visible only in
   bug reports. (It did bite: N113.)
2. **Synthesising `getTextWithFields`** (§9.3): all document reading depends
   on it.
3. **Variance in event reliability** (§8.3): every toolkit lies differently;
   expect per-toolkit tuning.
