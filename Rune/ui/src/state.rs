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

use crate::accessibility::PageAccessibility;
use crate::bridge::PageSurfaces;
use crate::chrome_popup::{self, DrawnMenu, PopupMenu};
use crate::engine::{
    Action, ChromeLayout, Engine, KeyEventKind, Modifiers, MouseButton, MouseEvent, MouseEventKind,
    RecentDirection, TabIndex, ViewId, Viewport, WindowId,
};
use crate::keys::{Platform, Shortcut, page_key_event, shortcut_for};
use crate::location::LocationBar;
use crate::page_prompts::{self, KeyContext, PagePrompts};
use crate::split_view::SplitViews;
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

#[derive(Clone, Debug, PartialEq)]
pub struct Download {
    pub id: u64,
    pub file_name: String,
    pub status_text: String,
    pub status: u32,
    pub progress: Option<f32>,
    pub can_resume: bool,
    pub private: bool,
}

impl Download {
    pub fn is_active(&self) -> bool {
        self.status == crate::ffi::XENON_DOWNLOAD_IN_PROGRESS
            || self.status == crate::ffi::XENON_DOWNLOAD_PAUSED
    }
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
    pub(crate) window_focused: bool,
    pressed_buttons: u32,
    forwarded_keys: HashSet<String>,
    pub page: PageGeometry,
    pub page_prompts: PagePrompts,
    system_dark: Cell<Option<bool>>,
    pub chrome: ChromeSettings,
    pub hover_card: Option<HoverCard>,
    pub vertical_tabs_expanded: bool,
    pub downloads: Vec<Download>,
    pub downloads_popover_open: bool,
    history_menu_direction: i32,
    pub shield_popover_open: bool,
    pub accessibility: PageAccessibility,
    pub tab_groups: crate::tab_groups::TabGroups,
    pub hibernation: crate::hibernation::TabHibernation,
    pub task_manager: crate::task_manager::TaskManager,
    pub chrome_menu: Option<DrawnMenu>,
    pub window: crate::window_state::WindowState,
    pub extensions: crate::extensions::Extensions,
    pub split: SplitViews,
    pub bookmarks: crate::bookmarks::Bookmarks,
    pub element_filter: Option<crate::element_picker::ElementFilter>,
    pub picture_in_picture: Vec<crate::picture_in_picture::PictureInPicture>,
    pub updates: crate::updates::Updates,
    pub screenshot: Option<crate::screenshots::ScreenshotSelection>,
    pub crash_report_offer: Option<String>,
    #[cfg(target_os = "linux")]
    wayland: Option<crate::wayland_page::WindowPage>,
    #[cfg(target_os = "linux")]
    native_views: HashSet<ViewId>,
    #[cfg(target_os = "linux")]
    native_handles: (usize, usize),
    #[cfg(target_os = "linux")]
    native_blocked_by_split: bool,
    #[cfg(target_os = "linux")]
    native_shown_view: Option<ViewId>,
    #[cfg(target_os = "linux")]
    native_waiting_for_cpu: bool,
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
            downloads: Vec::new(),
            downloads_popover_open: false,
            history_menu_direction: -1,
            shield_popover_open: false,
            accessibility: PageAccessibility::default(),
            tab_groups: crate::tab_groups::TabGroups::default(),
            hibernation: Default::default(),
            task_manager: Default::default(),
            chrome_menu: None,
            window: crate::window_state::WindowState::default(),
            extensions: crate::extensions::Extensions::default(),
            split: SplitViews::default(),
            bookmarks: crate::bookmarks::Bookmarks::default(),
            element_filter: None,
            picture_in_picture: Vec::new(),
            updates: crate::updates::Updates::default(),
            screenshot: None,
            crash_report_offer: None,
            #[cfg(target_os = "linux")]
            wayland: None,
            #[cfg(target_os = "linux")]
            native_views: HashSet::new(),
            #[cfg(target_os = "linux")]
            native_handles: (0, 0),
            #[cfg(target_os = "linux")]
            native_blocked_by_split: false,
            #[cfg(target_os = "linux")]
            native_shown_view: None,
            #[cfg(target_os = "linux")]
            native_waiting_for_cpu: false,
        }
    }

    pub fn engine_ready(&mut self, _: &mut Tx) {
        self.engine_ready = true;
    }

    pub fn shown_window(&self) -> Option<WindowId> {
        self.windows.shown_id()
    }

    pub fn shows_view(&self, view: ViewId) -> bool {
        self.extensions
            .popup
            .as_ref()
            .is_some_and(|popup| popup.view == view)
            || self
                .windows
                .window_of(view, &self.tabs)
                .is_some_and(|window| Some(window) == self.shown_window())
    }

    #[cfg(target_os = "linux")]
    pub fn set_window_surface(&mut self, display: usize, surface: usize) {
        self.native_handles = (display, surface);
        self.native_views.clear();
        self.wayland = if display != 0 && surface != 0 {
            unsafe {
                crate::wayland_page::WindowPage::new(
                    display as *mut std::ffi::c_void,
                    surface as *mut std::ffi::c_void,
                )
            }
        } else {
            None
        };
        if self.wayland.is_none() {
            self.native_handles = (0, 0);
        }
        if let Some(window) = self.shown_window() {
            self.engine
                .set_window_surface(window, self.native_handles.0, self.native_handles.1);
        }
    }

    #[cfg(not(target_os = "linux"))]
    pub fn set_window_surface(&mut self, _: usize, _: usize) {}

    #[cfg(target_os = "linux")]
    pub(crate) fn enable_native_surface(&self, window: WindowId) {
        self.engine
            .set_window_surface(window, self.native_handles.0, self.native_handles.1);
    }

    #[cfg(target_os = "linux")]
    pub fn dmabuf_frame(&mut self, view: ViewId, frame: crate::wayland_page::DmaBufFrame) -> bool {
        if self.split_blocks_native() || !self.tabs.is_active(view) || !self.shows_view(view) {
            return false;
        }
        let rect = self.page_rect();
        let Some(wayland) = self.wayland.as_mut() else {
            return false;
        };
        wayland.set_rect(rect);
        wayland.present(view, frame);
        self.native_waiting_for_cpu = false;
        self.native_views.insert(view)
    }

    pub fn cpu_frame(&mut self, view: ViewId) -> bool {
        #[cfg(target_os = "linux")]
        {
            let was_native = self.native_views.remove(&view);
            let was_waiting = self.native_waiting_for_cpu;
            if (was_native || was_waiting)
                && let Some(wayland) = self.wayland.as_mut()
            {
                wayland.hide();
                self.native_shown_view = None;
                self.native_waiting_for_cpu = false;
            }
            return was_native || was_waiting;
        }
        #[cfg(not(target_os = "linux"))]
        {
            let _ = view;
            false
        }
    }

    pub fn native_page_active(&self) -> bool {
        #[cfg(target_os = "linux")]
        {
            let Some(view) = self.tabs.active_view() else {
                return false;
            };
            return !self.split_blocks_native() && self.native_views.contains(&view);
        }
        #[cfg(not(target_os = "linux"))]
        false
    }

    #[cfg(target_os = "linux")]
    fn split_blocks_native(&self) -> bool {
        self.split.shown(&self.tabs).is_some()
    }

    #[cfg(target_os = "linux")]
    fn native_overlay_open(&self) -> bool {
        let active = self.tabs.active_view();
        let prompts = active.and_then(|view| self.page_prompts.of(view));
        let page_prompts = prompts.is_some_and(|prompts| {
            prompts.dialog.is_some()
                || prompts.find.is_some()
                || !prompts.permissions.is_empty()
                || prompts.tooltip.is_some()
                || prompts.color_picker.is_some()
                || prompts.date_picker.is_some()
                || prompts.web_authn.is_some()
                || prompts.crash_review.is_some()
        });
        let tab_overlay = self
            .tabs
            .active()
            .is_some_and(|tab| !tab.hovered_link.is_empty() || tab.crashed || tab.unresponsive);
        page_prompts
            || tab_overlay
            || self.page_prompts.menu.is_some()
            || self.screenshot.is_some()
            || !self.picture_in_picture.is_empty()
            || self.element_filter.is_some()
            || self.chrome_menu.is_some()
            || self.extensions.popup.is_some()
            || self.extensions.dialog_open
            || !self.extensions.prompts.is_empty()
            || self.extensions.confirmation.is_some()
            || self.extensions.error.is_some()
            || self.hover_card.is_some()
            || self.downloads_popover_open
            || self.shield_popover_open
            || self.updates.popover_open
            || self.task_manager.open
            || self.crash_report_offer.is_some()
            || self.tab_groups.editor.is_some()
            || self.window.stop_downloads_prompt.is_some()
            || self.bookmarks.dialog.is_some()
            || self.split.tab_drag.is_some()
            || self.location.edit.focused
            || self.location.popup_visible
            || self.location.site_info.is_some()
            || self.location.menu_request.is_some()
    }

    #[cfg(target_os = "linux")]
    fn page_rect(&self) -> (i32, i32, i32, i32) {
        let (x, y) = self.split.page_origin;
        let (width, height) = self.page.size.get();
        (
            x.round() as i32,
            y.round() as i32,
            width.round() as i32,
            height.round() as i32,
        )
    }

    pub fn poll_native(&mut self) -> bool {
        #[cfg(target_os = "linux")]
        {
            let page_rect = self.page_rect();
            let window = self.shown_window();
            let active_view = self.tabs.active_view();
            let blocked = self.split.shown(&self.tabs).is_some() || self.native_overlay_open();
            let Some(wayland) = self.wayland.as_mut() else {
                return false;
            };
            wayland.poll();
            if blocked != self.native_blocked_by_split {
                self.native_blocked_by_split = blocked;
                self.native_views.clear();
                self.native_shown_view = None;
                let (display, surface) = if blocked {
                    self.native_waiting_for_cpu = true;
                    (0, 0)
                } else {
                    self.native_waiting_for_cpu = false;
                    wayland.hide();
                    self.native_handles
                };
                if let Some(window) = window {
                    self.engine.set_window_surface(window, display, surface);
                }
            }
            let active = active_view.filter(|view| self.native_views.contains(view));
            if active != self.native_shown_view {
                self.native_shown_view = active;
                match active {
                    Some(view) => {
                        wayland.set_rect(page_rect);
                        wayland.show(view);
                    }
                    None => wayland.hide(),
                }
            }
        }
        false
    }

    pub fn new_tab(&mut self, _: &mut Tx) {
        self.engine
            .new_tab(self.shown_window(), "", TabIndex::End, true);
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

    pub(crate) fn surface_for(&self, view: ViewId) -> Pixels {
        self.surface(view)
    }

    pub(crate) fn close_surface(&self, view: ViewId) {
        self.surfaces.close(view);
    }

    pub(crate) fn page_prompts_now(&self) -> f64 {
        self.page_prompts.now()
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

    pub fn activate_action(&mut self, action: Action) {
        self.engine.activate_action(action);
    }

    pub fn open_main_menu(&mut self) {
        self.engine.request_main_menu();
    }

    pub fn show_main_menu(
        &mut self,
        menu: u64,
        entries: Vec<page_prompts::MenuEntry>,
        tx: &mut Tx,
    ) {
        chrome_popup::open(
            self,
            tx,
            PopupMenu::from_entries(entries),
            "menu-button",
            move |s, picked, _| {
                if let Ok(item) = picked.parse::<u32>() {
                    s.engine.activate_app_menu_item(menu, item);
                }
            },
        );
    }

    pub fn run_ui_command(&mut self, command: u32, tx: &mut Tx) {
        match command {
            crate::ffi::XENON_UI_COMMAND_FIND => page_prompts::open_find(self, tx),
            crate::ffi::XENON_UI_COMMAND_QUIT => self.request_quit(tx),
            crate::ffi::XENON_UI_COMMAND_OPEN_FILE => self.open_file(tx),
            crate::ffi::XENON_UI_COMMAND_TOGGLE_FULLSCREEN => self.toggle_fullscreen(tx),
            _ => {}
        }
    }

    pub fn open_file(&mut self, tx: &mut Tx) {
        let options = rune::FileDialogOptions {
            title: "Open File".to_owned(),
            directory: std::env::var_os("HOME").map(std::path::PathBuf::from),
            ..rune::FileDialogOptions::default()
        };
        tx.open_file(
            options,
            |s: &mut Browser, result: rune::FileDialogResult, _: &mut Tx| {
                if let Ok(Some(path)) = result {
                    let url = format!("file://{}", path.to_string_lossy());
                    s.engine
                        .new_tab(s.shown_window(), &url, TabIndex::End, true);
                }
            },
        );
    }

    pub fn find_step(&mut self, forward: bool, tx: &mut Tx) {
        let Some(view) = self.tabs.active_view() else {
            return;
        };
        if self
            .page_prompts
            .of(view)
            .is_some_and(|prompts| prompts.find.is_some())
        {
            page_prompts::find_step(self, view, forward);
        } else {
            page_prompts::open_find(self, tx);
        }
    }

    pub fn toggle_fullscreen(&mut self, _: &mut Tx) {
        crate::platform::set_fullscreen(!crate::platform::is_fullscreen());
    }

    pub fn downloads_changed(&mut self, downloads: Vec<Download>) {
        let started = downloads.iter().any(|download| {
            download.is_active() && !self.downloads.iter().any(|known| known.id == download.id)
        });
        self.downloads = downloads;
        if started && (self.window.link.is_none() || self.windows.shown_is_active()) {
            self.downloads_popover_open = true;
        }
        if self.downloads.is_empty() {
            self.downloads_popover_open = false;
        }
    }

    pub fn shown_downloads(&self) -> impl Iterator<Item = &Download> {
        let private = self.windows.shown_is_private();
        self.downloads
            .iter()
            .filter(move |download| download.private == private)
    }

    pub fn toggle_downloads_popover(&mut self) {
        self.downloads_popover_open = !self.downloads_popover_open;
    }

    pub fn close_downloads_popover(&mut self) {
        self.downloads_popover_open = false;
    }

    pub fn download_action(&mut self, download: u64, action: u32) {
        self.engine.download_action(download, action);
    }

    pub fn blocking_changed(
        &mut self,
        view: ViewId,
        state: u32,
        site: String,
        blocked_requests: u64,
    ) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.blocking_state = state;
            tab.blocking_site = site;
            tab.blocked_requests = blocked_requests;
        }
    }

    pub fn toggle_shield_popover(&mut self) {
        self.shield_popover_open = !self.shield_popover_open;
    }

    pub fn close_shield_popover(&mut self) {
        self.shield_popover_open = false;
    }

    pub fn blocking_action(&mut self, action: u32) {
        self.shield_popover_open = false;
        if let Some(view) = self.tabs.active_view() {
            self.engine.blocking_action(view, action);
        }
    }

    pub fn audio_changed(&mut self, view: ViewId, playing_audio: bool, muted: bool) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.playing_audio = playing_audio;
            tab.muted = muted;
        }
    }

    pub fn toggle_mute(&mut self, view: ViewId) {
        self.engine.toggle_mute(view);
    }

    pub fn unresponsive_changed(&mut self, view: ViewId, unresponsive: bool) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.unresponsive = unresponsive;
        }
    }

    pub fn answer_unresponsive_page(&mut self, view: ViewId, keep_waiting: bool) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.unresponsive = false;
        }
        self.engine.answer_unresponsive_page(view, keep_waiting);
    }

    pub fn open_history_menu(&mut self, direction: i32) {
        self.history_menu_direction = direction;
        if let Some(view) = self.tabs.active_view() {
            self.engine.request_history_menu(view, direction);
        }
    }

    pub fn show_history_menu(
        &mut self,
        view: ViewId,
        entries: Vec<page_prompts::MenuEntry>,
        tx: &mut Tx,
    ) {
        if self.tabs.active_view() != Some(view) || entries.is_empty() {
            return;
        }
        let anchor = if self.history_menu_direction < 0 {
            "back"
        } else {
            "forward"
        };
        chrome_popup::open(
            self,
            tx,
            PopupMenu::from_entries(entries),
            anchor,
            move |s, picked, _| {
                if let Ok(step) = picked.parse::<u32>() {
                    s.engine.traverse_history_to_step(view, step as i32);
                }
            },
        );
    }

    pub fn zoom_changed(&mut self, view: ViewId, percent: u32) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.zoom_percent = percent;
        }
    }

    pub fn bookmarked_changed(&mut self, view: ViewId, bookmarked: bool) {
        if let Some(tab) = self.tabs.get_mut(view) {
            tab.bookmarked = bookmarked;
        }
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
        self.request_close_shown_window();
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
            self.activate_view(crate::tab_groups::relative_shown_view(self, offset));
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
        if self.is_bound_to(window) {
            self.engine
                .set_window_viewport(Some(window), self.page.viewport());
        }
    }

    pub fn window_closed(&mut self, window: WindowId, tx: &mut Tx) {
        if self.is_bound_to(window) {
            crate::task_manager::close(self);
        }
        let previous = self.tabs.active_view();
        let Some(closed) = self.windows.closed(window, &mut self.tabs) else {
            return;
        };
        self.tab_groups.window_closed(window);
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

    pub(crate) fn shown_tabs_changed(&mut self, previous: Option<ViewId>, tx: &mut Tx) {
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
        self.sync_active_viewport();
        self.engine.set_focused(current, self.page_has_keyboard());
        self.switch_location(tx);
        self.refresh_title(tx);
    }

    fn refresh_title(&self, tx: &mut Tx) {
        let private = if self.windows.shown_is_private() {
            " (Private Browsing)"
        } else {
            ""
        };
        match self.tabs.active() {
            Some(tab) => tx.set_title(format!("{} - {APP_NAME}{private}", tab.display_title())),
            None => tx.set_title(format!("{APP_NAME}{private}")),
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
        tab.url = url.clone();
        if self.tabs.is_active(view) {
            if !self.location.edit.focused {
                self.reset_location();
            }
            self.refresh_title(tx);
        } else {
            self.location.parked_url_changed(view, &url);
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
        if self.extension_popup_cursor_changed(view, cursor) {
            return;
        }
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
        if let Some(view) = self.tabs.active_view() {
            self.reload_view(view);
        }
    }

    pub fn reload_view(&mut self, view: ViewId) {
        let engine = self.engine;
        if let Some(tab) = self.tabs.get_mut(view) {
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
        self.slot_focus_changed(focused);
        if !focused {
            chrome_popup::dismiss(self);
            self.release_forwarded_keys();
            self.end_recent_tab_cycle();
        }
        if let Some(view) = self.tabs.active_view() {
            self.engine.set_focused(view, self.page_has_keyboard());
        }
    }

    pub fn run_shortcut(&mut self, shortcut: Shortcut, tx: &mut Tx) {
        if self.forwards_to_target(shortcut) {
            return;
        }
        match shortcut {
            Shortcut::NewTab => self.new_tab(tx),
            Shortcut::NewSplitView => crate::split_view::new_split_view(self),
            Shortcut::CloseTab => self.close_active_tab(tx),
            Shortcut::FocusLocation => self.focus_location(),
            Shortcut::Reload => self.reload(),
            Shortcut::Back => self.go_back(),
            Shortcut::Forward => self.go_forward(),
            Shortcut::NextTab => {
                self.activate_view(crate::tab_groups::relative_shown_view(self, 1))
            }
            Shortcut::PreviousTab => {
                self.activate_view(crate::tab_groups::relative_shown_view(self, -1))
            }
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
            Shortcut::ZoomIn => self.activate_action(Action::ZoomIn),
            Shortcut::ZoomOut => self.activate_action(Action::ZoomOut),
            Shortcut::ResetZoom => self.activate_action(Action::ResetZoom),
            Shortcut::HardReload => self.activate_action(Action::ReloadBypassingCache),
            Shortcut::ToggleBookmark => self.activate_action(Action::ToggleBookmark),
            Shortcut::BookmarkAllTabs => self.activate_action(Action::BookmarkAllTabs),
            Shortcut::ToggleBookmarksBar => self.activate_action(Action::ToggleBookmarksBar),
            Shortcut::OpenSettings => self.activate_action(Action::OpenSettings),
            Shortcut::ShowHistory => self.activate_action(Action::ViewHistory),
            Shortcut::ShowDownloads => self.activate_action(Action::OpenDownloads),
            Shortcut::ClearBrowsingData => self.activate_action(Action::ClearBrowsingData),
            Shortcut::ViewSource => self.activate_action(Action::ViewSource),
            Shortcut::ToggleDevTools => self.activate_action(Action::ToggleDevTools),
            Shortcut::OpenFile => self.open_file(tx),
            Shortcut::FindNext => self.find_step(true, tx),
            Shortcut::FindPrevious => self.find_step(false, tx),
            Shortcut::ToggleFullscreen => self.toggle_fullscreen(tx),
            Shortcut::Quit => self.request_quit(tx),
            Shortcut::OpenTaskManager => crate::task_manager::open(self),
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
        if self.run_extension_command_shortcut(event) {
            return true;
        }
        if let Some(shortcut) = shortcut_for(&event.key, event.mods, Platform::CURRENT) {
            self.run_shortcut(shortcut, tx);
            return true;
        }
        if tx.focused().is_none() && self.extension_popup_window_key(event, tx) {
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
        if self.run_extension_command_shortcut(event) {
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
        if self.run_extension_command_shortcut(event) {
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
        self.observe_slot_scale(scale);
        if self.page.scale.get() != scale {
            self.page.scale.set(scale);
            self.send_window_viewport();
            self.sync_active_viewport();
        }
    }

    pub fn page_width(&self) -> f32 {
        self.page.size.get().0
    }

    pub fn page_size(&self) -> (f32, f32) {
        self.page.size.get()
    }

    pub fn page_laid_out(&mut self, [x, y, width, height]: [f32; 4]) {
        self.split.page_origin = (x, y);
        crate::platform_services::set_page_origin(x, y);
        if self.page.size.get() != (width, height) {
            self.page.size.set((width, height));
            self.send_window_viewport();
        }
        #[cfg(target_os = "linux")]
        if let Some(wayland) = self.wayland.as_mut() {
            wayland.set_rect((
                x.round() as i32,
                y.round() as i32,
                width.round() as i32,
                height.round() as i32,
            ));
        }
        self.sync_active_viewport();
    }

    pub(crate) fn send_window_viewport(&self) {
        if let Some(window) = self.shown_window() {
            self.engine
                .set_window_viewport(Some(window), self.page.viewport());
        }
    }

    pub(crate) fn sync_active_viewport(&self) {
        let Some(active) = self.tabs.active_view() else {
            return;
        };
        self.sync_viewport_of(active);
        if let Some(split) = self.split.shown(&self.tabs)
            && !page_prompts::chrome_hidden(self)
        {
            self.sync_viewport_of(split.partner(active));
        }
    }

    fn sync_viewport_of(&self, view: ViewId) {
        let Some(tab) = self.tabs.get(view) else {
            return;
        };
        let viewport =
            crate::split_view::pane_viewport(self, view).unwrap_or_else(|| self.page.viewport());
        if tab.sent_viewport.get() != Some(viewport) {
            tab.sent_viewport.set(Some(viewport));
            self.engine.set_viewport(view, viewport);
        }
    }

    fn send_mouse(
        &self,
        view: ViewId,
        kind: MouseEventKind,
        x: f32,
        y: f32,
        button: Option<MouseButton>,
        modifiers: rune::Mods,
    ) {
        let (x, y) = self.page.to_device(x, y);
        let event = MouseEvent {
            button,
            buttons: self.pressed_buttons,
            modifiers: Modifiers::from_rune(modifiers),
            ..MouseEvent::new(kind, x, y)
        };
        self.engine.mouse_event(view, &event);
    }

    pub fn page_drag(&mut self, view: ViewId, drag: &Drag, tx: &mut Tx) {
        let button = mouse_button(tx.button());
        let kind = match drag.phase {
            Phase::Down => {
                self.end_recent_tab_cycle();
                self.activate_shown_page(view, tx);
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
        self.send_mouse(view, kind, drag.x, drag.y, button, tx.mods());
    }

    fn activate_shown_page(&mut self, view: ViewId, tx: &mut Tx) {
        if !self.tabs.is_active(view) && self.split.is_shown(&self.tabs, view) {
            self.activate_tab(view, tx);
        }
    }

    pub fn page_pointer_moved(&mut self, view: ViewId, pointer: PointerEvent, tx: &mut Tx) -> bool {
        if self.pressed_buttons == 0 {
            self.send_mouse(
                view,
                MouseEventKind::Move,
                pointer.x,
                pointer.y,
                None,
                tx.mods(),
            );
        }
        page_prompts::page_pointer_moved(self, view, pointer.x, pointer.y, tx)
    }

    pub fn page_hover_changed(&mut self, view: ViewId, hovered: bool, tx: &Tx) {
        if !hovered && self.pressed_buttons == 0 {
            self.send_mouse(view, MouseEventKind::Leave, 0.0, 0.0, None, tx.mods());
            if let Some(tab) = self.tabs.get_mut(view) {
                tab.hovered_link.clear();
            }
        }
    }

    pub fn page_secondary_click(&mut self, view: ViewId, pointer: PointerEvent, tx: &mut Tx) {
        self.activate_shown_page(view, tx);
        self.focus_page();
        let button = MouseButton::Secondary;
        self.pressed_buttons |= button.bit();
        self.send_mouse(
            view,
            MouseEventKind::Down,
            pointer.x,
            pointer.y,
            Some(button),
            tx.mods(),
        );
        self.pressed_buttons &= !button.bit();
        self.send_mouse(
            view,
            MouseEventKind::Up,
            pointer.x,
            pointer.y,
            Some(button),
            tx.mods(),
        );
    }

    pub fn page_wheel(&mut self, view: ViewId, wheel: WheelEvent) {
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_the_shown_windows_views_repaint_it() {
        let mut browser = Browser::new(Arc::default());
        browser.windows.opened(WindowInfo {
            id: 1,
            private: false,
        });
        browser.windows.opened(WindowInfo {
            id: 2,
            private: false,
        });
        browser
            .tabs
            .insert(Tab::new(7, "about:blank", Pixels::new()), 0);
        browser
            .windows
            .tabs_mut(2, &mut browser.tabs)
            .unwrap()
            .insert(Tab::new(8, "about:blank", Pixels::new()), 0);
        assert!(browser.shows_view(7));
        assert!(!browser.shows_view(8));
        assert!(!browser.shows_view(9));
    }
}
