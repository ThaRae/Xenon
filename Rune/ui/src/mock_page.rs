/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, MutexGuard};

use crate::ffi::{
    XENON_MOUSE_BUTTON_PRIMARY, XENON_MOUSE_BUTTON_SECONDARY, XENON_MOUSE_EVENT_UP,
    XenonMouseEvent, XenonStr, XenonViewId,
};
use crate::ffi_page::{
    XENON_DIALOG_ALERT, XENON_DIALOG_CONFIRM, XENON_DIALOG_PROMPT, XENON_MENU_ITEM_ACTION,
    XENON_MENU_ITEM_HEADING, XENON_MENU_ITEM_SEPARATOR, XENON_MENU_ITEM_SUBMENU_BEGIN,
    XENON_MENU_ITEM_SUBMENU_END, XENON_PERMISSION_PROMPT_QUESTION, XenonDialog, XenonFileFilter,
    XenonFilePicker, XenonMenu, XenonMenuItem, XenonPageCallbacks, XenonPermissionRequest,
    XenonPromptId,
};

const FIND_TEXT: &str =
    "The quick brown fox jumps over the lazy dog. The dog sleeps while the fox runs.";

struct SendablePageCallbacks(XenonPageCallbacks);

unsafe impl Send for SendablePageCallbacks {}

static CALLBACKS: Mutex<Option<SendablePageCallbacks>> = Mutex::new(None);
static NEXT_PROMPT: AtomicU64 = AtomicU64::new(1);
static STATE: Mutex<Option<MockPageState>> = Mutex::new(None);

#[derive(Default)]
struct MockPageState {
    find: HashMap<XenonViewId, (usize, usize)>,
}

fn state() -> MutexGuard<'static, Option<MockPageState>> {
    STATE
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn record(answer: String) {
    eprintln!("mock page: {answer}");
}

fn callbacks() -> Option<XenonPageCallbacks> {
    CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .as_ref()
        .map(|callbacks| callbacks.0)
}

fn next_prompt() -> XenonPromptId {
    NEXT_PROMPT.fetch_add(1, Ordering::Relaxed)
}

fn str(text: &str) -> XenonStr {
    XenonStr::borrowed(text)
}

fn open_dialog(view: XenonViewId, kind: u32, message: &str, default_text: &str) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let Some(opened) = callbacks.dialog_opened else {
        return;
    };
    let dialog = XenonDialog {
        id: next_prompt(),
        kind,
        title: str("https://mock.test"),
        message: str(message),
        default_text: str(default_text),
        accept_label: str("OK"),
        cancel_label: str(if kind == XENON_DIALOG_ALERT {
            ""
        } else {
            "Cancel"
        }),
    };
    unsafe { opened(callbacks.context, view, &dialog) };
}

fn item(kind: u32, id: u32, title: &'static str) -> XenonMenuItem {
    XenonMenuItem {
        kind,
        id,
        title: str(title),
        enabled: true,
        checkable: false,
        checked: false,
        indented: false,
    }
}

fn show_context_menu(view: XenonViewId, x: f32, y: f32) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let Some(requested) = callbacks.context_menu_requested else {
        return;
    };
    let items = [
        XenonMenuItem {
            enabled: false,
            ..item(XENON_MENU_ITEM_ACTION, 0, "Back")
        },
        item(XENON_MENU_ITEM_ACTION, 1, "Forward"),
        item(XENON_MENU_ITEM_ACTION, 2, "Reload"),
        item(XENON_MENU_ITEM_SEPARATOR, 0, ""),
        item(XENON_MENU_ITEM_SUBMENU_BEGIN, 0, "Developer"),
        item(XENON_MENU_ITEM_ACTION, 3, "View Source"),
        XenonMenuItem {
            checkable: true,
            checked: true,
            ..item(XENON_MENU_ITEM_ACTION, 4, "Enable Scripting")
        },
        item(XENON_MENU_ITEM_SUBMENU_END, 0, ""),
        item(XENON_MENU_ITEM_SEPARATOR, 0, ""),
        item(XENON_MENU_ITEM_ACTION, 5, "Inspect Element"),
    ];
    let menu = XenonMenu {
        id: next_prompt(),
        x,
        y,
        minimum_width: 0.0,
        items: items.as_ptr(),
        item_count: items.len(),
    };
    unsafe { requested(callbacks.context, view, &menu) };
}

fn show_select(view: XenonViewId, x: f32, y: f32) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let Some(requested) = callbacks.select_dropdown_requested else {
        return;
    };
    let option = |id: u32, title: &'static str, selected: bool, indented: bool| XenonMenuItem {
        checkable: true,
        checked: selected,
        indented,
        ..item(XENON_MENU_ITEM_ACTION, id, title)
    };
    let items = [
        option(1, "Apple", false, false),
        option(2, "Banana", true, false),
        item(XENON_MENU_ITEM_HEADING, 0, "Citrus"),
        option(3, "Lemon", false, true),
        XenonMenuItem {
            enabled: false,
            ..option(4, "Lime", false, true)
        },
    ];
    let menu = XenonMenu {
        id: next_prompt(),
        x,
        y,
        minimum_width: 240.0,
        items: items.as_ptr(),
        item_count: items.len(),
    };
    unsafe { requested(callbacks.context, view, &menu) };
}

