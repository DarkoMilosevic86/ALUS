/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
 *
 * This file is part of ALUS (Access Linux Using Speech).
 *
 * A plugin whose ABI matches but which needs a newer ALUS than this one. The
 * second half of T8.5: the right ABI is not the only thing that has to agree,
 * because a call added in a later version is a null pointer in an earlier one.
 */
#include "alus/alus-plugin.h"

#include <stddef.h>

int alus_test_toonew_init_calls = 0;

static int init(AlusHost* host, const AlusHostApi* api) {
    (void)host;
    (void)api;
    ++alus_test_toonew_init_calls;
    return 0;
}

static const AlusPluginV1 kPlugin = {
    ALUS_PLUGIN_ABI_V1,
    "toonew",
    "99.0.0", /* no ALUS will be this old */
    &init,
    NULL,
    NULL,
};

const AlusPluginV1* alus_plugin_v1(void) { return &kPlugin; }
