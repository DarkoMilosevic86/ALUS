/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
 *
 * This file is part of ALUS (Access Linux Using Speech).
 *
 * A plugin from another ABI. It is what an add-on built against a future (or
 * a past) ALUS looks like, and T8.5 is the statement that ALUS refuses it
 * WITHOUT CALLING ANYTHING IN IT.
 *
 * `init` therefore sets a counter that the test asserts stayed at zero. If the
 * version check ever moves after the init call, this file fails the test
 * instead of crashing a user's screen reader.
 */
#include "alus/alus-plugin.h"

#include <stddef.h>

int alus_test_badabi_init_calls = 0;

static int init(AlusHost* host, const AlusHostApi* api) {
    (void)host;
    (void)api;
    ++alus_test_badabi_init_calls;
    return 0;
}

static const AlusPluginV1 kPlugin = {
    ALUS_PLUGIN_ABI_V1 + 42u, /* not this ALUS's ABI */
    "badabi",
    NULL,
    &init,
    NULL,
    NULL,
};

const AlusPluginV1* alus_plugin_v1(void) { return &kPlugin; }
