/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::{XenonStr, XenonViewId};
use crate::ffi_page::XenonMenu;

pub const XENON_BOOKMARK_ITEM_BOOKMARK: u32 = 0;
pub const XENON_BOOKMARK_ITEM_FOLDER_BEGIN: u32 = 1;
pub const XENON_BOOKMARK_ITEM_FOLDER_END: u32 = 2;

pub const XENON_BOOKMARKS_BAR_ALWAYS: u32 = 0;
pub const XENON_BOOKMARKS_BAR_NEVER: u32 = 1;
pub const XENON_BOOKMARKS_BAR_ON_HOVER: u32 = 2;

pub const XENON_BOOKMARK_OPEN_IN_CURRENT_TAB: u32 = 0;
pub const XENON_BOOKMARK_OPEN_IN_NEW_TAB: u32 = 1;
pub const XENON_BOOKMARK_OPEN_IN_NEW_BACKGROUND_TAB: u32 = 2;

pub const XENON_BOOKMARK_DIALOG_ADD_BOOKMARK: u32 = 0;
pub const XENON_BOOKMARK_DIALOG_EDIT_BOOKMARK: u32 = 1;
pub const XENON_BOOKMARK_DIALOG_ADD_FOLDER: u32 = 2;
pub const XENON_BOOKMARK_DIALOG_EDIT_FOLDER: u32 = 3;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonBookmarkItem {
    pub kind: u32,
    pub id: XenonStr,
    pub title: XenonStr,
    pub url: XenonStr,
    pub favicon_png: *const u8,
    pub favicon_png_length: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonBookmarksBarSettings {
    pub visibility: u32,
    pub hover_expand_delay_ms: u32,
    pub hover_expand_animation_ms: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonBookmarkFolderOption {
    pub id: XenonStr,
    pub path: XenonStr,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonBookmarkDialog {
    pub id: u64,
    pub kind: u32,
    pub url: XenonStr,
    pub title: XenonStr,
    pub selected_folder_id: XenonStr,
    pub folders: *const XenonBookmarkFolderOption,
    pub folder_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonBookmarksCallbacks {
    pub context: *mut c_void,
    pub bookmarks_changed: Option<
        unsafe extern "C" fn(context: *mut c_void, items: *const XenonBookmarkItem, count: usize),
    >,
    pub bar_settings_changed: Option<
        unsafe extern "C" fn(context: *mut c_void, settings: *const XenonBookmarksBarSettings),
    >,
    pub context_menu_requested: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            menu: *const XenonMenu,
            at_page_position: bool,
        ),
    >,
    pub dialog_opened:
        Option<unsafe extern "C" fn(context: *mut c_void, dialog: *const XenonBookmarkDialog)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_bookmarks_callbacks(callbacks: *const XenonBookmarksCallbacks);
    pub fn xenon_engine_bookmarks_start();
    pub fn xenon_engine_open_bookmark(view: XenonViewId, id: XenonStr, disposition: u32);
    pub fn xenon_engine_open_bookmark_folder(id: XenonStr);
    pub fn xenon_engine_show_bookmark_context_menu(
        view: XenonViewId,
        id: XenonStr,
        target_folder_id: XenonStr,
        parent_folder_id: XenonStr,
        x: f32,
        y: f32,
    );
    pub fn xenon_engine_activate_bookmark_menu_item(menu: u64, item: u32);
    pub fn xenon_engine_close_bookmark_dialog(
        dialog: u64,
        accepted: bool,
        url: XenonStr,
        title: XenonStr,
        folder_id: XenonStr,
    );
}
