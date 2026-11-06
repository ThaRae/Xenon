/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::{HashMap, HashSet};
use std::ffi::{c_int, c_void};
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::{Arc, Mutex};

use rune::{AppHandle, Pixels};

use crate::engine::{ViewId, WindowId};
use crate::ffi::{XenonFrame, XenonStr, XenonTabInfo, XenonUiCallbacks};
use crate::frame::straight_rgba_from_premultiplied_bgra;
use crate::state::Browser;
use crate::tab_list::TabInsertion;

#[derive(Default)]
pub struct PageSurfaces {
    surfaces: Mutex<SurfaceTable>,
}

#[derive(Default)]
struct SurfaceTable {
    live: HashMap<ViewId, Pixels>,
    closed: HashSet<ViewId>,
}

impl PageSurfaces {
    pub fn surface(&self, view: ViewId) -> Option<Pixels> {
        let mut table = self
            .surfaces
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if table.closed.contains(&view) {
            return None;
        }
        Some(table.live.entry(view).or_default().clone())
    }

    pub fn close(&self, view: ViewId) {
        let mut table = self
            .surfaces
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        table.live.remove(&view);
        table.closed.insert(view);
    }
}

pub struct UiContext {
    handle: AppHandle<Browser>,
    surfaces: Arc<PageSurfaces>,
    exit_code: AtomicI32,
    exited: AtomicBool,
}

impl UiContext {
    pub fn new(handle: AppHandle<Browser>, surfaces: Arc<PageSurfaces>) -> UiContext {
        UiContext {
            handle,
            surfaces,
            exit_code: AtomicI32::new(0),
            exited: AtomicBool::new(false),
        }
    }

    pub fn engine_exited(&self) -> bool {
        self.exited.load(Ordering::Acquire)
    }

    pub fn exit_code(&self) -> i32 {
        self.exit_code.load(Ordering::Acquire)
    }

    pub fn callbacks(self: &Arc<Self>) -> XenonUiCallbacks {
        XenonUiCallbacks {
            context: Arc::as_ptr(self).cast_mut().cast(),
            engine_ready: Some(engine_ready),
            engine_exited: Some(engine_exited),
            view_frame: Some(view_frame),
            view_title_changed: Some(view_title_changed),
            view_url_changed: Some(view_url_changed),
            view_loading_changed: Some(view_loading_changed),
            view_history_changed: Some(view_history_changed),
            view_favicon_changed: Some(view_favicon_changed),
            view_cursor_changed: Some(view_cursor_changed),
            view_link_hovered: Some(view_link_hovered),
            view_crashed: Some(view_crashed),
            window_opened: Some(window_opened),
            window_closed: Some(window_closed),
            window_activated: Some(window_activated),
            tab_inserted: Some(tab_inserted),
            tab_removed: Some(tab_removed),
            tab_moved: Some(tab_moved),
            tab_activated: Some(tab_activated),
            tab_pinned_changed: Some(tab_pinned_changed),
            closed_tabs_changed: Some(closed_tabs_changed),
            chrome_settings_changed: Some(chrome_settings_changed),
            tab_memory: Some(tab_memory),
            ui_command: Some(ui_command),
            app_menu: Some(app_menu),
            view_zoom_changed: Some(view_zoom_changed),
            view_bookmarked_changed: Some(view_bookmarked_changed),
            downloads_changed: Some(downloads_changed),
            view_audio_changed: Some(view_audio_changed),
            view_unresponsive_changed: Some(view_unresponsive_changed),
            history_menu: Some(history_menu),
            view_blocking_changed: Some(view_blocking_changed),
        }
    }
}

unsafe fn ui<'a>(context: *mut c_void) -> &'a UiContext {
    unsafe { &*context.cast::<UiContext>() }
}

unsafe extern "C" fn engine_ready(context: *mut c_void) {
    unsafe { ui(context) }
        .handle
        .update(|browser, tx| browser.engine_ready(tx));
}

unsafe extern "C" fn engine_exited(context: *mut c_void, exit_code: c_int) {
    let ui = unsafe { ui(context) };
    ui.exit_code.store(exit_code, Ordering::Release);
    ui.exited.store(true, Ordering::Release);
    ui.handle.quit();
}

