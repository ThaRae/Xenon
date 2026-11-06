/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t XenonViewId;

typedef struct XenonStr {
    char const* data;
    size_t length;
} XenonStr;

enum XenonModifier {
    XENON_MODIFIER_SHIFT = 1 << 0,
    XENON_MODIFIER_CTRL = 1 << 1,
    XENON_MODIFIER_ALT = 1 << 2,
    XENON_MODIFIER_SUPER = 1 << 3,
};

enum XenonMouseButton {
    XENON_MOUSE_BUTTON_NONE = 0,
    XENON_MOUSE_BUTTON_PRIMARY = 1 << 0,
    XENON_MOUSE_BUTTON_SECONDARY = 1 << 1,
    XENON_MOUSE_BUTTON_MIDDLE = 1 << 2,
    XENON_MOUSE_BUTTON_BACKWARD = 1 << 3,
    XENON_MOUSE_BUTTON_FORWARD = 1 << 4,
};

enum XenonMouseEventType {
    XENON_MOUSE_EVENT_DOWN,
    XENON_MOUSE_EVENT_UP,
    XENON_MOUSE_EVENT_MOVE,
    XENON_MOUSE_EVENT_WHEEL,
    XENON_MOUSE_EVENT_DOUBLE_CLICK,
    XENON_MOUSE_EVENT_LEAVE,
};

typedef struct XenonMouseEvent {
    uint32_t type;
    float x;
    float y;
    uint32_t button;
    uint32_t buttons;
    uint32_t modifiers;
    float wheel_delta_x;
    float wheel_delta_y;
} XenonMouseEvent;

enum XenonKeyEventType {
    XENON_KEY_EVENT_DOWN,
    XENON_KEY_EVENT_UP,
};

typedef struct XenonKeyEvent {
    uint32_t type;
    XenonStr code;
    XenonStr key;
    uint32_t modifiers;
    bool repeat;
} XenonKeyEvent;

enum XenonCursor {
    XENON_CURSOR_DEFAULT,
    XENON_CURSOR_POINTER,
    XENON_CURSOR_TEXT,
    XENON_CURSOR_VERTICAL_TEXT,
    XENON_CURSOR_CROSSHAIR,
    XENON_CURSOR_MOVE,
    XENON_CURSOR_NOT_ALLOWED,
    XENON_CURSOR_WAIT,
    XENON_CURSOR_PROGRESS,
    XENON_CURSOR_HELP,
    XENON_CURSOR_GRAB,
    XENON_CURSOR_GRABBING,
    XENON_CURSOR_RESIZE_EW,
    XENON_CURSOR_RESIZE_NS,
    XENON_CURSOR_RESIZE_NESW,
    XENON_CURSOR_RESIZE_NWSE,
    XENON_CURSOR_RESIZE_COLUMN,
    XENON_CURSOR_RESIZE_ROW,
    XENON_CURSOR_ZOOM_IN,
    XENON_CURSOR_ZOOM_OUT,
    XENON_CURSOR_HIDDEN,
};

typedef struct XenonFrame {
    uint32_t width;
    uint32_t height;
    size_t stride;
    uint8_t const* bgra;
    double device_pixel_ratio;
} XenonFrame;

typedef struct XenonUiCallbacks {
    void* context;
    void (*engine_ready)(void* context);
    void (*engine_exited)(void* context, int exit_code);
    void (*view_frame)(void* context, XenonViewId, XenonFrame const*);
    void (*view_title_changed)(void* context, XenonViewId, XenonStr title);
    void (*view_url_changed)(void* context, XenonViewId, XenonStr url);
    void (*view_loading_changed)(void* context, XenonViewId, bool is_loading);
    void (*view_history_changed)(void* context, XenonViewId, bool can_go_back, bool can_go_forward);
    void (*view_favicon_changed)(void* context, XenonViewId, uint32_t width, uint32_t height, uint8_t const* rgba);
    void (*view_cursor_changed)(void* context, XenonViewId, uint32_t cursor);
    void (*view_link_hovered)(void* context, XenonViewId, XenonStr url);
    void (*view_opened)(void* context, XenonViewId opener, XenonViewId view, bool activate);
    void (*view_close_requested)(void* context, XenonViewId);
    void (*view_crashed)(void* context, XenonViewId);
} XenonUiCallbacks;

int xenon_ui_main(int argc, char const* const* argv);

bool xenon_engine_start(XenonUiCallbacks const*, int argc, char const* const* argv);
void xenon_engine_shutdown(void);

XenonViewId xenon_engine_create_view(XenonStr url, uint32_t width, uint32_t height, double device_pixel_ratio);
void xenon_engine_close_view(XenonViewId);
void xenon_engine_load(XenonViewId, XenonStr input);
void xenon_engine_go_back(XenonViewId);
void xenon_engine_go_forward(XenonViewId);
void xenon_engine_reload(XenonViewId);
void xenon_engine_stop(XenonViewId);
void xenon_engine_set_viewport(XenonViewId, uint32_t width, uint32_t height, double device_pixel_ratio);
void xenon_engine_set_visible(XenonViewId, bool visible);
void xenon_engine_set_focused(XenonViewId, bool focused);
void xenon_engine_mouse_event(XenonViewId, XenonMouseEvent const*);
void xenon_engine_key_event(XenonViewId, XenonKeyEvent const*);
void xenon_engine_insert_text(XenonViewId, XenonStr text);

#ifdef __cplusplus
}
#endif
