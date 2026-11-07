/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};

use rune::{AppHandle, Tx, WindowHandle};

use crate::engine::{ViewId, WindowId};
use crate::state::Browser;

pub const WINDOW_SLOTS: usize = 12;
pub const PRIMARY_SLOT: usize = 0;

pub type Slot = usize;

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
    Waiting,
}

#[derive(Debug, Default, PartialEq, Eq)]
pub struct Closed {
    pub slot: Option<Slot>,
    pub next: Option<WindowId>,
}

#[derive(Default)]
pub struct SlotTable {
    bound: Vec<Option<WindowId>>,
    waiting: Vec<WindowId>,
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
        if self.waiting.contains(&window) {
            return Opened::Waiting;
        }
        match self.bound.iter().position(Option::is_none) {
            Some(slot) => {
                self.bound[slot] = Some(window);
                Opened::Bound(slot)
            }
            None => {
                self.waiting.push(window);
                Opened::Waiting
            }
        }
    }

    pub fn closed(&mut self, window: WindowId) -> Closed {
        self.views.retain(|_, owner| *owner != window);
        self.waiting.retain(|waiting| *waiting != window);
        if self.active == Some(window) {
            self.active = None;
        }
        let Some(slot) = self.slot_of(window) else {
            return Closed::default();
        };
        self.bound[slot] = None;
        if self.focused == Some(slot) {
            self.focused = None;
        }
        let next = (!self.waiting.is_empty()).then(|| self.waiting.remove(0));
        if let Some(next) = next {
            self.bound[slot] = Some(next);
        }
        Closed {
            slot: Some(slot),
            next,
        }
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

pub struct Slots {
    handles: Vec<AppHandle<Browser>>,
    windows: Vec<OnceLock<WindowHandle>>,
    table: Mutex<SlotTable>,
    always_on_top: Vec<AtomicBool>,
    scale_bits: AtomicU32,
    placements: Mutex<HashMap<WindowId, Placement>>,
    pending: Mutex<HashMap<Slot, Placement>>,
}

impl Slots {
    pub fn new(handles: Vec<AppHandle<Browser>>) -> Arc<Slots> {
        let count = handles.len();
        Arc::new(Slots {
            handles,
            windows: (0..count).map(|_| OnceLock::new()).collect(),
            table: Mutex::new(SlotTable::with_slots(count)),
            always_on_top: (0..count).map(|_| AtomicBool::new(false)).collect(),
            scale_bits: AtomicU32::new(1.0f32.to_bits()),
            placements: Mutex::new(HashMap::new()),
            pending: Mutex::new(HashMap::new()),
        })
    }

    pub fn table(&self) -> MutexGuard<'_, SlotTable> {
        self.table
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    pub fn handle(&self, slot: Slot) -> Option<&AppHandle<Browser>> {
        self.handles.get(slot)
    }

    pub fn set_window_handle(&self, slot: Slot, handle: WindowHandle) {
        if let Some(cell) = self.windows.get(slot) {
            let _ = cell.set(handle);
        }
    }

    pub fn window_handle(&self, slot: Slot) -> Option<&WindowHandle> {
        self.windows.get(slot)?.get()
    }

    pub fn observe_scale(&self, scale: f32) {
        if scale > 0.0 {
            self.scale_bits.store(scale.to_bits(), Ordering::Relaxed);
        }
    }

    pub fn scale(&self) -> f32 {
        f32::from_bits(self.scale_bits.load(Ordering::Relaxed))
    }

    pub fn update(&self, f: impl Fn(&mut Browser, &mut Tx) + Send + Sync + 'static) {
        let f = Arc::new(f);
        for handle in &self.handles {
            let f = f.clone();
            handle.update(move |browser, tx| f(browser, tx));
        }
    }

    pub fn update_slot(&self, slot: Slot, f: impl FnOnce(&mut Browser, &mut Tx) + Send + 'static) {
        if let Some(handle) = self.handles.get(slot) {
            handle.update(f);
        }
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
        if let Some(handle) = self.handles.first() {
            handle.quit();
        }
    }

    pub fn window_opened(&self, window: WindowId, private: bool) {
        let opened = self.table().opened(window);
        if let Opened::Bound(slot) = opened {
            self.bind(slot, window);
        }
        self.update(move |browser, _| browser.window_opened(window, private));
        if let Opened::Bound(slot) = opened {
            self.show(slot);
        }
    }

    pub fn window_closed(&self, window: WindowId) {
        let closed = self.table().closed(window);
        self.update(move |browser, tx| browser.window_closed(window, tx));
        let Some(slot) = closed.slot else {
            return;
        };
        match closed.next {
            Some(next) => {
                self.bind(slot, next);
                self.show(slot);
            }
            None => {
                if let Some(handle) = self.handles.get(slot) {
                    handle.hide();
                }
            }
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
            && let Some(handle) = self.handles.get(slot)
        {
            handle.open();
        }
    }

    pub fn show(&self, slot: Slot) {
        if let Some(handle) = self.handles.get(slot) {
            handle.open();
        }
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
        self.always_on_top
            .get(slot)
            .is_some_and(|flag| flag.load(Ordering::Relaxed))
    }

    pub fn set_always_on_top(&self, slot: Slot, on: bool) {
        if let Some(flag) = self.always_on_top.get(slot) {
            flag.store(on, Ordering::Relaxed);
        }
        if let Some(window) = self.window_handle(slot) {
            window.set_always_on_top(on);
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
        assert_eq!(
            table.closed(10),
            Closed {
                slot: Some(0),
                next: None
            }
        );
        assert_eq!(table.opened(12), Opened::Bound(0));
        assert_eq!(table.bound_slots(), 2);
    }

    #[test]
    fn a_window_beyond_the_slots_waits_for_one() {
        let mut table = SlotTable::with_slots(2);
        table.opened(1);
        table.opened(2);
        assert_eq!(table.opened(3), Opened::Waiting);
        assert_eq!(table.opened(4), Opened::Waiting);
        assert_eq!(table.closed(4), Closed::default());
        assert_eq!(
            table.closed(2),
            Closed {
                slot: Some(1),
                next: Some(3)
            }
        );
        assert_eq!(table.window_in(1), Some(3));
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
