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

enum XenonScreenshotAnswer {
    XENON_SCREENSHOT_SELECTION,
    XENON_SCREENSHOT_VISIBLE_PAGE,
    XENON_SCREENSHOT_FULL_PAGE,
    XENON_SCREENSHOT_CANCEL,
};

typedef struct XenonScreenshotCallbacks {
    void* context;
    void (*selection_requested)(void* context, XenonViewId, XenonViewId frame_surface);
    void (*save_location_requested)(void* context, uint64_t request, XenonStr directory, XenonStr file_name);
} XenonScreenshotCallbacks;

void xenon_engine_set_screenshot_callbacks(XenonScreenshotCallbacks const*);

void xenon_engine_refresh_screenshot_frame(XenonViewId);
void xenon_engine_finish_screenshot(XenonViewId, uint32_t answer, float x, float y, float width, float height);
void xenon_engine_answer_save_location(uint64_t request, bool chosen, XenonStr path);

#ifdef __cplusplus
}
#endif
