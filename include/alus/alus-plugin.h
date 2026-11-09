/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
 *
 * This file is part of ALUS (Access Linux Using Speech).
 *
 * The ALUS add-on ABI, version 1.
 *
 * 01-ARHITEKTURA.md §12.3. This is the ONLY header an add-on includes, it is
 * C and not C++, and it is the one file in ALUS whose layout may never change:
 * an add-on compiled against it must keep working against a later ALUS.
 *
 * WHY C AND NOT C++. A C++ ABI pins the compiler, the standard library and
 * the exception model; a plugin built with a different GCC, or with clang, or
 * against a different libstdc++, would load and then crash somewhere far from
 * the cause. The whole surface here is therefore plain C: fixed-width
 * integers, opaque pointers and function pointers. Nothing throws across this
 * boundary, nothing allocates across it, and no object defined here owns
 * anything.
 *
 * HOW A PLUGIN IS BUILT AND FOUND
 *
 *     cc -shared -fPIC -I<alus include dir> -o myplugin.so myplugin.c
 *
 * and installed into one of (first found wins, most specific first):
 *
 *     ~/.local/share/alus/scratchpad/          development
 *     ~/.local/share/alus/addons/<name>/       the user's add-ons
 *     <datadir>/alus/plugins/                  system-wide
 *
 * ALUS calls `alus_plugin_v1()` and checks `abi_version` and
 * `min_alus_version` BEFORE calling anything else. A plugin that fails either
 * check is refused with a log line naming it -- never loaded half way.
 *
 * WHAT A PLUGIN MAY DO. The three things an NVDA global plugin does, which is
 * what the exit criterion asks for (T8.6): add a script with a gesture, add
 * an overlay behaviour to objects it recognises, and intercept an event. It
 * may also register a braille table directory, which is phase 7's handover
 * item 1 -- a table shipped by an add-on wins over the system table of the
 * same name.
 *
 * STRING LIFETIME, the rule that is easy to get wrong in both directions:
 *
 *   * strings a plugin PASSES to ALUS are copied by ALUS; the plugin may free
 *     them as soon as the call returns;
 *   * strings ALUS RETURNS to a plugin are owned by ALUS and valid only until
 *     the plugin returns from the call it got them in. Copy them to keep them.
 *     They are never NULL; an absent value is the empty string.
 *
 * THREADING: every function here, in both directions, runs on the ALUS main
 * thread, inside a pump cycle. A plugin must not block: a plugin that sleeps
 * for a second freezes the screen reader for a second, and the watchdog will
 * say so out loud.
 */
#ifndef ALUS_PLUGIN_H
#define ALUS_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The only ABI version that exists. Bumped only for a change that is not
 * backwards compatible; a new call is added by growing AlusHostApi instead,
 * which is what its `size` field is for. */
#define ALUS_PLUGIN_ABI_V1 1u

/* The symbol ALUS looks up with dlsym(). */
#define ALUS_PLUGIN_ENTRY_POINT "alus_plugin_v1"

/* Opaque handles. A plugin never dereferences these and never stores an
 * AlusObject* beyond the call it was given in: objects live for one pump
 * cycle (the N49 rule), and a stored one is a dangling pointer the next time
 * the user presses a key. */
typedef struct AlusHost AlusHost;
typedef struct AlusObject AlusObject;
typedef struct AlusBehaviorList AlusBehaviorList;

typedef enum AlusLogLevel {
    ALUS_LOG_DEBUG = 0,
    ALUS_LOG_INFO = 1,
    ALUS_LOG_WARNING = 2,
    ALUS_LOG_ERROR = 3
} AlusLogLevel;

/* What a plugin's event handler decides. CONSUME is NVDA's "do not call
 * nextHandler": the event stops, and the object layer never announces it.
 * That is a loaded gun -- a plugin that consumes gainFocus makes the screen
 * reader silent -- so the default, and the value a handler that does not care
 * must return, is CONTINUE. */
typedef enum AlusEventDisposition {
    ALUS_EVENT_CONTINUE = 0,
    ALUS_EVENT_CONSUME = 1
} AlusEventDisposition;

/* A script's implementation. `user_data` is whatever was registered with it. */
typedef void (*AlusScriptFunc)(void* user_data);
/* An event handler. `object` may be NULL for a global event. */
typedef AlusEventDisposition (*AlusEventFunc)(AlusObject* object, void* user_data);

/* What ALUS offers a plugin. The struct is passed to `init` and is valid for
 * as long as the plugin is loaded.
 *
 * `size` is `sizeof(AlusHostApi)` as the running ALUS knows it. A plugin built
 * against an older header MUST check it before calling anything added after
 * its own version:
 *
 *     if (api->size >= offsetof(AlusHostApi, add_braille_table_source)
 *                      + sizeof(void*)) { ... }
 *
 * New members are only ever appended, so an older plugin's offsets stay
 * valid. */
