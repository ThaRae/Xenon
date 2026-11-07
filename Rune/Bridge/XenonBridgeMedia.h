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

typedef struct XenonPictureInPicture {
    XenonViewId id;
    XenonViewId owner;
    float width;
    float height;
    float minimum_width;
    float minimum_height;
    float maximum_width;
    float maximum_height;
} XenonPictureInPicture;

typedef struct XenonMediaCallbacks {
    void* context;
    void (*picture_in_picture_shown)(void* context, XenonPictureInPicture const*);
    void (*picture_in_picture_hidden)(void* context, XenonViewId);
    void (*picture_in_picture_press_handled)(void* context, XenonViewId, bool claimed_by_page);
} XenonMediaCallbacks;

void xenon_engine_set_media_callbacks(XenonMediaCallbacks const*);

void xenon_engine_picture_in_picture_mouse_event(XenonViewId, XenonMouseEvent const*);
void xenon_engine_picture_in_picture_cancel_press(XenonViewId);
void xenon_engine_picture_in_picture_resize(XenonViewId, float width, float height, double device_pixel_ratio);

#ifdef __cplusplus
}
#endif
