/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use rune::AppHandle;

use crate::engine::ViewId;
use crate::ffi::XenonStr;
use crate::ffi_page::{self, XenonDialog, XenonFilePicker, XenonMenu, XenonPageCallbacks};
use crate::page_prompts::{
    self, DialogKind, MenuPurpose, PermissionAnswer, PromptId, RawMenuItem, RawMenuItemKind,
};
use crate::state::Browser;

#[derive(Clone, Copy, Debug, Default)]
pub struct PageEngine;

impl PageEngine {
    pub fn close_dialog(self, view: ViewId, dialog: PromptId, accepted: bool, text: &str) {
        unsafe {
            ffi_page::xenon_engine_close_dialog(view, dialog, accepted, XenonStr::borrowed(text))
        }
    }

    pub fn activate_menu_item(self, view: ViewId, menu: PromptId, item: u32) {
        unsafe { ffi_page::xenon_engine_activate_menu_item(view, menu, item) }
    }

    pub fn close_select_dropdown(self, view: ViewId, dropdown: PromptId, item: Option<u32>) {
        unsafe {
            ffi_page::xenon_engine_close_select_dropdown(
                view,
                dropdown,
                item.is_some(),
                item.unwrap_or_default(),
            )
        }
    }

    pub fn close_file_picker(self, view: ViewId, picker: PromptId, paths: &[String]) {
        let paths: Vec<XenonStr> = paths.iter().map(|path| XenonStr::borrowed(path)).collect();
        unsafe {
            ffi_page::xenon_engine_close_file_picker(view, picker, paths.as_ptr(), paths.len())
        }
    }

    pub fn find_in_page(self, view: ViewId, query: &str, case_sensitive: bool) {
        unsafe {
            ffi_page::xenon_engine_find_in_page(view, XenonStr::borrowed(query), case_sensitive)
        }
    }

    pub fn find_next(self, view: ViewId) {
        unsafe { ffi_page::xenon_engine_find_next(view) }
    }

    pub fn find_previous(self, view: ViewId) {
        unsafe { ffi_page::xenon_engine_find_previous(view) }
    }

    pub fn end_find(self, view: ViewId) {
        unsafe { ffi_page::xenon_engine_end_find(view) }
    }

    pub fn set_fullscreen(self, view: ViewId, fullscreen: bool) {
        unsafe { ffi_page::xenon_engine_set_fullscreen(view, fullscreen) }
    }

    pub fn answer_permission(self, view: ViewId, request: PromptId, answer: PermissionAnswer) {
        let outcome = match answer {
            PermissionAnswer::AllowAlways => ffi_page::XENON_PERMISSION_ALLOW_ALWAYS,
            PermissionAnswer::AllowOnce => ffi_page::XENON_PERMISSION_ALLOW_ONCE,
            PermissionAnswer::Block => ffi_page::XENON_PERMISSION_BLOCK,
            PermissionAnswer::Dismiss => ffi_page::XENON_PERMISSION_DISMISSED,
        };
        unsafe { ffi_page::xenon_engine_answer_permission(view, request, outcome) }
    }
}

pub struct PageContext {
    handle: AppHandle<Browser>,
}

pub fn install(handle: AppHandle<Browser>) -> Box<PageContext> {
    let context = Box::new(PageContext { handle });
    let callbacks = XenonPageCallbacks {
        context: std::ptr::from_ref::<PageContext>(&context)
            .cast_mut()
            .cast(),
        dialog_opened: Some(dialog_opened),
        dialog_text_changed: Some(dialog_text_changed),
        dialog_closed: Some(dialog_closed),
        context_menu_requested: Some(context_menu_requested),
        select_dropdown_requested: Some(select_dropdown_requested),
        file_picker_requested: Some(file_picker_requested),
        find_result: Some(find_result),
        tooltip_changed: Some(tooltip_changed),
        fullscreen_requested: Some(fullscreen_requested),
        permission_requested: Some(permission_requested),
        permission_cancelled: Some(permission_cancelled),
    };
    unsafe { ffi_page::xenon_engine_set_page_callbacks(&callbacks) };
    context
}

unsafe fn handle<'a>(context: *mut c_void) -> &'a AppHandle<Browser> {
    unsafe { &(*context.cast::<PageContext>()).handle }
}

fn text(string: XenonStr) -> String {
    unsafe { string.to_string_lossy() }
}

fn dialog_kind(kind: u32) -> DialogKind {
    match kind {
        ffi_page::XENON_DIALOG_CONFIRM => DialogKind::Confirm,
        ffi_page::XENON_DIALOG_PROMPT => DialogKind::Prompt,
        ffi_page::XENON_DIALOG_BEFORE_UNLOAD => DialogKind::BeforeUnload,
        ffi_page::XENON_DIALOG_EXTERNAL_URL => DialogKind::ExternalUrl,
        _ => DialogKind::Alert,
    }
}

unsafe extern "C" fn dialog_opened(context: *mut c_void, view: ViewId, dialog: *const XenonDialog) {
    let Some(dialog) = (unsafe { dialog.as_ref() }) else {
        return;
    };
    let id = dialog.id;
    let kind = dialog_kind(dialog.kind);
    let title = text(dialog.title);
    let message = text(dialog.message);
    let default_text = text(dialog.default_text);
    let accept_label = text(dialog.accept_label);
    let cancel_label = Some(text(dialog.cancel_label)).filter(|label| !label.is_empty());
    unsafe { handle(context) }.update(move |browser, _| {
        page_prompts::dialog_opened(
            browser,
            view,
            id,
            kind,
            title,
            message,
            default_text,
            accept_label,
            cancel_label,
        )
    });
}

