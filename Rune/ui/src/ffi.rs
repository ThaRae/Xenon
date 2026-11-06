/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::{c_char, c_int, c_void};

pub type XenonViewId = u64;
pub type XenonWindowId = u64;

pub const XENON_WINDOW_NONE: XenonWindowId = 0;
pub const XENON_TAB_INDEX_END: i64 = -1;

pub const XENON_UI_COMMAND_FIND: u32 = 0;
pub const XENON_UI_COMMAND_QUIT: u32 = 1;
pub const XENON_UI_COMMAND_OPEN_FILE: u32 = 2;
pub const XENON_UI_COMMAND_TOGGLE_FULLSCREEN: u32 = 3;

pub const XENON_APP_MENU_MAIN: u32 = 0;

pub const XENON_CHROME_LAYOUT_CLASSIC: u8 = 0;
pub const XENON_CHROME_LAYOUT_COMPACT: u8 = 1;
pub const XENON_CHROME_LAYOUT_VERTICAL: u8 = 2;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonStr {
    pub data: *const c_char,
    pub length: usize,
}

impl XenonStr {
    pub fn borrowed(text: &str) -> XenonStr {
        XenonStr {
            data: text.as_ptr().cast(),
            length: text.len(),
        }
    }

    pub unsafe fn to_string_lossy(self) -> String {
        if self.data.is_null() || self.length == 0 {
            return String::new();
        }
        let bytes = unsafe { std::slice::from_raw_parts(self.data.cast::<u8>(), self.length) };
        String::from_utf8_lossy(bytes).into_owned()
    }
}

pub const XENON_MODIFIER_SHIFT: u32 = 1 << 0;
pub const XENON_MODIFIER_CTRL: u32 = 1 << 1;
pub const XENON_MODIFIER_ALT: u32 = 1 << 2;
pub const XENON_MODIFIER_SUPER: u32 = 1 << 3;

pub const XENON_MOUSE_BUTTON_NONE: u32 = 0;
pub const XENON_MOUSE_BUTTON_PRIMARY: u32 = 1 << 0;
pub const XENON_MOUSE_BUTTON_SECONDARY: u32 = 1 << 1;
pub const XENON_MOUSE_BUTTON_MIDDLE: u32 = 1 << 2;
pub const XENON_MOUSE_BUTTON_BACKWARD: u32 = 1 << 3;
pub const XENON_MOUSE_BUTTON_FORWARD: u32 = 1 << 4;

pub const XENON_MOUSE_EVENT_DOWN: u32 = 0;
pub const XENON_MOUSE_EVENT_UP: u32 = 1;
pub const XENON_MOUSE_EVENT_MOVE: u32 = 2;
pub const XENON_MOUSE_EVENT_WHEEL: u32 = 3;
pub const XENON_MOUSE_EVENT_DOUBLE_CLICK: u32 = 4;
pub const XENON_MOUSE_EVENT_LEAVE: u32 = 5;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct XenonMouseEvent {
    pub r#type: u32,
    pub x: f32,
    pub y: f32,
    pub button: u32,
    pub buttons: u32,
    pub modifiers: u32,
    pub wheel_delta_x: f32,
    pub wheel_delta_y: f32,
}

pub const XENON_KEY_EVENT_DOWN: u32 = 0;
pub const XENON_KEY_EVENT_UP: u32 = 1;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonKeyEvent {
    pub r#type: u32,
    pub code: XenonStr,
    pub key: XenonStr,
    pub modifiers: u32,
    pub repeat: bool,
}

