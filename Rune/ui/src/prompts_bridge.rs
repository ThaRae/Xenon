/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use std::sync::Arc;

use rune::TextEdit;

use crate::engine::ViewId;
use crate::ffi::XenonStr;
use crate::ffi_prompts::{self, XenonDatePicker, XenonPromptCallbacks, XenonWebAuthnPrompt};
use crate::page_prompts::{self, PromptId};
use crate::pickers::{self, WebAuthn, WebAuthnState};
use crate::window_slots::{Slots, ViewRoute};

#[derive(Clone, Copy, Debug, Default)]
pub struct PromptEngine;

impl PromptEngine {
    pub fn request_find_selection(self, view: ViewId, request: u64) {
        unsafe { ffi_prompts::xenon_engine_request_find_selection(view, request) }
    }

    pub fn color_picker_update(
        self,
        view: ViewId,
        picker: PromptId,
        rgb: Option<u32>,
        closed: bool,
    ) {
        unsafe {
            ffi_prompts::xenon_engine_color_picker_update(
                view,
                picker,
                rgb.is_some(),
                rgb.unwrap_or_default(),
                closed,
            )
        }
    }

    pub fn close_date_picker(self, view: ViewId, picker: PromptId, value: Option<&str>) {
        unsafe {
            ffi_prompts::xenon_engine_close_date_picker(
                view,
                picker,
                value.is_some(),
                XenonStr::borrowed(value.unwrap_or_default()),
            )
        }
    }

    pub fn web_authn_provide_pin(self, view: ViewId, pin: &str) {
        unsafe { ffi_prompts::xenon_engine_web_authn_provide_pin(view, XenonStr::borrowed(pin)) }
    }

    pub fn web_authn_choose_account(self, view: ViewId, index: usize) {
        unsafe { ffi_prompts::xenon_engine_web_authn_choose_account(view, index) }
    }

    pub fn web_authn_allow_attestation(self, view: ViewId, allow: bool) {
        unsafe { ffi_prompts::xenon_engine_web_authn_allow_attestation(view, allow) }
    }

    pub fn web_authn_cancel(self, view: ViewId) {
        unsafe { ffi_prompts::xenon_engine_web_authn_cancel(view) }
    }
}

pub struct PromptContext {
    slots: Arc<Slots>,
}

pub fn install(slots: Arc<Slots>) -> Box<PromptContext> {
    let context = Box::new(PromptContext { slots });
    let callbacks = XenonPromptCallbacks {
        context: std::ptr::from_ref::<PromptContext>(&context)
            .cast_mut()
            .cast(),
        find_selection: Some(find_selection),
        tooltip_override: Some(tooltip_override),
        color_picker_requested: Some(color_picker_requested),
        date_picker_requested: Some(date_picker_requested),
        web_authn_prompt: Some(web_authn_prompt),
        web_authn_prompt_closed: Some(web_authn_prompt_closed),
    };
    unsafe { ffi_prompts::xenon_engine_set_prompt_callbacks(&callbacks) };
    context
}

unsafe fn handle<'a>(context: *mut c_void) -> ViewRoute<'a> {
    ViewRoute(unsafe { &(*context.cast::<PromptContext>()).slots })
}

fn text(string: XenonStr) -> String {
    unsafe { string.to_string_lossy() }
}

unsafe extern "C" fn find_selection(
    context: *mut c_void,
    view: ViewId,
    request: u64,
    selection: XenonStr,
) {
    let selection = text(selection);
    unsafe { handle(context) }.update(view, move |browser, _| {
        page_prompts::find_selection(browser, view, request, selection)
    });
}

unsafe extern "C" fn tooltip_override(
    context: *mut c_void,
    view: ViewId,
    tooltip: XenonStr,
    x: f32,
    y: f32,
) {
    let tooltip = text(tooltip);
    unsafe { handle(context) }.update(view, move |browser, _| {
        page_prompts::tooltip_override(browser, view, tooltip, x, y)
    });
}

