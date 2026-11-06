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
typedef uint64_t XenonWindowId;

enum {
    XENON_WINDOW_NONE = 0,
};

enum {
    XENON_TAB_INDEX_END = -1,
};

enum {
    XENON_UI_COMMAND_FIND = 0,
    XENON_UI_COMMAND_QUIT = 1,
    XENON_UI_COMMAND_OPEN_FILE = 2,
    XENON_UI_COMMAND_TOGGLE_FULLSCREEN = 3,
};

enum {
    XENON_ACTION_ZOOM_IN = 0,
    XENON_ACTION_ZOOM_OUT,
    XENON_ACTION_RESET_ZOOM,
    XENON_ACTION_RELOAD_BYPASSING_CACHE,
    XENON_ACTION_VIEW_SOURCE,
    XENON_ACTION_TOGGLE_DEVTOOLS,
    XENON_ACTION_OPEN_SETTINGS,
    XENON_ACTION_OPEN_ABOUT,
    XENON_ACTION_OPEN_DOWNLOADS,
    XENON_ACTION_VIEW_HISTORY,
    XENON_ACTION_CLEAR_BROWSING_DATA,
    XENON_ACTION_TOGGLE_BOOKMARK,
    XENON_ACTION_MANAGE_BOOKMARKS,
    XENON_ACTION_BOOKMARK_ALL_TABS,
    XENON_ACTION_TOGGLE_BOOKMARKS_BAR,
    XENON_ACTION_REPORT_WEBSITE_ISSUE,
    XENON_ACTION_FORCE_DARK,
};

enum {
    XENON_APP_MENU_MAIN = 0,
};

enum {
    XENON_CHROME_LAYOUT_CLASSIC = 0,
    XENON_CHROME_LAYOUT_COMPACT = 1,
    XENON_CHROME_LAYOUT_VERTICAL = 2,
};

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

typedef struct XenonTabInfo {
    XenonWindowId window;
    XenonViewId view;
    XenonViewId opener;
    uint32_t index;
    bool pinned;
    bool is_private;
    bool loaded;
    bool is_new_tab_page;
    XenonStr url;
    XenonStr title;
} XenonTabInfo;

struct XenonMenu;

enum {
    XENON_BLOCKING_UNAVAILABLE = 0,
    XENON_BLOCKING_DISABLED,
    XENON_BLOCKING_SITE_DISABLED,
    XENON_BLOCKING_ENABLED,
    XENON_BLOCKING_NO_RULES,
};

enum {
    XENON_BLOCKING_ACTION_TOGGLE_SITE = 0,
    XENON_BLOCKING_ACTION_PICK_ELEMENT,
    XENON_BLOCKING_ACTION_OPEN_DASHBOARD,
};

enum {
    XENON_DOWNLOAD_IN_PROGRESS = 0,
    XENON_DOWNLOAD_PAUSED,
    XENON_DOWNLOAD_COMPLETED,
    XENON_DOWNLOAD_CANCELED,
    XENON_DOWNLOAD_FAILED,
};

enum {
    XENON_DOWNLOAD_ACTION_OPEN = 0,
    XENON_DOWNLOAD_ACTION_SHOW_IN_FOLDER,
    XENON_DOWNLOAD_ACTION_CANCEL,
    XENON_DOWNLOAD_ACTION_PAUSE,
    XENON_DOWNLOAD_ACTION_RESUME,
};

typedef struct XenonDownload {
    uint64_t id;
    XenonStr file_name;
    XenonStr status_text;
    uint32_t status;
    // From 0 to 1, or negative when the size isn't known.
    double progress;
    bool can_resume;
    bool is_private;
} XenonDownload;

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
    void (*view_crashed)(void* context, XenonViewId);
    void (*window_opened)(void* context, XenonWindowId, bool is_private);
    void (*window_closed)(void* context, XenonWindowId);
    void (*window_activated)(void* context, XenonWindowId);
    void (*tab_inserted)(void* context, XenonTabInfo const*);
    void (*tab_removed)(void* context, XenonWindowId, XenonViewId);
    void (*tab_moved)(void* context, XenonViewId, XenonWindowId from, XenonWindowId to, uint32_t index);
    void (*tab_activated)(void* context, XenonWindowId, XenonViewId);
    void (*tab_pinned_changed)(void* context, XenonViewId, bool pinned);
    void (*closed_tabs_changed)(void* context, bool can_reopen);
    void (*chrome_settings_changed)(void* context, uint8_t layout, bool cycle_tabs_in_recently_used_order);
    void (*tab_memory)(void* context, XenonViewId, XenonStr description);
    void (*ui_command)(void* context, uint32_t command);
    void (*app_menu)(void* context, uint32_t which, struct XenonMenu const*);
    void (*view_zoom_changed)(void* context, XenonViewId, uint32_t percent);
    void (*view_bookmarked_changed)(void* context, XenonViewId, bool bookmarked);
    void (*downloads_changed)(void* context, XenonDownload const*, size_t count);
    void (*view_audio_changed)(void* context, XenonViewId, bool playing_audio, bool muted);
    void (*view_unresponsive_changed)(void* context, XenonViewId, bool unresponsive);
    void (*history_menu)(void* context, XenonViewId, struct XenonMenu const*);
    void (*view_blocking_changed)(void* context, XenonViewId, uint32_t state, XenonStr site, uint64_t blocked_requests);
} XenonUiCallbacks;

int xenon_ui_main(int argc, char const* const* argv);

bool xenon_engine_start(XenonUiCallbacks const*, int argc, char const* const* argv);
void xenon_engine_shutdown(void);

void xenon_engine_set_window_viewport(XenonWindowId, uint32_t width, uint32_t height, double device_pixel_ratio);
void xenon_engine_set_system_dark(bool dark);
void xenon_engine_set_chrome_layout(uint8_t layout);
void xenon_engine_set_cycle_tabs_in_recently_used_order(bool);
void xenon_engine_activate_action(uint32_t action);
void xenon_engine_request_app_menu(uint32_t which);
void xenon_engine_activate_app_menu_item(uint64_t menu, uint32_t item);
void xenon_engine_download_action(uint64_t download, uint32_t action);
void xenon_engine_toggle_mute(XenonViewId);
void xenon_engine_blocking_action(XenonViewId, uint32_t action);
void xenon_engine_request_history_menu(XenonViewId, int32_t direction);
void xenon_engine_traverse_history_to_step(XenonViewId, int32_t step);
void xenon_engine_answer_unresponsive_page(XenonViewId, bool keep_waiting);
XenonWindowId xenon_engine_new_window(bool is_private);
void xenon_engine_activate_window(XenonWindowId);
void xenon_engine_close_window(XenonWindowId);
XenonViewId xenon_engine_new_tab(XenonWindowId, XenonStr url, int64_t index, bool activate);
void xenon_engine_activate_tab(XenonViewId);
void xenon_engine_close_tab(XenonViewId);
void xenon_engine_move_tab(XenonViewId, XenonWindowId, int64_t index);
void xenon_engine_set_tab_pinned(XenonViewId, bool pinned);
void xenon_engine_reopen_closed_tab(XenonWindowId);
XenonViewId xenon_engine_duplicate_tab(XenonViewId);
void xenon_engine_request_tab_memory(XenonViewId);
void xenon_engine_cycle_recent_tabs(XenonWindowId, bool toward_less_recent);
void xenon_engine_end_recent_tab_cycle(XenonWindowId);

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
