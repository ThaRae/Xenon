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

use crate::engine::ViewId;
use crate::ffi::{XenonFrame, XenonStr, XenonUiCallbacks};
use crate::frame::straight_rgba_from_premultiplied_bgra;
use crate::state::Browser;

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
            view_opened: Some(view_opened),
            view_close_requested: Some(view_close_requested),
            view_crashed: Some(view_crashed),
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

unsafe extern "C" fn view_opened(
    context: *mut c_void,
    opener: ViewId,
    view: ViewId,
    activate: bool,
) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.view_opened(opener, view, activate, tx));
}

unsafe extern "C" fn view_close_requested(context: *mut c_void, view: ViewId) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, tx| browser.close_tab(view, tx));
}

unsafe extern "C" fn view_crashed(context: *mut c_void, view: ViewId) {
    unsafe { ui(context) }
        .handle
        .update(move |browser, _| browser.crashed(view));
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
}
