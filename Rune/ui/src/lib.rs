/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

mod bridge;
mod engine;
#[allow(dead_code)]
mod ffi;
mod frame;
mod keys;
#[cfg(feature = "mock-engine")]
mod mock;
mod state;
mod tab_list;
mod view;

use std::ffi::{c_char, c_int};
use std::sync::Arc;

use rune::{App, AppSpec, Ime, KeyEvent, Tx};

use crate::bridge::{PageSurfaces, UiContext};
use crate::state::{APP_NAME, Browser, INITIAL_WINDOW_SIZE};

const MINIMUM_WINDOW_SIZE: (f32, f32) = (480.0, 360.0);
const FAILED_TO_START: c_int = 1;

#[unsafe(no_mangle)]
pub extern "C" fn xenon_ui_main(argc: c_int, argv: *const *const c_char) -> c_int {
    run(argc, argv, rune_args_from_environment())
}

fn rune_args_from_environment() -> Vec<String> {
    std::env::var("XENON_RUNE_ARGS")
        .map(|args| args.split_whitespace().map(String::from).collect())
        .unwrap_or_default()
}

pub fn run_with_rune_args(rune_args: Vec<String>) -> c_int {
    run(0, std::ptr::null(), rune_args)
}

fn run(argc: c_int, argv: *const *const c_char, rune_args: Vec<String>) -> c_int {
    let surfaces = Arc::new(PageSurfaces::default());
    let state_surfaces = surfaces.clone();
    let spec = AppSpec {
        min_size: Some(MINIMUM_WINDOW_SIZE),
        poll: Some(poll),
        on_key: Some(window_key),
        ..AppSpec::new(
            APP_NAME,
            INITIAL_WINDOW_SIZE,
            detached_browser,
            view::browser_view,
        )
    };
    let app = App::new(spec)
        .init(move || Browser::new(state_surfaces))
        .args(rune_args)
        .modifier_keys()
        .on_ime(ime);
    let ui = Arc::new(UiContext::new(app.handle(), surfaces));
    let callbacks = ui.callbacks();
    if !unsafe { ffi::xenon_engine_start(&callbacks, argc, argv) } {
        return FAILED_TO_START;
    }
    let outcome = if ui.engine_exited() {
        Ok(())
    } else {
        app.try_run()
    };
    unsafe { ffi::xenon_engine_shutdown() };
    match outcome {
        Ok(()) => ui.exit_code(),
        Err(error) => {
            eprintln!("{APP_NAME}: {error}");
            FAILED_TO_START
        }
    }
}

fn detached_browser() -> Browser {
    Browser::new(Arc::default())
}

fn poll(browser: &mut Browser, _: f64, _: &mut Tx) -> bool {
    browser.track_location_focus()
}

fn window_key(browser: &mut Browser, event: &KeyEvent, tx: &mut Tx) -> bool {
    browser.window_key(event, tx)
}

fn ime(browser: &mut Browser, ime: &Ime, _: &mut Tx) -> bool {
    browser.ime(ime)
}
