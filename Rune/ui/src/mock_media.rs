/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Mutex;

use crate::ffi::{XenonMouseEvent, XenonViewId};
use crate::ffi_media::{XenonMediaCallbacks, XenonPictureInPicture};

struct SendableMediaCallbacks(XenonMediaCallbacks);

unsafe impl Send for SendableMediaCallbacks {}

static MEDIA_CALLBACKS: Mutex<Option<SendableMediaCallbacks>> = Mutex::new(None);

fn record(answer: String) {
    eprintln!("mock page: {answer}");
}

fn media_callbacks() -> Option<XenonMediaCallbacks> {
    MEDIA_CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .as_ref()
        .map(|callbacks| callbacks.0)
}

fn open_picture_in_picture(owner: XenonViewId) {
    let Some(callbacks) = media_callbacks() else {
        return;
    };
    let Some(shown) = callbacks.picture_in_picture_shown else {
        return;
    };
    let window = XenonPictureInPicture {
        id: crate::mock::xenon_engine_allocate_mock_view(),
        owner,
        width: 320.0,
        height: 180.0,
        minimum_width: 160.0,
        minimum_height: 90.0,
        maximum_width: 640.0,
        maximum_height: 360.0,
    };
    unsafe { shown(callbacks.context, &window) };
}

pub fn navigated(view: XenonViewId, url: &str) {
    if url.contains("picture-in-picture") {
        open_picture_in_picture(view);
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_media_callbacks(callbacks: *const XenonMediaCallbacks) {
    let callbacks =
        unsafe { callbacks.as_ref() }.map(|callbacks| SendableMediaCallbacks(*callbacks));
    *MEDIA_CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner()) = callbacks;
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_picture_in_picture_mouse_event(
    window: XenonViewId,
    event: *const XenonMouseEvent,
) {
    let Some(event) = (unsafe { event.as_ref() }) else {
        return;
    };
    if (event.r#type == crate::ffi::XENON_MOUSE_EVENT_DOWN
        || event.r#type == crate::ffi::XENON_MOUSE_EVENT_DOUBLE_CLICK)
        && let Some(callbacks) = media_callbacks()
        && let Some(handled) = callbacks.picture_in_picture_press_handled
    {
        unsafe { handled(callbacks.context, window, false) };
    }
    if event.r#type != crate::ffi::XENON_MOUSE_EVENT_MOVE {
        record(format!(
            "picture-in-picture mouse {} {:.0} {:.0}",
            event.r#type, event.x, event.y
        ));
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_picture_in_picture_cancel_press(_window: XenonViewId) {
    record("picture-in-picture cancel press".to_owned());
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_picture_in_picture_resize(
    _window: XenonViewId,
    width: f32,
    height: f32,
    _device_pixel_ratio: f64,
) {
    record(format!("picture-in-picture resize {width:.0}x{height:.0}"));
}
