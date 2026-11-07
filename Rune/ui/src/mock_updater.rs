/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use crate::ffi::XenonStr;

fn record(answer: String) {
    eprintln!("mock page: {answer}");
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_updater_callbacks(
    callbacks: *const crate::ffi_updater::XenonUpdaterCallbacks,
) {
    let callbacks = unsafe { callbacks.as_ref() }.copied();
    if let Some(callbacks) = callbacks
        && let Some(changed) = callbacks.status_changed
        && let Ok(version) = std::env::var("XENON_TEST_UPDATE_READY")
    {
        unsafe { changed(callbacks.context, XenonStr::borrowed(&version), false) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_install_update() {
    record("install update".to_owned());
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_check_for_updates() {
    record("check for updates".to_owned());
}
