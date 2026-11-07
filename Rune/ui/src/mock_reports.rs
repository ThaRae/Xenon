/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Mutex;

use crate::ffi::{XenonStr, XenonViewId};

fn record(answer: String) {
    eprintln!("mock page: {answer}");
}

fn str(text: &str) -> XenonStr {
    XenonStr::borrowed(text)
}

struct SendableReportCallbacks(crate::ffi_reports::XenonReportCallbacks);

unsafe impl Send for SendableReportCallbacks {}

static REPORT_CALLBACKS: Mutex<Option<SendableReportCallbacks>> = Mutex::new(None);

fn report_callbacks() -> Option<crate::ffi_reports::XenonReportCallbacks> {
    REPORT_CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .as_ref()
        .map(|callbacks| callbacks.0)
}

fn open_crash_review(view: XenonViewId) {
    let Some(callbacks) = report_callbacks() else {
        return;
    };
    let Some(opened) = callbacks.crash_review_opened else {
        return;
    };
    let fields = [
        crate::ffi_reports::XenonReportField {
            label: str("Process"),
            value: str("WebContent"),
            is_code: false,
        },
        crate::ffi_reports::XenonReportField {
            label: str("Signal"),
            value: str("SIGSEGV"),
            is_code: true,
        },
    ];
    let review = crate::ffi_reports::XenonCrashReview {
        message: str("This page crashed. You can send us a crash report to help fix it."),
        exit_label: str("Reload page"),
        website: str("https://mock.test/crash-report"),
        fields: fields.as_ptr(),
        field_count: fields.len(),
    };
    unsafe { opened(callbacks.context, view, &review) };
}

pub fn navigated(view: XenonViewId, url: &str) {
    if url.contains("crash-report") {
        open_crash_review(view);
    }
    if url.contains("crash-offer")
        && let Some(callbacks) = report_callbacks()
        && let Some(offered) = callbacks.crash_report_offered
    {
        unsafe { offered(callbacks.context, str("mock-report")) };
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_report_callbacks(
    callbacks: *const crate::ffi_reports::XenonReportCallbacks,
) {
    let callbacks = unsafe { callbacks.as_ref() }.copied();
    *REPORT_CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner()) = callbacks.map(SendableReportCallbacks);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_review_crash_report(name: XenonStr) {
    record(format!("crash review {}", unsafe {
        name.to_string_lossy()
    }));
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_decline_crash_report_offer(name: XenonStr, replaced: bool) {
    record(format!("crash offer declined {} {replaced}", unsafe {
        name.to_string_lossy()
    }));
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_crash_review_send(
    view: XenonViewId,
    description: XenonStr,
    include_website: bool,
    website: XenonStr,
) {
    let description = unsafe { description.to_string_lossy() };
    let website = unsafe { website.to_string_lossy() };
    record(format!(
        "crash send [{description}] {include_website} {website}"
    ));
    if let Some(callbacks) = report_callbacks()
        && let Some(status) = callbacks.crash_review_status
    {
        let shown = crate::ffi_reports::XenonCrashReviewStatus {
            stage: crate::ffi_reports::XENON_CRASH_REVIEW_ANSWERED,
            title: str("Report sent"),
            message: str("Thank you for helping Ladybird."),
            progress: 100,
            has_next: false,
            answered_message: str("This page crashed. You can reload it to try again."),
        };
        unsafe { status(callbacks.context, view, &shown) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_crash_review_decline(_view: XenonViewId) {
    record("crash decline".to_owned());
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_crash_review_cancel(_view: XenonViewId) {
    record("crash cancel".to_owned());
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_crash_review_next(_view: XenonViewId) {
    record("crash next".to_owned());
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_crash_review_exit(view: XenonViewId) {
    record("crash exit".to_owned());
    if let Some(callbacks) = report_callbacks()
        && let Some(closed) = callbacks.crash_review_closed
    {
        unsafe { closed(callbacks.context, view) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_crash_review_show_text(view: XenonViewId) {
    if let Some(callbacks) = report_callbacks()
        && let Some(shown) = callbacks.crash_review_text
    {
        unsafe { shown(callbacks.context, view, str("{\"report\": \"mock\"}")) };
    }
}