pub const XENON_CURSOR_DEFAULT: u32 = 0;
pub const XENON_CURSOR_POINTER: u32 = 1;
pub const XENON_CURSOR_TEXT: u32 = 2;
pub const XENON_CURSOR_VERTICAL_TEXT: u32 = 3;
pub const XENON_CURSOR_CROSSHAIR: u32 = 4;
pub const XENON_CURSOR_MOVE: u32 = 5;
pub const XENON_CURSOR_NOT_ALLOWED: u32 = 6;
pub const XENON_CURSOR_WAIT: u32 = 7;
pub const XENON_CURSOR_PROGRESS: u32 = 8;
pub const XENON_CURSOR_HELP: u32 = 9;
pub const XENON_CURSOR_GRAB: u32 = 10;
pub const XENON_CURSOR_GRABBING: u32 = 11;
pub const XENON_CURSOR_RESIZE_EW: u32 = 12;
pub const XENON_CURSOR_RESIZE_NS: u32 = 13;
pub const XENON_CURSOR_RESIZE_NESW: u32 = 14;
pub const XENON_CURSOR_RESIZE_NWSE: u32 = 15;
pub const XENON_CURSOR_RESIZE_COLUMN: u32 = 16;
pub const XENON_CURSOR_RESIZE_ROW: u32 = 17;
pub const XENON_CURSOR_ZOOM_IN: u32 = 18;
pub const XENON_CURSOR_ZOOM_OUT: u32 = 19;
pub const XENON_CURSOR_HIDDEN: u32 = 20;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonFrame {
    pub width: u32,
    pub height: u32,
    pub stride: usize,
    pub bgra: *const u8,
    pub device_pixel_ratio: f64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonTabInfo {
    pub window: XenonWindowId,
    pub view: XenonViewId,
    pub opener: XenonViewId,
    pub index: u32,
    pub pinned: bool,
    pub is_private: bool,
    pub loaded: bool,
    pub is_new_tab_page: bool,
    pub url: XenonStr,
    pub title: XenonStr,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonUiCallbacks {
    pub context: *mut c_void,
    pub engine_ready: Option<unsafe extern "C" fn(context: *mut c_void)>,
    pub engine_exited: Option<unsafe extern "C" fn(context: *mut c_void, exit_code: c_int)>,
    pub view_frame: Option<
        unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, frame: *const XenonFrame),
    >,
    pub view_title_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, title: XenonStr)>,
    pub view_url_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, url: XenonStr)>,
    pub view_loading_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, is_loading: bool)>,
    pub view_history_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            can_go_back: bool,
            can_go_forward: bool,
        ),
    >,
    pub view_favicon_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            width: u32,
            height: u32,
            rgba: *const u8,
        ),
    >,
    pub view_cursor_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, cursor: u32)>,
    pub view_link_hovered:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, url: XenonStr)>,
    pub view_crashed: Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId)>,
    pub window_opened:
        Option<unsafe extern "C" fn(context: *mut c_void, window: XenonWindowId, is_private: bool)>,
    pub window_closed: Option<unsafe extern "C" fn(context: *mut c_void, window: XenonWindowId)>,
    pub window_activated: Option<unsafe extern "C" fn(context: *mut c_void, window: XenonWindowId)>,
    pub tab_inserted: Option<unsafe extern "C" fn(context: *mut c_void, tab: *const XenonTabInfo)>,
    pub tab_removed: Option<
        unsafe extern "C" fn(context: *mut c_void, window: XenonWindowId, view: XenonViewId),
    >,
    pub tab_moved: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            from: XenonWindowId,
            to: XenonWindowId,
            index: u32,
        ),
    >,
    pub tab_activated: Option<
        unsafe extern "C" fn(context: *mut c_void, window: XenonWindowId, view: XenonViewId),
    >,
    pub tab_pinned_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, pinned: bool)>,
    pub closed_tabs_changed: Option<unsafe extern "C" fn(context: *mut c_void, can_reopen: bool)>,
    pub chrome_settings_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            layout: u8,
            cycle_tabs_in_recently_used_order: bool,
        ),
    >,
    pub tab_memory: Option<
        unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, description: XenonStr),
    >,
    pub ui_command: Option<unsafe extern "C" fn(context: *mut c_void, command: u32)>,
    pub app_menu: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            which: u32,
            menu: *const crate::ffi_page::XenonMenu,
        ),
    >,
    pub view_zoom_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, percent: u32)>,
    pub view_bookmarked_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, bookmarked: bool)>,
    pub downloads_changed: Option<
        unsafe extern "C" fn(context: *mut c_void, downloads: *const XenonDownload, count: usize),
    >,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonDownload {
    pub id: u64,
    pub file_name: XenonStr,
    pub status_text: XenonStr,
    pub status: u32,
    pub progress: f64,
    pub can_resume: bool,
    pub is_private: bool,
}

