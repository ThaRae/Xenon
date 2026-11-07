/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Mutex;

use crate::ffi::XenonWindowId;
use crate::ffi_windows::{XenonWindowCallbacks, XenonWindowGeometry};

struct SendableCallbacks(XenonWindowCallbacks);

unsafe impl Send for SendableCallbacks {}

static CALLBACKS: Mutex<Option<SendableCallbacks>> = Mutex::new(None);

fn callbacks() -> Option<XenonWindowCallbacks> {
    CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .as_ref()
        .map(|callbacks| callbacks.0)
}

const CASCADE_OFFSET: i32 = 40;
const FIRST_WINDOW_ORIGIN: (i32, i32) = (60, 60);

pub fn place_window(window: XenonWindowId) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let Some(placement) = callbacks.window_placement else {
        return;
    };
    let step = i32::try_from(window.saturating_sub(1)).unwrap_or(0) % 8;
    let geometry = XenonWindowGeometry {
        x: FIRST_WINDOW_ORIGIN.0 + CASCADE_OFFSET * step,
        y: FIRST_WINDOW_ORIGIN.1 + CASCADE_OFFSET * step,
        width: 1000,
        height: 640,
        maximized: false,
        has_position: true,
    };
    unsafe { placement(callbacks.context, window, &geometry) };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_window_callbacks(callbacks: *const XenonWindowCallbacks) {
    *CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner()) =
        unsafe { callbacks.as_ref() }.map(|callbacks| SendableCallbacks(*callbacks));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_request_close_window(window: XenonWindowId) {
    println!("mock windows: close window {window}");
    crate::mock::xenon_engine_close_window(window);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_request_quit() {
    println!("mock windows: quit");
    if let Some(callbacks) = callbacks()
        && let Some(quit_approved) = callbacks.quit_approved
    {
        unsafe { quit_approved(callbacks.context) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_answer_stop_downloads(request: u64, stop: bool) {
    println!("mock windows: stop downloads {request} {stop}");
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_report_window_geometry(
    window: XenonWindowId,
    geometry: *const XenonWindowGeometry,
) {
    if let Some(geometry) = unsafe { geometry.as_ref() } {
        println!(
            "mock windows: window {window} at {},{} {}x{}{}",
            geometry.x,
            geometry.y,
            geometry.width,
            geometry.height,
            if geometry.maximized { " maximized" } else { "" }
        );
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_window_exposed(_window: XenonWindowId, _exposed: bool) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_launch_devtools_client() {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_disable_devtools() {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_platform_attach_window(_slot: u32, _window_number: u32) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_platform_bind_window(_slot: u32, _window: XenonWindowId) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_platform_window_is_maximized(_slot: u32) -> bool {
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_platform_window_is_fullscreen(_slot: u32) -> bool {
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_platform_window_request(_slot: u32, _request: u32) {}
