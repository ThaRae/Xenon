/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Arc;

use rune::Tx;

use crate::bridge::PageSurfaces;
use crate::engine::WindowId;
use crate::ffi_windows::{self, XenonWindowGeometry};
use crate::keys::Shortcut;
use crate::state::Browser;
use crate::tab_list::Windows;
use crate::window_slots::{Placement, Slot, SlotLink};

#[derive(Default)]
pub struct WindowState {
    pub link: Option<SlotLink>,
    pub devtools_port: Option<u16>,
    pub stop_downloads_prompt: Option<u64>,
    reported: Option<XenonWindowGeometry>,
    normal_rect: Option<[f32; 4]>,
}

impl WindowState {
    pub fn slot(&self) -> Option<Slot> {
        self.link.as_ref().map(|link| link.slot)
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct WindowEngine;

impl WindowEngine {
    pub fn request_close_window(self, window: WindowId) {
        unsafe { ffi_windows::xenon_engine_request_close_window(window) }
    }

    pub fn request_quit(self) {
        unsafe { ffi_windows::xenon_engine_request_quit() }
    }

    pub fn answer_stop_downloads(self, request: u64, stop: bool) {
        unsafe { ffi_windows::xenon_engine_answer_stop_downloads(request, stop) }
    }

    pub fn report_geometry(self, window: WindowId, geometry: XenonWindowGeometry) {
        unsafe { ffi_windows::xenon_engine_report_window_geometry(window, &geometry) }
    }

    pub fn launch_devtools_client(self) {
        unsafe { ffi_windows::xenon_engine_launch_devtools_client() }
    }

    pub fn disable_devtools(self) {
        unsafe { ffi_windows::xenon_engine_disable_devtools() }
    }
}

pub fn geometry_for(
    rect: [f32; 4],
    normal_rect: Option<[f32; 4]>,
    maximized: bool,
) -> XenonWindowGeometry {
    let [x, y, width, height] = if maximized {
        normal_rect.unwrap_or(rect)
    } else {
        rect
    };
    XenonWindowGeometry {
        x: x.round() as i32,
        y: y.round() as i32,
        width: width.round() as i32,
        height: height.round() as i32,
        maximized,
        has_position: true,
    }
}

impl Browser {
    pub fn for_slot(surfaces: Arc<PageSurfaces>, link: SlotLink) -> Browser {
        let mut browser = Browser::new(surfaces);
        browser.windows = Windows::slotted();
        browser.window.link = Some(link);
        browser
    }

    fn platform_slot(&self) -> Option<u32> {
        self.window.slot().map(|slot| slot as u32)
    }

    pub fn bind_window(&mut self, window: WindowId, tx: &mut Tx) {
        let previous = self.tabs.active_view();
        self.windows.bind(window, &mut self.tabs);
        self.window.reported = None;
        self.window.normal_rect = None;
        self.window.stop_downloads_prompt = None;
        if let Some(slot) = self.platform_slot() {
            unsafe { ffi_windows::xenon_platform_bind_window(slot, window) };
        }
        self.send_window_viewport();
        #[cfg(target_os = "linux")]
        self.enable_native_surface(window);
        self.shown_tabs_changed(previous, tx);
    }

    fn window_handle(&self) -> Option<&rune::WindowHandle> {
        let link = self.window.link.as_ref()?;
        link.slots.window_handle(link.slot)
    }

    pub fn window_is_maximized(&self) -> bool {
        self.window_handle()
            .is_some_and(rune::WindowHandle::is_maximized)
    }

    pub fn minimize_window(&mut self) {
        if let Some(handle) = self.window_handle() {
            handle.minimize();
        }
    }

    pub fn toggle_window_maximized(&mut self) {
        if let Some(handle) = self.window_handle() {
            handle.set_maximized(!handle.is_maximized());
        }
    }

    pub fn resize_window_from(&mut self, edge: (i8, i8)) {
        if let Some(handle) = self.window_handle() {
            handle.drag_resize(edge);
        }
    }

    pub fn is_bound_to(&self, window: WindowId) -> bool {
        self.window.link.is_none() || self.shown_window() == Some(window)
    }

    pub fn window_close_requested(&mut self) {
        let (Some(link), Some(window)) = (self.window.link.clone(), self.shown_window()) else {
            return;
        };
        link.slots.update_slot(link.slot, move |browser, _| {
            if browser.shown_window() == Some(window) {
                WindowEngine.request_close_window(window);
            }
        });
    }

    pub fn request_close_shown_window(&mut self) {
        if let Some(window) = self.shown_window() {
            WindowEngine.request_close_window(window);
        }
    }

    pub fn request_quit(&mut self, tx: &mut Tx) {
        if self.window.link.is_some() {
            WindowEngine.request_quit();
        } else {
            tx.quit();
        }
    }

    pub fn forwards_to_target(&self, shortcut: Shortcut) -> bool {
        let Some(link) = &self.window.link else {
            return false;
        };
        if link.is_target() {
            return false;
        }
        link.slots
            .update_target(move |browser, tx| browser.run_shortcut(shortcut, tx));
        true
    }

    pub fn slot_focus_changed(&mut self, focused: bool) {
        let Some(link) = &self.window.link else {
            return;
        };
        link.slots.focus_changed(link.slot, focused);
        if focused
            && let Some(window) = self.shown_window()
            && !self.windows.shown_is_active()
        {
            self.activate_shown_window(window);
        }
    }

    fn activate_shown_window(&self, window: WindowId) {
        crate::engine::Engine.activate_window(window);
    }

    pub fn observe_slot_scale(&self, scale: f32) {
        if let Some(link) = &self.window.link {
            link.slots.observe_scale(scale);
        }
    }

    pub fn window_rect_changed(&mut self, rect: [f32; 4]) {
        let Some(window) = self.shown_window() else {
            return;
        };
        if rect[2] <= 0.0 || rect[3] <= 0.0 {
            return;
        }
        let slot = self.platform_slot();
        let maximized = self.window_is_maximized()
            || slot.is_some_and(|slot| unsafe {
                ffi_windows::xenon_platform_window_is_maximized(slot)
            });
        let fullscreen = slot
            .is_some_and(|slot| unsafe { ffi_windows::xenon_platform_window_is_fullscreen(slot) });
        if fullscreen {
            return;
        }
        if !maximized {
            self.window.normal_rect = Some(rect);
        }
        let geometry = geometry_for(rect, self.window.normal_rect, maximized);
        if self.window.reported == Some(geometry) {
            return;
        }
        self.window.reported = Some(geometry);
        WindowEngine.report_geometry(window, geometry);
    }

    pub fn window_frame_requested(
        &mut self,
        request: u32,
        x: i32,
        y: i32,
        width: i32,
        height: i32,
    ) {
        let Some(link) = self.window.link.clone() else {
            return;
        };
        match request {
            ffi_windows::XENON_WINDOW_FRAME_MOVE => {
                if let Some(handle) = link.slots.window_handle(link.slot) {
                    let scale = link.slots.scale();
                    handle.set_position((
                        (x as f32 * scale).round() as i32,
                        (y as f32 * scale).round() as i32,
                    ));
                }
            }
            ffi_windows::XENON_WINDOW_FRAME_RESIZE => {
                if width > 0
                    && height > 0
                    && let Some(handle) = link.slots.window_handle(link.slot)
                {
                    handle.resize_instant((width as f32, height as f32));
                }
            }
            ffi_windows::XENON_WINDOW_FRAME_MINIMIZE => {
                if let Some(handle) = link.slots.handle(link.slot) {
                    handle.minimize();
                }
            }
            ffi_windows::XENON_WINDOW_FRAME_MAXIMIZE | ffi_windows::XENON_WINDOW_FRAME_RESTORE => {
                if request == ffi_windows::XENON_WINDOW_FRAME_RESTORE
                    && let Some(handle) = link.slots.handle(link.slot)
                {
                    handle.open();
                }
                unsafe { ffi_windows::xenon_platform_window_request(link.slot as u32, request) };
            }
            _ => {}
        }
    }

    pub fn place_window(&mut self, placement: Placement) {
        if placement.maximized
            && let Some(slot) = self.platform_slot()
        {
            unsafe {
                ffi_windows::xenon_platform_window_request(
                    slot,
                    ffi_windows::XENON_WINDOW_FRAME_MAXIMIZE,
                )
            };
        }
    }

    pub fn is_always_on_top(&self) -> bool {
        match &self.window.link {
            Some(link) if !cfg!(target_os = "macos") => link.slots.is_always_on_top(link.slot),
            _ => crate::platform::is_always_on_top(),
        }
    }

    pub fn toggle_always_on_top(&mut self) {
        match &self.window.link {
            Some(link) if !cfg!(target_os = "macos") => {
                let slot = link.slots.table().target();
                let on = !link.slots.is_always_on_top(slot);
                link.slots.set_always_on_top(slot, on);
            }
            _ => crate::platform::set_always_on_top(!crate::platform::is_always_on_top()),
        }
    }

    pub fn devtools_changed(&mut self, enabled: bool, port: u16) {
        self.window.devtools_port = enabled.then_some(port);
    }

    pub fn launch_devtools_client(&mut self) {
        WindowEngine.launch_devtools_client();
    }

    pub fn disable_devtools(&mut self) {
        WindowEngine.disable_devtools();
    }

    pub fn confirm_stop_downloads(&mut self, request: u64) {
        if let Some(previous) = self.window.stop_downloads_prompt.replace(request) {
            WindowEngine.answer_stop_downloads(previous, false);
        }
    }

    pub fn answer_stop_downloads(&mut self, stop: bool) {
        if let Some(request) = self.window.stop_downloads_prompt.take() {
            WindowEngine.answer_stop_downloads(request, stop);
        }
    }

    pub fn quit_approved(&mut self, tx: &mut Tx) {
        tx.quit();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_maximized_window_keeps_its_normal_geometry() {
        let normal = [100.0, 80.0, 1200.0, 700.0];
        let zoomed = geometry_for([0.0, 25.0, 1728.0, 1080.0], Some(normal), true);
        assert_eq!(
            (zoomed.x, zoomed.y, zoomed.width, zoomed.height),
            (100, 80, 1200, 700)
        );
        assert!(zoomed.maximized && zoomed.has_position);
        let plain = geometry_for([10.4, 20.6, 800.0, 600.0], None, false);
        assert_eq!(
            (plain.x, plain.y, plain.width, plain.height),
            (10, 21, 800, 600)
        );
        assert!(!plain.maximized);
    }
}
