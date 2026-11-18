/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::HashMap;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};

use rune::{App, AppHandle, Tx, WindowHandle, WindowSpec};

use crate::engine::{ViewId, WindowId};
use crate::state::Browser;

pub const PRIMARY_SLOT: usize = 0;
pub const HEADLESS_WINDOW_SLOTS: usize = 12;

pub type Slot = usize;

pub type SlotFactory = Box<dyn Fn(Slot, Browser) -> App<Browser> + Send + Sync>;

pub fn slot_key(slot: Slot) -> String {
    if slot == PRIMARY_SLOT {
        crate::state::APP_NAME.to_owned()
    } else {
        format!("window-{slot}")
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Placement {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub maximized: bool,
    pub has_position: bool,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Opened {
    Bound(Slot),
    Added(Slot),
}

impl Opened {
    pub fn slot(&self) -> Slot {
        match self {
            Opened::Bound(slot) | Opened::Added(slot) => *slot,
        }
    }
}

#[derive(Default)]
pub struct SlotTable {
    bound: Vec<Option<WindowId>>,
    views: HashMap<ViewId, WindowId>,
    focused: Option<Slot>,
    active: Option<WindowId>,
}

impl SlotTable {
    pub fn with_slots(count: usize) -> SlotTable {
        SlotTable {
            bound: vec![None; count.max(1)],
            ..SlotTable::default()
        }
    }

    pub fn slot_of(&self, window: WindowId) -> Option<Slot> {
        self.bound.iter().position(|bound| *bound == Some(window))
    }

    pub fn window_in(&self, slot: Slot) -> Option<WindowId> {
        self.bound.get(slot).copied().flatten()
    }

    pub fn window_of_view(&self, view: ViewId) -> Option<WindowId> {
        self.views.get(&view).copied()
    }

    pub fn slot_of_view(&self, view: ViewId) -> Option<Slot> {
        self.slot_of(self.window_of_view(view)?)
    }

    pub fn opened(&mut self, window: WindowId) -> Opened {
        if let Some(slot) = self.slot_of(window) {
            return Opened::Bound(slot);
        }
        if let Some(slot) = self.bound.iter().position(Option::is_none) {
            self.bound[slot] = Some(window);
            return Opened::Bound(slot);
        }
        self.bound.push(Some(window));
        Opened::Added(self.bound.len() - 1)
    }

    pub fn closed(&mut self, window: WindowId) -> Option<Slot> {
        self.views.retain(|_, owner| *owner != window);
        if self.active == Some(window) {
            self.active = None;
        }
        let slot = self.slot_of(window)?;
        self.bound[slot] = None;
        if self.focused == Some(slot) {
            self.focused = None;
        }
        Some(slot)
    }

    pub fn activated(&mut self, window: WindowId) {
        self.active = Some(window);
    }

    pub fn view_placed(&mut self, view: ViewId, window: WindowId) {
        self.views.insert(view, window);
    }

    pub fn view_removed(&mut self, view: ViewId) {
        self.views.remove(&view);
    }

    pub fn focus_changed(&mut self, slot: Slot, focused: bool) {
        if focused {
            self.focused = Some(slot);
        } else if self.focused == Some(slot) {
            self.focused = None;
        }
    }

    pub fn is_focused(&self, slot: Slot) -> bool {
        self.focused == Some(slot)
    }

    pub fn target(&self) -> Slot {
        self.focused
            .filter(|slot| self.bound.get(*slot).is_some_and(Option::is_some))
            .or_else(|| self.active.and_then(|window| self.slot_of(window)))
            .or_else(|| self.bound.iter().position(Option::is_some))
            .unwrap_or(PRIMARY_SLOT)
    }

    #[cfg(test)]
    pub fn bound_slots(&self) -> usize {
        self.bound.iter().filter(|bound| bound.is_some()).count()
    }
}

type SlotUpdate = Box<dyn FnOnce(&mut Browser, &mut Tx) + Send>;
type SlotUpdateIf = Box<dyn FnOnce(&mut Browser, &mut Tx) -> bool + Send>;

enum Request {
    Update(SlotUpdate),
    UpdateIf(SlotUpdateIf),
    Open,
    Hide,
}

impl Request {
    fn send_to(self, handle: &AppHandle<Browser>) {
        match self {
            Request::Update(f) => handle.update(f),
            Request::UpdateIf(f) => handle.update_if(f),
            Request::Open => handle.open(),
            Request::Hide => handle.hide(),
        }
    }
}

enum SlotApp {
    Running(AppHandle<Browser>),
    Starting(Vec<Request>),
}

impl SlotApp {
    fn send(&mut self, request: Request) {
        match self {
            SlotApp::Running(handle) => request.send_to(handle),
            SlotApp::Starting(queued) => queued.push(request),
        }
    }
}

#[derive(Default)]
struct SlotWindow {
    handle: Option<WindowHandle>,
    always_on_top: bool,
}

pub struct Slots {
    apps: Mutex<Vec<SlotApp>>,
    windows: Mutex<Vec<SlotWindow>>,
    table: Mutex<SlotTable>,
    scale_bits: AtomicU32,
    placements: Mutex<HashMap<WindowId, Placement>>,
    pending: Mutex<HashMap<Slot, Placement>>,
    factory: OnceLock<SlotFactory>,
}

impl Slots {
    pub fn new(handles: Vec<AppHandle<Browser>>) -> Arc<Slots> {
        let count = handles.len();
        Arc::new(Slots {
            apps: Mutex::new(handles.into_iter().map(SlotApp::Running).collect()),
            windows: Mutex::new((0..count).map(|_| SlotWindow::default()).collect()),
            table: Mutex::new(SlotTable::with_slots(count)),
            scale_bits: AtomicU32::new(1.0f32.to_bits()),
            placements: Mutex::new(HashMap::new()),
            pending: Mutex::new(HashMap::new()),
            factory: OnceLock::new(),
        })
    }

    pub fn set_factory(&self, factory: SlotFactory) {
        let _ = self.factory.set(factory);
    }

    pub fn table(&self) -> MutexGuard<'_, SlotTable> {
        lock(&self.table)
    }

    pub fn handle(&self, slot: Slot) -> Option<AppHandle<Browser>> {
        match lock(&self.apps).get(slot)? {
            SlotApp::Running(handle) => Some(handle.clone()),
            SlotApp::Starting(_) => None,
        }
    }

    pub fn set_window_handle(&self, slot: Slot, handle: WindowHandle) {
        if let Some(window) = lock(&self.windows).get_mut(slot) {
            window.handle = Some(handle);
        }
    }

    pub fn window_handle(&self, slot: Slot) -> Option<WindowHandle> {
        lock(&self.windows).get(slot)?.handle.clone()
    }

    pub fn observe_scale(&self, scale: f32) {
        if scale > 0.0 {
            self.scale_bits.store(scale.to_bits(), Ordering::Relaxed);
        }
    }

    pub fn scale(&self) -> f32 {
        f32::from_bits(self.scale_bits.load(Ordering::Relaxed))
    }

    fn send(&self, slot: Slot, request: Request) {
        if let Some(app) = lock(&self.apps).get_mut(slot) {
            app.send(request);
        }
    }

    fn send_all(&self, request: impl Fn() -> Request) {
        for app in lock(&self.apps).iter_mut() {
            app.send(request());
        }
    }

    pub fn update(&self, f: impl Fn(&mut Browser, &mut Tx) + Send + Sync + 'static) {
        let f = Arc::new(f);
        self.send_all(|| {
            let f = f.clone();
            Request::Update(Box::new(move |browser, tx| f(browser, tx)))
        });
    }

    pub fn update_slot(&self, slot: Slot, f: impl FnOnce(&mut Browser, &mut Tx) + Send + 'static) {
        self.send(slot, Request::Update(Box::new(f)));
    }

    pub fn update_showing(
        &self,
        view: ViewId,
        f: impl Fn(&mut Browser, &mut Tx) + Send + Sync + 'static,
    ) {
        let f = Arc::new(f);
        self.send_all(|| {
            let f = f.clone();
            Request::UpdateIf(Box::new(move |browser, tx| {
                f(browser, tx);
                browser.shows_view(view)
            }))
        });
    }

    pub fn update_showing_if(
        &self,
        view: ViewId,
        f: impl Fn(&mut Browser, &mut Tx) -> bool + Send + Sync + 'static,
    ) {
        let f = Arc::new(f);
        self.send_all(|| {
            let f = f.clone();
            Request::UpdateIf(Box::new(move |browser, tx| {
                f(browser, tx) && browser.shows_view(view)
            }))
        });
    }

    pub fn update_view(
        &self,
        view: ViewId,
        f: impl FnOnce(&mut Browser, &mut Tx) + Send + 'static,
    ) {
        let slot = self.table().slot_of_view(view);
        if let Some(slot) = slot {
            self.update_slot(slot, f);
        }
    }

    pub fn update_view_if(
        &self,
        view: ViewId,
        f: impl FnOnce(&mut Browser, &mut Tx) -> bool + Send + 'static,
    ) {
        let slot = self.table().slot_of_view(view);
        if let Some(slot) = slot {
            self.send(slot, Request::UpdateIf(Box::new(f)));
        }
    }

    pub fn update_window(
        &self,
        window: WindowId,
        f: impl FnOnce(&mut Browser, &mut Tx) + Send + 'static,
    ) {
        let slot = self.table().slot_of(window);
        if let Some(slot) = slot {
            self.update_slot(slot, f);
        }
    }

    pub fn update_target(&self, f: impl FnOnce(&mut Browser, &mut Tx) + Send + 'static) {
        let slot = self.table().target();
        self.update_slot(slot, f);
    }

    pub fn quit(&self) {
        if let Some(handle) = self.handle(PRIMARY_SLOT) {
            handle.quit();
        }
    }

    pub fn window_opened(self: &Arc<Self>, window: WindowId, private: bool) {
        let opened = self.table().opened(window);
        if let Opened::Added(slot) = opened {
            self.add_slot(slot);
        }
        let slot = opened.slot();
        self.bind(slot, window);
        self.update(move |browser, _| browser.window_opened(window, private));
        self.show(slot);
    }

    fn add_slot(self: &Arc<Self>, slot: Slot) {
        {
            let mut windows = lock(&self.windows);
            while windows.len() <= slot {
                windows.push(SlotWindow::default());
            }
        }
        let slots = self.clone();
        let start = Request::Update(Box::new(move |primary, _| slots.start_slot(slot, primary)));
        let mut apps = lock(&self.apps);
        while apps.len() <= slot {
            apps.push(SlotApp::Starting(Vec::new()));
        }
        apps[PRIMARY_SLOT].send(start);
    }

    fn start_slot(self: &Arc<Self>, slot: Slot, primary: &Browser) {
        let Some(factory) = self.factory.get() else {
            return;
        };
        let link = SlotLink {
            slot,
            slots: self.clone(),
        };
        let app = factory(slot, primary.fork_for_slot(link));
        let handle = app.handle();
        let window = rune::open_window(WindowSpec::from_app(slot_key(slot), app));
        self.set_window_handle(slot, window);
        let mut apps = lock(&self.apps);
        let Some(entry) = apps.get_mut(slot) else {
            return;
        };
        if let SlotApp::Starting(queued) =
            std::mem::replace(entry, SlotApp::Running(handle.clone()))
        {
            for request in queued {
                request.send_to(&handle);
            }
        }
    }

    pub fn window_closed(&self, window: WindowId) {
        let slot = self.table().closed(window);
        self.update(move |browser, tx| browser.window_closed(window, tx));
        if let Some(slot) = slot {
            self.send(slot, Request::Hide);
        }
    }

    pub fn window_activated(&self, window: WindowId) {
        let (slot, focused) = {
            let mut table = self.table();
            table.activated(window);
            let slot = table.slot_of(window);
            (slot, slot.is_some_and(|slot| table.is_focused(slot)))
        };
        self.update(move |browser, tx| browser.window_activated(window, tx));
        if let Some(slot) = slot
            && !focused
        {
            self.show(slot);
        }
    }

    pub fn show(&self, slot: Slot) {
        self.send(slot, Request::Open);
    }

    fn bind(&self, slot: Slot, window: WindowId) {
        self.update_slot(slot, move |browser, tx| browser.bind_window(window, tx));
        let placement = lock(&self.placements).remove(&window);
        if let Some(placement) = placement {
            lock(&self.pending).insert(slot, placement);
            self.apply_placement(slot);
        }
    }

    pub fn set_placement(&self, window: WindowId, placement: Placement) {
        lock(&self.placements).insert(window, placement);
    }

    fn apply_placement(&self, slot: Slot) {
        let Some(window) = self.window_handle(slot) else {
            return;
        };
        let Some(placement) = lock(&self.pending).remove(&slot) else {
            return;
        };
        if placement.has_position {
            let scale = self.scale();
            window.set_position((
                (placement.x as f32 * scale).round() as i32,
                (placement.y as f32 * scale).round() as i32,
            ));
        }
        if placement.width > 0 && placement.height > 0 {
            window.resize_instant((placement.width as f32, placement.height as f32));
        }
        if placement.maximized {
            self.update_slot(slot, move |browser, _| browser.place_window(placement));
        }
    }

    pub fn window_created(&self, slot: Slot, handle: &WindowHandle) {
        self.set_window_handle(slot, handle.clone());
        self.apply_placement(slot);
        if self.is_always_on_top(slot) {
            handle.set_always_on_top(true);
        }
    }

    pub fn window_ready(&self, slot: Slot, handle: &WindowHandle) {
        if let Some(number) = handle.window_number() {
            unsafe { crate::ffi_windows::xenon_platform_attach_window(slot as u32, number) };
            if let Some(window) = self.window_in(slot) {
                unsafe { crate::ffi_windows::xenon_platform_bind_window(slot as u32, window) };
            }
        }
        #[cfg(target_os = "linux")]
        if let Some((display, surface)) = wayland_handles(handle) {
            self.update_slot(slot, move |browser, _| {
                browser.set_window_surface(display, surface)
            });
            if let Some(window) = self.window_in(slot) {
                self.update_slot(slot, move |browser, _| {
                    browser.enable_native_surface(window)
                });
            }
        }
    }

    pub fn view_placed(&self, view: ViewId, window: WindowId) {
        self.table().view_placed(view, window);
    }

    pub fn view_removed(&self, view: ViewId) {
        self.table().view_removed(view);
    }

    pub fn focus_changed(&self, slot: Slot, focused: bool) {
        self.table().focus_changed(slot, focused);
    }

    pub fn is_target(&self, slot: Slot) -> bool {
        self.table().target() == slot
    }

    pub fn window_in(&self, slot: Slot) -> Option<WindowId> {
        self.table().window_in(slot)
    }

    pub fn is_always_on_top(&self, slot: Slot) -> bool {
        lock(&self.windows)
            .get(slot)
            .is_some_and(|window| window.always_on_top)
    }

    pub fn set_always_on_top(&self, slot: Slot, on: bool) {
        let handle = {
            let mut windows = lock(&self.windows);
            let Some(window) = windows.get_mut(slot) else {
                return;
            };
            window.always_on_top = on;
            window.handle.clone()
        };
        if let Some(handle) = handle {
            handle.set_always_on_top(on);
        }
    }
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

pub struct ViewRoute<'a>(pub &'a Slots);

impl ViewRoute<'_> {
    pub fn update(&self, view: ViewId, f: impl FnOnce(&mut Browser, &mut Tx) + Send + 'static) {
        self.0.update_view(view, f);
    }

    pub fn update_if(
        &self,
        view: ViewId,
        f: impl FnOnce(&mut Browser, &mut Tx) -> bool + Send + 'static,
    ) {
        self.0.update_view_if(view, f);
    }
}

#[derive(Clone)]
pub struct SlotLink {
    pub slot: Slot,
    pub slots: Arc<Slots>,
}

impl SlotLink {
    pub fn is_target(&self) -> bool {
        self.slots.is_target(self.slot)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn windows_take_the_lowest_free_slot() {
        let mut table = SlotTable::with_slots(3);
        assert_eq!(table.opened(10), Opened::Bound(0));
        assert_eq!(table.opened(11), Opened::Bound(1));
        assert_eq!(table.opened(10), Opened::Bound(0));
        assert_eq!(table.closed(10), Some(0));
        assert_eq!(table.opened(12), Opened::Bound(0));
        assert_eq!(table.bound_slots(), 2);
    }

    #[test]
    fn a_window_beyond_the_slots_adds_one() {
        let mut table = SlotTable::with_slots(2);
        table.opened(1);
        table.opened(2);
        assert_eq!(table.opened(3), Opened::Added(2));
        assert_eq!(table.opened(4), Opened::Added(3));
        assert_eq!(table.closed(4), Some(3));
        assert_eq!(table.closed(2), Some(1));
        assert_eq!(table.opened(5), Opened::Bound(1));
        assert_eq!(table.window_in(2), Some(3));
    }

    #[test]
    fn views_route_to_the_slot_of_their_window() {
        let mut table = SlotTable::with_slots(2);
        table.opened(1);
        table.opened(2);
        table.view_placed(7, 2);
        assert_eq!(table.slot_of_view(7), Some(1));
        table.view_placed(7, 1);
        assert_eq!(table.slot_of_view(7), Some(0));
        table.closed(1);
        assert_eq!(table.slot_of_view(7), None);
    }

    #[test]
    fn the_target_is_the_focused_window_then_the_active_one() {
        let mut table = SlotTable::with_slots(3);
        table.opened(1);
        table.opened(2);
        assert_eq!(table.target(), 0);
        table.activated(2);
        assert_eq!(table.target(), 1);
        table.focus_changed(0, true);
        assert_eq!(table.target(), 0);
        table.focus_changed(0, false);
        assert_eq!(table.target(), 1);
        table.focus_changed(2, true);
        assert_eq!(table.target(), 1);
    }
}

#[cfg(target_os = "linux")]
fn wayland_handles(handle: &WindowHandle) -> Option<(usize, usize)> {
    use rune::raw_window_handle::{RawDisplayHandle, RawWindowHandle};
    let surface = handle
        .with_raw_handle(|window| match window.as_raw() {
            RawWindowHandle::Wayland(window) => Some(window.surface.as_ptr() as usize),
            _ => None,
        })
        .ok()??;
    let display = handle
        .with_raw_display_handle(|display| match display {
            RawDisplayHandle::Wayland(display) => Some(display.display.as_ptr() as usize),
            _ => None,
        })
        .ok()??;
    Some((display, surface))
}
