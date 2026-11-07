/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cell::Cell;
use std::collections::{HashMap, VecDeque};
use std::ffi::c_void;
use std::sync::{Arc, Mutex, OnceLock};

use rune::{Drag, ImageCache, Key, KeyEvent, Mods, Phase, Pixels, PointerEvent, Tx, WheelEvent};

use crate::chrome_popup::{self, PopupItem, PopupMenu};

use crate::engine::{
    Engine, KeyEventKind, Modifiers, MouseButton, MouseEvent, MouseEventKind, ViewId, Viewport,
};
use crate::ffi::{XenonStr, XenonViewId};
use crate::ffi_extensions::{
    self, XenonExtensionAction, XenonExtensionCallbacks, XenonExtensionCommand, XenonExtensionInfo,
    XenonExtensionPermissionPrompt, XenonPasswordManagers,
};
use crate::ffi_page::XenonPromptId;
use crate::keys::{Platform, page_key_event};
use crate::state::Browser;
use crate::window_slots::Slots;

pub const POPUP_MINIMUM_SIZE: (f32, f32) = (25.0, 25.0);
pub const POPUP_MAXIMUM_SIZE: (f32, f32) = (800.0, 600.0);

#[derive(Clone, Debug)]
pub struct ActionButton {
    pub extension_id: String,
    pub name: String,
    pub title: String,
    pub icon: Option<Pixels>,
    pub badge_text: String,
    pub badge_background: u32,
    pub badge_text_color: Option<u32>,
    pub enabled: bool,
    pub has_options_page: bool,
}

impl ActionButton {
    pub fn badge_ink(&self) -> u32 {
        self.badge_text_color.unwrap_or_else(|| {
            if lightness(self.badge_background) > 0.6 {
                0x0000_00ff
            } else {
                0xffff_ffff
            }
        })
    }

    pub fn shown_badge(&self) -> Option<String> {
        let badge: String = self.badge_text.chars().take(4).collect();
        (!badge.is_empty()).then_some(badge)
    }
}

fn lightness(rgba: u32) -> f32 {
    let channels = [(rgba >> 24) & 0xff, (rgba >> 16) & 0xff, (rgba >> 8) & 0xff];
    let max = channels.iter().copied().max().unwrap_or(0) as f32 / 255.0;
    let min = channels.iter().copied().min().unwrap_or(0) as f32 / 255.0;
    (max + min) / 2.0
}

#[derive(Clone, Debug)]
pub struct ExtensionInfo {
    pub id: String,
    pub name: String,
    pub version: String,
    pub description: String,
    pub details: String,
    pub warnings: Vec<String>,
    pub icon: Option<Pixels>,
    pub has_options_page: bool,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct PasswordManagers {
    pub choices: Vec<(String, String)>,
    pub current: String,
    pub installing: String,
    pub status: String,
}

impl PasswordManagers {
    pub fn name_of(&self, id: &str) -> Option<&str> {
        self.choices
            .iter()
            .find(|(choice, _)| choice == id)
            .map(|(_, name)| name.as_str())
    }