typedef struct AlusHostApi {
    uint32_t size;

    /* "0.8.0". Compare it yourself if a check finer than min_alus_version is
     * wanted. */
    const char* (*alus_version)(void);
    void (*log)(AlusHost* host, AlusLogLevel level, const char* message);
    /* NVDA: ui.message. Spoken (and brailled) at the normal priority. */
    void (*message)(AlusHost* host, const char* text);

    /* Adds a global script. `gesture` is a gesture in `gestures.ini` spelling
     * ("kb:alus+shift+y"), or NULL for a script the user must bind. The name
     * is what a gesture map section addresses, so it must be unique within
     * the plugin. Returns 0 on success, -1 on a bad argument, -2 when the
     * name is already taken. */
    int (*register_script)(AlusHost* host, const char* name, const char* description,
                           const char* category, const char* gesture, AlusScriptFunc run,
                           void* user_data);

    /* Intercepts an ALUS event by name ("gainFocus", "nameChange",
     * "valueChange", ... -- the names in a11y/events.hpp, which are NVDA's).
     * Several plugins may handle the same event; they run in load order, and
     * the first to CONSUME stops the rest. Returns 0 on success. */
    int (*register_event_handler)(AlusHost* host, const char* event_name,
                                  AlusEventFunc handler, void* user_data);

    /* Phase 7's handover, item 1. The directory is searched for braille
     * tables before the system one, so a table shipped with the add-on wins
     * over a system table of the same name. Returns 0 on success, -1 when the
     * directory does not exist, -2 when braille is not running. */
    int (*add_braille_table_source)(AlusHost* host, const char* directory);

    /* ---- what a plugin may ask about an object ---- */
    /* Never NULL; the empty string when there is nothing to say. */
    const char* (*object_name)(AlusObject* object);
    /* The ALUS role name as `alus-cli` prints it: UPPER CASE with no spaces
     * ("BUTTON", "LISTITEM", "DOCUMENT"). It is the enumeration's own name and
     * not a label for the user, so it is stable across languages and across
     * ALUS versions -- which is what a plugin needs to compare against. */
    const char* (*object_role_name)(AlusObject* object);
    /* One AT-SPI object attribute ("xml-roles", "tag", "level", "id"). */
    const char* (*object_attribute)(AlusObject* object, const char* key);
    /* The toolkit of the application the object lives in ("gtk", "chromium",
     * "qt", "gail"), lower case. */
    const char* (*object_toolkit)(AlusObject* object);

    /* ---- editing the overlay list (mechanism 1 of §12.2) ---- */
    /* NVDA's `clsList[0:0] = (cls,)`. The named behaviour is added at the
     * FRONT, where it wins, and the name becomes part of the object's script
     * class chain -- so a gesture map section with that name reaches the
     * plugin's scripts. A name a plugin invents is fine; so is one of ALUS's
     * own ("NVDAObjects.behaviors.Terminal"). */
    void (*behaviors_prepend)(AlusBehaviorList* list, const char* class_name);
    /* NVDA's `clsList.remove(cls)`. 1 when it was there, 0 when it was not. */
    int (*behaviors_remove)(AlusBehaviorList* list, const char* class_name);
    int (*behaviors_contains)(const AlusBehaviorList* list, const char* class_name);
} AlusHostApi;

/* What a plugin offers ALUS. Every function pointer except `init` may be NULL. */
typedef struct AlusPluginV1 {
    /* Must be ALUS_PLUGIN_ABI_V1. Checked first, before anything is called. */
    uint32_t abi_version;
    /* The plugin's own name, for the log, for `alus-cli app-info` and for the
     * two-phase uninstall. Must not be NULL. */
    const char* name;
    /* The oldest ALUS this plugin works with, as "major.minor.patch", or NULL
     * for "any". A plugin that needs a call added in a later ALUS says so
     * here and is refused cleanly instead of crashing on a NULL pointer. */
    const char* min_alus_version;

    /* Called once, after the version checks pass. Return 0 to accept; any
     * other value refuses the plugin, which is then unloaded and logged. The
     * host and the api are valid until `terminate`. */
    int (*init)(AlusHost* host, const AlusHostApi* api);
    /* Called once, before the library is unloaded. Everything the plugin
     * registered is removed by ALUS afterwards; the plugin only has to free
     * its own memory. */
    void (*terminate)(void);
    /* Mechanism 1: called for every object ALUS creates, in the Plugin stage
     * -- after the platform layer and after the app module, so it sees and may
     * undo what they proposed. It must be CHEAP: it runs for every object of
     * every application. */
    void (*choose_overlays)(AlusObject* object, AlusBehaviorList* list);
} AlusPluginV1;

/* The entry point every plugin defines. */
const AlusPluginV1* alus_plugin_v1(void);

typedef const AlusPluginV1* (*AlusPluginEntryFunc)(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* ALUS_PLUGIN_H */
