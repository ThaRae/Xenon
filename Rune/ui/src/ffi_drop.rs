/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use crate::ffi::{XenonStr, XenonViewId};

pub const XENON_DRAG_ENTER: u32 = 0;
pub const XENON_DRAG_MOVE: u32 = 1;
pub const XENON_DRAG_LEAVE: u32 = 2;
pub const XENON_DRAG_DROP: u32 = 3;

unsafe extern "C" {
    pub fn xenon_engine_drag_event(
        view: XenonViewId,
        r#type: u32,
        x: f32,
        y: f32,
        modifiers: u32,
        paths: *const XenonStr,
        path_count: usize,
    );
}
