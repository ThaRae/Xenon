/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;
use std::sync::{Arc, OnceLock};

use rune::{ClipboardContent, PointerPress, Tx};

use crate::chrome_popup::{self, PopupItem, PopupMenu};
use crate::ffi::{XenonStr, XenonViewId};
use crate::state::Browser;
use crate::window_slots::Slots;

pub const HEADLESS_CLIPBOARD_VARIABLE: &str = "XENON_RUNE_CLIPBOARD";

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonLocationMenuCallbacks {
    pub context: *mut c_void,
    pub paste_and_go_described:
        Option<unsafe extern "C" fn(context: *mut c_void, request: u64, label: XenonStr)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_location_menu_callbacks(callbacks: *const XenonLocationMenuCallbacks);
    pub fn xenon_engine_location_describe_paste_and_go(request: u64, clipboard_text: XenonStr);
    pub fn xenon_engine_location_paste_and_go(view: XenonViewId, generation: u64, text: XenonStr);
    pub fn xenon_engine_location_set_suspended(view: XenonViewId, generation: u64, suspended: bool);
}

static HEADLESS: OnceLock<bool> = OnceLock::new();

pub fn set_headless(headless: bool) {
    let _ = HEADLESS.set(headless);
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ClipboardSource {
    System,
    Scripted(String),
}

impl ClipboardSource {
    pub fn for_run(headless: bool, scripted: Option<String>) -> ClipboardSource {
        if headless {
            ClipboardSource::Scripted(scripted.unwrap_or_default())
        } else {
            ClipboardSource::System
        }
    }

    fn current() -> ClipboardSource {
        let headless = HEADLESS.get().copied().unwrap_or(false);
        ClipboardSource::for_run(
            headless,
            headless
                .then(|| std::env::var(HEADLESS_CLIPBOARD_VARIABLE).ok())
                .flatten(),
        )
    }
}

pub fn read_clipboard_text(
    s: &mut Browser,
    tx: &mut Tx,
    then: impl FnOnce(&mut Browser, String, &mut Tx) + 'static,
) {
    match ClipboardSource::current() {
        ClipboardSource::Scripted(text) => then(s, text, tx),
        ClipboardSource::System => {
            tx.read_clipboard(move |s: &mut Browser, content: &ClipboardContent, tx| {
                then(s, content.text().to_owned(), tx)
            })
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct MenuRequest {
    pub id: u64,
    pub clipboard: Option<String>,
    pub at: (f32, f32),
}

fn item(id: &str, title: &str, enabled: bool) -> PopupItem {
    PopupItem::new(id, title).enabled(enabled)
}

pub fn menu(
    has_selection: bool,
    has_text: bool,
    clipboard: &str,
    paste_and_go_label: &str,
) -> PopupMenu {
    let has_clipboard = !clipboard.is_empty();
    PopupMenu::new()
        .item(item("undo", "Undo", true))
        .item(item("redo", "Redo", true))
        .separator()
        .item(item("cut", "Cut", has_selection))
        .item(item("copy", "Copy", has_selection))
        .item(item("paste", "Paste", has_clipboard))
        .item(item("paste-and-go", paste_and_go_label, has_clipboard))
        .item(item("delete", "Delete", has_selection))
        .separator()
        .item(item("select-all", "Select All", has_text))
}

impl Browser {
    pub fn show_location_menu(&mut self, at: PointerPress, tx: &mut Tx) {
        if !self.location.edit.focused {
            self.focus_location();
        }
        let id = self.location.next_menu_request();
        self.location.menu_request = Some(MenuRequest {
            id,
            clipboard: None,
            at: (at.x, at.y),
        });
        read_clipboard_text(self, tx, move |s, clipboard, _| {
            s.location_clipboard_read(id, clipboard)
        });
    }

    fn location_clipboard_read(&mut self, request: u64, clipboard: String) {
        let Some(menu_request) = self
            .location
            .menu_request
            .as_mut()
            .filter(|pending| pending.id == request)
        else {
            return;
        };
        unsafe {
            xenon_engine_location_describe_paste_and_go(request, XenonStr::borrowed(&clipboard))
        };
        menu_request.clipboard = Some(clipboard);
    }

    pub fn location_paste_and_go_described(&mut self, request: u64, label: String, tx: &mut Tx) {
        let Some(menu_request) = self
            .location
            .menu_request
            .take_if(|pending| pending.id == request && pending.clipboard.is_some())
        else {
            return;
        };
        let edit = &self.location.edit;
        let (start, end) = edit.range();
        let clipboard = menu_request.clipboard.unwrap_or_default();
        let popup = menu(start != end, !edit.text.is_empty(), &clipboard, &label);
        self.location.menu_opened();
        chrome_popup::open_tracked(
            self,
            tx,
            popup,
            menu_request.at,
            move |s: &mut Browser, picked: &str, tx: &mut Tx| {
                s.location.menu_closed();
                s.run_location_menu_item(picked, &clipboard, tx);
            },
            |s: &mut Browser| s.location.menu_closed(),
        );
    }

    fn run_location_menu_item(&mut self, picked: &str, clipboard: &str, tx: &mut Tx) {
        let edit = &mut self.location.edit;
        match picked {
            "undo" => edit.undo(),
            "redo" => edit.redo(),
            "cut" => {
                tx.copy(edit.selected().to_owned());
                edit.insert("");
            }
            "copy" => tx.copy(edit.selected().to_owned()),
            "paste" => edit.insert(clipboard),
            "delete" => edit.insert(""),
            "select-all" => {
                let end = edit.text.len();
                edit.set_selection(0, end);
            }
            "paste-and-go" => self.location.paste_and_go(clipboard),
            _ => {}
        }
    }
}

pub struct LocationMenuContext {
    slots: Arc<Slots>,
}

pub fn install(slots: Arc<Slots>) -> Box<LocationMenuContext> {
    let context = Box::new(LocationMenuContext { slots });
    let callbacks = XenonLocationMenuCallbacks {
        context: std::ptr::from_ref::<LocationMenuContext>(&context)
            .cast_mut()
            .cast(),
        paste_and_go_described: Some(paste_and_go_described),
    };
    unsafe { xenon_engine_set_location_menu_callbacks(&callbacks) };
    context
}

unsafe extern "C" fn paste_and_go_described(context: *mut c_void, request: u64, label: XenonStr) {
    let label = unsafe { label.to_string_lossy() };
    let slots = unsafe { &(*context.cast::<LocationMenuContext>()).slots };
    slots.update_target(move |browser, tx| {
        browser.location_paste_and_go_described(request, label, tx)
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::page_prompts::MenuEntry;

    fn find(menu: &PopupMenu, name: &str) -> Option<(String, bool)> {
        menu.entries().iter().find_map(|entry| match entry {
            MenuEntry::Action {
                id, title, enabled, ..
            } if menu.name(*id) == Some(name) => Some((title.clone(), *enabled)),
            _ => None,
        })
    }

    fn enabled(menu: &PopupMenu, name: &str) -> bool {
        find(menu, name).is_some_and(|(_, enabled)| enabled)
    }

    #[test]
    fn paste_and_go_follows_paste_and_needs_a_clipboard() {
        let full = menu(true, true, "ladybird.org", "Paste and Go to ladybird.org");
        let names: Vec<&str> = (0..9).filter_map(|item| full.name(item)).collect();
        assert_eq!(
            names,
            [
                "undo",
                "redo",
                "cut",
                "copy",
                "paste",
                "paste-and-go",
                "delete",
                "select-all"
            ]
        );
        assert_eq!(
            find(&full, "paste-and-go").map(|(title, _)| title),
            Some("Paste and Go to ladybird.org".to_owned())
        );
        assert!(enabled(&full, "paste-and-go"));
        assert!(enabled(&full, "cut"));

        let empty = menu(false, false, "", "Paste and Search for ''");
        assert!(!enabled(&empty, "paste-and-go"));
        assert!(!enabled(&empty, "paste"));
        assert!(!enabled(&empty, "copy"));
        assert!(!enabled(&empty, "select-all"));
    }

    #[test]
    fn a_headless_run_reads_the_scripted_clipboard_and_never_the_system_one() {
        assert_eq!(
            ClipboardSource::for_run(true, Some("ladybird.org".into())),
            ClipboardSource::Scripted("ladybird.org".into())
        );
        assert_eq!(
            ClipboardSource::for_run(true, None),
            ClipboardSource::Scripted(String::new())
        );
        assert_eq!(
            ClipboardSource::for_run(false, Some("ignored".into())),
            ClipboardSource::System
        );
    }
}
