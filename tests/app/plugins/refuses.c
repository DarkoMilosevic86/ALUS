/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
 *
 * This file is part of ALUS (Access Linux Using Speech).
 *
 * A plugin that passes every version check and then refuses to start -- the
 * case a plugin uses to say "my configuration is missing" or "the program I
 * extend is not installed".
 *
 * It registers a script FIRST and then returns non-zero, which is the part
 * that is easy to get wrong: ALUS must take the registration back. A screen
 * reader with a key bound into an unloaded library crashes on the keystroke,
 * not on the load.
 */
#include "alus/alus-plugin.h"

#include <stddef.h>

static void onScript(void* userData) { (void)userData; }

static int init(AlusHost* host, const AlusHostApi* api) {
    api->register_script(host, "neverRuns", "Never runs.", "Testing", "kb:alus+shift+z",
                         &onScript, NULL);
    return 7;
}

static const AlusPluginV1 kPlugin = {
    ALUS_PLUGIN_ABI_V1, "refuses", NULL, &init, NULL, NULL
};

const AlusPluginV1* alus_plugin_v1(void) { return &kPlugin; }
