/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::{XenonStr, XenonViewId};
use crate::ffi_page::XenonPromptId;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonExtensionAction {
    pub extension_id: XenonStr,
    pub name: XenonStr,
    pub title: XenonStr,
    pub icon_path: XenonStr,
    pub badge_text: XenonStr,
    pub badge_background: u32,
    pub has_badge_text_color: bool,
    pub badge_text_color: u32,
    pub has_popup: bool,
    pub enabled: bool,
    pub has_options_page: bool,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonExtensionInfo {
    pub id: XenonStr,
    pub name: XenonStr,
    pub version: XenonStr,
    pub description: XenonStr,
    pub details: XenonStr,
    pub warnings: XenonStr,
    pub icon_path: XenonStr,
    pub has_options_page: bool,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonPasswordManagerChoice {
    pub id: XenonStr,
    pub name: XenonStr,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonPasswordManagers {
    pub choices: *const XenonPasswordManagerChoice,
    pub choice_count: usize,
    pub current: XenonStr,
    pub installing: XenonStr,
    pub status: XenonStr,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonExtensionCommand {
    pub extension_id: XenonStr,
    pub command: XenonStr,
    pub shortcut: XenonStr,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonExtensionPermissionPrompt {
    pub id: XenonPromptId,
    pub extension_name: XenonStr,
    pub warnings: *const XenonStr,
    pub warning_count: usize,
    pub tab: XenonViewId,
    pub popup: XenonViewId,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonExtensionCallbacks {
    pub context: *mut c_void,
    pub actions_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            tab: XenonViewId,
            actions: *const XenonExtensionAction,
            count: usize,
        ),
    >,
    pub popup_opened: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            tab: XenonViewId,
            extension_id: XenonStr,
            popup: XenonViewId,
            width: u32,
            height: u32,
        ),
    >,
    pub popup_resized: Option<
        unsafe extern "C" fn(context: *mut c_void, popup: XenonViewId, width: u32, height: u32),
    >,
    pub popup_closed: Option<unsafe extern "C" fn(context: *mut c_void, popup: XenonViewId)>,
    pub permission_prompt: Option<
        unsafe extern "C" fn(context: *mut c_void, prompt: *const XenonExtensionPermissionPrompt),
    >,
    pub extensions_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            extensions: *const XenonExtensionInfo,
            count: usize,
        ),
    >,
    pub password_managers_changed: Option<
        unsafe extern "C" fn(context: *mut c_void, password_managers: *const XenonPasswordManagers),
    >,
    pub commands_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            commands: *const XenonExtensionCommand,
            count: usize,
        ),
    >,
    pub show_extensions: Option<unsafe extern "C" fn(context: *mut c_void)>,
    pub error:
        Option<unsafe extern "C" fn(context: *mut c_void, title: XenonStr, message: XenonStr)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_extension_callbacks(callbacks: *const XenonExtensionCallbacks);

    pub fn xenon_engine_extension_action_clicked(tab: XenonViewId, extension_id: XenonStr);
    pub fn xenon_engine_close_extension_popup(popup: XenonViewId);
    pub fn xenon_engine_answer_extension_permissions(prompt: XenonPromptId, granted: bool);
    pub fn xenon_engine_run_extension_command(
        tab: XenonViewId,
        extension_id: XenonStr,
        command: XenonStr,
    );
    pub fn xenon_engine_open_extension_options(extension_id: XenonStr);
    pub fn xenon_engine_reload_extension(extension_id: XenonStr);
    pub fn xenon_engine_remove_extension(extension_id: XenonStr);
    pub fn xenon_engine_load_unpacked_extension(directory: XenonStr);
    pub fn xenon_engine_choose_password_manager(id: XenonStr);
    pub fn xenon_engine_show_extensions();
}
