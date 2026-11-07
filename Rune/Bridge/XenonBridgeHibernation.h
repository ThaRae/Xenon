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

enum XenonTabLoadState {
    XENON_TAB_LOADED,
    XENON_TAB_NOT_LOADED,
    XENON_TAB_HIBERNATED,
};

typedef struct XenonTabIcon {
    uint32_t width;
    uint32_t height;
    uint8_t const* rgba;
} XenonTabIcon;

typedef struct XenonHibernationSettings {
    bool enabled;
    uint64_t timeout_seconds;
    uint32_t const* timeout_choices_in_minutes;
    size_t timeout_choice_count;
    bool hibernate_on_memory_pressure;
} XenonHibernationSettings;

typedef struct XenonHibernationCallbacks {
    void* context;
    void (*tab_load_state_changed)(void* context, XenonViewId, uint32_t state, XenonTabIcon const* hibernated_icon);
    void (*settings_changed)(void* context, XenonHibernationSettings const*);
} XenonHibernationCallbacks;

void xenon_engine_set_hibernation_callbacks(XenonHibernationCallbacks const*);

void xenon_engine_hibernate_tab(XenonViewId);
void xenon_engine_set_tab_hibernation(bool enabled, uint64_t timeout_seconds);

#ifdef __cplusplus
}
#endif
