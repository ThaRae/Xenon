/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cell::Cell;
use std::collections::HashSet;
use std::sync::Arc;

use rune::{
    Drag, Ime, Key, KeyEvent, Modifier, Phase, Pixels, PointerButton, PointerEvent, Tx, WheelEvent,
};

use crate::bridge::PageSurfaces;
use crate::engine::{
    ChromeLayout, Engine, KeyEventKind, Modifiers, MouseButton, MouseEvent, MouseEventKind,
    RecentDirection, TabIndex, ViewId, Viewport, WindowId,
};
use crate::keys::{Platform, Shortcut, page_key_event, shortcut_for};
use crate::location::LocationBar;
use crate::page_prompts::{self, KeyContext, PagePrompts};
use crate::tab_list::{Activation, Tab, TabInsertion, TabList, WindowInfo, Windows};

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

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ChromeSettings {
    pub layout: ChromeLayout,
    pub cycle_tabs_in_recently_used_order: bool,
}

impl Default for ChromeSettings {
    fn default() -> ChromeSettings {
        ChromeSettings {
            layout: ChromeLayout::Classic,
            cycle_tabs_in_recently_used_order: true,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HoverCard {
    pub view: ViewId,
    pub memory: Option<String>,
}

pub struct Browser {
    engine: Engine,
    surfaces: Arc<PageSurfaces>,
    pub tabs: TabList,
    pub windows: Windows,
    cycling_recent_tabs: bool,
    pub location: LocationBar,
    pub engine_ready: bool,
    page_focused: bool,
    window_focused: bool,
    pressed_buttons: u32,
    forwarded_keys: HashSet<String>,
    pub page: PageGeometry,
    pub page_prompts: PagePrompts,
    system_dark: Cell<Option<bool>>,
    pub chrome: ChromeSettings,
    pub hover_card: Option<HoverCard>,
    pub vertical_tabs_expanded: bool,
}

impl Browser {
    pub fn new(surfaces: Arc<PageSurfaces>) -> Browser {
        Browser {
            engine: Engine,
            surfaces,
            tabs: TabList::default(),
            windows: Windows::default(),
            cycling_recent_tabs: false,
            location: LocationBar::default(),
            engine_ready: false,
            page_focused: false,
            window_focused: true,
            pressed_buttons: 0,
            forwarded_keys: HashSet::new(),
            page: PageGeometry::default(),
            page_prompts: PagePrompts::default(),
            system_dark: Cell::new(None),
            chrome: ChromeSettings::default(),
            hover_card: None,
            vertical_tabs_expanded: true,
        }
    }

    pub fn engine_ready(&mut self, _: &mut Tx) {
        self.engine_ready = true;
    }

    pub fn shown_window(&self) -> Option<WindowId> {
        self.windows.shown_id()
    }

    pub fn new_tab(&mut self, _: &mut Tx) {
        self.engine
            .new_tab(self.shown_window(), "", TabIndex::End, true);
        self.focus_location();
    }

    pub fn new_window(&mut self, private: bool) {
        self.engine.new_window(private);
    }

    pub fn reopen_closed_tab(&mut self) {
        self.engine.reopen_closed_tab(self.shown_window());
    }

    fn surface(&self, view: ViewId) -> Pixels {
        self.surfaces.surface(view).unwrap_or_default()
    }

    pub fn activate_tab(&mut self, view: ViewId, _: &mut Tx) {
        self.hover_card = None;
        self.engine.activate_tab(view);
    }

    pub fn close_tab(&mut self, view: ViewId, _: &mut Tx) {
        self.hover_card = None;
        self.engine.request_close_tab(view);
    }

    pub fn duplicate_tab(&mut self, view: ViewId) {
        self.engine.duplicate_tab(view);
    }

    pub fn reload_tab(&mut self, view: ViewId) {
        self.engine.reload(view);
    }

    pub fn move_tab_to_end(&mut self, view: ViewId) {
        if let Some(window) = self.shown_window() {
            self.engine.move_tab(view, window, TabIndex::End);
        }
    }

    pub fn close_tabs(&mut self, keep: ViewId, views: Vec<ViewId>) {
        if views.is_empty() {
            return;
        }
        if self
            .tabs
            .active_view()
            .is_some_and(|active| views.contains(&active))
        {
            self.engine.activate_tab(keep);
        }
        self.hover_card = None;
        for view in views {
            self.engine.request_close_tab(view);
        }
    }

    pub fn chrome_settings_changed(&mut self, layout: u8, cycle_tabs_in_recently_used_order: bool) {
        self.chrome = ChromeSettings {
            layout: ChromeLayout::from_raw(layout),
            cycle_tabs_in_recently_used_order,
        };
    }

    pub fn set_chrome_layout(&mut self, layout: ChromeLayout) {
        self.chrome.layout = layout;
        self.engine.set_chrome_layout(layout);
    }

    pub fn toggle_cycle_tabs_in_recently_used_order(&mut self) {
        let enabled = !self.chrome.cycle_tabs_in_recently_used_order;
        self.chrome.cycle_tabs_in_recently_used_order = enabled;
        self.engine.set_cycle_tabs_in_recently_used_order(enabled);
    }

    pub fn toggle_vertical_tabs_expanded(&mut self) {
        self.vertical_tabs_expanded = !self.vertical_tabs_expanded;
        self.hover_card = None;
    }

    pub fn tab_hover_changed(&mut self, view: ViewId, hovered: bool) {
        if hovered {
            if self
                .hover_card
                .as_ref()
                .is_some_and(|card| card.view == view)
            {
                return;
            }
            self.hover_card = Some(HoverCard { view, memory: None });
            self.engine.request_tab_memory(view);
        } else if self
            .hover_card
            .as_ref()
            .is_some_and(|card| card.view == view)
        {
            self.hover_card = None;
        }
    }

    pub fn tab_memory_reported(&mut self, view: ViewId, description: String) {
        if let Some(card) = self.hover_card.as_mut().filter(|card| card.view == view) {
            card.memory = (!description.is_empty()).then_some(description);
        }
    }

    pub fn close_active_tab(&mut self, tx: &mut Tx) {
        if let Some(view) = self.tabs.active_view() {
            self.close_tab(view, tx);
        }
    }

    pub fn toggle_tab_pinned(&mut self, view: ViewId) {
        if let Some(tab) = self.tabs.get(view) {
            self.engine.set_tab_pinned(view, !tab.pinned);
        }
    }

    pub fn move_tab(&mut self, view: ViewId, index: usize) {
        if let Some(window) = self.shown_window() {
            self.engine.move_tab(view, window, TabIndex::At(index));
        }
    }

    fn move_active_tab(&mut self, offset: isize) {
        let Some(view) = self.tabs.active_view() else {
            return;
        };
        let Some(index) = self
            .tabs
            .position(view)
            .and_then(|index| index.checked_add_signed(offset))
        else {
            return;
        };
        self.move_tab(view, index);
    }

    fn close_shown_window(&mut self) {
        if let Some(window) = self.shown_window() {
            self.engine.close_window(window);
        }
    }

    fn show_next_window(&mut self) {
        if let Some(window) = self.windows.least_recently_active_hidden() {
            self.engine.activate_window(window);
        }
    }

    fn cycle_recent_tabs(&mut self, direction: RecentDirection) {
        if !self.chrome.cycle_tabs_in_recently_used_order {
            let offset = match direction {
                RecentDirection::LessRecent => 1,
                RecentDirection::MoreRecent => -1,
            };
            self.activate_view(self.tabs.relative_view(offset));
            return;
        }
        if let Some(window) = self.shown_window() {
            self.cycling_recent_tabs = true;
            self.engine.cycle_recent_tabs(window, direction);
        }
    }

    fn end_recent_tab_cycle(&mut self) {
        if !self.cycling_recent_tabs {
            return;
        }
        self.cycling_recent_tabs = false;
        if let Some(window) = self.shown_window() {
            self.engine.end_recent_tab_cycle(window);
        }
    }

    fn observe_cycle_modifier(&mut self, key: &Key) {
        if let Key::Modifier {
            modifier: Modifier::ControlLeft | Modifier::ControlRight,
            pressed: false,
        } = key
        {
            self.end_recent_tab_cycle();
        }
    }

    pub fn window_opened(&mut self, window: WindowId, private: bool) {
        self.windows.opened(WindowInfo {
            id: window,
            private,
        });
        self.engine
            .set_window_viewport(Some(window), self.page.viewport());
    }

    pub fn window_closed(&mut self, window: WindowId, tx: &mut Tx) {
        let previous = self.tabs.active_view();
        let Some(closed) = self.windows.closed(window, &mut self.tabs) else {
            return;
        };
        for tab in closed {
            self.surfaces.close(tab.view);
        }
        if self.windows.is_empty() {
            tx.quit();
            return;
        }
        self.shown_tabs_changed(previous, tx);
    }

    pub fn window_activated(&mut self, window: WindowId, tx: &mut Tx) {
        let previous = self.tabs.active_view();
        if self.windows.activated(window, &mut self.tabs) {
            self.end_recent_tab_cycle();
            self.shown_tabs_changed(previous, tx);
        }
    }

    fn shown_tabs_changed(&mut self, previous: Option<ViewId>, tx: &mut Tx) {
        let current = self.tabs.active_view();
        if previous == current {
            return;
        }
        match current {
            Some(current) => self.apply(
                Some(Activation {
                    previous: previous.filter(|view| self.tabs.get(*view).is_none()),
                    current,
                }),
                tx,
            ),
            None => {
                self.reset_location();
                self.refresh_title(tx);
            }
        }
    }

    pub fn tab_inserted(&mut self, insertion: TabInsertion) {
        let tab = Tab::from_insertion(&insertion, self.surface(insertion.view));
        if !self.windows.contains(insertion.window) {
            self.window_opened(insertion.window, insertion.private);
        }
        if let Some(tabs) = self.windows.tabs_mut(insertion.window, &mut self.tabs) {
            tabs.insert(tab, insertion.index);
        }
    }

    pub fn tab_removed(&mut self, window: WindowId, view: ViewId, tx: &mut Tx) {
        let was_active = self.tabs.is_active(view);
        if self
            .hover_card
            .as_ref()
            .is_some_and(|card| card.view == view)
        {
            self.hover_card = None;
        }
        if let Some(tabs) = self.windows.tabs_mut(window, &mut self.tabs) {
            tabs.remove(view);
        }
        self.surfaces.close(view);
        if was_active {
            self.reset_location();
            self.refresh_title(tx);
        }
    }

    pub fn tab_moved(&mut self, view: ViewId, from: WindowId, to: WindowId, index: usize) {
        if from == to {
            if let Some(tabs) = self.windows.tabs_mut(to, &mut self.tabs) {
                tabs.move_to(view, index);
            }
            return;
        }
        let Some(tab) = self
            .windows
            .tabs_mut(from, &mut self.tabs)
            .and_then(|tabs| tabs.remove(view))
        else {
            return;
        };
        if let Some(tabs) = self.windows.tabs_mut(to, &mut self.tabs) {
            tabs.insert(tab, index);
        }
    }

    pub fn tab_activated(&mut self, window: WindowId, view: ViewId, tx: &mut Tx) {
        let Some(tabs) = self.windows.tabs_mut(window, &mut self.tabs) else {
            return;
        };
        let activation = tabs.activate(view);
        if self.shown_window() != Some(window) {
            return;
        }
        self.apply(activation, tx);
        if let Some(tab) = self.tabs.get_mut(view)
            && std::mem::take(&mut tab.focus_location_when_shown)
        {
            self.focus_location();
        }
    }

    pub fn tab_pinned_changed(&mut self, view: ViewId, pinned: bool) {
        let Some(window) = self.windows.window_of(view, &self.tabs) else {
            return;
        };
        if let Some(tab) = self
            .windows
            .tabs_mut(window, &mut self.tabs)
            .and_then(|tabs| tabs.get_mut(view))
        {
            tab.pinned = pinned;
        }
    }

    pub fn closed_tabs_changed(&mut self, can_reopen: bool) {
        self.windows.can_reopen_closed_tab = can_reopen;
    }

    fn apply(&mut self, activation: Option<Activation>, tx: &mut Tx) {
        let Some(Activation { previous, current }) = activation else {
            return;
        };
        if let Some(previous) = previous {
            self.release_forwarded_keys_of(previous);
            self.engine.set_focused(previous, false);
        }
        self.sync_viewport_of(current);
        self.engine.set_focused(current, self.page_has_keyboard());
        self.reset_location();
        self.refresh_title(tx);
    }

    fn refresh_title(&self, tx: &mut Tx) {
        tx.set_title(self.tabs.active().map_or(APP_NAME, Tab::display_title));
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
            if !self.location.edit.focused {
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

    pub(crate) fn focus_page(&mut self) {
        if self.page_focused {
            return;
        }
        self.page_focused = true;
        if let Some(view) = self.tabs.active_view() {
            self.engine.set_focused(view, self.window_focused);
        }
    }

    pub(crate) fn blur_page(&mut self) {
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
            self.end_recent_tab_cycle();
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
            Shortcut::NextTab => self.activate_view(self.tabs.relative_view(1)),
            Shortcut::PreviousTab => self.activate_view(self.tabs.relative_view(-1)),
            Shortcut::SelectTab(index) => self.activate_view(self.tabs.view_at(index)),
            Shortcut::LastTab => self.activate_view(self.tabs.last_view()),
            Shortcut::LessRecentTab => self.cycle_recent_tabs(RecentDirection::LessRecent),
            Shortcut::MoreRecentTab => self.cycle_recent_tabs(RecentDirection::MoreRecent),
            Shortcut::ReopenClosedTab => self.reopen_closed_tab(),
            Shortcut::NewWindow => self.new_window(false),
            Shortcut::NewPrivateWindow => self.new_window(true),
            Shortcut::CloseWindow => self.close_shown_window(),
            Shortcut::NextWindow => self.show_next_window(),
            Shortcut::MoveTabLeft => self.move_active_tab(-1),
            Shortcut::MoveTabRight => self.move_active_tab(1),
        }
    }

    fn activate_view(&mut self, view: Option<ViewId>) {
        if let Some(view) = view {
            self.engine.activate_tab(view);
        }
    }

    pub fn window_key(&mut self, event: &KeyEvent, tx: &mut Tx) -> bool {
        self.observe_cycle_modifier(&event.key);
        if self.location_shortcut(event, tx) {
            return true;
        }
        if page_prompts::key(self, event, tx, KeyContext::Window) {
            return true;
        }
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
        self.observe_cycle_modifier(&event.key);
        if self.location_shortcut(event, tx) {
            return true;
        }
        if page_prompts::key(self, event, tx, KeyContext::Page) {
            return true;
        }
        if let Some(shortcut) = shortcut_for(&event.key, event.mods, Platform::CURRENT) {
            self.run_shortcut(shortcut, tx);
            return true;
        }
        self.focus_page();
        self.page_key_down(event, tx)
    }

    pub fn location_key(&mut self, event: &KeyEvent, tx: &mut Tx) -> bool {
        self.observe_cycle_modifier(&event.key);
        if page_prompts::key(self, event, tx, KeyContext::Location) {
            return true;
        }
        if let Some(shortcut) = shortcut_for(&event.key, event.mods, Platform::CURRENT) {
            self.run_shortcut(shortcut, tx);
            return true;
        }
        self.location_field_key(event, tx)
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
        self.release_forwarded_keys_of(view);
    }

    fn release_forwarded_keys_of(&mut self, view: ViewId) {
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

    pub fn observe_system_dark(&self, dark: bool) {
        if self.system_dark.get() != Some(dark) {
            self.system_dark.set(Some(dark));
            self.engine.set_system_dark(dark);
        }
    }

    pub fn observe_scale(&self, scale: f32) {
        if self.page.scale.get() != scale {
            self.page.scale.set(scale);
            self.send_window_viewport();
            self.sync_active_viewport();
        }
    }

    pub fn page_width(&self) -> f32 {
        self.page.size.get().0
    }

    pub fn page_laid_out(&mut self, [_, _, width, height]: [f32; 4]) {
        if self.page.size.get() != (width, height) {
            self.page.size.set((width, height));
            self.send_window_viewport();
        }
        self.sync_active_viewport();
    }

    fn send_window_viewport(&self) {
        if let Some(window) = self.shown_window() {
            self.engine
                .set_window_viewport(Some(window), self.page.viewport());
        }
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
                self.end_recent_tab_cycle();
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

fn mouse_button(button: PointerButton) -> MouseButton {
    match button {
        PointerButton::Secondary => MouseButton::Secondary,
        PointerButton::Middle => MouseButton::Middle,
        PointerButton::Back => MouseButton::Backward,
        PointerButton::Forward => MouseButton::Forward,
        _ => MouseButton::Primary,
    }
}
