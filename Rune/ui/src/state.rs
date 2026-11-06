/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cell::Cell;
use std::collections::HashSet;
use std::sync::Arc;

use rune::{
    Drag, Ime, Key, KeyEvent, Phase, Pixels, PointerButton, PointerEvent, TextEdit, Tx, WheelEvent,
};

use crate::bridge::PageSurfaces;
use crate::engine::{
    Engine, KeyEventKind, Modifiers, MouseButton, MouseEvent, MouseEventKind, ViewId, Viewport,
};
use crate::keys::{Platform, Shortcut, page_key_event, shortcut_for};
use crate::tab_list::{Activation, Placement, Tab, TabList};

pub const NEW_TAB_URL: &str = "about:newtab";
pub const APP_NAME: &str = "Xenon";
pub const INITIAL_WINDOW_SIZE: (f32, f32) = (1280.0, 800.0);
pub const TAB_STRIP_HEIGHT: f32 = 40.0;
pub const TOOLBAR_HEIGHT: f32 = 44.0;

pub struct PageGeometry {
    size: Cell<(f32, f32)>,
    scale: Cell<f32>,
}

impl Default for PageGeometry {
    fn default() -> PageGeometry {
        let (width, height) = INITIAL_WINDOW_SIZE;
        PageGeometry {
            size: Cell::new((width, height - TAB_STRIP_HEIGHT - TOOLBAR_HEIGHT)),
            scale: Cell::new(1.0),
        }
    }
}

impl PageGeometry {
    pub fn scale(&self) -> f32 {
        self.scale.get()
    }

    pub fn viewport(&self) -> Viewport {
        let (width, height) = self.size.get();
        Viewport::from_points(width, height, self.scale.get())
    }

    pub fn to_device(&self, x: f32, y: f32) -> (f32, f32) {
        let scale = self.scale.get();
        (x * scale, y * scale)
    }
}

pub struct Browser {
    engine: Engine,
    surfaces: Arc<PageSurfaces>,
    pub tabs: TabList,
    pub location: TextEdit,
    location_had_focus: bool,
    pub engine_ready: bool,
    page_focused: bool,
    window_focused: bool,
    pressed_buttons: u32,
    forwarded_keys: HashSet<String>,
    pub page: PageGeometry,
}

impl Browser {
    pub fn new(surfaces: Arc<PageSurfaces>) -> Browser {
        Browser {
            engine: Engine,
            surfaces,
            tabs: TabList::default(),
            location: TextEdit::new(""),
            location_had_focus: false,
            engine_ready: false,
            page_focused: false,
            window_focused: true,
            pressed_buttons: 0,
            forwarded_keys: HashSet::new(),
            page: PageGeometry::default(),
        }
    }

    pub fn engine_ready(&mut self, tx: &mut Tx) {
        self.engine_ready = true;
        if self.tabs.is_empty() {
            self.new_tab(tx);
        }
    }

    pub fn new_tab(&mut self, tx: &mut Tx) {
        self.open_tab(NEW_TAB_URL, tx);
        self.focus_location();
    }

    fn open_tab(&mut self, url: &str, tx: &mut Tx) {
        let viewport = self.page.viewport();
        let view = self.engine.create_view(url, viewport);
        let tab = Tab::new(view, url, self.surface(view));
        tab.sent_viewport.set(Some(viewport));
        let activation = self.tabs.insert(tab, Placement::End, true);
        self.apply(activation, tx);
    }

    fn surface(&self, view: ViewId) -> Pixels {
        self.surfaces.surface(view).unwrap_or_default()
    }

    pub fn view_opened(&mut self, opener: ViewId, view: ViewId, activate: bool, tx: &mut Tx) {
        if self.tabs.get(view).is_some() {
            return;
        }
        let tab = Tab::new(view, "", self.surface(view));
        let activation = self.tabs.insert(tab, Placement::After(opener), activate);
        if activation.is_none() && !self.tabs.is_active(view) {
            self.engine.set_visible(view, false);
        }
        self.apply(activation, tx);
        self.sync_viewport_of(view);
    }

    pub fn activate_tab(&mut self, view: ViewId, tx: &mut Tx) {
        let activation = self.tabs.activate(view);
        self.apply(activation, tx);
    }

    pub fn close_tab(&mut self, view: ViewId, tx: &mut Tx) {
        let Some((closed, activation)) = self.tabs.remove(view) else {
            return;
        };
        self.engine.close_view(closed.view);
        self.surfaces.close(closed.view);
        if self.tabs.is_empty() {
            tx.quit();
            return;
        }
        self.apply(activation, tx);
    }