fn show_file_picker(view: XenonViewId) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let Some(requested) = callbacks.file_picker_requested else {
        return;
    };
    let filters = [
        XenonFileFilter {
            name: str("Image files"),
            extensions: str("png jpg"),
        },
        XenonFileFilter {
            name: str("All files"),
            extensions: str("*"),
        },
    ];
    let picker = XenonFilePicker {
        id: next_prompt(),
        allow_multiple: false,
        filters: filters.as_ptr(),
        filter_count: filters.len(),
    };
    unsafe { requested(callbacks.context, view, &picker) };
}

fn request_permission(view: XenonViewId) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let Some(requested) = callbacks.permission_requested else {
        return;
    };
    let request = XenonPermissionRequest {
        id: next_prompt(),
        kind: XENON_PERMISSION_PROMPT_QUESTION,
        permission: str("geolocation"),
        requester: str("https://mock.test"),
    };
    unsafe { requested(callbacks.context, view, &request) };
}

fn show_tooltip(view: XenonViewId, text: &str, x: f32, y: f32) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    if let Some(changed) = callbacks.tooltip_changed {
        unsafe { changed(callbacks.context, view, str(text), x, y) };
    }
}

pub fn navigated(view: XenonViewId, url: &str) {
    if url.contains("alert") {
        open_dialog(view, XENON_DIALOG_ALERT, "Hello from the mock page.", "");
    } else if url.contains("confirm") {
        open_dialog(view, XENON_DIALOG_CONFIRM, "Leave the mock page?", "");
    } else if url.contains("prompt") {
        open_dialog(view, XENON_DIALOG_PROMPT, "What is your name?", "Xenon");
    }
    if url.contains("permission") {
        request_permission(view);
    }
    if url.contains("tooltip") {
        show_tooltip(view, "A tooltip from the mock page", 240.0, 160.0);
    }
    if url.contains("fullscreen")
        && let Some(callbacks) = callbacks()
        && let Some(requested) = callbacks.fullscreen_requested
    {
        unsafe { requested(callbacks.context, view, true) };
    }
}

pub fn mouse(view: XenonViewId, url: &str, event: &XenonMouseEvent) {
    if event.r#type != XENON_MOUSE_EVENT_UP {
        return;
    }
    if event.button == XENON_MOUSE_BUTTON_SECONDARY {
        show_context_menu(view, event.x, event.y);
    } else if event.button == XENON_MOUSE_BUTTON_PRIMARY {
        if url.contains("select") {
            show_select(view, event.x, event.y);
        } else if url.contains("file") {
            show_file_picker(view);
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_page_callbacks(callbacks: *const XenonPageCallbacks) {
    let callbacks =
        unsafe { callbacks.as_ref() }.map(|callbacks| SendablePageCallbacks(*callbacks));
    *CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner()) = callbacks;
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_close_dialog(
    _view: XenonViewId,
    dialog: XenonPromptId,
    accepted: bool,
    text: XenonStr,
) {
    let text = unsafe { text.to_string_lossy() };
    record(format!("dialog {dialog} {accepted} {text}"));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_activate_menu_item(
    _view: XenonViewId,
    menu: XenonPromptId,
    item: u32,
) {
    record(format!("menu {menu} {item}"));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_close_select_dropdown(
    _view: XenonViewId,
    dropdown: XenonPromptId,
    has_selection: bool,
    item: u32,
) {
    record(if has_selection {
        format!("select {dropdown} {item}")
    } else {
        format!("select {dropdown} none")
    });
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_close_file_picker(
    _view: XenonViewId,
    picker: XenonPromptId,
    paths: *const XenonStr,
    path_count: usize,
) {
    let paths: Vec<String> = if paths.is_null() {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(paths, path_count) }
            .iter()
            .map(|path| unsafe { path.to_string_lossy() })
            .collect()
    };
    record(format!("files {picker} {}", paths.join(",")));
}

fn report_find(view: XenonViewId, current: usize, total: usize) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    if let Some(result) = callbacks.find_result {
        unsafe { result(callbacks.context, view, current, true, total) };
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_find_in_page(
    view: XenonViewId,
    query: XenonStr,
    _case_sensitive: bool,
) {
    let query = unsafe { query.to_string_lossy() }.to_lowercase();
    let total = if query.is_empty() {
        0
    } else {
        FIND_TEXT.to_lowercase().matches(&query).count()
    };
    state()
        .get_or_insert_with(MockPageState::default)
        .find
        .insert(view, (0, total));
    report_find(view, 0, total);
}

fn step_find(view: XenonViewId, forward: bool) {
    let found = {
        let mut state = state();
        let state = state.get_or_insert_with(MockPageState::default);
        state
            .find
            .get_mut(&view)
            .filter(|(_, total)| *total > 0)
            .map(|(current, total)| {
                *current = if forward {
                    (*current + 1) % *total
                } else {
                    (*current + *total - 1) % *total
                };
                (*current, *total)
            })
    };
    if let Some((current, total)) = found {
        report_find(view, current, total);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_find_next(view: XenonViewId) {
    step_find(view, true);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_find_previous(view: XenonViewId) {
    step_find(view, false);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_end_find(view: XenonViewId) {
    if let Some(state) = state().as_mut() {
        state.find.remove(&view);
    }
    record("find end".to_owned());
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_fullscreen(_view: XenonViewId, fullscreen: bool) {
    record(format!("fullscreen {fullscreen}"));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_answer_permission(
    _view: XenonViewId,
    request: XenonPromptId,
    outcome: u32,
) {
    record(format!("permission {request} {outcome}"));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_request_close(view: XenonViewId) {
    record("close".to_owned());
    crate::mock::xenon_engine_close_tab(view);
}
