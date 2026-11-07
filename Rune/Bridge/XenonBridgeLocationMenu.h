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

typedef struct XenonLocationMenuCallbacks {
    void* context;
    void (*paste_and_go_described)(void* context, uint64_t request, XenonStr label);
} XenonLocationMenuCallbacks;

void xenon_engine_set_location_menu_callbacks(XenonLocationMenuCallbacks const*);

void xenon_engine_location_describe_paste_and_go(uint64_t request, XenonStr clipboard_text);
void xenon_engine_location_paste_and_go(XenonViewId, uint64_t generation, XenonStr text);
void xenon_engine_location_set_suspended(XenonViewId, uint64_t generation, bool suspended);

#ifdef __cplusplus
}
#endif
