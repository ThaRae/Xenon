/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::{XenonStr, XenonViewId};

pub const XENON_ACCESSIBILITY_FOCUSED: u32 = 1 << 0;
pub const XENON_ACCESSIBILITY_DISABLED: u32 = 1 << 1;
pub const XENON_ACCESSIBILITY_SELECTED: u32 = 1 << 2;
pub const XENON_ACCESSIBILITY_EDITABLE: u32 = 1 << 3;
pub const XENON_ACCESSIBILITY_MULTI_LINE: u32 = 1 << 4;
pub const XENON_ACCESSIBILITY_PASSWORD: u32 = 1 << 5;
pub const XENON_ACCESSIBILITY_READ_ONLY: u32 = 1 << 6;
pub const XENON_ACCESSIBILITY_REQUIRED: u32 = 1 << 7;
pub const XENON_ACCESSIBILITY_INVALID: u32 = 1 << 8;
pub const XENON_ACCESSIBILITY_VISITED: u32 = 1 << 9;
pub const XENON_ACCESSIBILITY_MULTI_SELECTABLE: u32 = 1 << 10;
pub const XENON_ACCESSIBILITY_HAS_NUMERIC_VALUE: u32 = 1 << 11;

pub const XENON_ACCESSIBILITY_TOGGLE_NONE: u8 = 0;
pub const XENON_ACCESSIBILITY_TOGGLE_OFF: u8 = 1;
pub const XENON_ACCESSIBILITY_TOGGLE_ON: u8 = 2;
pub const XENON_ACCESSIBILITY_TOGGLE_MIXED: u8 = 3;

pub const XENON_ACCESSIBILITY_EXPANDED_NONE: u8 = 0;
pub const XENON_ACCESSIBILITY_EXPANDED_COLLAPSED: u8 = 1;
pub const XENON_ACCESSIBILITY_EXPANDED_EXPANDED: u8 = 2;

pub const XENON_ACCESSIBILITY_ACTION_PRESS: u32 = 0;
pub const XENON_ACCESSIBILITY_ACTION_FOCUS: u32 = 1;
pub const XENON_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW: u32 = 2;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonAccessibilityNode {
    pub id: i64,
    pub parent_id: i64,
    pub child_ids: *const i64,
    pub child_count: usize,
    pub role: XenonStr,
    pub name: XenonStr,
    pub description: XenonStr,
    pub value: XenonStr,
    pub url: XenonStr,
    pub live: XenonStr,
    pub keybinding: XenonStr,
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
    pub flags: u32,
    pub checked: u8,
    pub pressed: u8,
    pub expanded: u8,
    pub level: i32,
    pub value_now: f64,
    pub value_min: f64,
    pub value_max: f64,
    pub value_step: f64,
    pub caret_offset: i32,
    pub selection_start: i32,
    pub selection_end: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonAccessibilityTree {
    pub root_id: i64,
    pub focused_id: i64,
    pub zoom: f64,
    pub nodes: *const XenonAccessibilityNode,
    pub node_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonAccessibilityCallbacks {
    pub context: *mut c_void,
    pub tree_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            tree: *const XenonAccessibilityTree,
        ),
    >,
    pub focus_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, node: i64)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_accessibility_callbacks(callbacks: *const XenonAccessibilityCallbacks);
    pub fn xenon_engine_set_accessibility_enabled(enabled: bool);
    pub fn xenon_engine_request_accessibility_tree(view: XenonViewId);
    pub fn xenon_engine_accessibility_action(view: XenonViewId, node: i64, action: u32);
    pub fn xenon_engine_accessibility_set_value(view: XenonViewId, node: i64, value: XenonStr);
    pub fn xenon_accessibility_assistive_technology_running() -> bool;
}
