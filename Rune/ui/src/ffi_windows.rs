/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::XenonWindowId;

pub const XENON_WINDOW_FRAME_MOVE: u32 = 0;
pub const XENON_WINDOW_FRAME_RESIZE: u32 = 1;
pub const XENON_WINDOW_FRAME_MAXIMIZE: u32 = 2;
pub const XENON_WINDOW_FRAME_MINIMIZE: u32 = 3;
pub const XENON_WINDOW_FRAME_RESTORE: u32 = 4;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct XenonWindowGeometry {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub maximized: bool,
    pub has_position: bool,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonWindowCallbacks {
    pub context: *mut c_void,
    pub window_placement: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            window: XenonWindowId,
            geometry: *const XenonWindowGeometry,
        ),
    >,
    pub window_frame_requested: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            window: XenonWindowId,
            request: u32,
            x: i32,
            y: i32,
            width: i32,
            height: i32,
        ),
    >,
    pub window_raise: Option<unsafe extern "C" fn(context: *mut c_void, window: XenonWindowId)>,
    pub confirm_stop_downloads: Option<unsafe extern "C" fn(context: *mut c_void, request: u64)>,
    pub quit_approved: Option<unsafe extern "C" fn(context: *mut c_void)>,
    pub devtools_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, enabled: bool, port: u16)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_window_callbacks(callbacks: *const XenonWindowCallbacks);
    pub fn xenon_engine_request_close_window(window: XenonWindowId);
    pub fn xenon_engine_request_quit();
    pub fn xenon_engine_answer_stop_downloads(request: u64, stop: bool);
    pub fn xenon_engine_report_window_geometry(
        window: XenonWindowId,
        geometry: *const XenonWindowGeometry,
    );
    pub fn xenon_engine_set_window_exposed(window: XenonWindowId, exposed: bool);
    pub fn xenon_engine_launch_devtools_client();
    pub fn xenon_engine_disable_devtools();
}

unsafe extern "C" {
    pub fn xenon_platform_attach_window(slot: u32, window_number: u32);
    pub fn xenon_platform_bind_window(slot: u32, window: XenonWindowId);
    pub fn xenon_platform_window_is_maximized(slot: u32) -> bool;
    pub fn xenon_platform_window_is_fullscreen(slot: u32) -> bool;
    pub fn xenon_platform_window_request(slot: u32, request: u32);
}
