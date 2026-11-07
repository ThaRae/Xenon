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
pub struct XenonDatePicker {
    pub id: XenonPromptId,
    pub input_type: XenonStr,
    pub value: XenonStr,
    pub minimum: XenonStr,
    pub maximum: XenonStr,
}

pub const XENON_WEB_AUTHN_INSERT_KEY: u32 = 0;
pub const XENON_WEB_AUTHN_ENTER_PIN: u32 = 1;
pub const XENON_WEB_AUTHN_TOUCH_KEY: u32 = 2;
pub const XENON_WEB_AUTHN_CHOOSE_ACCOUNT: u32 = 3;
pub const XENON_WEB_AUTHN_ATTESTATION_CONSENT: u32 = 4;
pub const XENON_WEB_AUTHN_KEY_NOT_REGISTERED: u32 = 5;
pub const XENON_WEB_AUTHN_KEY_ALREADY_REGISTERED: u32 = 6;
pub const XENON_WEB_AUTHN_PIN_BLOCKED: u32 = 7;
pub const XENON_WEB_AUTHN_PIN_NOT_SET: u32 = 8;
pub const XENON_WEB_AUTHN_KEY_NOT_SUPPORTED: u32 = 9;
pub const XENON_WEB_AUTHN_NO_PLATFORM_AUTHENTICATOR: u32 = 10;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonWebAuthnPrompt {
    pub state: u32,
    pub registering: bool,
    pub relying_party: XenonStr,
    pub user_name: XenonStr,
    pub caller: XenonStr,
    pub pin_was_wrong: bool,
    pub pin_retries: i32,
    pub accounts: *const XenonStr,
    pub account_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonPromptCallbacks {
    pub context: *mut c_void,
    pub find_selection: Option<
        unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, request: u64, text: XenonStr),
    >,
    pub tooltip_override: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            text: XenonStr,
            x: f32,
            y: f32,
        ),
    >,
    pub color_picker_requested: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            picker: XenonPromptId,
            rgb: u32,
        ),
    >,
    pub date_picker_requested: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            picker: *const XenonDatePicker,
        ),
    >,
    pub web_authn_prompt: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            prompt: *const XenonWebAuthnPrompt,
        ),
    >,
    pub web_authn_prompt_closed:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_prompt_callbacks(callbacks: *const XenonPromptCallbacks);

    pub fn xenon_engine_request_find_selection(view: XenonViewId, request: u64);
    pub fn xenon_engine_color_picker_update(
        view: XenonViewId,
        picker: XenonPromptId,
        has_color: bool,
        rgb: u32,
        closed: bool,
    );
    pub fn xenon_engine_close_date_picker(
        view: XenonViewId,
        picker: XenonPromptId,
        has_value: bool,
        value: XenonStr,
    );
    pub fn xenon_engine_web_authn_provide_pin(view: XenonViewId, pin: XenonStr);
    pub fn xenon_engine_web_authn_choose_account(view: XenonViewId, index: usize);
    pub fn xenon_engine_web_authn_allow_attestation(view: XenonViewId, allow: bool);
    pub fn xenon_engine_web_authn_cancel(view: XenonViewId);
}
