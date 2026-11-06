/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::{Mutex, MutexGuard};

use crate::ffi::XenonViewId;
use crate::ffi_hibernation::{
    XENON_TAB_HIBERNATED, XenonHibernationCallbacks, XenonHibernationSettings,
};

const TIMEOUT_CHOICES_IN_MINUTES: [u32; 6] = [15, 30, 60, 2 * 60, 4 * 60, 6 * 60];

struct SendableHibernationCallbacks(XenonHibernationCallbacks);

unsafe impl Send for SendableHibernationCallbacks {}

static CALLBACKS: Mutex<Option<SendableHibernationCallbacks>> = Mutex::new(None);

fn callbacks() -> MutexGuard<'static, Option<SendableHibernationCallbacks>> {
    CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn hibernation_callbacks() -> Option<XenonHibernationCallbacks> {
    callbacks().as_ref().map(|callbacks| callbacks.0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_hibernation_callbacks(
    callbacks: *const XenonHibernationCallbacks,
) {
    let callbacks =
        unsafe { callbacks.as_ref() }.map(|callbacks| SendableHibernationCallbacks(*callbacks));
    *self::callbacks() = callbacks;
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_hibernate_tab(view: XenonViewId) {
    let Some(callbacks) = hibernation_callbacks() else {
        return;
    };
    if let Some(changed) = callbacks.tab_load_state_changed {
        unsafe {
            changed(
                callbacks.context,
                view,
                XENON_TAB_HIBERNATED,
                std::ptr::null(),
            )
        };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_tab_hibernation(enabled: bool, timeout_seconds: u64) {
    let Some(callbacks) = hibernation_callbacks() else {
        return;
    };
    let settings = XenonHibernationSettings {
        enabled,
        timeout_seconds: if timeout_seconds == 0 {
            2 * 60 * 60
        } else {
            timeout_seconds
        },
        timeout_choices_in_minutes: TIMEOUT_CHOICES_IN_MINUTES.as_ptr(),
        timeout_choice_count: TIMEOUT_CHOICES_IN_MINUTES.len(),
        hibernate_on_memory_pressure: true,
    };
    if let Some(changed) = callbacks.settings_changed {
        unsafe { changed(callbacks.context, &settings) };
    }
}
