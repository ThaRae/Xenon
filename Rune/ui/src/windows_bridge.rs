/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;
use std::sync::Arc;

use crate::ffi::XenonWindowId;
use crate::ffi_windows::{self, XenonWindowCallbacks, XenonWindowGeometry};
use crate::window_slots::{Placement, Slots};

pub fn install(slots: &Arc<Slots>) {
    let callbacks = XenonWindowCallbacks {
        context: Arc::as_ptr(slots).cast_mut().cast(),
        window_placement: Some(window_placement),
        window_frame_requested: Some(window_frame_requested),
        window_raise: Some(window_raise),
        confirm_stop_downloads: Some(confirm_stop_downloads),
        quit_approved: Some(quit_approved),
        devtools_changed: Some(devtools_changed),
    };
    unsafe { ffi_windows::xenon_engine_set_window_callbacks(&callbacks) };
}

unsafe fn slots<'a>(context: *mut c_void) -> &'a Slots {
    unsafe { &*context.cast::<Slots>() }
}

unsafe extern "C" fn window_placement(
    context: *mut c_void,
    window: XenonWindowId,
    geometry: *const XenonWindowGeometry,
) {
    let Some(geometry) = (unsafe { geometry.as_ref() }) else {
        return;
    };
    unsafe { slots(context) }.set_placement(
        window,
        Placement {
            x: geometry.x,
            y: geometry.y,
            width: geometry.width,
            height: geometry.height,
            maximized: geometry.maximized,
            has_position: geometry.has_position,
        },
    );
}

unsafe extern "C" fn window_frame_requested(
    context: *mut c_void,
    window: XenonWindowId,
    request: u32,
    x: i32,
    y: i32,
    width: i32,
    height: i32,
) {
    unsafe { slots(context) }.update_window(window, move |browser, _| {
        browser.window_frame_requested(request, x, y, width, height)
    });
}

unsafe extern "C" fn window_raise(context: *mut c_void, window: XenonWindowId) {
    let slots = unsafe { slots(context) };
    let slot = slots.table().slot_of(window);
    if let Some(slot) = slot {
        slots.show(slot);
    }
}

unsafe extern "C" fn confirm_stop_downloads(context: *mut c_void, request: u64) {
    unsafe { slots(context) }
        .update_target(move |browser, _| browser.confirm_stop_downloads(request));
}

unsafe extern "C" fn quit_approved(context: *mut c_void) {
    unsafe { slots(context) }.update_target(|browser, tx| browser.quit_approved(tx));
}

unsafe extern "C" fn devtools_changed(context: *mut c_void, enabled: bool, port: u16) {
    unsafe { slots(context) }.update(move |browser, _| browser.devtools_changed(enabled, port));
}
