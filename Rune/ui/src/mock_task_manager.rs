/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::{Mutex, MutexGuard};

use crate::ffi::XenonStr;
use crate::ffi_task_manager::{
    XENON_PROCESS_ICON_BROWSER, XENON_PROCESS_ICON_PAGE, XENON_PROCESS_ICON_SERVICE,
    XENON_PROCESS_ICON_SPARE, XenonProcess, XenonTaskManagerCallbacks,
};

struct SendableTaskManagerCallbacks(XenonTaskManagerCallbacks);

unsafe impl Send for SendableTaskManagerCallbacks {}

static CALLBACKS: Mutex<Option<SendableTaskManagerCallbacks>> = Mutex::new(None);

fn callbacks() -> MutexGuard<'static, Option<SendableTaskManagerCallbacks>> {
    CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn task_manager_callbacks() -> Option<XenonTaskManagerCallbacks> {
    callbacks().as_ref().map(|callbacks| callbacks.0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_task_manager_callbacks(
    callbacks: *const XenonTaskManagerCallbacks,
) {
    let callbacks =
        unsafe { callbacks.as_ref() }.map(|callbacks| SendableTaskManagerCallbacks(*callbacks));
    *self::callbacks() = callbacks;
}

fn mock_process(
    child_id: u64,
    pid: i64,
    icon: u32,
    name: &'static str,
    cpu_percent: f64,
    memory_bytes: u64,
) -> XenonProcess {
    XenonProcess {
        child_id,
        pid,
        icon,
        is_browser: icon == XENON_PROCESS_ICON_BROWSER,
        can_end: icon != XENON_PROCESS_ICON_BROWSER,
        name: XenonStr::borrowed(name),
        cpu_percent,
        memory_bytes,
        favicon_hash: XenonStr::borrowed(""),
        favicon_png: std::ptr::null(),
        favicon_png_length: 0,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_task_manager_set_visible(visible: bool) {
    let Some(callbacks) = task_manager_callbacks().filter(|_| visible) else {
        return;
    };
    let processes = [
        mock_process(
            0,
            4100,
            XENON_PROCESS_ICON_BROWSER,
            "Browser",
            3.25,
            182 << 20,
        ),
        mock_process(
            1,
            4102,
            XENON_PROCESS_ICON_SERVICE,
            "RequestServer",
            0.4,
            24 << 20,
        ),
        mock_process(
            2,
            4107,
            XENON_PROCESS_ICON_PAGE,
            "WebContent — example.com",
            12.5,
            96 << 20,
        ),
        mock_process(
            3,
            4111,
            XENON_PROCESS_ICON_SERVICE,
            "ImageDecoder — example.com",
            0.0,
            9 << 20,
        ),
        mock_process(
            4,
            4113,
            XENON_PROCESS_ICON_SPARE,
            "WebContent — (spare)",
            0.0,
            31 << 20,
        ),
    ];
    if let Some(changed) = callbacks.processes_changed {
        unsafe { changed(callbacks.context, processes.as_ptr(), processes.len()) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_task_manager_end_process(_child_id: u64) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_task_manager_copy_pid(_child_id: u64) {}
