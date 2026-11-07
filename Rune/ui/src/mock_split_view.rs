/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::{Mutex, MutexGuard};

use crate::ffi::{XENON_TAB_INDEX_END, XENON_WINDOW_NONE, XenonStr, XenonViewId};
use crate::ffi_split_view::{XENON_SPLIT_SIDE_RIGHT, XenonSplit, XenonSplitViewCallbacks};

struct SendableCallbacks(XenonSplitViewCallbacks);

unsafe impl Send for SendableCallbacks {}

#[derive(Default)]
struct MockSplits {
    callbacks: Option<SendableCallbacks>,
    splits: Vec<XenonSplit>,
}

static STATE: Mutex<Option<MockSplits>> = Mutex::new(None);

fn state() -> MutexGuard<'static, Option<MockSplits>> {
    STATE
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn update(change: impl FnOnce(&mut Vec<XenonSplit>)) {
    let mut guard = state();
    let mock = guard.get_or_insert_with(MockSplits::default);
    change(&mut mock.splits);
    let Some(SendableCallbacks(callbacks)) = &mock.callbacks else {
        return;
    };
    if let Some(splits_changed) = callbacks.splits_changed {
        unsafe { splits_changed(callbacks.context, mock.splits.as_ptr(), mock.splits.len()) };
    }
}

fn pair(anchor: XenonViewId, other: XenonViewId, other_side: u32) -> XenonSplit {
    if other_side == XENON_SPLIT_SIDE_RIGHT {
        XenonSplit {
            left: anchor,
            right: other,
        }
    } else {
        XenonSplit {
            left: other,
            right: anchor,
        }
    }
}

fn without(splits: &mut Vec<XenonSplit>, views: &[XenonViewId]) {
    splits.retain(|split| !views.contains(&split.left) && !views.contains(&split.right));
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_split_view_callbacks(
    callbacks: *const XenonSplitViewCallbacks,
) {
    let callbacks = unsafe { callbacks.as_ref() }.map(|callbacks| SendableCallbacks(*callbacks));
    state().get_or_insert_with(MockSplits::default).callbacks = callbacks;
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_split_view_open_new_tab(beside: XenonViewId, new_tab_side: u32) {
    let opened = unsafe {
        crate::mock::xenon_engine_new_tab(
            XENON_WINDOW_NONE,
            XenonStr::borrowed(""),
            XENON_TAB_INDEX_END,
            true,
        )
    };
    update(|splits| {
        without(splits, &[beside]);
        splits.push(pair(beside, opened, new_tab_side));
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_split_view_create(
    anchor: XenonViewId,
    other: XenonViewId,
    other_side: u32,
    activate: XenonViewId,
) {
    update(|splits| {
        without(splits, &[anchor, other]);
        splits.push(pair(anchor, other, other_side));
    });
    crate::mock::xenon_engine_activate_tab(activate);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_split_view_reverse(view: XenonViewId) {
    update(|splits| {
        if let Some(split) = splits
            .iter_mut()
            .find(|split| split.left == view || split.right == view)
        {
            std::mem::swap(&mut split.left, &mut split.right);
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_split_view_separate(view: XenonViewId) {
    update(|splits| without(splits, &[view]));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_split_view_drop_tab(dragged: XenonViewId, side: u32) {
    println!("mock split view: drop tab {dragged} on side {side}");
}
