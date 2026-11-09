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

enum XenonWindowFrameRequest {
    XENON_WINDOW_FRAME_MOVE,
    XENON_WINDOW_FRAME_RESIZE,
    XENON_WINDOW_FRAME_MAXIMIZE,
    XENON_WINDOW_FRAME_MINIMIZE,
    XENON_WINDOW_FRAME_RESTORE,
};

typedef struct XenonWindowGeometry {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    bool maximized;
    bool has_position;
} XenonWindowGeometry;

typedef struct XenonWindowCallbacks {
    void* context;
    void (*window_placement)(void* context, XenonWindowId, XenonWindowGeometry const*);
    void (*window_frame_requested)(void* context, XenonWindowId, uint32_t request, int32_t x, int32_t y, int32_t width, int32_t height);
    void (*window_raise)(void* context, XenonWindowId);
    void (*confirm_stop_downloads)(void* context, uint64_t request);
    void (*quit_approved)(void* context);
    void (*devtools_changed)(void* context, bool enabled, uint16_t port);
} XenonWindowCallbacks;

void xenon_engine_set_window_callbacks(XenonWindowCallbacks const*);

void xenon_engine_request_close_window(XenonWindowId);
void xenon_engine_request_quit(void);
void xenon_engine_answer_stop_downloads(uint64_t request, bool stop);
void xenon_engine_report_window_geometry(XenonWindowId, XenonWindowGeometry const*);
void xenon_engine_set_window_exposed(XenonWindowId, bool exposed);
void xenon_engine_launch_devtools_client(void);
void xenon_engine_disable_devtools(void);

void xenon_platform_attach_window(uint32_t slot, uint32_t window_number);
void xenon_platform_bind_window(uint32_t slot, XenonWindowId);
bool xenon_platform_window_is_maximized(uint32_t slot);
bool xenon_platform_window_is_fullscreen(uint32_t slot);
void xenon_platform_window_request(uint32_t slot, uint32_t request);

#ifdef __cplusplus
}
#endif
