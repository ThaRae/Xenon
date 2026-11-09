/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::XenonStr;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonUpdaterCallbacks {
    pub context: *mut c_void,
    pub status_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, ready_version: XenonStr, checking: bool)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_updater_callbacks(callbacks: *const XenonUpdaterCallbacks);
    pub fn xenon_engine_install_update();
    pub fn xenon_engine_check_for_updates();
}
