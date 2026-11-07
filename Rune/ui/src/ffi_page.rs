/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::{XenonStr, XenonViewId};

pub type XenonPromptId = u64;

pub const XENON_DIALOG_ALERT: u32 = 0;
pub const XENON_DIALOG_CONFIRM: u32 = 1;
pub const XENON_DIALOG_PROMPT: u32 = 2;
pub const XENON_DIALOG_BEFORE_UNLOAD: u32 = 3;
pub const XENON_DIALOG_EXTERNAL_URL: u32 = 4;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonDialog {
    pub id: XenonPromptId,
    pub kind: u32,
    pub title: XenonStr,
    pub message: XenonStr,
    pub default_text: XenonStr,
    pub accept_label: XenonStr,
    pub cancel_label: XenonStr,
}

pub const XENON_MENU_ITEM_ACTION: u32 = 0;
pub const XENON_MENU_ITEM_SEPARATOR: u32 = 1;
pub const XENON_MENU_ITEM_HEADING: u32 = 2;
pub const XENON_MENU_ITEM_SUBMENU_BEGIN: u32 = 3;
pub const XENON_MENU_ITEM_SUBMENU_END: u32 = 4;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonMenuItem {
    pub kind: u32,
    pub id: u32,
    pub title: XenonStr,
    pub enabled: bool,
    pub checkable: bool,
    pub checked: bool,
    pub indented: bool,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonMenu {
    pub id: XenonPromptId,
    pub x: f32,
    pub y: f32,
    pub minimum_width: f32,
    pub items: *const XenonMenuItem,
    pub item_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonFileFilter {
    pub name: XenonStr,
    pub extensions: XenonStr,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonFilePicker {
    pub id: XenonPromptId,
    pub allow_multiple: bool,
    pub filters: *const XenonFileFilter,
    pub filter_count: usize,
}

pub const XENON_PERMISSION_PROMPT_QUESTION: u32 = 0;
pub const XENON_PERMISSION_PROMPT_SYSTEM_DENIED_NOTICE: u32 = 1;

pub const XENON_PERMISSION_ALLOW_ALWAYS: u32 = 0;
pub const XENON_PERMISSION_ALLOW_ONCE: u32 = 1;
pub const XENON_PERMISSION_BLOCK: u32 = 2;
pub const XENON_PERMISSION_DISMISSED: u32 = 3;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonPermissionRequest {
    pub id: XenonPromptId,
    pub kind: u32,
    pub permission: XenonStr,
    pub requester: XenonStr,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonPageCallbacks {
    pub context: *mut c_void,
    pub dialog_opened: Option<
        unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, dialog: *const XenonDialog),
    >,
    pub dialog_text_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            dialog: XenonPromptId,
            text: XenonStr,
        ),
    >,
    pub dialog_closed: Option<
        unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, dialog: XenonPromptId),
    >,
    pub context_menu_requested: Option<
        unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, menu: *const XenonMenu),
    >,
    pub select_dropdown_requested: Option<
        unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, menu: *const XenonMenu),
    >,
    pub file_picker_requested: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            picker: *const XenonFilePicker,
        ),
    >,
    pub find_result: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            current_match: usize,
            has_total: bool,
            total_matches: usize,
        ),
    >,
    pub tooltip_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            text: XenonStr,
            x: f32,
            y: f32,
        ),
    >,
    pub fullscreen_requested:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, fullscreen: bool)>,
    pub permission_requested: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            request: *const XenonPermissionRequest,
        ),
    >,
    pub permission_cancelled: Option<
        unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, request: XenonPromptId),
    >,
}

unsafe extern "C" {
    pub fn xenon_engine_set_page_callbacks(callbacks: *const XenonPageCallbacks);

    pub fn xenon_engine_close_dialog(
        view: XenonViewId,
        dialog: XenonPromptId,
        accepted: bool,
        text: XenonStr,
    );
    pub fn xenon_engine_activate_menu_item(view: XenonViewId, menu: XenonPromptId, item: u32);
    pub fn xenon_engine_close_select_dropdown(
        view: XenonViewId,
        dropdown: XenonPromptId,
        has_selection: bool,
        item: u32,
    );
    pub fn xenon_engine_close_file_picker(
        view: XenonViewId,
        picker: XenonPromptId,
        paths: *const XenonStr,
        path_count: usize,
    );
    pub fn xenon_engine_find_in_page(
        view: XenonViewId,
        query: XenonStr,
        case_sensitive: bool,
        highlight_all: bool,
    );
    pub fn xenon_engine_find_next(view: XenonViewId);
    pub fn xenon_engine_find_previous(view: XenonViewId);
    pub fn xenon_engine_end_find(view: XenonViewId);
    pub fn xenon_engine_set_fullscreen(view: XenonViewId, fullscreen: bool);
    pub fn xenon_engine_answer_permission(view: XenonViewId, request: XenonPromptId, outcome: u32);
    pub fn xenon_engine_request_close(view: XenonViewId);
}
