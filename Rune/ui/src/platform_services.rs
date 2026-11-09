/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

unsafe extern "C" {
    fn xenon_engine_set_page_origin(x: f32, y: f32);
}

pub fn set_page_origin(x: f32, y: f32) {
    unsafe { xenon_engine_set_page_origin(x, y) }
}
