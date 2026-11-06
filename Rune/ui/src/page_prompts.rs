/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::HashMap;
use std::path::PathBuf;

use rune::{
    FileDialogOptions, FileDialogResult, Key, KeyEvent, PopupItem, PopupMenu, TextEdit, Tx,
    popup_menu_supported,
};

use crate::engine::ViewId;
use crate::keys::{Platform, shortcut_for};
use crate::page_bridge::PageEngine;
use crate::state::Browser;

pub type PromptId = u64;

pub const MENU_ANCHOR_KEY: &str = "page-menu-anchor";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DialogKind {
    Alert,
    Confirm,
    Prompt,
    BeforeUnload,
    ExternalUrl,
}

pub struct Dialog {
    pub id: PromptId,
    pub kind: DialogKind,
    pub title: String,
    pub message: String,
    pub accept_label: String,
    pub cancel_label: Option<String>,
    pub input: TextEdit,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RawMenuItem {
    pub kind: RawMenuItemKind,
    pub id: u32,
    pub title: String,
    pub enabled: bool,
    pub checkable: bool,
    pub checked: bool,
    pub indented: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RawMenuItemKind {
    Action,
    Separator,
    Heading,
    SubmenuBegin,
    SubmenuEnd,
}

#[derive(Clone, Debug, PartialEq)]
pub enum MenuEntry {
    Action {
        id: u32,
        title: String,
        enabled: bool,
        checkable: bool,
        checked: bool,
        indented: bool,
    },
    Separator,
    Heading(String),
    Submenu {
        title: String,
        entries: Vec<MenuEntry>,
    },
}

pub fn menu_entries(items: &[RawMenuItem]) -> Vec<MenuEntry> {
    let mut stack: Vec<(String, Vec<MenuEntry>)> = vec![(String::new(), Vec::new())];
    for item in items {
        match item.kind {
            RawMenuItemKind::Action => {
                if let Some((_, entries)) = stack.last_mut() {
                    entries.push(MenuEntry::Action {
                        id: item.id,
                        title: item.title.clone(),
                        enabled: item.enabled,
                        checkable: item.checkable,
                        checked: item.checked,
                        indented: item.indented,
                    });
                }
            }
            RawMenuItemKind::Separator => {
                if let Some((_, entries)) = stack.last_mut() {
                    entries.push(MenuEntry::Separator);
                }
            }
            RawMenuItemKind::Heading => {
                if let Some((_, entries)) = stack.last_mut() {
                    entries.push(MenuEntry::Heading(item.title.clone()));
                }
            }
            RawMenuItemKind::SubmenuBegin => stack.push((item.title.clone(), Vec::new())),
            RawMenuItemKind::SubmenuEnd => {
                if stack.len() > 1
                    && let Some((title, entries)) = stack.pop()
                    && let Some((_, parent)) = stack.last_mut()
                {
                    parent.push(MenuEntry::Submenu { title, entries });
                }
            }
        }
    }
    while stack.len() > 1 {
        if let Some((title, entries)) = stack.pop()
            && let Some((_, parent)) = stack.last_mut()
        {
            parent.push(MenuEntry::Submenu { title, entries });
        }
    }
    stack.pop().map(|(_, entries)| entries).unwrap_or_default()
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MenuPurpose {
    Context,
    Select,
}

pub struct ShownMenu {
    pub view: ViewId,
    pub id: PromptId,
    pub purpose: MenuPurpose,
    pub x: f32,
    pub y: f32,
    pub minimum_width: f32,
    pub entries: Vec<MenuEntry>,
    pub open_submenus: Vec<usize>,
    pub native: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FindResult {
    Searching,
    NotFound,
    Match { current: usize, total: usize },
}

impl FindResult {
    pub fn label(self) -> Option<String> {
        match self {
            FindResult::Searching => None,
            FindResult::NotFound => Some("No matches".to_owned()),
            FindResult::Match { current, total } => Some(format!("{} of {}", current + 1, total)),
        }
    }
}

pub struct FindBar {
    pub query: TextEdit,
    sent_query: Option<String>,
    pub result: Option<FindResult>,
}

pub struct PermissionPrompt {
    pub id: PromptId,
    pub notice: bool,
    pub permission: String,
    pub requester: String,
}

impl PermissionPrompt {
    pub fn question(&self) -> String {
        if self.notice {
            return format!(
                "Your system keeps Xenon from using your {}, so no site can use it.",
                self.permission
            );
        }
        format!("Allow {} to use your {}?", self.requester, self.permission)
    }
}

pub struct Tooltip {
    pub text: String,
    pub x: f32,
    pub y: f32,
}

#[derive(Default)]
pub struct ViewPrompts {
    pub dialog: Option<Dialog>,
    pub find: Option<FindBar>,
    pub permissions: Vec<PermissionPrompt>,
    pub tooltip: Option<Tooltip>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KeyContext {
    Window,
    Page,
    Location,
}

#[derive(Default)]
pub struct PagePrompts {
    views: HashMap<ViewId, ViewPrompts>,
    pub menu: Option<ShownMenu>,
    fullscreen: Option<ViewId>,
    window_fullscreen_for_page: bool,
}

impl PagePrompts {
    pub fn of(&self, view: ViewId) -> Option<&ViewPrompts> {
        self.views.get(&view)
    }

    fn of_mut(&mut self, view: ViewId) -> &mut ViewPrompts {
        self.views.entry(view).or_default()
    }

    pub fn dialog_input(&mut self, view: ViewId) -> &mut TextEdit {
        &mut self
            .of_mut(view)
            .dialog
            .get_or_insert_with(|| Dialog {
                id: 0,
                kind: DialogKind::Alert,
                title: String::new(),
                message: String::new(),
                accept_label: String::new(),
                cancel_label: None,
                input: TextEdit::new(""),
            })
            .input
    }

    pub fn find_query(&mut self, view: ViewId) -> &mut TextEdit {
        &mut self
            .of_mut(view)
            .find
            .get_or_insert_with(FindBar::closed)
            .query
    }
}

impl FindBar {
    fn closed() -> FindBar {
        FindBar {
            query: TextEdit::new(""),
            sent_query: None,
            result: None,
        }
    }
}

pub fn active_view(s: &Browser) -> Option<ViewId> {
    s.tabs.active_view()
}

fn view_exists(s: &Browser, view: ViewId) -> bool {
    s.tabs.get(view).is_some()
}

fn points(s: &Browser, device: f32) -> f32 {
    device / s.page.scale().max(0.01)
}

pub fn chrome_hidden(s: &Browser) -> bool {
    s.page_prompts.fullscreen.is_some() && s.page_prompts.fullscreen == active_view(s)
}

pub fn dialog_opened(
    s: &mut Browser,
    view: ViewId,
    id: PromptId,
    kind: DialogKind,
    title: String,
    message: String,
    default_text: String,
    accept_label: String,
    cancel_label: Option<String>,
) {
    if !view_exists(s, view) {
        return;
    }
    let focus_input = kind == DialogKind::Prompt && active_view(s) == Some(view);
    let mut input = TextEdit::new(default_text);
    if focus_input {
        let end = input.text.len();
        input.set_selection(0, end);
        input.focused = true;
    }
    if s.page_prompts
        .menu
        .as_ref()
        .is_some_and(|menu| menu.view == view)
    {
        dismiss_menu(s);
    }
    let prompts = s.page_prompts.of_mut(view);
    prompts.tooltip = None;
    prompts.dialog = Some(Dialog {
        id,
        kind,
        title,
        message,
        accept_label,
        cancel_label,
        input,
    });
}

pub fn dialog_text_changed(s: &mut Browser, view: ViewId, id: PromptId, text: String) {
    if let Some(dialog) = s
        .page_prompts
        .views
        .get_mut(&view)
        .and_then(|prompts| prompts.dialog.as_mut())
        .filter(|dialog| dialog.id == id)
    {
        dialog.input.replace_text(text);
    }
}

pub fn dialog_closed(s: &mut Browser, view: ViewId, id: PromptId) {
    if let Some(prompts) = s.page_prompts.views.get_mut(&view)
        && prompts
            .dialog
            .as_ref()
            .is_some_and(|dialog| dialog.id == id)
    {
        prompts.dialog = None;
    }
}

pub fn answer_dialog(s: &mut Browser, view: ViewId, accepted: bool, tx: &mut Tx) {
    let Some(dialog) = s
        .page_prompts
        .views
        .get_mut(&view)
        .and_then(|prompts| prompts.dialog.take())
    else {
        return;
    };
    if dialog.input.focused {
        tx.blur();
    }
    PageEngine.close_dialog(view, dialog.id, accepted, &dialog.input.text);
}

pub fn active_dialog(s: &Browser) -> Option<(ViewId, &Dialog)> {
    let view = active_view(s)?;
    let dialog = s.page_prompts.of(view)?.dialog.as_ref()?;
    (dialog.id != 0).then_some((view, dialog))
}

pub fn show_menu(
    s: &mut Browser,
    view: ViewId,
    id: PromptId,
    purpose: MenuPurpose,
    (x, y, minimum_width): (f32, f32, f32),
    entries: Vec<MenuEntry>,
    tx: &mut Tx,
) {
    if active_view(s) != Some(view)
        || entries.is_empty()
        || s.page_prompts
            .of(view)
            .is_some_and(|prompts| prompts.dialog.is_some())
    {
        if purpose == MenuPurpose::Select {
            PageEngine.close_select_dropdown(view, id, None);
        }
        return;
    }
    if s.page_prompts.menu.is_some() {
        dismiss_menu(s);
    }
    s.page_prompts.of_mut(view).tooltip = None;
    let native = popup_menu_supported();
    s.page_prompts.menu = Some(ShownMenu {
        view,
        id,
        purpose,
        x: points(s, x),
        y: points(s, y),
        minimum_width: points(s, minimum_width),
        entries,
        open_submenus: Vec::new(),
        native,
    });
    if !native {
        return;
    }
    let Some(menu) = s.page_prompts.menu.as_ref() else {
        return;
    };
    let popup = popup_menu_for(&menu.entries);
    tx.popup_menu(
        popup,
        MENU_ANCHOR_KEY,
        move |s: &mut Browser, picked: Option<&str>, _: &mut Tx| match picked
            .and_then(|picked| picked.parse::<u32>().ok())
        {
            Some(item) => pick_menu_item(s, view, id, item),
            None => dismiss_menu_with_id(s, view, id),
        },
    );
}

pub fn popup_menu_for(entries: &[MenuEntry]) -> PopupMenu {
    let mut popup = PopupMenu::new();
    for entry in entries {
        popup = match entry {
            MenuEntry::Action {
                id,
                title,
                enabled,
                checked,
                indented,
                ..
            } => {
                let title = if *indented {
                    format!("    {title}")
                } else {
                    title.clone()
                };
                popup.item(
                    PopupItem::new(id.to_string(), title)
                        .enabled(*enabled)
                        .checked(*checked),
                )
            }
            MenuEntry::Separator => popup.separator(),
            MenuEntry::Heading(title) => {
                popup.item(PopupItem::new(String::new(), title.clone()).enabled(false))
            }
            MenuEntry::Submenu { title, entries } => {
                popup.submenu(title.clone(), popup_menu_for(entries))
            }
        };
    }
    popup
}

pub fn pick_menu_item(s: &mut Browser, view: ViewId, id: PromptId, item: u32) {
    let Some(menu) = s
        .page_prompts
        .menu
        .take_if(|menu| menu.view == view && menu.id == id)
    else {
        return;
    };
    match menu.purpose {
        MenuPurpose::Context => PageEngine.activate_menu_item(view, id, item),
        MenuPurpose::Select => PageEngine.close_select_dropdown(view, id, Some(item)),
    }
}

fn dismiss_menu_with_id(s: &mut Browser, view: ViewId, id: PromptId) {
    if s.page_prompts
        .menu
        .as_ref()
        .is_some_and(|menu| menu.view == view && menu.id == id)
    {
        dismiss_menu(s);
    }
}

pub fn dismiss_menu(s: &mut Browser) {
    let Some(menu) = s.page_prompts.menu.take() else {
        return;
    };
    if menu.purpose == MenuPurpose::Select {
        PageEngine.close_select_dropdown(menu.view, menu.id, None);
    }
}

pub fn open_submenu(s: &mut Browser, depth: usize, index: usize) {
    let Some(menu) = s.page_prompts.menu.as_mut() else {
        return;
    };
    menu.open_submenus.truncate(depth);
    menu.open_submenus.push(index);
}

pub fn close_submenus_below(s: &mut Browser, depth: usize) {
    if let Some(menu) = s.page_prompts.menu.as_mut() {
        menu.open_submenus.truncate(depth);
    }
}

pub fn file_picker_requested(
    s: &mut Browser,
    view: ViewId,
    id: PromptId,
    allow_multiple: bool,
    filters: Vec<(String, Vec<String>)>,
    tx: &mut Tx,
) {
    if !view_exists(s, view) {
        PageEngine.close_file_picker(view, id, &[]);
        return;
    }
    let mut options = FileDialogOptions {
        title: if allow_multiple {
            "Select files".to_owned()
        } else {
            "Select file".to_owned()
        },
        directory: std::env::var_os("HOME").map(PathBuf::from),
        ..FileDialogOptions::default()
    };
    for (name, extensions) in &filters {
        let extensions: Vec<&str> = extensions.iter().map(String::as_str).collect();
        options = options.filter(name.clone(), &extensions);
    }
    tx.open_file(
        options,
        move |_: &mut Browser, result: FileDialogResult, _: &mut Tx| {
            let paths: Vec<String> = match result {
                Ok(Some(path)) => vec![path.to_string_lossy().into_owned()],
                Ok(None) => Vec::new(),
                Err(error) => {
                    eprintln!("Xenon: Unable to show the file picker: {error}");
                    Vec::new()
                }
            };
            PageEngine.close_file_picker(view, id, &paths);
        },
    );
}

pub fn open_find(s: &mut Browser, tx: &mut Tx) {
    let Some(view) = active_view(s) else {
        return;
    };
    let _ = tx;
    let find = s
        .page_prompts
        .of_mut(view)
        .find
        .get_or_insert_with(FindBar::closed);
    let end = find.query.text.len();
    find.query.set_selection(0, end);
    find.query.focused = true;
}

pub fn close_find(s: &mut Browser, view: ViewId, tx: &mut Tx) {
    let Some(find) = s
        .page_prompts
        .views
        .get_mut(&view)
        .and_then(|prompts| prompts.find.take())
    else {
        return;
    };
    if find.query.focused {
        tx.blur();
    }
    PageEngine.end_find(view);
}

pub fn find_step(s: &mut Browser, view: ViewId, forward: bool) {
    let Some(find) = s
        .page_prompts
        .views
        .get_mut(&view)
        .and_then(|prompts| prompts.find.as_mut())
    else {
        return;
    };
    if find.query.text.is_empty() {
        return;
    }
    if find.sent_query.as_deref() != Some(find.query.text.as_str()) {
        find.sent_query = Some(find.query.text.clone());
        find.result = Some(FindResult::Searching);
        PageEngine.find_in_page(view, &find.query.text);
        return;
    }
    if forward {
        PageEngine.find_next(view);
    } else {
        PageEngine.find_previous(view);
    }
}

pub fn find_result(s: &mut Browser, view: ViewId, current: usize, total: Option<usize>) {
    if let Some(find) = s
        .page_prompts
        .views
        .get_mut(&view)
        .and_then(|prompts| prompts.find.as_mut())
    {
        find.result = Some(match total {
            Some(0) => FindResult::NotFound,
            Some(total) => FindResult::Match { current, total },
            None => FindResult::Searching,
        });
    }
}

pub fn find_key(s: &mut Browser, view: ViewId, event: &KeyEvent, tx: &mut Tx) -> bool {
    match event.key {
        Key::Escape => {
            close_find(s, view, tx);
            true
        }
        Key::Enter => {
            find_step(s, view, !event.mods.shift);
            true
        }
        _ => false,
    }
}

pub fn tooltip_changed(s: &mut Browser, view: ViewId, text: String, x: f32, y: f32) {
    if !view_exists(s, view) {
        return;
    }
    let tooltip = (!text.trim().is_empty()).then(|| Tooltip {
        text,
        x: points(s, x),
        y: points(s, y),
    });
    s.page_prompts.of_mut(view).tooltip = tooltip;
}

pub fn fullscreen_requested(s: &mut Browser, view: ViewId, fullscreen: bool) {
    if fullscreen {
        if active_view(s) != Some(view) {
            PageEngine.set_fullscreen(view, false);
            return;
        }
        s.page_prompts.fullscreen = Some(view);
        if !crate::platform::is_fullscreen() {
            s.page_prompts.window_fullscreen_for_page = true;
            crate::platform::set_fullscreen(true);
        }
        PageEngine.set_fullscreen(view, true);
    } else if s.page_prompts.fullscreen == Some(view) {
        s.page_prompts.fullscreen = None;
        leave_window_fullscreen_for_page(s);
        PageEngine.set_fullscreen(view, false);
    }
}

fn leave_window_fullscreen_for_page(s: &mut Browser) {
    if std::mem::take(&mut s.page_prompts.window_fullscreen_for_page) {
        crate::platform::set_fullscreen(false);
    }
}

pub fn exit_fullscreen(s: &mut Browser) {
    if let Some(view) = s.page_prompts.fullscreen.take() {
        leave_window_fullscreen_for_page(s);
        PageEngine.set_fullscreen(view, false);
    }
}

pub fn permission_requested(
    s: &mut Browser,
    view: ViewId,
    id: PromptId,
    notice: bool,
    permission: String,
    requester: String,
) {
    if !view_exists(s, view) {
        PageEngine.answer_permission(view, id, PermissionAnswer::Dismiss);
        return;
    }
    s.page_prompts
        .of_mut(view)
        .permissions
        .push(PermissionPrompt {
            id,
            notice,
            permission,
            requester,
        });
}

pub fn permission_cancelled(s: &mut Browser, view: ViewId, id: PromptId) {
    if let Some(prompts) = s.page_prompts.views.get_mut(&view) {
        prompts.permissions.retain(|prompt| prompt.id != id);
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PermissionAnswer {
    AllowAlways,
    AllowOnce,
    Block,
    Dismiss,
}

pub fn answer_permission(s: &mut Browser, view: ViewId, id: PromptId, answer: PermissionAnswer) {
    let Some(prompts) = s.page_prompts.views.get_mut(&view) else {
        return;
    };
    let before = prompts.permissions.len();
    prompts.permissions.retain(|prompt| prompt.id != id);
    if prompts.permissions.len() != before {
        PageEngine.answer_permission(view, id, answer);
    }
}

pub fn key(s: &mut Browser, event: &KeyEvent, tx: &mut Tx, context: KeyContext) -> bool {
    let primary_only = event.mods.cmd && !event.mods.alt && !event.mods.shift;
    if matches!(event.key, Key::Command('f')) && primary_only {
        open_find(s, tx);
        return true;
    }
    if context == KeyContext::Location {
        return false;
    }
    if let Some((view, _)) = active_dialog(s) {
        if shortcut_for(&event.key, event.mods, Platform::CURRENT).is_some() {
            return false;
        }
        match event.key {
            Key::Escape => answer_dialog(s, view, false, tx),
            Key::Enter => answer_dialog(s, view, true, tx),
            _ => {}
        }
        return true;
    }
    if event.key == Key::Escape && chrome_hidden(s) {
        exit_fullscreen(s);
        return true;
    }
    false
}

pub fn poll(s: &mut Browser, _: &mut Tx) -> bool {
    let mut changed = false;
    if s.page_prompts
        .views
        .keys()
        .any(|view| !view_exists(s, *view))
    {
        let live: Vec<ViewId> = s
            .page_prompts
            .views
            .keys()
            .copied()
            .filter(|view| view_exists(s, *view))
            .collect();
        s.page_prompts.views.retain(|view, _| live.contains(view));
        changed = true;
    }
    if let Some(menu) = &s.page_prompts.menu
        && active_view(s) != Some(menu.view)
    {
        dismiss_menu(s);
        changed = true;
    }
    if let Some(view) = s.page_prompts.fullscreen
        && active_view(s) != Some(view)
    {
        exit_fullscreen(s);
        changed = true;
    }
    if let Some(view) = active_view(s)
        && let Some(find) = s
            .page_prompts
            .views
            .get_mut(&view)
            .and_then(|prompts| prompts.find.as_mut())
        && find.sent_query.as_deref() != Some(find.query.text.as_str())
    {
        find.sent_query = Some(find.query.text.clone());
        find.result = (!find.query.text.is_empty()).then_some(FindResult::Searching);
        PageEngine.find_in_page(view, &find.query.text);
        changed = true;
    }
    changed
}

#[cfg(test)]
mod tests {
    use super::*;

    fn raw(kind: RawMenuItemKind, id: u32, title: &str) -> RawMenuItem {
        RawMenuItem {
            kind,
            id,
            title: title.to_owned(),
            enabled: true,
            checkable: false,
            checked: false,
            indented: false,
        }
    }

    #[test]
    fn flat_menu_items_nest_into_submenus() {
        let items = [
            raw(RawMenuItemKind::Action, 0, "Back"),
            raw(RawMenuItemKind::Separator, 0, ""),
            raw(RawMenuItemKind::SubmenuBegin, 0, "Developer"),
            raw(RawMenuItemKind::Action, 1, "Dump DOM"),
            raw(RawMenuItemKind::SubmenuBegin, 0, "Deeper"),
            raw(RawMenuItemKind::Action, 2, "Crash"),
            raw(RawMenuItemKind::SubmenuEnd, 0, ""),
            raw(RawMenuItemKind::SubmenuEnd, 0, ""),
            raw(RawMenuItemKind::Action, 3, "Inspect"),
        ];
        let entries = menu_entries(&items);
        assert_eq!(entries.len(), 4);
        let MenuEntry::Submenu {
            title,
            entries: developer,
        } = &entries[2]
        else {
            panic!("the third entry is a submenu");
        };
        assert_eq!(title, "Developer");
        assert_eq!(developer.len(), 2);
        assert!(matches!(&entries[3], MenuEntry::Action { id: 3, .. }));
    }

    #[test]
    fn an_unterminated_submenu_still_closes() {
        let items = [
            raw(RawMenuItemKind::SubmenuBegin, 0, "More"),
            raw(RawMenuItemKind::Action, 5, "Item"),
        ];
        let entries = menu_entries(&items);
        assert_eq!(entries.len(), 1);
        assert!(matches!(&entries[0], MenuEntry::Submenu { entries, .. } if entries.len() == 1));
    }

    #[test]
    fn find_results_read_as_the_find_bar_shows_them() {
        assert_eq!(FindResult::Searching.label(), None);
        assert_eq!(FindResult::NotFound.label().as_deref(), Some("No matches"));
        assert_eq!(
            FindResult::Match {
                current: 0,
                total: 3
            }
            .label()
            .as_deref(),
            Some("1 of 3")
        );
    }

    #[test]
    fn permission_prompts_name_the_requester() {
        let prompt = PermissionPrompt {
            id: 1,
            notice: false,
            permission: "geolocation".to_owned(),
            requester: "https://example.com".to_owned(),
        };
        assert_eq!(
            prompt.question(),
            "Allow https://example.com to use your geolocation?"
        );
    }
}
