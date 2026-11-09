/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
 *
 * This file is part of ALUS (Access Linux Using Speech).
 *
 * A test plugin that does the three things T8.6 asks for: a script, an
 * overlay behaviour and an event handler.
 *
 * IT IS WRITTEN IN C, COMPILED AS C, AND INCLUDES ONLY `alus/alus-plugin.h`.
 * That is the test: if this file ever needs a C++ header, an ALUS internal or
 * a second include, the add-on ABI has leaked and the promise of §12.3 is
 * broken. The test harness checks what it observes; the compiler checks that.
 *
 * It also records what happened in exported counters, so the test can assert
 * that each mechanism was really reached rather than merely registered.
 */
#include "alus/alus-plugin.h"

#include <string.h>

/* Observable from the test through dlsym on the same handle ALUS opened --
 * which is also a check that RTLD_LOCAL did not hide them from their own
 * library. */
int alus_test_init_calls = 0;
int alus_test_terminate_calls = 0;
int alus_test_script_calls = 0;
int alus_test_overlay_calls = 0;
int alus_test_event_calls = 0;
int alus_test_braille_result = -99;
char alus_test_last_object_name[256];
char alus_test_last_role[64];

static AlusHost* g_host = NULL;
static const AlusHostApi* g_api = NULL;

static void onScript(void* userData) {
    ++alus_test_script_calls;
    /* The user data round trip: the pointer handed to register_script must
     * come back unchanged. */
    if (userData != (void*)&alus_test_script_calls) {
        alus_test_script_calls = -1;
    }
    if (g_api != NULL && g_host != NULL) {
        g_api->message(g_host, "hello from the test plugin");
    }
}

static AlusEventDisposition onEvent(AlusObject* object, void* userData) {
    ++alus_test_event_calls;
    (void)userData;
    if (g_api != NULL && object != NULL) {
        const char* name = g_api->object_name(object);
        strncpy(alus_test_last_object_name, name != NULL ? name : "",
                sizeof(alus_test_last_object_name) - 1);
        const char* role = g_api->object_role_name(object);
        strncpy(alus_test_last_role, role != NULL ? role : "",
                sizeof(alus_test_last_role) - 1);
    }
    /* An object whose name says so is consumed, so that the test can prove
     * both halves of the chain contract with one plugin. */
    if (strcmp(alus_test_last_object_name, "swallow me") == 0) {
        return ALUS_EVENT_CONSUME;
    }
    return ALUS_EVENT_CONTINUE;
}

static void chooseOverlays(AlusObject* object, AlusBehaviorList* list) {
    ++alus_test_overlay_calls;
    if (g_api == NULL || object == NULL || list == NULL) {
        return;
    }
    /* Only for the objects this plugin claims, which is how a real plugin
     * behaves: this runs for every object of every application. */
    if (strcmp(g_api->object_role_name(object), "BUTTON") != 0) {
        return;
    }
    g_api->behaviors_prepend(list, "plugins.good.Button");
}

static int init(AlusHost* host, const AlusHostApi* api) {
    ++alus_test_init_calls;
    g_host = host;
    g_api = api;
    if (api == NULL || api->size < sizeof(AlusHostApi)) {
        /* A plugin built against a NEWER header than the running ALUS. The
         * check is the one alus-plugin.h documents; refusing is the safe
         * answer and the test asserts that ALUS accepts the refusal. */
        return 1;
    }
    api->log(host, ALUS_LOG_INFO, "test plugin initialising");
    if (api->register_script(host, "sayHello", "Says hello, for the tests.", "Testing",
                             "kb:alus+shift+y", &onScript,
                             (void*)&alus_test_script_calls) != 0) {
        return 2;
    }
    if (api->register_event_handler(host, "gainFocus", &onEvent, NULL) != 0) {
        return 3;
    }
    /* Phase 7's handover item 1. "/" exists everywhere, so this measures the
     * call and not the filesystem; the test asserts on the answer, which is 0
     * when braille is running and -2 when it is not. */
    alus_test_braille_result = api->add_braille_table_source(host, "/");
    return 0;
}

static void terminate(void) {
    ++alus_test_terminate_calls;
    g_host = NULL;
    g_api = NULL;
}

static const AlusPluginV1 kPlugin = {
    ALUS_PLUGIN_ABI_V1,
    "good",
    "0.0.1",
    &init,
    &terminate,
    &chooseOverlays,
};

const AlusPluginV1* alus_plugin_v1(void) { return &kPlugin; }