pub const XENON_DOWNLOAD_IN_PROGRESS: u32 = 0;
pub const XENON_DOWNLOAD_PAUSED: u32 = 1;
pub const XENON_DOWNLOAD_COMPLETED: u32 = 2;

pub const XENON_DOWNLOAD_ACTION_OPEN: u32 = 0;
pub const XENON_DOWNLOAD_ACTION_SHOW_IN_FOLDER: u32 = 1;
pub const XENON_DOWNLOAD_ACTION_CANCEL: u32 = 2;
pub const XENON_DOWNLOAD_ACTION_PAUSE: u32 = 3;
pub const XENON_DOWNLOAD_ACTION_RESUME: u32 = 4;

unsafe extern "C" {
    pub fn xenon_engine_start(
        callbacks: *const XenonUiCallbacks,
        argc: c_int,
        argv: *const *const c_char,
    ) -> bool;
    pub fn xenon_engine_shutdown();

    pub fn xenon_engine_set_window_viewport(
        window: XenonWindowId,
        width: u32,
        height: u32,
        device_pixel_ratio: f64,
    );
    pub fn xenon_engine_set_system_dark(dark: bool);
    pub fn xenon_engine_set_chrome_layout(layout: u8);
    pub fn xenon_engine_set_cycle_tabs_in_recently_used_order(enabled: bool);
    pub fn xenon_engine_activate_action(action: u32);
    pub fn xenon_engine_request_app_menu(which: u32);
    pub fn xenon_engine_activate_app_menu_item(menu: u64, item: u32);
    pub fn xenon_engine_download_action(download: u64, action: u32);
    pub fn xenon_engine_new_window(is_private: bool) -> XenonWindowId;
    pub fn xenon_engine_activate_window(window: XenonWindowId);
    pub fn xenon_engine_close_window(window: XenonWindowId);
    pub fn xenon_engine_new_tab(
        window: XenonWindowId,
        url: XenonStr,
        index: i64,
        activate: bool,
    ) -> XenonViewId;
    pub fn xenon_engine_activate_tab(view: XenonViewId);
    pub fn xenon_engine_close_tab(view: XenonViewId);
    pub fn xenon_engine_move_tab(view: XenonViewId, window: XenonWindowId, index: i64);
    pub fn xenon_engine_set_tab_pinned(view: XenonViewId, pinned: bool);
    pub fn xenon_engine_reopen_closed_tab(window: XenonWindowId);
    pub fn xenon_engine_duplicate_tab(view: XenonViewId) -> XenonViewId;
    pub fn xenon_engine_request_tab_memory(view: XenonViewId);
    pub fn xenon_engine_cycle_recent_tabs(window: XenonWindowId, toward_less_recent: bool);
    pub fn xenon_engine_end_recent_tab_cycle(window: XenonWindowId);
    pub fn xenon_engine_load(view: XenonViewId, input: XenonStr);
    pub fn xenon_engine_go_back(view: XenonViewId);
    pub fn xenon_engine_go_forward(view: XenonViewId);
    pub fn xenon_engine_reload(view: XenonViewId);
    pub fn xenon_engine_stop(view: XenonViewId);
    pub fn xenon_engine_set_viewport(
        view: XenonViewId,
        width: u32,
        height: u32,
        device_pixel_ratio: f64,
    );
    pub fn xenon_engine_set_visible(view: XenonViewId, visible: bool);
    pub fn xenon_engine_set_focused(view: XenonViewId, focused: bool);
    pub fn xenon_engine_mouse_event(view: XenonViewId, event: *const XenonMouseEvent);
    pub fn xenon_engine_key_event(view: XenonViewId, event: *const XenonKeyEvent);
    pub fn xenon_engine_insert_text(view: XenonViewId, text: XenonStr);
}

unsafe extern "C" {
    pub fn xenon_platform_is_fullscreen() -> bool;
    pub fn xenon_platform_set_fullscreen(fullscreen: bool);
    pub fn xenon_platform_is_always_on_top() -> bool;
    pub fn xenon_platform_set_always_on_top(always_on_top: bool);
}