    pub fn shown(&self) -> &str {
        if self.installing.is_empty() {
            &self.current
        } else {
            &self.installing
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum ShortcutKey {
    Char(char),
    Named(Key),
}

#[derive(Clone, Debug, PartialEq)]
pub struct CommandShortcut {
    pub primary: bool,
    pub control: bool,
    pub alt: bool,
    pub shift: bool,
    pub key: ShortcutKey,
}

impl CommandShortcut {
    pub fn parse(shortcut: &str, platform: Platform) -> Option<CommandShortcut> {
        let mut parsed = CommandShortcut {
            primary: false,
            control: false,
            alt: false,
            shift: false,
            key: ShortcutKey::Char(' '),
        };
        let mut key = None;
        for part in shortcut.split('+').map(str::trim) {
            match part {
                "Ctrl" | "Command" => parsed.primary = true,
                "MacCtrl" if platform == Platform::Mac => parsed.control = true,
                "MacCtrl" => parsed.primary = true,
                "Alt" | "Option" => parsed.alt = true,
                "Shift" => parsed.shift = true,
                _ => key = Some(shortcut_key(part)?),
            }
        }
        parsed.key = key?;
        Some(parsed)
    }

    pub fn matches(&self, key: &Key, mods: Mods, platform: Platform) -> bool {
        let control = if platform == Platform::Mac {
            mods.ctrl
        } else {
            false
        };
        if mods.cmd != self.primary
            || control != self.control
            || mods.alt != self.alt
            || mods.shift != self.shift
        {
            return false;
        }
        match (&self.key, key) {
            (ShortcutKey::Char(wanted), Key::Command(typed)) => typed.eq_ignore_ascii_case(wanted),
            (ShortcutKey::Char(wanted), Key::Text(typed)) => {
                let mut chars = typed.chars();
                matches!((chars.next(), chars.next()), (Some(typed), None) if typed.eq_ignore_ascii_case(wanted))
            }
            (ShortcutKey::Named(wanted), key) => wanted == key,
            _ => false,
        }
    }
}

fn shortcut_key(name: &str) -> Option<ShortcutKey> {
    let named = match name {
        "Comma" => return Some(ShortcutKey::Char(',')),
        "Period" => return Some(ShortcutKey::Char('.')),
        "Space" => return Some(ShortcutKey::Char(' ')),
        "Home" => Key::Home,
        "End" => Key::End,
        "PageUp" => Key::PageUp,
        "PageDown" => Key::PageDown,
        "Insert" => Key::Insert,
        "Delete" => Key::Delete,
        "Up" => Key::Up,
        "Down" => Key::Down,
        "Left" => Key::Left,
        "Right" => Key::Right,
        _ => {
            let mut chars = name.chars();
            return match (chars.next(), chars.next()) {
                (Some(c), None) if c.is_ascii_alphanumeric() => {
                    Some(ShortcutKey::Char(c.to_ascii_lowercase()))
                }
                _ => None,
            };
        }
    };
    Some(ShortcutKey::Named(named))
}

#[derive(Clone, Debug, PartialEq)]
pub struct Command {
    pub extension_id: String,
    pub command: String,
    pub shortcut: CommandShortcut,
}

#[derive(Debug)]
pub struct Popup {
    pub tab: ViewId,
    pub extension_id: String,
    pub view: ViewId,
    pub size: (f32, f32),
    pub pixels: Pixels,
    pub cursor: u32,
    sent_viewport: Cell<Option<Viewport>>,
    pressed_buttons: u32,
}

pub fn clamp_popup_size(width: u32, height: u32) -> (f32, f32) {
    (
        (width as f32).clamp(POPUP_MINIMUM_SIZE.0, POPUP_MAXIMUM_SIZE.0),
        (height as f32).clamp(POPUP_MINIMUM_SIZE.1, POPUP_MAXIMUM_SIZE.1),
    )
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PermissionPrompt {
    pub id: XenonPromptId,
    pub extension_name: String,
    pub warnings: Vec<String>,
    pub popup: Option<ViewId>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Confirmation {
    RemoveExtension { id: String, name: String },
    ChangePasswordManager { id: String, current: String },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ErrorNotice {
    pub title: String,
    pub message: String,
}

#[derive(Debug, Default)]
pub struct Extensions {
    actions: HashMap<ViewId, Vec<ActionButton>>,
    latest_actions: Vec<ActionButton>,
    pub popup: Option<Popup>,
    pub prompts: VecDeque<PermissionPrompt>,
    pub dialog_open: bool,
    pub installed: Vec<ExtensionInfo>,
    pub password_managers: PasswordManagers,
    pub commands: Vec<Command>,
    pub confirmation: Option<Confirmation>,
    pub error: Option<ErrorNotice>,
}

impl Extensions {
    pub fn actions_for(&self, view: Option<ViewId>) -> &[ActionButton] {
        view.and_then(|view| self.actions.get(&view))
            .unwrap_or(&self.latest_actions)
    }

    pub fn set_actions(&mut self, view: ViewId, actions: Vec<ActionButton>) {
        self.latest_actions = actions.clone();
        self.actions.insert(view, actions);
    }

    pub fn forget_views(&mut self, live: impl Fn(ViewId) -> bool) -> bool {
        let before = self.actions.len();
        self.actions.retain(|view, _| live(*view));
        before != self.actions.len()
    }

    pub fn popup_view(&self) -> Option<ViewId> {
        self.popup.as_ref().map(|popup| popup.view)
    }

    pub fn popup_open_for(&self, extension_id: &str) -> bool {
        self.popup
            .as_ref()
            .is_some_and(|popup| popup.extension_id == extension_id)
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct ExtensionsEngine;

impl ExtensionsEngine {
    pub fn action_clicked(self, tab: ViewId, extension_id: &str) {
        unsafe {
            ffi_extensions::xenon_engine_extension_action_clicked(
                tab,
                XenonStr::borrowed(extension_id),
            )
        }
    }

    pub fn close_popup(self, popup: ViewId) {
        unsafe { ffi_extensions::xenon_engine_close_extension_popup(popup) }
    }

    pub fn answer_permissions(self, prompt: XenonPromptId, granted: bool) {
        unsafe { ffi_extensions::xenon_engine_answer_extension_permissions(prompt, granted) }
    }

    pub fn run_command(self, tab: ViewId, extension_id: &str, command: &str) {
        unsafe {
            ffi_extensions::xenon_engine_run_extension_command(
                tab,
                XenonStr::borrowed(extension_id),
                XenonStr::borrowed(command),
            )
        }
    }

    pub fn open_options(self, extension_id: &str) {
        unsafe {
            ffi_extensions::xenon_engine_open_extension_options(XenonStr::borrowed(extension_id))
        }
    }

    pub fn reload(self, extension_id: &str) {
        unsafe { ffi_extensions::xenon_engine_reload_extension(XenonStr::borrowed(extension_id)) }
    }

    pub fn remove(self, extension_id: &str) {
        unsafe { ffi_extensions::xenon_engine_remove_extension(XenonStr::borrowed(extension_id)) }
    }

    pub fn load_unpacked(self, directory: &str) {
        unsafe {
            ffi_extensions::xenon_engine_load_unpacked_extension(XenonStr::borrowed(directory))
        }
    }

    pub fn choose_password_manager(self, id: &str) {
        unsafe { ffi_extensions::xenon_engine_choose_password_manager(XenonStr::borrowed(id)) }
    }

    pub fn show_extensions(self) {
        unsafe { ffi_extensions::xenon_engine_show_extensions() }
    }
}

impl Browser {
    pub fn extension_action_pressed(&mut self, extension_id: &str) {
        let Some(tab) = self.tabs.active_view() else {
            return;
        };
        if self.extensions.popup_open_for(extension_id) {
            self.close_extension_popup();
            return;
        }
        ExtensionsEngine.action_clicked(tab, extension_id);
    }

    pub fn show_extension_action_menu(
        &mut self,
        extension_id: &str,
        at: rune::PointerPress,
        tx: &mut Tx,
    ) {
        let Some(action) = self
            .extensions
            .actions_for(self.tabs.active_view())
            .iter()
            .find(|action| action.extension_id == extension_id)
            .cloned()
        else {
            return;
        };
        let mut menu = PopupMenu::new()
            .item(PopupItem::new("name", action.name.clone()).enabled(false))
            .separator();
        if action.has_options_page {
            menu = menu.item(PopupItem::new("options", "Options"));
        }
        menu = menu
            .item(PopupItem::new("manage", "Manage Extensions"))
            .item(PopupItem::new("remove", "Remove from Xenon…"));
        chrome_popup::open(self, tx, menu, at, move |s, picked, _| match picked {
            "options" => ExtensionsEngine.open_options(&action.extension_id),
            "manage" => s.show_extensions_dialog(),
            "remove" => s.confirm_remove_extension(&action.extension_id, &action.name),
            _ => {}
        });
    }

    pub fn show_extensions_dialog(&mut self) {
        ExtensionsEngine.show_extensions();
    }

    pub fn close_extensions_dialog(&mut self) {
        self.extensions.dialog_open = false;
        self.extensions.confirmation = None;
    }

    pub fn load_unpacked_extension(&mut self, tx: &mut Tx) {
        let options = rune::FileDialogOptions {
            title: "Select the extension's directory".to_owned(),
            directory: std::env::var_os("HOME").map(std::path::PathBuf::from),
            ..rune::FileDialogOptions::default()
        };
        tx.open_folder(
            options,
            |_: &mut Browser, result: rune::FileDialogResult, _: &mut Tx| {
                if let Ok(Some(path)) = result {
                    ExtensionsEngine.load_unpacked(&path.to_string_lossy());
                }
            },
        );
    }

    pub fn reload_extension(&mut self, id: &str) {
        ExtensionsEngine.reload(id);
    }

    pub fn confirm_remove_extension(&mut self, id: &str, name: &str) {
        self.extensions.confirmation = Some(Confirmation::RemoveExtension {
            id: id.to_owned(),
            name: name.to_owned(),
        });
    }

    pub fn answer_confirmation(&mut self, accepted: bool) {
        let Some(confirmation) = self.extensions.confirmation.take() else {
            return;
        };
        if !accepted {
            return;
        }
        match confirmation {
            Confirmation::RemoveExtension { id, .. } => ExtensionsEngine.remove(&id),
            Confirmation::ChangePasswordManager { id, .. } => {
                ExtensionsEngine.choose_password_manager(&id)
            }
        }
    }

    pub fn show_password_manager_menu(&mut self, tx: &mut Tx) {
        let managers = &self.extensions.password_managers;
        let shown = managers.shown();
        let mut menu = PopupMenu::new();
        for (id, name) in std::iter::once((String::new(), "None".to_owned()))
            .chain(managers.choices.iter().cloned())
        {
            let checked = id == shown;
            menu = menu.item(PopupItem::new(password_manager_item(&id), name).checked(checked));
        }
        chrome_popup::open(
            self,
            tx,
            menu,
            "extensions-password-manager",
            |s, picked, _| {
                if let Some(id) = picked.strip_prefix(PASSWORD_MANAGER_ITEM) {
                    s.choose_password_manager(id);
                }
            },
        );
    }

    pub fn choose_password_manager(&mut self, id: &str) {
        let managers = &self.extensions.password_managers;
        if !managers.installing.is_empty() || managers.current == id {
            return;
        }
        if !managers.current.is_empty() {
            let current = managers
                .name_of(&managers.current)
                .unwrap_or(&managers.current)
                .to_owned();
            self.extensions.confirmation = Some(Confirmation::ChangePasswordManager {
                id: id.to_owned(),
                current,
            });
            return;
        }
        ExtensionsEngine.choose_password_manager(id);
    }

    pub fn dismiss_extension_error(&mut self) {
        self.extensions.error = None;
    }

    pub fn answer_extension_permissions(&mut self, granted: bool) {
        if let Some(prompt) = self.extensions.prompts.pop_front() {
            ExtensionsEngine.answer_permissions(prompt.id, granted);
        }
    }

    pub fn close_extension_popup(&mut self) {
        let Some(popup) = self.extensions.popup.take() else {
            return;
        };
        self.deny_prompts_of_popup(popup.view);
        self.engine_for_extensions().set_focused(popup.view, false);
        ExtensionsEngine.close_popup(popup.view);
        self.close_surface(popup.view);
    }

    fn deny_prompts_of_popup(&mut self, view: ViewId) {
        let (from_popup, others): (VecDeque<_>, VecDeque<_>) = self
            .extensions
            .prompts
            .drain(..)
            .partition(|prompt| prompt.popup == Some(view));
        self.extensions.prompts = others;
        for prompt in from_popup {
            ExtensionsEngine.answer_permissions(prompt.id, false);
        }
    }

    fn engine_for_extensions(&self) -> Engine {
        Engine
    }

    fn sync_popup_viewport(&self) {
        let Some(popup) = &self.extensions.popup else {
            return;
        };
        let viewport = Viewport::from_points(popup.size.0, popup.size.1, self.page.scale());
        if popup.sent_viewport.get() != Some(viewport) {
            popup.sent_viewport.set(Some(viewport));
            self.engine_for_extensions()
                .set_viewport(popup.view, viewport);
        }
    }

    pub fn extension_popup_laid_out(&mut self) {
        self.sync_popup_viewport();
    }

    fn popup_opened(
        &mut self,
        tab: ViewId,
        extension_id: String,
        view: ViewId,
        width: u32,
        height: u32,
    ) {
        if let Some(previous) = self.extensions.popup.take() {
            self.close_surface(previous.view);
        }
        let pixels = self.surface_for(view);
        self.extensions.popup = Some(Popup {
            tab,
            extension_id,
            view,
            size: clamp_popup_size(width, height),
            pixels,
            cursor: crate::ffi::XENON_CURSOR_DEFAULT,
            sent_viewport: Cell::new(None),
            pressed_buttons: 0,
        });
        self.sync_popup_viewport();
        self.engine_for_extensions().set_focused(view, true);
    }

    fn popup_resized(&mut self, view: ViewId, width: u32, height: u32) {
        let Some(popup) = self
            .extensions
            .popup
            .as_mut()
            .filter(|popup| popup.view == view)
        else {
            return;
        };
        popup.size = clamp_popup_size(width, height);
        self.sync_popup_viewport();
    }

    fn popup_closed(&mut self, view: ViewId) {
        if self.extensions.popup_view() != Some(view) {
            return;
        }
        self.extensions.popup = None;
        self.deny_prompts_of_popup(view);
        self.close_surface(view);
    }

    pub fn extension_popup_cursor_changed(&mut self, view: ViewId, cursor: u32) -> bool {
        match self
            .extensions
            .popup
            .as_mut()
            .filter(|popup| popup.view == view)
        {
            Some(popup) => {
                popup.cursor = cursor;
                true
            }
            None => false,
        }
    }

    fn send_popup_mouse(
        &self,
        kind: MouseEventKind,
        x: f32,
        y: f32,
        button: Option<MouseButton>,
        mods: Mods,
    ) {
        let Some(popup) = &self.extensions.popup else {
            return;
        };
        let scale = self.page.scale();
        let event = MouseEvent {
            button,
            buttons: popup.pressed_buttons,
            modifiers: Modifiers::from_rune(mods),
            ..MouseEvent::new(kind, x * scale, y * scale)
        };
        self.engine_for_extensions().mouse_event(popup.view, &event);
    }

    pub fn popup_drag(&mut self, drag: &Drag, tx: &Tx) {
        let Some(popup) = self.extensions.popup.as_mut() else {
            return;
        };
        let button = match tx.button() {
            rune::PointerButton::Secondary => MouseButton::Secondary,
            rune::PointerButton::Middle => MouseButton::Middle,
            _ => MouseButton::Primary,
        };
        let kind = match drag.phase {
            Phase::Down => {
                popup.pressed_buttons |= button.bit();
                if drag.clicks == 2 {
                    MouseEventKind::DoubleClick
                } else {
                    MouseEventKind::Down
                }
            }
            Phase::Move => MouseEventKind::Move,
            Phase::Up => {
                popup.pressed_buttons &= !button.bit();
                MouseEventKind::Up
            }
        };
        let view = popup.view;
        if kind == MouseEventKind::Down || kind == MouseEventKind::DoubleClick {
            self.engine_for_extensions().set_focused(view, true);
        }
        let button = (kind != MouseEventKind::Move).then_some(button);
        self.send_popup_mouse(kind, drag.x, drag.y, button, tx.mods());
    }

    pub fn popup_pointer_moved(&mut self, pointer: PointerEvent, tx: &Tx) {
        if self
            .extensions
            .popup
            .as_ref()
            .is_some_and(|popup| popup.pressed_buttons == 0)
        {
            self.send_popup_mouse(MouseEventKind::Move, pointer.x, pointer.y, None, tx.mods());
        }
    }

    pub fn popup_hover_changed(&mut self, hovered: bool, tx: &Tx) {
        if !hovered
            && self
                .extensions
                .popup
                .as_ref()
                .is_some_and(|popup| popup.pressed_buttons == 0)
        {
            self.send_popup_mouse(MouseEventKind::Leave, 0.0, 0.0, None, tx.mods());
        }
    }

    pub fn popup_secondary_click(&mut self, pointer: PointerEvent, tx: &Tx) {
        let button = Some(MouseButton::Secondary);
        self.send_popup_mouse(
            MouseEventKind::Down,
            pointer.x,
            pointer.y,
            button,
            tx.mods(),
        );
        self.send_popup_mouse(MouseEventKind::Up, pointer.x, pointer.y, button, tx.mods());
    }

    pub fn popup_wheel(&mut self, wheel: WheelEvent) {
        let Some(popup) = &self.extensions.popup else {
            return;
        };
        let scale = self.page.scale();
        let event = MouseEvent {
            buttons: popup.pressed_buttons,
            modifiers: Modifiers::from_rune(wheel.mods),
            wheel_delta: (-wheel.dx * scale, -wheel.dy * scale),
            ..MouseEvent::new(MouseEventKind::Wheel, wheel.x * scale, wheel.y * scale)
        };
        self.engine_for_extensions().mouse_event(popup.view, &event);
    }

    pub fn extension_popup_window_key(&mut self, event: &KeyEvent, tx: &Tx) -> bool {
        self.popup_key(event, tx, KeyEventKind::Down)
    }

    pub fn popup_key(&mut self, event: &KeyEvent, tx: &Tx, kind: KeyEventKind) -> bool {
        let Some(view) = self.extensions.popup_view() else {
            return false;
        };
        if event.key == Key::Escape {
            if kind == KeyEventKind::Down {
                self.close_extension_popup();
            }
            return true;
        }
        let repeat = kind == KeyEventKind::Down && tx.key_repeat();
        let Some(key) = page_key_event(kind, &event.key, event.mods, tx.physical_key(), repeat)
        else {
            return false;
        };
        self.engine_for_extensions().key_event(view, &key);
        true
    }

    pub fn run_extension_command_shortcut(&mut self, event: &KeyEvent) -> bool {
        let Some(tab) = self.tabs.active_view() else {
            return false;
        };
        let Some(command) = self.extensions.commands.iter().find(|command| {
            command
                .shortcut
                .matches(&event.key, event.mods, Platform::CURRENT)
        }) else {
            return false;
        };
        ExtensionsEngine.run_command(tab, &command.extension_id, &command.command);
        true
    }
}

const PASSWORD_MANAGER_ITEM: &str = "password-manager-";

fn password_manager_item(id: &str) -> String {
    format!("{PASSWORD_MANAGER_ITEM}{id}")
}

pub fn action_button_key(extension_id: &str) -> String {
    format!("extension-action-{extension_id}")
}

pub fn poll(s: &mut Browser, _: &mut Tx) -> bool {
    let tabs = &s.tabs;
    let mut changed = s.extensions.forget_views(|view| tabs.get(view).is_some());
    if let Some(popup) = &s.extensions.popup
        && s.tabs.active_view() != Some(popup.tab)
    {
        s.close_extension_popup();
        changed = true;
    }
    changed
}

fn icon_decoder() -> &'static ImageCache {
    static DECODER: OnceLock<ImageCache> = OnceLock::new();
    DECODER.get_or_init(|| ImageCache::new(Arc::new(|| {})))
}

fn icon_cache() -> &'static Mutex<HashMap<String, Pixels>> {
    static CACHE: OnceLock<Mutex<HashMap<String, Pixels>>> = OnceLock::new();
    CACHE.get_or_init(Mutex::default)
}

fn icon(path: &str) -> Option<Pixels> {
    if path.is_empty() {
        return None;
    }
    let mut cache = icon_cache()
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner());
    if let Some(pixels) = cache.get(path) {
        return Some(pixels.clone());
    }
    let bytes = std::fs::read(path).ok()?;
    let pixels = icon_decoder().load(bytes).pixels().clone();
    cache.insert(path.to_owned(), pixels.clone());
    Some(pixels)
}

fn forget_icons() {
    icon_cache()
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .clear();
}

pub struct ExtensionsContext {
    slots: Arc<Slots>,
}

impl ExtensionsContext {
    pub fn new(slots: Arc<Slots>) -> Arc<ExtensionsContext> {
        Arc::new(ExtensionsContext { slots })
    }

    pub fn register(self: &Arc<Self>) {
        let callbacks = XenonExtensionCallbacks {
            context: Arc::as_ptr(self).cast_mut().cast(),
            actions_changed: Some(actions_changed),
            popup_opened: Some(popup_opened),
            popup_resized: Some(popup_resized),
            popup_closed: Some(popup_closed),
            permission_prompt: Some(permission_prompt),
            extensions_changed: Some(extensions_changed),
            password_managers_changed: Some(password_managers_changed),
            commands_changed: Some(commands_changed),
            show_extensions: Some(show_extensions),
            error: Some(error),
        };
        unsafe { ffi_extensions::xenon_engine_set_extension_callbacks(&callbacks) };
    }
}

unsafe fn slots<'a>(context: *mut c_void) -> &'a Slots {
    unsafe { &(*context.cast::<ExtensionsContext>()).slots }
}

fn text(string: XenonStr) -> String {
    unsafe { string.to_string_lossy() }
}

unsafe fn slice<'a, T>(items: *const T, count: usize) -> &'a [T] {
    if items.is_null() || count == 0 {
        return &[];
    }
    unsafe { std::slice::from_raw_parts(items, count) }
}

pub fn action_from(action: &XenonExtensionAction) -> ActionButton {
    ActionButton {
        extension_id: text(action.extension_id),
        name: text(action.name),
        title: text(action.title),
        icon: icon(&text(action.icon_path)),
        badge_text: text(action.badge_text),
        badge_background: action.badge_background,
        badge_text_color: action
            .has_badge_text_color
            .then_some(action.badge_text_color),
        enabled: action.enabled,
        has_options_page: action.has_options_page,
    }
}

unsafe extern "C" fn actions_changed(
    context: *mut c_void,
    tab: XenonViewId,
    actions: *const XenonExtensionAction,
    count: usize,
) {
    let actions: Vec<ActionButton> = unsafe { slice(actions, count) }
        .iter()
        .map(action_from)
        .collect();
    unsafe { slots(context) }.update_view(tab, move |browser, _| {
        browser.extensions.set_actions(tab, actions)
    });
}

unsafe extern "C" fn popup_opened(
    context: *mut c_void,
    tab: XenonViewId,
    extension_id: XenonStr,
    popup: XenonViewId,
    width: u32,
    height: u32,
) {
    let extension_id = text(extension_id);
    let slots = unsafe { slots(context) };
    let Some(window) = slots.table().window_of_view(tab) else {
        ExtensionsEngine.close_popup(popup);
        return;
    };
    slots.view_placed(popup, window);
    slots.update_window(window, move |browser, _| {
        browser.popup_opened(tab, extension_id, popup, width, height)
    });
}

unsafe extern "C" fn popup_resized(
    context: *mut c_void,
    popup: XenonViewId,
    width: u32,
    height: u32,
) {
    unsafe { slots(context) }.update_view(popup, move |browser, _| {
        browser.popup_resized(popup, width, height)
    });
}

unsafe extern "C" fn popup_closed(context: *mut c_void, popup: XenonViewId) {
    let slots = unsafe { slots(context) };
    slots.update_view(popup, move |browser, _| browser.popup_closed(popup));
    slots.view_removed(popup);
}

unsafe extern "C" fn permission_prompt(
    context: *mut c_void,
    prompt: *const XenonExtensionPermissionPrompt,
) {
    let Some(prompt) = (unsafe { prompt.as_ref() }) else {
        return;
    };
    let raw_tab = prompt.tab;
    let prompt = PermissionPrompt {
        id: prompt.id,
        extension_name: text(prompt.extension_name),
        warnings: unsafe { slice(prompt.warnings, prompt.warning_count) }
            .iter()
            .map(|warning| text(*warning))
            .collect(),
        popup: (prompt.popup != 0).then_some(prompt.popup),
    };
    let tab = (raw_tab != 0).then_some(raw_tab);
    let slots = unsafe { slots(context) };
    let window = tab.and_then(|tab| slots.table().window_of_view(tab));
    let show =
        move |browser: &mut Browser, _: &mut Tx| browser.extensions.prompts.push_back(prompt);
    match window {
        Some(window) => slots.update_window(window, show),
        None => slots.update_target(show),
    }
}

pub fn extension_from(info: &XenonExtensionInfo) -> ExtensionInfo {
    let warnings = text(info.warnings);
    ExtensionInfo {
        id: text(info.id),
        name: text(info.name),
        version: text(info.version),
        description: text(info.description),
        details: text(info.details),
        warnings: warnings
            .lines()
            .filter(|line| !line.is_empty())
            .map(str::to_owned)
            .collect(),
        icon: icon(&text(info.icon_path)),
        has_options_page: info.has_options_page,
    }
}

unsafe extern "C" fn extensions_changed(
    context: *mut c_void,
    extensions: *const XenonExtensionInfo,
    count: usize,
) {
    forget_icons();
    let installed: Vec<ExtensionInfo> = unsafe { slice(extensions, count) }
        .iter()
        .map(extension_from)
        .collect();
    unsafe { slots(context) }
        .update(move |browser, _| browser.extensions.installed = installed.clone());
}

unsafe extern "C" fn password_managers_changed(
    context: *mut c_void,
    managers: *const XenonPasswordManagers,
) {
    let Some(managers) = (unsafe { managers.as_ref() }) else {
        return;
    };
    let managers = PasswordManagers {
        choices: unsafe { slice(managers.choices, managers.choice_count) }
            .iter()
            .map(|choice| (text(choice.id), text(choice.name)))
            .collect(),
        current: text(managers.current),
        installing: text(managers.installing),
        status: text(managers.status),
    };
    unsafe { slots(context) }
        .update(move |browser, _| browser.extensions.password_managers = managers.clone());
}

unsafe extern "C" fn commands_changed(
    context: *mut c_void,
    commands: *const XenonExtensionCommand,
    count: usize,
) {
    let commands: Vec<Command> = unsafe { slice(commands, count) }
        .iter()
        .filter_map(|command| {
            Some(Command {
                extension_id: text(command.extension_id),
                command: text(command.command),
                shortcut: CommandShortcut::parse(&text(command.shortcut), Platform::CURRENT)?,
            })
        })
        .collect();
    unsafe { slots(context) }
        .update(move |browser, _| browser.extensions.commands = commands.clone());
}

unsafe extern "C" fn show_extensions(context: *mut c_void) {
    unsafe { slots(context) }.update_target(|browser, _| {
        browser.extensions.dialog_open = true;
        browser.hover_card = None;
    });
}

unsafe extern "C" fn error(context: *mut c_void, title: XenonStr, message: XenonStr) {
    let notice = ErrorNotice {
        title: text(title),
        message: text(message),
    };
    unsafe { slots(context) }
        .update_target(move |browser, _| browser.extensions.error = Some(notice));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn mods(cmd: bool, ctrl: bool, alt: bool, shift: bool) -> Mods {
        Mods {
            shift,
            cmd,
            alt,
            ctrl,
            logo: false,
        }
    }

    #[test]
    fn a_suggested_key_matches_its_keystroke() {
        let shortcut = CommandShortcut::parse("Ctrl+Shift+Y", Platform::Mac).unwrap();
        assert!(shortcut.matches(
            &Key::Command('y'),
            mods(true, false, false, true),
            Platform::Mac
        ));
        assert!(!shortcut.matches(
            &Key::Command('y'),
            mods(true, false, false, false),
            Platform::Mac
        ));
        assert!(!shortcut.matches(
            &Key::Command('u'),
            mods(true, false, false, true),
            Platform::Mac
        ));
    }

    #[test]
    fn mac_ctrl_is_the_control_key_on_macos_only() {
        let mac = CommandShortcut::parse("MacCtrl+Shift+L", Platform::Mac).unwrap();
        assert!(mac.control && !mac.primary);
        assert!(mac.matches(
            &Key::Text("L".into()),
            mods(false, true, false, true),
            Platform::Mac
        ));
        let other = CommandShortcut::parse("MacCtrl+Shift+L", Platform::Other).unwrap();
        assert!(other.primary && !other.control);
    }

    #[test]
    fn named_keys_and_bad_shortcuts() {
        let comma = CommandShortcut::parse("Alt+Comma", Platform::Other).unwrap();
        assert_eq!(comma.key, ShortcutKey::Char(','));
        let up = CommandShortcut::parse("Ctrl+Up", Platform::Other).unwrap();
        assert!(up.matches(&Key::Up, mods(true, false, false, false), Platform::Other));
        assert!(CommandShortcut::parse("Ctrl+Shift", Platform::Other).is_none());
        assert!(CommandShortcut::parse("Ctrl+MediaPlayPause", Platform::Other).is_none());
    }

    #[test]
    fn popups_stay_within_chromes_bounds() {
        assert_eq!(clamp_popup_size(10, 5000), (25.0, 600.0));
        assert_eq!(clamp_popup_size(360, 480), (360.0, 480.0));
        assert_eq!(clamp_popup_size(2000, 30), (800.0, 30.0));
    }

    fn button(background: u32, text_color: Option<u32>, badge: &str) -> ActionButton {
        ActionButton {
            extension_id: "abc".into(),
            name: "Test".into(),
            title: "Test".into(),
            icon: None,
            badge_text: badge.into(),
            badge_background: background,
            badge_text_color: text_color,
            enabled: true,
            has_options_page: false,
        }
    }

    #[test]
    fn badges_pick_a_readable_ink_and_show_four_characters() {
        assert_eq!(button(0xff0050ff, None, "").badge_ink(), 0xffffffff);
        assert_eq!(button(0xffff80ff, None, "").badge_ink(), 0x000000ff);
        assert_eq!(
            button(0xffff80ff, Some(0x123456ff), "").badge_ink(),
            0x123456ff
        );
        assert_eq!(
            button(0, None, "123456").shown_badge().as_deref(),
            Some("1234")
        );
        assert_eq!(button(0, None, "").shown_badge(), None);
    }

    #[test]
    fn a_tab_without_its_own_actions_shows_the_latest() {
        let mut extensions = Extensions::default();
        extensions.set_actions(3, vec![button(0, None, "1")]);
        extensions.set_actions(4, vec![button(0, None, "2")]);
        assert_eq!(extensions.actions_for(Some(3))[0].badge_text, "1");
        assert_eq!(extensions.actions_for(Some(9))[0].badge_text, "2");
        assert!(extensions.forget_views(|view| view == 4));
        assert_eq!(extensions.actions_for(Some(3))[0].badge_text, "2");
    }

    #[test]
    fn the_shown_password_manager_is_the_one_installing() {
        let managers = PasswordManagers {
            choices: vec![("bitwarden".into(), "Bitwarden".into())],
            current: "bitwarden".into(),
            installing: "proton-pass".into(),
            status: String::new(),
        };
        assert_eq!(managers.shown(), "proton-pass");
        assert_eq!(managers.name_of("bitwarden"), Some("Bitwarden"));
    }

    #[test]
    fn a_popups_view_is_shown_by_the_window_drawing_it() {
        let mut browser = Browser::new(Arc::default());
        browser.windows.opened(crate::tab_list::WindowInfo {
            id: 1,
            private: false,
        });
        browser.extensions.popup = Some(Popup {
            tab: 7,
            extension_id: String::new(),
            view: 9,
            size: (0.0, 0.0),
            pixels: Pixels::new(),
            cursor: 0,
            sent_viewport: Cell::new(None),
            pressed_buttons: 0,
        });
        assert!(browser.shows_view(9));
    }
}