unsafe extern "C" fn view_frame(context: *mut c_void, view: ViewId, frame: *const XenonFrame) {
    let ui = unsafe { ui(context) };
    let Some(frame) = (unsafe { frame.as_ref() }) else {
        return;
    };
    if frame.bgra.is_null() || frame.width == 0 || frame.height == 0 {
        return;
    }
    let Some(surface) = ui.surfaces.surface(view) else {
        return;
    };
    let length = frame.stride * (frame.height as usize - 1) + frame.width as usize * 4;
    let bgra = unsafe { std::slice::from_raw_parts(frame.bgra, length) };
    surface.set(
        frame.width,
        frame.height,
        straight_rgba_from_premultiplied_bgra(frame.width, frame.height, frame.stride, bgra),
    );
}

unsafe extern "C" fn view_title_changed(context: *mut c_void, view: ViewId, title: XenonStr) {
    let title = unsafe { title.to_string_lossy() };
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.title_changed(view, title, tx));
}

unsafe extern "C" fn view_url_changed(context: *mut c_void, view: ViewId, url: XenonStr) {
    let url = unsafe { url.to_string_lossy() };
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.url_changed(view, url, tx));
}

unsafe extern "C" fn view_loading_changed(context: *mut c_void, view: ViewId, is_loading: bool) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.loading_changed(view, is_loading));
}

unsafe extern "C" fn view_history_changed(
    context: *mut c_void,
    view: ViewId,
    can_go_back: bool,
    can_go_forward: bool,
) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.history_changed(view, can_go_back, can_go_forward));
}

unsafe extern "C" fn view_favicon_changed(
    context: *mut c_void,
    view: ViewId,
    width: u32,
    height: u32,
    rgba: *const u8,
) {
    let favicon = (!rgba.is_null() && width > 0 && height > 0).then(|| {
        let pixels = Pixels::new();
        let length = width as usize * height as usize * 4;
        pixels.set(
            width,
            height,
            unsafe { std::slice::from_raw_parts(rgba, length) }.to_vec(),
        );
        pixels
    });
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.favicon_changed(view, favicon));
}

unsafe extern "C" fn view_cursor_changed(context: *mut c_void, view: ViewId, cursor: u32) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.cursor_changed(view, cursor));
}

unsafe extern "C" fn view_link_hovered(context: *mut c_void, view: ViewId, url: XenonStr) {
    let url = unsafe { url.to_string_lossy() };
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.link_hovered(view, url));
}

unsafe extern "C" fn view_crashed(context: *mut c_void, view: ViewId) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.crashed(view));
}

unsafe extern "C" fn window_opened(context: *mut c_void, window: WindowId, is_private: bool) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.window_opened(window, is_private));
}

unsafe extern "C" fn window_closed(context: *mut c_void, window: WindowId) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.window_closed(window, tx));
}

unsafe extern "C" fn window_activated(context: *mut c_void, window: WindowId) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.window_activated(window, tx));
}

pub fn tab_insertion_from(info: &XenonTabInfo) -> TabInsertion {
    TabInsertion {
        window: info.window,
        view: info.view,
        index: info.index as usize,
        pinned: info.pinned,
        private: info.is_private,
        is_new_tab_page: info.is_new_tab_page,
        url: unsafe { info.url.to_string_lossy() },
        title: unsafe { info.title.to_string_lossy() },
    }
}

unsafe extern "C" fn tab_inserted(context: *mut c_void, info: *const XenonTabInfo) {
    let Some(info) = (unsafe { info.as_ref() }) else {
        return;
    };
    let insertion = tab_insertion_from(info);
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.tab_inserted(insertion));
}

unsafe extern "C" fn tab_removed(context: *mut c_void, window: WindowId, view: ViewId) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.tab_removed(window, view, tx));
}

unsafe extern "C" fn tab_moved(
    context: *mut c_void,
    view: ViewId,
    from: WindowId,
    to: WindowId,
    index: u32,
) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.tab_moved(view, from, to, index as usize));
}

unsafe extern "C" fn tab_activated(context: *mut c_void, window: WindowId, view: ViewId) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.tab_activated(window, view, tx));
}

unsafe extern "C" fn tab_pinned_changed(context: *mut c_void, view: ViewId, pinned: bool) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.tab_pinned_changed(view, pinned));
}

