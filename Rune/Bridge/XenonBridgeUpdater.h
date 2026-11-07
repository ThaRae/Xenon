/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <Rune/Bridge/XenonBridge.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XenonUpdaterCallbacks {
    void* context;
    void (*status_changed)(void* context, XenonStr ready_version, bool checking);
} XenonUpdaterCallbacks;

void xenon_engine_set_updater_callbacks(XenonUpdaterCallbacks const*);

void xenon_engine_install_update(void);
void xenon_engine_check_for_updates(void);

#ifdef __cplusplus
}
#endif
