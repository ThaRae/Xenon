/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Mutex;

use crate::ffi::{XenonStr, XenonViewId};
use crate::ffi_extensions::{
    XenonExtensionAction, XenonExtensionCallbacks, XenonExtensionCommand, XenonExtensionInfo,
    XenonPasswordManagerChoice, XenonPasswordManagers,
};
use crate::ffi_page::XenonPromptId;

const SHOWS_EXTENSIONS: &str = "XENON_MOCK_EXTENSIONS";
const EXTENSION_ID: &str = "mockextensionaaaaaaaaaaaaaaaaaaa";
const POPUP_VIEW: XenonViewId = 1_000_000;

struct SendableCallbacks(XenonExtensionCallbacks);

unsafe impl Send for SendableCallbacks {}

static CALLBACKS: Mutex<Option<SendableCallbacks>> = Mutex::new(None);

fn str(text: &str) -> XenonStr {
    XenonStr::borrowed(text)
}

fn callbacks() -> Option<XenonExtensionCallbacks> {
    CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .as_ref()
        .map(|callbacks| callbacks.0)
}

fn report_mock_extension(callbacks: &XenonExtensionCallbacks) {
    let action = XenonExtensionAction {
        extension_id: str(EXTENSION_ID),
        name: str("Mock Extension"),
        title: str("Mock Extension"),
        icon_path: str(""),
        badge_text: str("12"),
        badge_background: 0xff00_50ff,
        has_badge_text_color: false,
        badge_text_color: 0,
        has_popup: true,
        enabled: true,
        has_options_page: true,
    };
    if let Some(actions_changed) = callbacks.actions_changed {
        unsafe { actions_changed(callbacks.context, 1, &action, 1) };
    }
    let info = XenonExtensionInfo {
        id: str(EXTENSION_ID),
        name: str("Mock Extension"),
        version: str("1.2"),
        description: str("An extension the mock engine makes up."),
        details: str("ID: mockextensionaaaaaaaaaaaaaaaaaaa\n/tmp/mock-extension"),
        warnings: str("Unknown permission: \"magic\""),
        icon_path: str(""),
        has_options_page: true,
    };
    if let Some(extensions_changed) = callbacks.extensions_changed {
        unsafe { extensions_changed(callbacks.context, &info, 1) };
    }
    let choices = [XenonPasswordManagerChoice {
        id: str("bitwarden"),
        name: str("Bitwarden"),
    }];
    let managers = XenonPasswordManagers {
        choices: choices.as_ptr(),
        choice_count: choices.len(),
        current: str("bitwarden"),
        installing: str(""),
        status: str("From the Chrome Web Store, and kept up to date"),
    };
    if let Some(password_managers_changed) = callbacks.password_managers_changed {
        unsafe { password_managers_changed(callbacks.context, &managers) };
    }
    let command = XenonExtensionCommand {
        extension_id: str(EXTENSION_ID),
        command: str("_execute_action"),
        shortcut: str("Ctrl+Shift+Y"),
    };
    if let Some(commands_changed) = callbacks.commands_changed {
        unsafe { commands_changed(callbacks.context, &command, 1) };
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_extension_callbacks(
    callbacks: *const XenonExtensionCallbacks,
) {
    let Some(callbacks) = (unsafe { callbacks.as_ref() }) else {
        return;
    };
    *CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner()) = Some(SendableCallbacks(*callbacks));
    if std::env::var_os(SHOWS_EXTENSIONS).is_some() {
        report_mock_extension(callbacks);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_extension_action_clicked(tab: XenonViewId, _extension_id: XenonStr) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    if let Some(popup_opened) = callbacks.popup_opened {
        unsafe {
            popup_opened(
                callbacks.context,
                tab,
                str(EXTENSION_ID),
                POPUP_VIEW,
                320,
                200,
            )
        };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_close_extension_popup(popup: XenonViewId) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    if let Some(popup_closed) = callbacks.popup_closed {
        unsafe { popup_closed(callbacks.context, popup) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_answer_extension_permissions(
    _prompt: XenonPromptId,
    _granted: bool,
) {
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_run_extension_command(
    tab: XenonViewId,
    extension_id: XenonStr,
    _command: XenonStr,
) {
    xenon_engine_extension_action_clicked(tab, extension_id);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_open_extension_options(_extension_id: XenonStr) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_reload_extension(_extension_id: XenonStr) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_remove_extension(_extension_id: XenonStr) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_load_unpacked_extension(_directory: XenonStr) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_choose_password_manager(_id: XenonStr) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_show_extensions() {
    let Some(callbacks) = callbacks() else {
        return;
    };
    if let Some(show_extensions) = callbacks.show_extensions {
        unsafe { show_extensions(callbacks.context) };
    }
}
