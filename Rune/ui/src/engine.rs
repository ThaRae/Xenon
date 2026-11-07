/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use crate::ffi::{self, XenonStr};

pub type ViewId = ffi::XenonViewId;
pub type WindowId = ffi::XenonWindowId;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TabIndex {
    End,
    At(usize),
}

impl TabIndex {
    fn raw(self) -> i64 {
        match self {
            TabIndex::End => ffi::XENON_TAB_INDEX_END,
            TabIndex::At(index) => i64::try_from(index).unwrap_or(i64::MAX),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RecentDirection {
    LessRecent,
    MoreRecent,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Viewport {
    pub width: u32,
    pub height: u32,
    pub scale: f64,
}

impl Viewport {
    pub fn from_points(width: f32, height: f32, scale: f32) -> Viewport {
        let device = |points: f32| (points * scale).round().max(1.0) as u32;
        Viewport {
            width: device(width),
            height: device(height),
            scale: f64::from(scale),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MouseEventKind {
    Down,
    Up,
    Move,
    Wheel,
    DoubleClick,
    Leave,
}

impl MouseEventKind {
    fn raw(self) -> u32 {
        match self {
            MouseEventKind::Down => ffi::XENON_MOUSE_EVENT_DOWN,
            MouseEventKind::Up => ffi::XENON_MOUSE_EVENT_UP,
            MouseEventKind::Move => ffi::XENON_MOUSE_EVENT_MOVE,
            MouseEventKind::Wheel => ffi::XENON_MOUSE_EVENT_WHEEL,
            MouseEventKind::DoubleClick => ffi::XENON_MOUSE_EVENT_DOUBLE_CLICK,
            MouseEventKind::Leave => ffi::XENON_MOUSE_EVENT_LEAVE,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum MouseButton {
    Primary,
    Secondary,
    Middle,
    Backward,
    Forward,
}

impl MouseButton {
    pub fn bit(self) -> u32 {
        match self {
            MouseButton::Primary => ffi::XENON_MOUSE_BUTTON_PRIMARY,
            MouseButton::Secondary => ffi::XENON_MOUSE_BUTTON_SECONDARY,
            MouseButton::Middle => ffi::XENON_MOUSE_BUTTON_MIDDLE,
            MouseButton::Backward => ffi::XENON_MOUSE_BUTTON_BACKWARD,
            MouseButton::Forward => ffi::XENON_MOUSE_BUTTON_FORWARD,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Modifiers(pub u32);

impl Modifiers {
    pub fn from_rune(mods: rune::Mods) -> Modifiers {
        let bit = |held: bool, bit: u32| if held { bit } else { 0 };
        Modifiers(
            bit(mods.shift, ffi::XENON_MODIFIER_SHIFT)
                | bit(mods.ctrl, ffi::XENON_MODIFIER_CTRL)
                | bit(mods.alt, ffi::XENON_MODIFIER_ALT)
                | bit(mods.logo, ffi::XENON_MODIFIER_SUPER),
        )
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct MouseEvent {
    pub kind: MouseEventKind,
    pub x: f32,
    pub y: f32,
    pub button: Option<MouseButton>,
    pub buttons: u32,
    pub modifiers: Modifiers,
    pub wheel_delta: (f32, f32),
}

impl MouseEvent {
    pub fn new(kind: MouseEventKind, x: f32, y: f32) -> MouseEvent {
        MouseEvent {
            kind,
            x,
            y,
            button: None,
            buttons: 0,
            modifiers: Modifiers::default(),
            wheel_delta: (0.0, 0.0),
        }
    }

    fn raw(&self) -> ffi::XenonMouseEvent {
        ffi::XenonMouseEvent {
            r#type: self.kind.raw(),
            x: self.x,
            y: self.y,
            button: self
                .button
                .map_or(ffi::XENON_MOUSE_BUTTON_NONE, MouseButton::bit),
            buttons: self.buttons,
            modifiers: self.modifiers.0,
            wheel_delta_x: self.wheel_delta.0,
            wheel_delta_y: self.wheel_delta.1,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KeyEventKind {
    Down,
    Up,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct KeyEvent {
    pub kind: KeyEventKind,
    pub code: String,
    pub key: String,
    pub modifiers: Modifiers,
    pub repeat: bool,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Engine;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ChromeLayout {
    #[default]
    Classic,
    Compact,
    Vertical,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Action {
    ZoomIn = 0,
    ZoomOut,
    ResetZoom,
    ReloadBypassingCache,
    ViewSource,
    ToggleDevTools,
    OpenSettings,
    OpenAbout,
    OpenDownloads,
    ViewHistory,
    ClearBrowsingData,
    ToggleBookmark,
    ManageBookmarks,
    BookmarkAllTabs,
    ToggleBookmarksBar,
    ReportWebsiteIssue,
    ForceDark,
}

impl ChromeLayout {
    pub fn from_raw(raw: u8) -> ChromeLayout {
        match raw {
            ffi::XENON_CHROME_LAYOUT_COMPACT => ChromeLayout::Compact,
            ffi::XENON_CHROME_LAYOUT_VERTICAL => ChromeLayout::Vertical,
            _ => ChromeLayout::Classic,
        }
    }

    pub fn raw(self) -> u8 {
        match self {
            ChromeLayout::Classic => ffi::XENON_CHROME_LAYOUT_CLASSIC,
            ChromeLayout::Compact => ffi::XENON_CHROME_LAYOUT_COMPACT,
            ChromeLayout::Vertical => ffi::XENON_CHROME_LAYOUT_VERTICAL,
        }
    }
}

impl Engine {
    pub fn set_window_viewport(self, window: Option<WindowId>, viewport: Viewport) {
        unsafe {
            ffi::xenon_engine_set_window_viewport(
                window.unwrap_or(ffi::XENON_WINDOW_NONE),
                viewport.width,
                viewport.height,
                viewport.scale,
            )
        }
    }

    pub fn set_chrome_layout(self, layout: ChromeLayout) {
        unsafe { ffi::xenon_engine_set_chrome_layout(layout.raw()) }
    }

    pub fn set_cycle_tabs_in_recently_used_order(self, enabled: bool) {
        unsafe { ffi::xenon_engine_set_cycle_tabs_in_recently_used_order(enabled) }
    }

    pub fn activate_action(self, action: Action) {
        unsafe { ffi::xenon_engine_activate_action(action as u32) }
    }

    pub fn blocking_action(self, view: ViewId, action: u32) {
        unsafe { ffi::xenon_engine_blocking_action(view, action) }
    }

    pub fn toggle_mute(self, view: ViewId) {
        unsafe { ffi::xenon_engine_toggle_mute(view) }
    }

    pub fn answer_unresponsive_page(self, view: ViewId, keep_waiting: bool) {
        unsafe { ffi::xenon_engine_answer_unresponsive_page(view, keep_waiting) }
    }

    pub fn request_history_menu(self, view: ViewId, direction: i32) {
        unsafe { ffi::xenon_engine_request_history_menu(view, direction) }
    }

    pub fn traverse_history_to_step(self, view: ViewId, step: i32) {
        unsafe { ffi::xenon_engine_traverse_history_to_step(view, step) }
    }

    pub fn download_action(self, download: u64, action: u32) {
        unsafe { ffi::xenon_engine_download_action(download, action) }
    }

    pub fn request_main_menu(self) {
        unsafe { ffi::xenon_engine_request_app_menu(ffi::XENON_APP_MENU_MAIN) }
    }

    pub fn activate_app_menu_item(self, menu: u64, item: u32) {
        unsafe { ffi::xenon_engine_activate_app_menu_item(menu, item) }
    }

    pub fn duplicate_tab(self, view: ViewId) -> ViewId {
        unsafe { ffi::xenon_engine_duplicate_tab(view) }
    }

    pub fn request_tab_memory(self, view: ViewId) {
        unsafe { ffi::xenon_engine_request_tab_memory(view) }
    }

    pub fn set_system_dark(self, dark: bool) {
        unsafe { ffi::xenon_engine_set_system_dark(dark) }
    }

    pub fn new_window(self, private: bool) -> WindowId {
        unsafe { ffi::xenon_engine_new_window(private) }
    }

    pub fn activate_window(self, window: WindowId) {
        unsafe { ffi::xenon_engine_activate_window(window) }
    }

    pub fn new_tab(
        self,
        window: Option<WindowId>,
        input: &str,
        index: TabIndex,
        activate: bool,
    ) -> ViewId {
        unsafe {
            ffi::xenon_engine_new_tab(
                window.unwrap_or(ffi::XENON_WINDOW_NONE),
                XenonStr::borrowed(input),
                index.raw(),
                activate,
            )
        }
    }

    pub fn activate_tab(self, view: ViewId) {
        unsafe { ffi::xenon_engine_activate_tab(view) }
    }

    pub fn request_close_tab(self, view: ViewId) {
        unsafe { crate::ffi_page::xenon_engine_request_close(view) }
    }

    pub fn move_tab(self, view: ViewId, window: WindowId, index: TabIndex) {
        unsafe { ffi::xenon_engine_move_tab(view, window, index.raw()) }
    }

    pub fn set_tab_pinned(self, view: ViewId, pinned: bool) {
        unsafe { ffi::xenon_engine_set_tab_pinned(view, pinned) }
    }

    pub fn reopen_closed_tab(self, window: Option<WindowId>) {
        unsafe { ffi::xenon_engine_reopen_closed_tab(window.unwrap_or(ffi::XENON_WINDOW_NONE)) }
    }

    pub fn cycle_recent_tabs(self, window: WindowId, direction: RecentDirection) {
        unsafe {
            ffi::xenon_engine_cycle_recent_tabs(window, direction == RecentDirection::LessRecent)
        }
    }

    pub fn end_recent_tab_cycle(self, window: WindowId) {
        unsafe { ffi::xenon_engine_end_recent_tab_cycle(window) }
    }

    pub fn go_back(self, view: ViewId) {
        unsafe { ffi::xenon_engine_go_back(view) }
    }

    pub fn go_forward(self, view: ViewId) {
        unsafe { ffi::xenon_engine_go_forward(view) }
    }

    pub fn reload(self, view: ViewId) {
        unsafe { ffi::xenon_engine_reload(view) }
    }

    pub fn stop(self, view: ViewId) {
        unsafe { ffi::xenon_engine_stop(view) }
    }

    pub fn set_viewport(self, view: ViewId, viewport: Viewport) {
        unsafe {
            ffi::xenon_engine_set_viewport(view, viewport.width, viewport.height, viewport.scale)
        }
    }

    pub fn set_focused(self, view: ViewId, focused: bool) {
        unsafe { ffi::xenon_engine_set_focused(view, focused) }
    }

    pub fn mouse_event(self, view: ViewId, event: &MouseEvent) {
        let raw = event.raw();
        unsafe { ffi::xenon_engine_mouse_event(view, &raw) }
    }

    pub fn key_event(self, view: ViewId, event: &KeyEvent) {
        let raw = ffi::XenonKeyEvent {
            r#type: match event.kind {
                KeyEventKind::Down => ffi::XENON_KEY_EVENT_DOWN,
                KeyEventKind::Up => ffi::XENON_KEY_EVENT_UP,
            },
            code: XenonStr::borrowed(&event.code),
            key: XenonStr::borrowed(&event.key),
            modifiers: event.modifiers.0,
            repeat: event.repeat,
        };
        unsafe { ffi::xenon_engine_key_event(view, &raw) }
    }

    pub fn insert_text(self, view: ViewId, text: &str) {
        unsafe { ffi::xenon_engine_insert_text(view, XenonStr::borrowed(text)) }
    }
}
