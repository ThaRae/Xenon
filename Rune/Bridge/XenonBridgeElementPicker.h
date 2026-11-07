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

typedef struct XenonElementPickerCallbacks {
    void* context;
    void (*filter_confirmation_requested)(void* context, XenonViewId, uint64_t request, XenonStr filter);
} XenonElementPickerCallbacks;

void xenon_engine_set_element_picker_callbacks(XenonElementPickerCallbacks const*);

void xenon_engine_confirm_element_filter(uint64_t request, bool accepted, XenonStr filter);

#ifdef __cplusplus
}
#endif
