/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

mod accessibility;
mod app_menu;
#[cfg(all(target_os = "linux", not(feature = "mock-engine")))]
mod at_spi;
mod bridge;
mod chrome_menu;
mod chrome_popup;
mod engine;
#[allow(dead_code)]
mod ffi;
#[allow(dead_code)]
mod ffi_accessibility;
#[allow(dead_code)]
mod ffi_hibernation;
#[allow(dead_code)]
mod ffi_location;
#[allow(dead_code)]
mod ffi_page;
mod ffi_tab_groups;
#[allow(dead_code)]
mod ffi_task_manager;
mod frame;
mod hibernation;
mod keys;
mod location;
#[cfg(feature = "mock-engine")]
mod mock;
#[cfg(feature = "mock-engine")]
mod mock_location;
mod page_bridge;
mod page_prompts;
mod platform;
mod state;
mod tab_group_bridge;
mod tab_groups;
mod tab_list;
mod task_manager;
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
    let mut app = App::new(spec)
        .init(move || Browser::new(state_surfaces))
        .standard_menus();
    for item in app_menu::menu_bar() {
        app = app.menu_item(item);
    }
    let app = app.args(rune_args).modifier_keys().on_ime(ime);
    let ui = Arc::new(UiContext::new(app.handle(), surfaces));
    let callbacks = ui.callbacks();
    engine::Engine.set_window_viewport(None, state::PageGeometry::default().viewport());
    let _page_context = page_bridge::install(app.handle());
    let _accessibility_context = accessibility::install(app.handle());
    let _tab_group_context = tab_group_bridge::install(app.handle());
    let _hibernation_context = hibernation::install(app.handle());
    let _task_manager_context = task_manager::install(app.handle());
    let location_context = ffi_location::LocationContext::new(app.handle());
    location_context.register();
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

fn poll(browser: &mut Browser, _: f64, tx: &mut Tx) -> bool {
    browser.track_location_focus()
        | page_prompts::poll(browser, tx)
        | accessibility::poll(browser, tx)
        | tab_groups::poll(browser)
}

fn window_key(browser: &mut Browser, event: &KeyEvent, tx: &mut Tx) -> bool {
    browser.window_key(event, tx)
}

fn ime(browser: &mut Browser, ime: &Ime, _: &mut Tx) -> bool {
    browser.ime(ime)
}
