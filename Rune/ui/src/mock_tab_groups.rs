/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Mutex;

use crate::ffi::{XenonStr, XenonViewId, XenonWindowId};
use crate::ffi_tab_groups::{XenonTabGroup, XenonTabGroupCallbacks, XenonTabGroupId};

const MOCK_WINDOW: XenonWindowId = 1;

struct MockGroup {
    id: XenonTabGroupId,
    title: String,
    color: u32,
    collapsed: bool,
    tabs: Vec<XenonViewId>,
}

struct SendableCallbacks(XenonTabGroupCallbacks);

unsafe impl Send for SendableCallbacks {}

#[derive(Default)]
struct MockGroups {
    callbacks: Option<SendableCallbacks>,
    groups: Vec<MockGroup>,
    next_id: XenonTabGroupId,
}

static STATE: Mutex<Option<MockGroups>> = Mutex::new(None);

fn change(f: impl FnOnce(&mut MockGroups) -> Option<XenonTabGroupId>) {
    let mut guard = STATE
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner());
    let state = guard.get_or_insert_with(MockGroups::default);
    let edit = f(state);
    state.groups.retain(|group| !group.tabs.is_empty());
    let Some(SendableCallbacks(callbacks)) = &state.callbacks else {
        return;
    };
    let raw: Vec<XenonTabGroup> = state
        .groups
        .iter()
        .map(|group| XenonTabGroup {
            id: group.id,
            title: XenonStr::borrowed(&group.title),
            color: group.color,
            collapsed: group.collapsed,
            tabs: group.tabs.as_ptr(),
            tab_count: group.tabs.len(),
        })
        .collect();
    if let Some(groups_changed) = callbacks.groups_changed {
        unsafe { groups_changed(callbacks.context, MOCK_WINDOW, raw.as_ptr(), raw.len()) };
    }
    if let (Some(group), Some(edit_requested)) = (edit, callbacks.edit_requested) {
        unsafe { edit_requested(callbacks.context, MOCK_WINDOW, group) };
    }
}

fn with_group(group: XenonTabGroupId, f: impl FnOnce(&mut MockGroup)) {
    change(|state| {
        if let Some(found) = state.groups.iter_mut().find(|other| other.id == group) {
            f(found);
        }
        None
    });
}

fn leave_groups(state: &mut MockGroups, view: XenonViewId) {
    for group in &mut state.groups {
        group.tabs.retain(|tab| *tab != view);
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_tab_group_callbacks(
    callbacks: *const XenonTabGroupCallbacks,
) {
    let callbacks = unsafe { callbacks.as_ref() }.map(|callbacks| SendableCallbacks(*callbacks));
    STATE
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .get_or_insert_with(MockGroups::default)
        .callbacks = callbacks;
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_add_tab_to_new_group(view: XenonViewId) {
    change(|state| {
        leave_groups(state, view);
        state.next_id += 1;
        let id = state.next_id;
        let color = (state.groups.len() % 9) as u32;
        state.groups.push(MockGroup {
            id,
            title: String::new(),
            color,
            collapsed: false,
            tabs: vec![view],
        });
        Some(id)
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_add_tab_to_group(view: XenonViewId, group: XenonTabGroupId) {
    change(|state| {
        leave_groups(state, view);
        if let Some(found) = state.groups.iter_mut().find(|other| other.id == group) {
            found.tabs.push(view);
        }
        None
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_remove_tab_from_group(view: XenonViewId) {
    change(|state| {
        leave_groups(state, view);
        None
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_tab_group_title(group: XenonTabGroupId, title: XenonStr) {
    let title = unsafe { title.to_string_lossy() };
    with_group(group, |found| found.title = title);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_tab_group_color(group: XenonTabGroupId, color: u32) {
    with_group(group, |found| found.color = color);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_tab_group_collapsed(group: XenonTabGroupId, collapsed: bool) {
    with_group(group, |found| found.collapsed = collapsed);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_ungroup_tabs(group: XenonTabGroupId) {
    with_group(group, |found| found.tabs.clear());
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_close_tab_group(group: XenonTabGroupId) {
    xenon_engine_ungroup_tabs(group);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_new_tab_in_group(_group: XenonTabGroupId) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_move_tab_group_to_new_window(_group: XenonTabGroupId) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_drop_tab(view: XenonViewId, _index: u32, group: XenonTabGroupId) {
    if group == 0 {
        xenon_engine_remove_tab_from_group(view);
    } else {
        xenon_engine_add_tab_to_group(view, group);
    }
}
