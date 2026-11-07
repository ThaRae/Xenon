/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use std::sync::Arc;

use crate::engine::{MouseEvent, ViewId};
use crate::ffi_media::{self, XenonMediaCallbacks, XenonPictureInPicture};
use crate::picture_in_picture::{self, Shown};
use crate::window_slots::Slots;

#[derive(Clone, Copy, Debug, Default)]
pub struct MediaEngine;

impl MediaEngine {
    pub fn picture_in_picture_mouse_event(self, window: ViewId, event: &MouseEvent) {
        let raw = event.raw();
        unsafe { ffi_media::xenon_engine_picture_in_picture_mouse_event(window, &raw) }
    }

    pub fn picture_in_picture_cancel_press(self, window: ViewId) {
        unsafe { ffi_media::xenon_engine_picture_in_picture_cancel_press(window) }
    }

    pub fn picture_in_picture_resize(self, window: ViewId, width: f32, height: f32, scale: f32) {
        unsafe {
            ffi_media::xenon_engine_picture_in_picture_resize(window, width, height, scale as f64)
        }
    }
}

pub struct MediaContext {
    slots: Arc<Slots>,
}

pub fn install(slots: Arc<Slots>) -> Box<MediaContext> {
    let context = Box::new(MediaContext { slots });
    let callbacks = XenonMediaCallbacks {
        context: std::ptr::from_ref::<MediaContext>(&context)
            .cast_mut()
            .cast(),
        picture_in_picture_shown: Some(picture_in_picture_shown),
        picture_in_picture_hidden: Some(picture_in_picture_hidden),
        picture_in_picture_press_handled: Some(picture_in_picture_press_handled),
    };
    unsafe { ffi_media::xenon_engine_set_media_callbacks(&callbacks) };
    context
}

unsafe fn slots<'a>(context: *mut c_void) -> &'a Slots {
    unsafe { &(*context.cast::<MediaContext>()).slots }
}

unsafe extern "C" fn picture_in_picture_shown(
    context: *mut c_void,
    window: *const XenonPictureInPicture,
) {
    let Some(window) = (unsafe { window.as_ref() }) else {
        return;
    };
    let shown = Shown {
        id: window.id,
        owner: window.owner,
        size: (window.width, window.height),
        minimum: (window.minimum_width, window.minimum_height),
        maximum: (window.maximum_width, window.maximum_height),
    };
    unsafe { slots(context) }.update_view(shown.owner, move |browser, _| {
        picture_in_picture::shown(browser, shown)
    });
}

unsafe extern "C" fn picture_in_picture_hidden(context: *mut c_void, window: ViewId) {
    unsafe { slots(context) }.update(move |browser, _| picture_in_picture::hidden(browser, window));
}

unsafe extern "C" fn picture_in_picture_press_handled(
    context: *mut c_void,
    window: ViewId,
    claimed_by_page: bool,
) {
    unsafe { slots(context) }.update(move |browser, _| {
        picture_in_picture::press_handled(browser, window, claimed_by_page)
    });
}
