# Writing ALUS add-ons

An ALUS add-on is a shared library written in C (or any language that can
export a C function) against one header, `alus/alus-plugin.h`. It can do the
three things an NVDA global plugin does:

1. add a **script** with a gesture,
2. add an **overlay behaviour** to objects it recognises, so gestures in a
   section of that name reach its scripts,
3. **intercept an event** (`gainFocus`, `nameChange`, …) and let it continue
   or consume it,

and it can add a directory of **braille tables** that takes precedence over
the system's.

## Why C

A C++ ABI pins the compiler, the standard library and the exception model. A
plugin built with another compiler would load and then crash far from the
cause. The add-on surface is therefore plain C: fixed-width integers, opaque
pointers and function pointers. Nothing throws, allocates or transfers
ownership across it, and the layout of the header never changes; new host
functions are only appended.

## A minimal add-on

```c
/* hello.c */
#include <alus/alus-plugin.h>

#include <stddef.h>

static AlusHost* host;
static const AlusHostApi* api;

static void sayHello(void* userData) {
    (void)userData;
    api->message(host, "Hello from my add-on");
}

static AlusEventDisposition onFocus(AlusObject* object, void* userData) {
    (void)userData;
    api->log(host, ALUS_LOG_DEBUG, api->object_name(object));
    return ALUS_EVENT_CONTINUE;     /* ALUS_EVENT_CONSUME stops the chain */
}

static int init(AlusHost* h, const AlusHostApi* a) {
    host = h;
    api = a;
    if (api->register_script(host, "sayHello", "Says hello", "Miscellaneous",
                             "kb:alus+shift+y", sayHello, NULL) != 0) {
        return -1;                  /* refuse to load */
    }
    api->register_event_handler(host, "gainFocus", onFocus, NULL);
    return 0;
}

static const AlusPluginV1 plugin = {
    .abi_version = ALUS_PLUGIN_ABI_V1,
    .name = "hello",
    .min_alus_version = "2026.1.0",
    .init = init,
    .terminate = NULL,
    .choose_overlays = NULL,
};

const AlusPluginV1* alus_plugin_v1(void) { return &plugin; }
```

Build it:

```bash
cc -shared -fPIC -I/usr/local/include -o hello.so hello.c
```

(`-I` points at wherever `alus/alus-plugin.h` was installed; in the source
tree it is `include/`.)

## Where add-ons are found

The first directory that has a plugin of a given name wins, most specific
first:

| Directory | For |
|---|---|
| `~/.local/share/alus/scratchpad/` | development: drop a bare `.so` here |
| `~/.local/share/alus/addons/<name>/` | the user's installed add-ons |
| `<datadir>/alus/plugins/` | system-wide add-ons |

Restart ALUS (or change any setting, which restarts the subsystems in place)
to load a new plugin. `alus-cli app-info` lists the plugins it finds and
whether each one loaded. `alus-daemon --no-plugins` starts without any.

Uninstalling is two-phase, as in NVDA: a loaded library cannot be deleted, so
it is marked and removed at the next start.

## The contract

**Loading.** ALUS calls `alus_plugin_v1()` and checks `abi_version` and
`min_alus_version` **before** anything else. A plugin that fails either
check is refused with a log line naming it, never loaded half way. `init`
returning non-zero also refuses the plugin.

**Host API.** `AlusHostApi::size` is the size of the struct the running ALUS
knows. A plugin built against an older header must check it before calling a
function added later:

```c
#include <stddef.h>   /* offsetof */

if (api->size >= offsetof(AlusHostApi, add_braille_table_source) + sizeof(void*)) {
    api->add_braille_table_source(host, "/path/to/tables");
}
```

**Strings.** Strings a plugin passes to ALUS are copied; the plugin may free
them when the call returns. Strings ALUS returns are owned by ALUS and valid
only until the plugin returns from the call it got them in; copy them to keep
them. They are never `NULL`; an absent value is the empty string.

**Threading.** Every function, in both directions, runs on the ALUS main
thread, inside a pump cycle. **Never block.** A plugin that sleeps for a
second freezes the screen reader for a second, and the watchdog will say so.
`choose_overlays` runs for every object of every application, so it must be
cheap.

**Objects.** `object_role_name` returns the role's enumeration name in upper
case ("BUTTON", "LISTITEM", "DOCUMENT"), stable across languages and
versions. `object_attribute` reads one AT-SPI object attribute
(`xml-roles`, `tag`, `level`, `id`); `object_toolkit` returns the
application's toolkit in lower case (`gtk`, `chromium`, `qt`).

**Events.** The event names are NVDA's (`gainFocus`, `nameChange`,
`valueChange`, `stateChange`, …; see `include/alus/a11y/events.hpp`). Several
plugins may handle one event; they run in load order, and the first to
return `ALUS_EVENT_CONSUME` stops the rest, including ALUS's own handling.

**Overlays.** In `choose_overlays`, `behaviors_prepend(list, "myplugin.Button")`
puts a behaviour name at the front of the object's class chain, so a
`[myplugin.Button]` section in a gesture map reaches scripts for that object.
`behaviors_remove` and `behaviors_contains` take one of ALUS's own behaviour
names (for example `NVDAObjects.behaviors.Terminal`) out or test for it.

The header itself, `include/alus/alus-plugin.h`, documents every function and
is the reference. Four test plugins in `tests/app/plugins/` show a working
plugin (`good.c`) and the three ways a plugin is refused (`badabi.c`,
`toonew.c`, `refuses.c`).

## Not yet

- No Python add-on host. It is planned behind the C ABI, as one plugin that
  hosts Python modules.
- No add-on store or packaging format; copy the `.so` into one of the
  directories above.
