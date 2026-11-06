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

typedef uint64_t XenonTabGroupId;

#define XENON_TAB_GROUP_NONE ((XenonTabGroupId)0)

enum XenonTabGroupColor {
    XENON_TAB_GROUP_COLOR_ROSE,
    XENON_TAB_GROUP_COLOR_SKY,
    XENON_TAB_GROUP_COLOR_AMBER,
    XENON_TAB_GROUP_COLOR_JADE,
    XENON_TAB_GROUP_COLOR_ORCHID,
    XENON_TAB_GROUP_COLOR_EMBER,
    XENON_TAB_GROUP_COLOR_IRIS,
    XENON_TAB_GROUP_COLOR_MOSS,
    XENON_TAB_GROUP_COLOR_SLATE,
};

typedef struct XenonTabGroup {
    XenonTabGroupId id;
    XenonStr title;
    uint32_t color;
    bool collapsed;
    XenonViewId const* tabs;
    size_t tab_count;
} XenonTabGroup;

typedef struct XenonTabGroupCallbacks {
    void* context;
    void (*groups_changed)(void* context, XenonWindowId, XenonTabGroup const* groups, size_t group_count);
    void (*edit_requested)(void* context, XenonWindowId, XenonTabGroupId);
} XenonTabGroupCallbacks;

void xenon_engine_set_tab_group_callbacks(XenonTabGroupCallbacks const*);

void xenon_engine_add_tab_to_new_group(XenonViewId);
void xenon_engine_add_tab_to_group(XenonViewId, XenonTabGroupId);
void xenon_engine_remove_tab_from_group(XenonViewId);
void xenon_engine_set_tab_group_title(XenonTabGroupId, XenonStr title);
void xenon_engine_set_tab_group_color(XenonTabGroupId, uint32_t color);
void xenon_engine_set_tab_group_collapsed(XenonTabGroupId, bool collapsed);
void xenon_engine_ungroup_tabs(XenonTabGroupId);
void xenon_engine_close_tab_group(XenonTabGroupId);
void xenon_engine_new_tab_in_group(XenonTabGroupId);
void xenon_engine_move_tab_group_to_new_window(XenonTabGroupId);
void xenon_engine_drop_tab(XenonViewId, uint32_t index, XenonTabGroupId);

#ifdef __cplusplus
}
#endif