unsafe extern "C" fn closed_tabs_changed(context: *mut c_void, can_reopen: bool) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.closed_tabs_changed(can_reopen));
}

unsafe extern "C" fn chrome_settings_changed(
    context: *mut c_void,
    layout: u8,
    cycle_tabs_in_recently_used_order: bool,
) {
    unsafe { ui(context) }.handle.update(move |browser, _| {
        browser.chrome_settings_changed(layout, cycle_tabs_in_recently_used_order)
    });
}

unsafe extern "C" fn tab_memory(context: *mut c_void, view: ViewId, description: XenonStr) {
    let description = unsafe { description.to_string_lossy() };
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.tab_memory_reported(view, description));
}

unsafe extern "C" fn ui_command(context: *mut c_void, command: u32) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.run_ui_command(command, tx));
}

unsafe extern "C" fn app_menu(
    context: *mut c_void,
    _which: u32,
    menu: *const crate::ffi_page::XenonMenu,
) {
    let Some(menu) = (unsafe { menu.as_ref() }) else {
        return;
    };
    let id = menu.id;
    let entries = crate::page_prompts::menu_entries(&crate::page_bridge::raw_menu_items(menu));
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.show_main_menu(id, entries, tx));
}

unsafe extern "C" fn downloads_changed(
    context: *mut c_void,
    downloads: *const crate::ffi::XenonDownload,
    count: usize,
) {
    let downloads = if downloads.is_null() || count == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(downloads, count) }
            .iter()
            .map(|download| crate::state::Download {
                id: download.id,
                file_name: unsafe { download.file_name.to_string_lossy() },
                status_text: unsafe { download.status_text.to_string_lossy() },
                status: download.status,
                progress: (download.progress >= 0.0).then_some(download.progress as f32),
                can_resume: download.can_resume,
                private: download.is_private,
            })
            .collect()
    };
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.downloads_changed(downloads));
}

unsafe extern "C" fn view_audio_changed(
    context: *mut c_void,
    view: ViewId,
    playing_audio: bool,
    muted: bool,
) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.audio_changed(view, playing_audio, muted));
}

unsafe extern "C" fn view_unresponsive_changed(
    context: *mut c_void,
    view: ViewId,
    unresponsive: bool,
) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.unresponsive_changed(view, unresponsive));
}

unsafe extern "C" fn history_menu(
    context: *mut c_void,
    view: ViewId,
    menu: *const crate::ffi_page::XenonMenu,
) {
    let Some(menu) = (unsafe { menu.as_ref() }) else {
        return;
    };
    let entries = crate::page_prompts::menu_entries(&crate::page_bridge::raw_menu_items(menu));
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.show_history_menu(view, entries, tx));
}

unsafe extern "C" fn view_blocking_changed(
    context: *mut c_void,
    view: ViewId,
    state: u32,
    site: XenonStr,
    blocked_requests: u64,
) {
    let site = unsafe { site.to_string_lossy() };
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.blocking_changed(view, state, site, blocked_requests));
}

unsafe extern "C" fn view_zoom_changed(context: *mut c_void, view: ViewId, percent: u32) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.zoom_changed(view, percent));
}

unsafe extern "C" fn view_bookmarked_changed(context: *mut c_void, view: ViewId, bookmarked: bool) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.bookmarked_changed(view, bookmarked));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_closed_view_takes_no_more_frames() {
        let surfaces = PageSurfaces::default();
        let early = surfaces.surface(7).unwrap();
        early.set(1, 1, vec![1, 2, 3, 255]);
        assert_eq!(surfaces.surface(7).unwrap().size(), (1, 1));
        surfaces.close(7);
        assert!(surfaces.surface(7).is_none());
    }

    #[test]
    fn tab_info_crosses_into_an_owned_insertion() {
        let url = "https://example.com/";
        let info = XenonTabInfo {
            window: 3,
            view: 9,
            opener: 0,
            index: 2,
            pinned: true,
            is_private: false,
            loaded: true,
            is_new_tab_page: false,
            url: XenonStr::borrowed(url),
            title: XenonStr::borrowed(""),
        };
        let insertion = tab_insertion_from(&info);
        assert_eq!(insertion.index, 2);
        assert_eq!(insertion.url, url);
        assert!(insertion.title.is_empty() && insertion.pinned);
    }
}