unsafe extern "C" fn color_picker_requested(
    context: *mut c_void,
    view: ViewId,
    picker: PromptId,
    rgb: u32,
) {
    unsafe { handle(context) }.update(view, move |browser, _| {
        pickers::color_picker_requested(browser, view, picker, rgb)
    });
}

unsafe extern "C" fn date_picker_requested(
    context: *mut c_void,
    view: ViewId,
    picker: *const XenonDatePicker,
) {
    let Some(picker) = (unsafe { picker.as_ref() }) else {
        return;
    };
    let id = picker.id;
    let input_type = text(picker.input_type);
    let value = text(picker.value);
    let minimum = text(picker.minimum);
    let maximum = text(picker.maximum);
    unsafe { handle(context) }.update(view, move |browser, _| {
        pickers::date_picker_requested(browser, view, id, input_type, value, minimum, maximum)
    });
}

fn web_authn_state(state: u32) -> WebAuthnState {
    match state {
        ffi_prompts::XENON_WEB_AUTHN_ENTER_PIN => WebAuthnState::EnterPin,
        ffi_prompts::XENON_WEB_AUTHN_TOUCH_KEY => WebAuthnState::TouchKey,
        ffi_prompts::XENON_WEB_AUTHN_CHOOSE_ACCOUNT => WebAuthnState::ChooseAccount,
        ffi_prompts::XENON_WEB_AUTHN_ATTESTATION_CONSENT => WebAuthnState::AttestationConsent,
        ffi_prompts::XENON_WEB_AUTHN_KEY_NOT_REGISTERED => WebAuthnState::KeyNotRegistered,
        ffi_prompts::XENON_WEB_AUTHN_KEY_ALREADY_REGISTERED => WebAuthnState::KeyAlreadyRegistered,
        ffi_prompts::XENON_WEB_AUTHN_PIN_BLOCKED => WebAuthnState::PinBlocked,
        ffi_prompts::XENON_WEB_AUTHN_PIN_NOT_SET => WebAuthnState::PinNotSet,
        ffi_prompts::XENON_WEB_AUTHN_KEY_NOT_SUPPORTED => WebAuthnState::KeyNotSupported,
        ffi_prompts::XENON_WEB_AUTHN_NO_PLATFORM_AUTHENTICATOR => {
            WebAuthnState::NoPlatformAuthenticator
        }
        _ => WebAuthnState::InsertKey,
    }
}

pub fn web_authn_from(prompt: &XenonWebAuthnPrompt) -> WebAuthn {
    let accounts = if prompt.accounts.is_null() || prompt.account_count == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(prompt.accounts, prompt.account_count) }
            .iter()
            .map(|account| text(*account))
            .collect()
    };
    WebAuthn {
        state: web_authn_state(prompt.state),
        registering: prompt.registering,
        relying_party: text(prompt.relying_party),
        user_name: text(prompt.user_name),
        caller: text(prompt.caller),
        pin_was_wrong: prompt.pin_was_wrong,
        pin_retries: u32::try_from(prompt.pin_retries).ok(),
        accounts,
        chosen_account: 0,
        pin: TextEdit::new("").secure(),
    }
}

unsafe extern "C" fn web_authn_prompt(
    context: *mut c_void,
    view: ViewId,
    prompt: *const XenonWebAuthnPrompt,
) {
    let Some(prompt) = (unsafe { prompt.as_ref() }) else {
        return;
    };
    let prompt = web_authn_from(prompt);
    unsafe { handle(context) }.update(view, move |browser, _| {
        pickers::web_authn_prompt(browser, view, prompt)
    });
}

unsafe extern "C" fn web_authn_prompt_closed(context: *mut c_void, view: ViewId) {
    unsafe { handle(context) }.update(view, move |browser, _| {
        pickers::web_authn_prompt_closed(browser, view)
    });
}