unsafe extern "C" fn dialog_text_changed(
    context: *mut c_void,
    view: ViewId,
    dialog: PromptId,
    new_text: XenonStr,
) {
    let new_text = text(new_text);
    unsafe { handle(context) }.update(move |browser, _| {
        page_prompts::dialog_text_changed(browser, view, dialog, new_text)
    });
}

unsafe extern "C" fn dialog_closed(context: *mut c_void, view: ViewId, dialog: PromptId) {
    unsafe { handle(context) }
        .update(move |browser, _| page_prompts::dialog_closed(browser, view, dialog));
}

pub fn raw_menu_items(menu: &XenonMenu) -> Vec<RawMenuItem> {
    if menu.items.is_null() || menu.item_count == 0 {
        return Vec::new();
    }
    let items = unsafe { std::slice::from_raw_parts(menu.items, menu.item_count) };
    items
        .iter()
        .map(|item| RawMenuItem {
            kind: match item.kind {
                ffi_page::XENON_MENU_ITEM_SEPARATOR => RawMenuItemKind::Separator,
                ffi_page::XENON_MENU_ITEM_HEADING => RawMenuItemKind::Heading,
                ffi_page::XENON_MENU_ITEM_SUBMENU_BEGIN => RawMenuItemKind::SubmenuBegin,
                ffi_page::XENON_MENU_ITEM_SUBMENU_END => RawMenuItemKind::SubmenuEnd,
                _ => RawMenuItemKind::Action,
            },
            id: item.id,
            title: text(item.title),
            enabled: item.enabled,
            checkable: item.checkable,
            checked: item.checked,
            indented: item.indented,
        })
        .collect()
}

fn show_menu(context: *mut c_void, view: ViewId, menu: *const XenonMenu, purpose: MenuPurpose) {
    let Some(menu) = (unsafe { menu.as_ref() }) else {
        return;
    };
    let id = menu.id;
    let geometry = (menu.x, menu.y, menu.minimum_width);
    let entries = page_prompts::menu_entries(&raw_menu_items(menu));
    unsafe { handle(context) }.update(move |browser, tx| {
        page_prompts::show_menu(browser, view, id, purpose, geometry, entries, tx)
    });
}

unsafe extern "C" fn context_menu_requested(
    context: *mut c_void,
    view: ViewId,
    menu: *const XenonMenu,
) {
    show_menu(context, view, menu, MenuPurpose::Context);
}

unsafe extern "C" fn select_dropdown_requested(
    context: *mut c_void,
    view: ViewId,
    menu: *const XenonMenu,
) {
    show_menu(context, view, menu, MenuPurpose::Select);
}

unsafe extern "C" fn file_picker_requested(
    context: *mut c_void,
    view: ViewId,
    picker: *const XenonFilePicker,
) {
    let Some(picker) = (unsafe { picker.as_ref() }) else {
        return;
    };
    let filters = if picker.filters.is_null() || picker.filter_count == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(picker.filters, picker.filter_count) }
            .iter()
            .map(|filter| {
                (
                    text(filter.name),
                    text(filter.extensions)
                        .split_whitespace()
                        .map(str::to_owned)
                        .collect(),
                )
            })
            .collect()
    };
    let (id, allow_multiple) = (picker.id, picker.allow_multiple);
    unsafe { handle(context) }.update(move |browser, tx| {
        page_prompts::file_picker_requested(browser, view, id, allow_multiple, filters, tx)
    });
}

unsafe extern "C" fn find_result(
    context: *mut c_void,
    view: ViewId,
    current_match: usize,
    has_total: bool,
    total_matches: usize,
) {
    let total = has_total.then_some(total_matches);
    unsafe { handle(context) }
        .update(move |browser, _| page_prompts::find_result(browser, view, current_match, total));
}

unsafe extern "C" fn tooltip_changed(
    context: *mut c_void,
    view: ViewId,
    tooltip: XenonStr,
    x: f32,
    y: f32,
) {
    let tooltip = text(tooltip);
    unsafe { handle(context) }
        .update(move |browser, _| page_prompts::tooltip_changed(browser, view, tooltip, x, y));
}

unsafe extern "C" fn fullscreen_requested(context: *mut c_void, view: ViewId, fullscreen: bool) {
    unsafe { handle(context) }
        .update(move |browser, _| page_prompts::fullscreen_requested(browser, view, fullscreen));
}

unsafe extern "C" fn permission_requested(
    context: *mut c_void,
    view: ViewId,
    request: *const ffi_page::XenonPermissionRequest,
) {
    let Some(request) = (unsafe { request.as_ref() }) else {
        return;
    };
    let id = request.id;
    let notice = request.kind == ffi_page::XENON_PERMISSION_PROMPT_SYSTEM_DENIED_NOTICE;
    let permission = text(request.permission);
    let requester = text(request.requester);
    unsafe { handle(context) }.update(move |browser, _| {
        page_prompts::permission_requested(browser, view, id, notice, permission, requester)
    });
}

unsafe extern "C" fn permission_cancelled(context: *mut c_void, view: ViewId, request: PromptId) {
    unsafe { handle(context) }
        .update(move |browser, _| page_prompts::permission_cancelled(browser, view, request));
}
