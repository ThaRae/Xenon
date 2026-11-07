/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use std::sync::Arc;

use crate::ffi::XenonStr;
use crate::ffi_updater::{self, XenonUpdaterCallbacks};
use crate::state::Browser;
use crate::window_slots::Slots;

#[derive(Default)]
pub struct Updates {
    ready_version: Option<String>,
    put_off_version: Option<String>,
    pub popover_open: bool,
}

impl Updates {
    pub fn offered_version(&self) -> Option<&str> {
        self.ready_version
            .as_deref()
            .filter(|version| self.put_off_version.as_deref() != Some(*version))
    }

    pub fn status_changed(&mut self, ready_version: Option<String>) {
        if ready_version != self.ready_version && ready_version.is_some() {
            eprintln!(
                "The toolbar offers to relaunch to update to Xenon {}",
                ready_version.as_deref().unwrap_or_default()
            );
        }
        self.ready_version = ready_version;
        if self.offered_version().is_none() {
            self.popover_open = false;
        }
    }

    pub fn put_off(&mut self) {
        self.put_off_version = self.ready_version.clone();
        self.popover_open = false;
    }
}

pub fn popover_body(private_windows: usize) -> String {
    let reopen = match private_windows {
        0 => "your windows and tabs reopen",
        1 => "your windows and tabs reopen, but your private window doesn't",
        _ => "your windows and tabs reopen, but private windows don't",
    };
    format!(
        "Relaunch to finish updating, and {reopen}. Otherwise, it installs when you quit Xenon."
    )
}

pub fn toggle_popover(s: &mut Browser) {
    s.updates.popover_open = !s.updates.popover_open && s.updates.offered_version().is_some();
}

pub fn close_popover(s: &mut Browser) {
    s.updates.popover_open = false;
}

pub fn later(s: &mut Browser) {
    s.updates.put_off();
}

pub fn check_or_offer(s: &mut Browser) {
    if s.updates.ready_version.is_some() {
        s.updates.put_off_version = None;
        s.updates.popover_open = true;
        return;
    }
    unsafe { ffi_updater::xenon_engine_check_for_updates() }
}

pub fn relaunch(s: &mut Browser) {
    s.updates.popover_open = false;
    unsafe { ffi_updater::xenon_engine_install_update() }
}

pub struct UpdaterContext {
    slots: Arc<Slots>,
}

pub fn install(slots: Arc<Slots>) -> Box<UpdaterContext> {
    let context = Box::new(UpdaterContext { slots });
    let callbacks = XenonUpdaterCallbacks {
        context: std::ptr::from_ref::<UpdaterContext>(&context)
            .cast_mut()
            .cast(),
        status_changed: Some(status_changed),
    };
    unsafe { ffi_updater::xenon_engine_set_updater_callbacks(&callbacks) };
    context
}

unsafe extern "C" fn status_changed(
    context: *mut c_void,
    ready_version: XenonStr,
    _checking: bool,
) {
    let ready_version = unsafe { ready_version.to_string_lossy() };
    let ready_version = (!ready_version.is_empty()).then_some(ready_version);
    let slots = unsafe { &(*context.cast::<UpdaterContext>()).slots };
    slots.update(move |browser, _| browser.updates.status_changed(ready_version.clone()));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn later_hides_the_update_until_a_newer_one_is_ready() {
        let mut updates = Updates::default();
        assert_eq!(updates.offered_version(), None);
        updates.status_changed(Some("1.2.0".to_owned()));
        assert_eq!(updates.offered_version(), Some("1.2.0"));
        updates.popover_open = true;
        updates.put_off();
        assert_eq!(updates.offered_version(), None);
        assert!(!updates.popover_open);
        updates.status_changed(Some("1.3.0".to_owned()));
        assert_eq!(updates.offered_version(), Some("1.3.0"));
    }

    #[test]
    fn the_popover_says_what_reopens() {
        assert!(popover_body(0).contains("your windows and tabs reopen. Otherwise"));
        assert!(popover_body(1).contains("but your private window doesn't"));
        assert!(popover_body(2).contains("but private windows don't"));
    }
}
