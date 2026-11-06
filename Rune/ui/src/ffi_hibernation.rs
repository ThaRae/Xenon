/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::XenonViewId;

pub const XENON_TAB_LOADED: u32 = 0;
pub const XENON_TAB_NOT_LOADED: u32 = 1;
pub const XENON_TAB_HIBERNATED: u32 = 2;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonTabIcon {
    pub width: u32,
    pub height: u32,
    pub rgba: *const u8,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonHibernationSettings {
    pub enabled: bool,
    pub timeout_seconds: u64,
    pub timeout_choices_in_minutes: *const u32,
    pub timeout_choice_count: usize,
    pub hibernate_on_memory_pressure: bool,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonHibernationCallbacks {
    pub context: *mut c_void,
    pub tab_load_state_changed:
        Option<unsafe extern "C" fn(*mut c_void, XenonViewId, u32, *const XenonTabIcon)>,
    pub settings_changed:
        Option<unsafe extern "C" fn(*mut c_void, *const XenonHibernationSettings)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_hibernation_callbacks(callbacks: *const XenonHibernationCallbacks);
    pub fn xenon_engine_hibernate_tab(view: XenonViewId);
    pub fn xenon_engine_set_tab_hibernation(enabled: bool, timeout_seconds: u64);
}