    pub fn close_active_tab(&mut self, tx: &mut Tx) {
        if let Some(view) = self.tabs.active_view() {
            self.close_tab(view, tx);
        }
    }

    fn apply(&mut self, activation: Option<Activation>, tx: &mut Tx) {
        let Some(Activation { previous, current }) = activation else {
            return;
        };
        if let Some(previous) = previous {
            self.engine.set_focused(previous, false);
            self.engine.set_visible(previous, false);
        }
        self.engine.set_visible(current, true);
        self.sync_viewport_of(current);
        self.engine.set_focused(current, self.page_has_keyboard());
        self.reset_location();
        self.refresh_title(tx);
    }

    fn refresh_title(&self, tx: &mut Tx) {
        tx.set_title(self.tabs.active().map_or(APP_NAME, Tab::display_title));
    }

    fn reset_location(&mut self) {
        let shown = self
            .tabs
            .active()
            .map(|tab| location_text(&tab.url))
            .unwrap_or_default();
        if self.location.text != shown || !self.location.is_pristine() {
            self.location.replace_text(shown);
        }
    }

    pub fn page_has_keyboard(&self) -> bool {
        self.page_focused && self.window_focused
    }

    pub fn title_changed(&mut self, view: ViewId, title: String, tx: &mut Tx) {
        let Some(tab) = self.tabs.get_mut(view) else {
            return;
        };
        tab.title = title;
        if self.tabs.is_active(view) {
            self.refresh_title(tx);
        }
    }

    pub fn url_changed(&mut self, view: ViewId, url: String, tx: &mut Tx) {
        let Some(tab) = self.tabs.get_mut(view) else {
            return;
        };
        tab.url = url;
        if self.tabs.is_active(view) {
            if !self.location.focused {
                self.reset_location();
            }
            self.refresh_title(tx);
        }
    }

