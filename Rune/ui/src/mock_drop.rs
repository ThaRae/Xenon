/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use crate::ffi::{XenonStr, XenonViewId};

fn record(answer: String) {
    eprintln!("mock page: {answer}");
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_drag_event(
    _view: XenonViewId,
    r#type: u32,
    x: f32,
    y: f32,
    _modifiers: u32,
    paths: *const XenonStr,
    path_count: usize,
) {
    let paths: Vec<String> = if paths.is_null() {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(paths, path_count) }
            .iter()
            .map(|path| unsafe { path.to_string_lossy() })
            .collect()
    };
    record(format!("drag {type} {x:.0} {y:.0} {}", paths.join(",")));
}
