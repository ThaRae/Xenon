/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use crate::ffi;

pub fn is_fullscreen() -> bool {
    unsafe { ffi::xenon_platform_is_fullscreen() }
}

pub fn set_fullscreen(fullscreen: bool) {
    unsafe { ffi::xenon_platform_set_fullscreen(fullscreen) }
}

pub fn is_always_on_top() -> bool {
    unsafe { ffi::xenon_platform_is_always_on_top() }
}

pub fn set_always_on_top(always_on_top: bool) {
    unsafe { ffi::xenon_platform_set_always_on_top(always_on_top) }
}
