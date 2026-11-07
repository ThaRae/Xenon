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

enum XenonSplitSide {
    XENON_SPLIT_SIDE_LEFT,
    XENON_SPLIT_SIDE_RIGHT,
};

typedef struct XenonSplit {
    XenonViewId left;
    XenonViewId right;
} XenonSplit;

typedef struct XenonSplitViewCallbacks {
    void* context;
    void (*splits_changed)(void* context, XenonSplit const*, size_t count);
    void (*drag_and_drop_changed)(void* context, bool enabled);
} XenonSplitViewCallbacks;

void xenon_engine_set_split_view_callbacks(XenonSplitViewCallbacks const*);

void xenon_engine_split_view_open_new_tab(XenonViewId beside, uint32_t new_tab_side);
void xenon_engine_split_view_create(XenonViewId anchor, XenonViewId other, uint32_t other_side, XenonViewId activate);
void xenon_engine_split_view_reverse(XenonViewId);
void xenon_engine_split_view_separate(XenonViewId);
void xenon_engine_split_view_drop_tab(XenonViewId dragged, uint32_t side);

#ifdef __cplusplus
}
#endif
