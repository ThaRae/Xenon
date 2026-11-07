/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::{XenonMouseEvent, XenonViewId};

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonPictureInPicture {
    pub id: XenonViewId,
    pub owner: XenonViewId,
    pub width: f32,
    pub height: f32,
    pub minimum_width: f32,
    pub minimum_height: f32,
    pub maximum_width: f32,
    pub maximum_height: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonMediaCallbacks {
    pub context: *mut c_void,
    pub picture_in_picture_shown:
        Option<unsafe extern "C" fn(context: *mut c_void, window: *const XenonPictureInPicture)>,
    pub picture_in_picture_hidden:
        Option<unsafe extern "C" fn(context: *mut c_void, window: XenonViewId)>,
    pub picture_in_picture_press_handled: Option<
        unsafe extern "C" fn(context: *mut c_void, window: XenonViewId, claimed_by_page: bool),
    >,
}

unsafe extern "C" {
    pub fn xenon_engine_set_media_callbacks(callbacks: *const XenonMediaCallbacks);

    pub fn xenon_engine_picture_in_picture_mouse_event(
        window: XenonViewId,
        event: *const XenonMouseEvent,
    );
    pub fn xenon_engine_picture_in_picture_cancel_press(window: XenonViewId);
    pub fn xenon_engine_picture_in_picture_resize(
        window: XenonViewId,
        width: f32,
        height: f32,
        device_pixel_ratio: f64,
    );
}
