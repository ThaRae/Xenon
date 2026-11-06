/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::{XenonStr, XenonViewId, XenonWindowId};

pub type XenonTabGroupId = u64;

pub const XENON_TAB_GROUP_NONE: XenonTabGroupId = 0;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonTabGroup {
    pub id: XenonTabGroupId,
    pub title: XenonStr,
    pub color: u32,
    pub collapsed: bool,
    pub tabs: *const XenonViewId,
    pub tab_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonTabGroupCallbacks {
    pub context: *mut c_void,
    pub groups_changed:
        Option<unsafe extern "C" fn(*mut c_void, XenonWindowId, *const XenonTabGroup, usize)>,
    pub edit_requested: Option<unsafe extern "C" fn(*mut c_void, XenonWindowId, XenonTabGroupId)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_tab_group_callbacks(callbacks: *const XenonTabGroupCallbacks);
    pub fn xenon_engine_add_tab_to_new_group(view: XenonViewId);
    pub fn xenon_engine_add_tab_to_group(view: XenonViewId, group: XenonTabGroupId);
    pub fn xenon_engine_remove_tab_from_group(view: XenonViewId);
    pub fn xenon_engine_set_tab_group_title(group: XenonTabGroupId, title: XenonStr);
    pub fn xenon_engine_set_tab_group_color(group: XenonTabGroupId, color: u32);
    pub fn xenon_engine_set_tab_group_collapsed(group: XenonTabGroupId, collapsed: bool);
    pub fn xenon_engine_ungroup_tabs(group: XenonTabGroupId);
    pub fn xenon_engine_close_tab_group(group: XenonTabGroupId);
    pub fn xenon_engine_new_tab_in_group(group: XenonTabGroupId);
    pub fn xenon_engine_move_tab_group_to_new_window(group: XenonTabGroupId);
    pub fn xenon_engine_drop_tab(view: XenonViewId, index: u32, group: XenonTabGroupId);
}
