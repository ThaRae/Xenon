/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::{XenonStr, XenonViewId};

pub const XENON_SCREENSHOT_SELECTION: u32 = 0;
pub const XENON_SCREENSHOT_VISIBLE_PAGE: u32 = 1;
pub const XENON_SCREENSHOT_FULL_PAGE: u32 = 2;
pub const XENON_SCREENSHOT_CANCEL: u32 = 3;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonScreenshotCallbacks {
    pub context: *mut c_void,
    pub selection_requested: Option<
        unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, frame_surface: XenonViewId),
    >,
    pub save_location_requested: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            request: u64,
            directory: XenonStr,
            file_name: XenonStr,
        ),
    >,
}

unsafe extern "C" {
    pub fn xenon_engine_set_screenshot_callbacks(callbacks: *const XenonScreenshotCallbacks);
    pub fn xenon_engine_refresh_screenshot_frame(view: XenonViewId);
    pub fn xenon_engine_answer_save_location(request: u64, chosen: bool, path: XenonStr);
    pub fn xenon_engine_finish_screenshot(
        view: XenonViewId,
        answer: u32,
        x: f32,
        y: f32,
        width: f32,
        height: f32,
    );
}
