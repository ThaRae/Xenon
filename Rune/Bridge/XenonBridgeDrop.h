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

enum XenonDragEventType {
    XENON_DRAG_ENTER,
    XENON_DRAG_MOVE,
    XENON_DRAG_LEAVE,
    XENON_DRAG_DROP,
};

void xenon_engine_drag_event(XenonViewId, uint32_t type, float x, float y, uint32_t modifiers, XenonStr const* paths, size_t path_count);

#ifdef __cplusplus
}
#endif
