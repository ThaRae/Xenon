/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;
use std::sync::Arc;

use crate::ffi::XenonViewId;
use crate::window_slots::Slots;

pub const XENON_SPLIT_SIDE_LEFT: u32 = 0;
pub const XENON_SPLIT_SIDE_RIGHT: u32 = 1;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonSplit {
    pub left: XenonViewId,
    pub right: XenonViewId,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonSplitViewCallbacks {
    pub context: *mut c_void,
    pub splits_changed:
        Option<unsafe extern "C" fn(context: *mut c_void, splits: *const XenonSplit, count: usize)>,
    pub drag_and_drop_changed: Option<unsafe extern "C" fn(context: *mut c_void, enabled: bool)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_split_view_callbacks(callbacks: *const XenonSplitViewCallbacks);
    pub fn xenon_engine_split_view_open_new_tab(beside: XenonViewId, new_tab_side: u32);
    pub fn xenon_engine_split_view_create(
        anchor: XenonViewId,
        other: XenonViewId,
        other_side: u32,
        activate: XenonViewId,
    );
    pub fn xenon_engine_split_view_reverse(view: XenonViewId);
    pub fn xenon_engine_split_view_separate(view: XenonViewId);
    pub fn xenon_engine_split_view_drop_tab(dragged: XenonViewId, side: u32);
}

pub struct SplitViewContext {
    slots: Arc<Slots>,
}

impl SplitViewContext {
    pub fn new(slots: Arc<Slots>) -> Arc<SplitViewContext> {
        Arc::new(SplitViewContext { slots })
    }

    pub fn register(self: &Arc<Self>) {
        let callbacks = XenonSplitViewCallbacks {
            context: Arc::as_ptr(self).cast_mut().cast(),
            splits_changed: Some(splits_changed),
            drag_and_drop_changed: Some(drag_and_drop_changed),
        };
        unsafe { xenon_engine_set_split_view_callbacks(&callbacks) }
    }
}

unsafe fn slots<'a>(context: *mut c_void) -> &'a Slots {
    unsafe { &(*context.cast::<SplitViewContext>()).slots }
}

pub fn pairs_from(splits: *const XenonSplit, count: usize) -> Vec<(XenonViewId, XenonViewId)> {
    if splits.is_null() || count == 0 {
        return Vec::new();
    }
    unsafe { std::slice::from_raw_parts(splits, count) }
        .iter()
        .map(|split| (split.left, split.right))
        .collect()
}

unsafe extern "C" fn splits_changed(context: *mut c_void, splits: *const XenonSplit, count: usize) {
    let pairs = pairs_from(splits, count);
    unsafe { slots(context) }
        .update(move |browser, _| crate::split_view::splits_changed(browser, &pairs));
}

unsafe extern "C" fn drag_and_drop_changed(context: *mut c_void, enabled: bool) {
    unsafe { slots(context) }.update(move |browser, _| browser.split.drag_and_drop = enabled);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn splits_cross_as_pairs() {
        let splits = [
            XenonSplit { left: 1, right: 2 },
            XenonSplit { left: 7, right: 5 },
        ];
        assert_eq!(pairs_from(splits.as_ptr(), 2), [(1, 2), (7, 5)]);
        assert!(pairs_from(std::ptr::null(), 3).is_empty());
    }
}
