/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

mod accessibility;
mod app_menu;
#[cfg(all(target_os = "linux", not(feature = "mock-engine")))]
mod at_spi;
mod bookmarks;
mod bridge;
mod chrome_menu;
mod chrome_popup;
mod crash_reports;
mod element_picker;
mod engine;
mod extensions;
#[allow(dead_code)]
mod ffi;
#[allow(dead_code)]
mod ffi_accessibility;
#[allow(dead_code)]
mod ffi_bookmarks;
#[allow(dead_code)]
mod ffi_drop;
#[allow(dead_code)]
mod ffi_extensions;
#[allow(dead_code)]
mod ffi_hibernation;
#[allow(dead_code)]
mod ffi_location;
#[allow(dead_code)]
mod ffi_media;
#[allow(dead_code)]
mod ffi_page;
#[allow(dead_code)]
mod ffi_prompts;
#[allow(dead_code)]
mod ffi_reports;
#[allow(dead_code)]
mod ffi_screenshots;
mod ffi_split_view;
mod ffi_tab_groups;
#[allow(dead_code)]
mod ffi_task_manager;
#[allow(dead_code)]
mod ffi_updater;
#[allow(dead_code)]
mod ffi_windows;
mod frame;
mod hibernation;
mod keys;
mod location;
mod location_menu;
mod media_bridge;
#[cfg(feature = "mock-engine")]
mod mock;
#[cfg(feature = "mock-engine")]
mod mock_location;
#[cfg(feature = "mock-engine")]
mod mock_split_view;
#[cfg(feature = "mock-engine")]
mod mock_windows;
mod page_bridge;
mod page_drop;
mod page_prompts;
mod pickers;
mod picture_in_picture;
mod platform;
mod platform_services;
mod prompts_bridge;
mod screenshots;
mod split_view;
mod state;
mod tab_group_bridge;
mod tab_groups;
mod tab_list;
mod task_manager;
mod updates;
mod view;
mod wayland_page;
mod window_slots;
mod window_state;
mod windows_bridge;

use std::ffi::{c_char, c_int};
use std::sync::Arc;

use rune::{App, AppSpec, Ime, KeyEvent, Tx, WindowOptions, WindowSpec};

use crate::bridge::{PageSurfaces, UiContext};
use crate::state::{APP_NAME, Browser, INITIAL_WINDOW_SIZE};
use crate::window_slots::{PRIMARY_SLOT, SlotLink, Slots, WINDOW_SLOTS, slot_key};

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

fn browser_spec() -> AppSpec<Browser> {
    AppSpec {
        min_size: Some(MINIMUM_WINDOW_SIZE),
        poll: Some(poll),
        on_key: Some(window_key),
        ..AppSpec::new(
            APP_NAME,
            INITIAL_WINDOW_SIZE,
            detached_browser,
            view::browser_view,
        )
    }
}

fn browser_window(
    app: App<Browser>,
    slot: usize,
    slots: &Arc<Slots>,
    surfaces: &Arc<PageSurfaces>,
) -> App<Browser> {
    let link = SlotLink {
        slot,
        slots: slots.clone(),
    };
    let surfaces = surfaces.clone();
    let created = slots.clone();
    let ready = slots.clone();
    app.init(move || Browser::for_slot(surfaces, link))
        .modifier_keys()
        .on_ime(ime)
        .on_close(window_closed)
        .on_window(move |handle| created.window_created(slot, handle))
        .on_ready(move |handle| ready.window_ready(slot, handle))
}

fn run(argc: c_int, argv: *const *const c_char, rune_args: Vec<String>) -> c_int {
    let surfaces = Arc::new(PageSurfaces::default());
    let options = || WindowOptions {
        transparent: true,
        borderless: view::draws_window_frame(),
        ..WindowOptions::default()
    };
    let mut app = App::new(browser_spec())
        .window_options(options())
        .standard_menus()
        .keep_running();
    for item in app_menu::menu_bar() {
        app = app.menu_item(item);
    }
    let others: Vec<App<Browser>> = (1..WINDOW_SLOTS)
        .map(|_| {
            App::new(browser_spec())
                .window_options(options())
                .windowless()
                .release_gpu_when_closed()
        })
        .collect();
    let handles = std::iter::once(app.handle())
        .chain(others.iter().map(App::handle))
        .collect();
    let slots = Slots::new(handles);
    location_menu::set_headless(rune::tooling::Args::parse_from(rune_args.clone()).headless());
    let mut app = browser_window(app.args(rune_args), PRIMARY_SLOT, &slots, &surfaces);
    for (index, other) in others.into_iter().enumerate() {
        let slot = PRIMARY_SLOT + 1 + index;
        let other = browser_window(other, slot, &slots, &surfaces);
        let (_, window) = app.window(WindowSpec::from_app(slot_key(slot), other));
        slots.set_window_handle(slot, window);
    }
    let ui = Arc::new(UiContext::new(slots.clone(), surfaces));
    let callbacks = ui.callbacks();
    engine::Engine.set_window_viewport(None, state::PageGeometry::default().viewport());
    let _page_context = page_bridge::install(slots.clone());
    let _accessibility_context = accessibility::install(slots.clone());
    let _tab_group_context = tab_group_bridge::install(slots.clone());
    let _hibernation_context = hibernation::install(slots.clone());
    let _task_manager_context = task_manager::install(slots.clone());
    let _bookmarks_context = bookmarks::install(slots.clone());
    let _element_picker_context = element_picker::install(slots.clone());
    let _location_menu_context = location_menu::install(slots.clone());
    let _prompt_context = prompts_bridge::install(slots.clone());
    let _media_context = media_bridge::install(slots.clone());
    let _updater_context = updates::install(slots.clone());
    let _screenshot_context = screenshots::install(slots.clone());
    let _report_context = crash_reports::install(slots.clone());
    let location_context = ffi_location::LocationContext::new(slots.clone());
    location_context.register();
    windows_bridge::install(&slots);
    let extensions_context = extensions::ExtensionsContext::new(slots.clone());
    extensions_context.register();
    let split_view_context = ffi_split_view::SplitViewContext::new(slots.clone());
    split_view_context.register();
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

fn poll(browser: &mut Browser, now: f64, tx: &mut Tx) -> bool {
    browser.track_location_focus()
        | page_prompts::poll(browser, now, tx)
        | accessibility::poll(browser, now, tx)
        | tab_groups::poll(browser)
        | extensions::poll(browser, tx)
        | split_view::poll(browser, now, tx)
        | screenshots::poll(browser, now, tx)
        | page_drop::poll(browser, tx)
        | browser.poll_native(now, tx)
}

fn window_key(browser: &mut Browser, event: &KeyEvent, tx: &mut Tx) -> bool {
    browser.window_key(event, tx)
}

fn window_closed(browser: &mut Browser, _: &mut Tx) {
    browser.window_close_requested();
}

fn ime(browser: &mut Browser, ime: &Ime, _: &mut Tx) -> bool {
    browser.ime(ime)
}
