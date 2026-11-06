/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::{c_char, c_int, c_void};

pub type XenonViewId = u64;

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
    pub view_opened: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            opener: XenonViewId,
            view: XenonViewId,
            activate: bool,
        ),
    >,
    pub view_close_requested: Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId)>,
    pub view_crashed: Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId)>,
}

unsafe extern "C" {
    pub fn xenon_engine_start(
        callbacks: *const XenonUiCallbacks,
        argc: c_int,
        argv: *const *const c_char,
    ) -> bool;
    pub fn xenon_engine_shutdown();

    pub fn xenon_engine_create_view(
        url: XenonStr,
        width: u32,
        height: u32,
        device_pixel_ratio: f64,
    ) -> XenonViewId;
    pub fn xenon_engine_close_view(view: XenonViewId);
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
