/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;
use std::sync::Arc;

use crate::engine::WindowId;
use crate::ffi_tab_groups::{self, XenonTabGroup, XenonTabGroupCallbacks, XenonTabGroupId};
use crate::tab_groups::{self, Group, GroupColor};
use crate::window_slots::Slots;

pub struct TabGroupContext {
    handle: Arc<Slots>,
}

pub fn install(handle: Arc<Slots>) -> Box<TabGroupContext> {
    let context = Box::new(TabGroupContext { handle });
    let callbacks = XenonTabGroupCallbacks {
        context: std::ptr::from_ref::<TabGroupContext>(&context)
            .cast_mut()
            .cast(),
        groups_changed: Some(groups_changed),
        edit_requested: Some(edit_requested),
    };
    unsafe { ffi_tab_groups::xenon_engine_set_tab_group_callbacks(&callbacks) };
    context
}

unsafe fn handle<'a>(context: *mut c_void) -> &'a Slots {
    unsafe { &(*context.cast::<TabGroupContext>()).handle }
}

fn group_from(raw: &XenonTabGroup) -> Group {
    let tabs = if raw.tabs.is_null() || raw.tab_count == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(raw.tabs, raw.tab_count) }.to_vec()
    };
    Group {
        id: raw.id,
        title: unsafe { raw.title.to_string_lossy() },
        color: GroupColor::from_raw(raw.color),
        collapsed: raw.collapsed,
        tabs,
    }
}

unsafe extern "C" fn groups_changed(
    context: *mut c_void,
    window: WindowId,
    groups: *const XenonTabGroup,
    group_count: usize,
) {
    let groups: Vec<Group> = if groups.is_null() || group_count == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(groups, group_count) }
            .iter()
            .map(group_from)
            .collect()
    };
    unsafe { handle(context) }
        .update(move |browser, _| browser.tab_groups.groups_changed(window, groups.clone()));
}

unsafe extern "C" fn edit_requested(
    context: *mut c_void,
    window: WindowId,
    group: XenonTabGroupId,
) {
    unsafe { handle(context) }.update_window(window, move |browser, _| {
        tab_groups::edit_requested(browser, window, group)
    });
}