    pub fn loading_changed(&mut self, view: ViewId, loading: bool) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.loading = loading;
        }
    }

    pub fn history_changed(&mut self, view: ViewId, can_go_back: bool, can_go_forward: bool) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.can_go_back = can_go_back;
            tab.can_go_forward = can_go_forward;
        }
    }

    pub fn favicon_changed(&mut self, view: ViewId, favicon: Option<Pixels>) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.favicon = favicon;
        }
    }

    pub fn cursor_changed(&mut self, view: ViewId, cursor: u32) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.cursor = cursor;
        }
    }

    pub fn link_hovered(&mut self, view: ViewId, url: String) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.hovered_link = url;
        }
    }

    pub fn crashed(&mut self, view: ViewId) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.crashed = true;
            tab.loading = false;
            tab.hovered_link.clear();
        }
    }

    pub fn go_back(&mut self) {
        if let Some(tab) = self.tabs.active().filter(|tab| tab.can_go_back) {
            self.engine.go_back(tab.view);
        }
    }

    pub fn go_forward(&mut self) {
        if let Some(tab) = self.tabs.active().filter(|tab| tab.can_go_forward) {
            self.engine.go_forward(tab.view);
        }
    }

    pub fn reload(&mut self) {
        let engine = self.engine;
        if let Some(view) = self.tabs.active_view()
            && let Some(tab) = self.tabs.get_mut(view)
        {
            tab.crashed = false;
            engine.reload(view);
        }
    }

    pub fn reload_or_stop(&mut self) {
        match self.tabs.active() {
            Some(tab) if tab.loading => self.engine.stop(tab.view),
            Some(_) => self.reload(),
            None => {}
        }
    }

    pub fn focus_location(&mut self) {
        self.location.focused = true;
        self.select_location();
        self.blur_page();
    }

    fn select_location(&mut self) {
        let end = self.location.text.len();
        self.location.set_selection(0, end);
    }

    pub fn submit_location(&mut self, tx: &mut Tx) {
        let input = self.location.text.trim().to_owned();
        if input.is_empty() {
            return;
        }
        match self.tabs.active_view() {
            Some(view) => self.engine.load(view, &input),
            None => self.open_tab(&input, tx),
        }
        tx.blur();
        self.location.focused = false;
        self.location_had_focus = false;
        self.focus_page();
    }

    pub fn revert_location(&mut self, tx: &mut Tx) {
        self.reset_location();
        tx.blur();
        self.location.focused = false;
        self.location_had_focus = false;
        self.focus_page();
    }

    pub fn track_location_focus(&mut self) -> bool {
        if self.location.focused == self.location_had_focus {
            return false;
        }
        self.location_had_focus = self.location.focused;
        if self.location.focused {
            self.select_location();
            self.blur_page();
        } else {
            self.reset_location();
        }
        true
    }

    fn focus_page(&mut self) {
        if self.page_focused {
            return;
        }
        self.page_focused = true;
        if let Some(view) = self.tabs.active_view() {
            self.engine.set_focused(view, self.window_focused);
        }
    }

    fn blur_page(&mut self) {
        if !self.page_focused {
            return;
        }
        self.page_focused = false;
        self.release_forwarded_keys();
        if let Some(view) = self.tabs.active_view() {
            self.engine.set_focused(view, false);
        }
    }

    pub fn window_focus_changed(&mut self, focused: bool) {
        self.window_focused = focused;
        if !focused {
            self.release_forwarded_keys();
        }
        if let Some(view) = self.tabs.active_view() {
            self.engine.set_focused(view, self.page_has_keyboard());
        }
    }

    pub fn run_shortcut(&mut self, shortcut: Shortcut, tx: &mut Tx) {
        match shortcut {
            Shortcut::NewTab => self.new_tab(tx),
            Shortcut::CloseTab => self.close_active_tab(tx),
            Shortcut::FocusLocation => self.focus_location(),
            Shortcut::Reload => self.reload(),
            Shortcut::Back => self.go_back(),
            Shortcut::Forward => self.go_forward(),
            Shortcut::NextTab => {
                let activation = self.tabs.activate_relative(1);
                self.apply(activation, tx);
            }
            Shortcut::PreviousTab => {
                let activation = self.tabs.activate_relative(-1);
                self.apply(activation, tx);
            }
            Shortcut::SelectTab(index) => {
                let activation = self.tabs.activate_index(index);
                self.apply(activation, tx);
            }
            Shortcut::LastTab => {
                let activation = self.tabs.activate_last();
                self.apply(activation, tx);
            }
        }
    }

    pub fn window_key(&mut self, event: &KeyEvent, tx: &mut Tx) -> bool {
        if let Some(shortcut) = shortcut_for(&event.key, event.mods, Platform::CURRENT) {
            self.run_shortcut(shortcut, tx);
            return true;
        }
        if self.page_focused && tx.focused().is_none() {
            return self.page_key_down(event, tx);
        }
        false
    }

    pub fn page_key(&mut self, event: &KeyEvent, tx: &mut Tx) -> bool {
        if let Some(shortcut) = shortcut_for(&event.key, event.mods, Platform::CURRENT) {
            self.run_shortcut(shortcut, tx);
            return true;
        }
        self.focus_page();
        self.page_key_down(event, tx)
    }

    pub fn location_key(&mut self, event: &KeyEvent, tx: &mut Tx) -> bool {
        if let Some(shortcut) = shortcut_for(&event.key, event.mods, Platform::CURRENT) {
            self.run_shortcut(shortcut, tx);
            return true;
        }
        if event.key == Key::Escape {
            self.revert_location(tx);
            return true;
        }
        false
    }

    fn page_key_down(&mut self, event: &KeyEvent, tx: &Tx) -> bool {
        let Some(view) = self.tabs.active_view() else {
            return false;
        };
        let Some(key) = page_key_event(
            KeyEventKind::Down,
            &event.key,
            event.mods,
            tx.physical_key(),
            tx.key_repeat(),
        ) else {
            return false;
        };
        match key.kind {
            KeyEventKind::Down => self.forwarded_keys.insert(key.code.clone()),
            KeyEventKind::Up => self.forwarded_keys.remove(&key.code),
        };
        self.engine.key_event(view, &key);
        true
    }

    pub fn page_key_up(&mut self, event: &KeyEvent, tx: &Tx) -> bool {
        let Some(view) = self.tabs.active_view() else {
            return false;
        };
        let Some(key) = page_key_event(
            KeyEventKind::Up,
            &event.key,
            event.mods,
            tx.physical_key(),
            false,
        ) else {
            return false;
        };
        if !self.forwarded_keys.remove(&key.code) {
            return false;
        }
        self.engine.key_event(view, &key);
        true
    }

    fn release_forwarded_keys(&mut self) {
        let Some(view) = self.tabs.active_view() else {
            self.forwarded_keys.clear();
            return;
        };
        for code in self.forwarded_keys.drain() {
            let key = crate::engine::KeyEvent {
                kind: KeyEventKind::Up,
                key: String::new(),
                code,
                modifiers: Modifiers::default(),
                repeat: false,
            };
            self.engine.key_event(view, &key);
        }
    }

    pub fn ime(&mut self, ime: &Ime) -> bool {
        let (Ime::Commit(text), Some(view)) = (ime, self.tabs.active_view()) else {
            return false;
        };
        if !self.page_has_keyboard() {
            return false;
        }
        self.engine.insert_text(view, text);
        true
    }

    pub fn observe_scale(&self, scale: f32) {
        if self.page.scale.get() != scale {
            self.page.scale.set(scale);
            self.sync_active_viewport();
        }
    }

    pub fn page_laid_out(&mut self, [_, _, width, height]: [f32; 4]) {
        self.page.size.set((width, height));
        self.sync_active_viewport();
    }

    fn sync_active_viewport(&self) {
        if let Some(view) = self.tabs.active_view() {
            self.sync_viewport_of(view);
        }
    }

    fn sync_viewport_of(&self, view: ViewId) {
        let Some(tab) = self.tabs.get(view) else {
            return;
        };
        let viewport = self.page.viewport();
        if tab.sent_viewport.get() != Some(viewport) {
            tab.sent_viewport.set(Some(viewport));
            self.engine.set_viewport(view, viewport);
        }
    }

    fn send_mouse(
        &self,
        kind: MouseEventKind,
        x: f32,
        y: f32,
        button: Option<MouseButton>,
        modifiers: rune::Mods,
    ) {
        let Some(view) = self.tabs.active_view() else {
            return;
        };
        let (x, y) = self.page.to_device(x, y);
        let event = MouseEvent {
            button,
            buttons: self.pressed_buttons,
            modifiers: Modifiers::from_rune(modifiers),
            ..MouseEvent::new(kind, x, y)
        };
        self.engine.mouse_event(view, &event);
    }

    pub fn page_drag(&mut self, drag: &Drag, tx: &Tx) {
        let button = mouse_button(tx.button());
        let kind = match drag.phase {
            Phase::Down => {
                self.focus_page();
                self.pressed_buttons |= button.bit();
                if drag.clicks == 2 {
                    MouseEventKind::DoubleClick
                } else {
                    MouseEventKind::Down
                }
            }
            Phase::Move => MouseEventKind::Move,
            Phase::Up => {
                self.pressed_buttons &= !button.bit();
                MouseEventKind::Up
            }
        };
        let button = (kind != MouseEventKind::Move).then_some(button);
        self.send_mouse(kind, drag.x, drag.y, button, tx.mods());
    }

    pub fn page_pointer_moved(&mut self, pointer: PointerEvent, tx: &Tx) {
        if self.pressed_buttons == 0 {
            self.send_mouse(MouseEventKind::Move, pointer.x, pointer.y, None, tx.mods());
        }
    }

    pub fn page_hover_changed(&mut self, hovered: bool, tx: &Tx) {
        if !hovered && self.pressed_buttons == 0 {
            self.send_mouse(MouseEventKind::Leave, 0.0, 0.0, None, tx.mods());
            if let Some(view) = self.tabs.active_view()
                && let Some(tab) = self.tabs.get_mut(view)
            {
                tab.hovered_link.clear();
            }
        }
    }

    pub fn page_secondary_click(&mut self, pointer: PointerEvent, tx: &Tx) {
        self.focus_page();
        let button = MouseButton::Secondary;
        self.pressed_buttons |= button.bit();
        self.send_mouse(
            MouseEventKind::Down,
            pointer.x,
            pointer.y,
            Some(button),
            tx.mods(),
        );
        self.pressed_buttons &= !button.bit();
        self.send_mouse(
            MouseEventKind::Up,
            pointer.x,
            pointer.y,
            Some(button),
            tx.mods(),
        );
    }

    pub fn page_wheel(&mut self, wheel: WheelEvent) {
        let Some(view) = self.tabs.active_view() else {
            return;
        };
        let (x, y) = self.page.to_device(wheel.x, wheel.y);
        let scale = self.page.scale();
        let event = MouseEvent {
            buttons: self.pressed_buttons,
            modifiers: Modifiers::from_rune(wheel.mods),
            wheel_delta: (-wheel.dx * scale, -wheel.dy * scale),
            ..MouseEvent::new(MouseEventKind::Wheel, x, y)
        };
        self.engine.mouse_event(view, &event);
    }
}

fn location_text(url: &str) -> String {
    if url == NEW_TAB_URL {
        String::new()
    } else {
        url.to_owned()
    }
}

fn mouse_button(button: PointerButton) -> MouseButton {
    match button {
        PointerButton::Secondary => MouseButton::Secondary,
        PointerButton::Middle => MouseButton::Middle,
        PointerButton::Back => MouseButton::Backward,
        PointerButton::Forward => MouseButton::Forward,
        _ => MouseButton::Primary,
    }
}
