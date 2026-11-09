/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Mutex;
use std::sync::atomic::{AtomicU64, Ordering};

use crate::ffi::{XenonStr, XenonViewId};
use crate::ffi_page::XenonPromptId;
use crate::ffi_prompts::{
    XENON_WEB_AUTHN_CHOOSE_ACCOUNT, XENON_WEB_AUTHN_ENTER_PIN, XenonDatePicker,
    XenonPromptCallbacks, XenonWebAuthnPrompt,
};

const SELECTED_TEXT: &str = "brown fox";

struct SendablePromptCallbacks(XenonPromptCallbacks);

unsafe impl Send for SendablePromptCallbacks {}

static PROMPT_CALLBACKS: Mutex<Option<SendablePromptCallbacks>> = Mutex::new(None);
static NEXT_PICKER: AtomicU64 = AtomicU64::new(1 << 32);
static URLS: Mutex<Vec<(XenonViewId, String)>> = Mutex::new(Vec::new());

fn record(answer: String) {
    eprintln!("mock page: {answer}");
}

fn str(text: &str) -> XenonStr {
    XenonStr::borrowed(text)
}

fn prompt_callbacks() -> Option<XenonPromptCallbacks> {
    PROMPT_CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .as_ref()
        .map(|callbacks| callbacks.0)
}

fn url_of(view: XenonViewId) -> String {
    URLS.lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .iter()
        .find(|(known, _)| *known == view)
        .map(|(_, url)| url.clone())
        .unwrap_or_default()
}

fn date_input_type(url: &str) -> Option<&'static str> {
    ["datetime-local", "month", "week", "time", "date"]
        .into_iter()
        .find(|input_type| url.contains(&format!("{input_type}-picker")))
}

pub fn navigated(view: XenonViewId, url: &str) {
    {
        let mut urls = URLS.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        urls.retain(|(known, _)| *known != view);
        urls.push((view, url.to_owned()));
    }
    let Some(callbacks) = prompt_callbacks() else {
        return;
    };
    if url.contains("color-picker")
        && let Some(requested) = callbacks.color_picker_requested
    {
        let id = NEXT_PICKER.fetch_add(1, Ordering::Relaxed);
        unsafe { requested(callbacks.context, view, id, 0xff0050) };
    }
    if let Some(input_type) = date_input_type(url)
        && let Some(requested) = callbacks.date_picker_requested
    {
        let value = match input_type {
            "datetime-local" => "2026-11-06T09:30",
            "month" => "2026-11",
            "week" => "2026-W45",
            "time" => "09:30",
            _ => "2026-11-06",
        };
        let picker = XenonDatePicker {
            id: NEXT_PICKER.fetch_add(1, Ordering::Relaxed),
            input_type: str(input_type),
            value: str(value),
            minimum: str(""),
            maximum: str(""),
        };
        unsafe { requested(callbacks.context, view, &picker) };
    }
    if url.contains("webauthn")
        && let Some(prompt) = callbacks.web_authn_prompt
    {
        let accounts = [str("Ada Lovelace (ada)"), str("grace")];
        let choose = url.contains("account");
        let shown = XenonWebAuthnPrompt {
            state: if choose {
                XENON_WEB_AUTHN_CHOOSE_ACCOUNT
            } else {
                XENON_WEB_AUTHN_ENTER_PIN
            },
            registering: false,
            relying_party: str("mock.test"),
            user_name: str(""),
            caller: str(""),
            pin_was_wrong: false,
            pin_retries: 8,
            accounts: accounts.as_ptr(),
            account_count: if choose { accounts.len() } else { 0 },
        };
        unsafe { prompt(callbacks.context, view, &shown) };
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_prompt_callbacks(callbacks: *const XenonPromptCallbacks) {
    let callbacks =
        unsafe { callbacks.as_ref() }.map(|callbacks| SendablePromptCallbacks(*callbacks));
    *PROMPT_CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner()) = callbacks;
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_request_find_selection(view: XenonViewId, request: u64) {
    let selection = if url_of(view).contains("selection") {
        SELECTED_TEXT
    } else {
        ""
    };
    if let Some(callbacks) = prompt_callbacks()
        && let Some(found) = callbacks.find_selection
    {
        unsafe { found(callbacks.context, view, request, str(selection)) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_color_picker_update(
    _view: XenonViewId,
    picker: XenonPromptId,
    has_color: bool,
    rgb: u32,
    closed: bool,
) {
    let color = if has_color {
        format!("#{rgb:06x}")
    } else {
        "none".to_owned()
    };
    let state = if closed { "closed" } else { "update" };
    record(format!("color {picker} {state} {color}"));
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_close_date_picker(
    _view: XenonViewId,
    picker: XenonPromptId,
    has_value: bool,
    value: XenonStr,
) {
    let value = unsafe { value.to_string_lossy() };
    record(if has_value {
        format!("date {picker} {value}")
    } else {
        format!("date {picker} none")
    });
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_web_authn_provide_pin(_view: XenonViewId, pin: XenonStr) {
    let pin = unsafe { pin.to_string_lossy() };
    record(format!("webauthn pin {} digits", pin.len()));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_web_authn_choose_account(_view: XenonViewId, index: usize) {
    record(format!("webauthn account {index}"));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_web_authn_allow_attestation(_view: XenonViewId, allow: bool) {
    record(format!("webauthn attestation {allow}"));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_web_authn_cancel(view: XenonViewId) {
    record("webauthn cancel".to_owned());
    if let Some(callbacks) = prompt_callbacks()
        && let Some(closed) = callbacks.web_authn_prompt_closed
    {
        unsafe { closed(callbacks.context, view) };
    }
}
