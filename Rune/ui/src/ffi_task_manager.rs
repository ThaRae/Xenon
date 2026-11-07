/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::XenonStr;

pub const XENON_PROCESS_ICON_BROWSER: u32 = 0;
pub const XENON_PROCESS_ICON_PAGE: u32 = 1;
pub const XENON_PROCESS_ICON_SPARE: u32 = 2;
pub const XENON_PROCESS_ICON_SERVICE: u32 = 3;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonProcess {
    pub child_id: u64,
    pub pid: i64,
    pub icon: u32,
    pub is_browser: bool,
    pub can_end: bool,
    pub name: XenonStr,
    pub cpu_percent: f64,
    pub memory_bytes: u64,
    pub favicon_hash: XenonStr,
    pub favicon_png: *const u8,
    pub favicon_png_length: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonTaskManagerCallbacks {
    pub context: *mut c_void,
    pub open_requested: Option<unsafe extern "C" fn(*mut c_void)>,
    pub processes_changed: Option<unsafe extern "C" fn(*mut c_void, *const XenonProcess, usize)>,
    pub end_process_failed: Option<unsafe extern "C" fn(*mut c_void, XenonStr)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_task_manager_callbacks(callbacks: *const XenonTaskManagerCallbacks);
    pub fn xenon_engine_task_manager_set_visible(visible: bool);
    pub fn xenon_engine_task_manager_end_process(child_id: u64);
    pub fn xenon_engine_task_manager_copy_pid(child_id: u64);
}
