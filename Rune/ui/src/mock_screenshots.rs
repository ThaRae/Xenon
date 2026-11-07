/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Mutex;

use crate::ffi::{XenonStr, XenonViewId};

fn record(answer: String) {
    eprintln!("mock page: {answer}");
}

pub fn navigated(view: XenonViewId, url: &str) {
    if url.contains("screenshot") {
        request_screenshot(view);
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_screenshot_callbacks(
    callbacks: *const crate::ffi_screenshots::XenonScreenshotCallbacks,
) {
    let callbacks = unsafe { callbacks.as_ref() }.copied();
    *SCREENSHOT_CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner()) =
        callbacks.map(SendableScreenshotCallbacks);
}

struct SendableScreenshotCallbacks(crate::ffi_screenshots::XenonScreenshotCallbacks);

unsafe impl Send for SendableScreenshotCallbacks {}

static SCREENSHOT_CALLBACKS: Mutex<Option<SendableScreenshotCallbacks>> = Mutex::new(None);

fn request_screenshot(view: XenonViewId) {
    let callbacks = SCREENSHOT_CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .as_ref()
        .map(|callbacks| callbacks.0);
    if let Some(callbacks) = callbacks
        && let Some(requested) = callbacks.selection_requested
    {
        let surface = crate::mock::xenon_engine_allocate_mock_view();
        unsafe { requested(callbacks.context, view, surface) };
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_answer_save_location(
    request: u64,
    chosen: bool,
    path: XenonStr,
) {
    let path = unsafe { path.to_string_lossy() };
    record(format!("save location {request} {chosen} {path}"));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_refresh_screenshot_frame(_view: XenonViewId) {
    record("screenshot refresh".to_owned());
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_finish_screenshot(
    _view: XenonViewId,
    answer: u32,
    x: f32,
    y: f32,
    width: f32,
    height: f32,
) {
    record(format!(
        "screenshot {answer} {x:.0} {y:.0} {width:.0} {height:.0}"
    ));
}
