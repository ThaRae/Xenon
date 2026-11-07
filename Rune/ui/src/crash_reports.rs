/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use std::sync::Arc;

use rune::{TextEdit, Tx};

use crate::engine::ViewId;
use crate::ffi::XenonStr;
use crate::ffi_reports::{self, XenonCrashReview, XenonCrashReviewStatus, XenonReportCallbacks};
use crate::state::Browser;
use crate::window_slots::Slots;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Field {
    pub label: String,
    pub value: String,
    pub is_code: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stage {
    Sending,
    Retrying,
    FailedRetryable,
    Answered,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Status {
    pub stage: Stage,
    pub title: String,
    pub message: String,
    pub progress: Option<f32>,
    pub has_next: bool,
}

pub struct CrashReview {
    pub message: String,
    pub exit_label: String,
    pub website: Option<String>,
    pub fields: Vec<Field>,
    pub description: TextEdit,
    pub include_website: bool,
    pub website_edit: TextEdit,
    pub details_open: bool,
    pub report_text: Option<String>,
    pub validation: Option<String>,
    pub status: Option<Status>,
}

fn text(string: XenonStr) -> String {
    unsafe { string.to_string_lossy() }
}

fn review_of(s: &mut Browser, view: ViewId) -> Option<&mut CrashReview> {
    s.page_prompts.of_mut(view).crash_review.as_mut()
}

pub fn review_opened(
    s: &mut Browser,
    view: ViewId,
    message: String,
    exit_label: String,
    website: Option<String>,
    fields: Vec<Field>,
) {
    let website_text = website.clone().unwrap_or_default();
    s.page_prompts.of_mut(view).crash_review = Some(CrashReview {
        message,
        exit_label,
        website,
        fields,
        description: TextEdit::new(""),
        include_website: false,
        website_edit: TextEdit::new(website_text),
        details_open: false,
        report_text: None,
        validation: None,
        status: None,
    });
}

pub fn send(s: &mut Browser, view: ViewId, tx: &mut Tx) {
    let Some(review) = review_of(s, view) else {
        return;
    };
    if review.description.focused || review.website_edit.focused {
        tx.blur();
    }
    let include = review.include_website && review.website.is_some();
    unsafe {
        ffi_reports::xenon_engine_crash_review_send(
            view,
            XenonStr::borrowed(&review.description.text),
            include,
            XenonStr::borrowed(&review.website_edit.text),
        )
    }
}

pub fn decline(_: &mut Browser, view: ViewId) {
    unsafe { ffi_reports::xenon_engine_crash_review_decline(view) }
}

pub fn cancel(_: &mut Browser, view: ViewId) {
    unsafe { ffi_reports::xenon_engine_crash_review_cancel(view) }
}

pub fn next(_: &mut Browser, view: ViewId) {
    unsafe { ffi_reports::xenon_engine_crash_review_next(view) }
}

pub fn exit(_: &mut Browser, view: ViewId) {
    unsafe { ffi_reports::xenon_engine_crash_review_exit(view) }
}

pub fn show_report_text(_: &mut Browser, view: ViewId) {
    unsafe { ffi_reports::xenon_engine_crash_review_show_text(view) }
}

pub fn hide_report_text(s: &mut Browser, view: ViewId) {
    if let Some(review) = review_of(s, view) {
        review.report_text = None;
    }
}

pub fn toggle_details(s: &mut Browser, view: ViewId) {
    if let Some(review) = review_of(s, view) {
        review.details_open = !review.details_open;
    }
}

pub fn toggle_include_website(s: &mut Browser, view: ViewId) {
    if let Some(review) = review_of(s, view) {
        review.include_website = !review.include_website;
    }
}

fn placeholder_review() -> CrashReview {
    CrashReview {
        message: String::new(),
        exit_label: String::new(),
        website: None,
        fields: Vec::new(),
        description: TextEdit::new(""),
        include_website: false,
        website_edit: TextEdit::new(""),
        details_open: false,
        report_text: None,
        validation: None,
        status: None,
    }
}

pub fn description_edit(s: &mut Browser, view: ViewId) -> &mut TextEdit {
    &mut s
        .page_prompts
        .of_mut(view)
        .crash_review
        .get_or_insert_with(placeholder_review)
        .description
}

pub fn website_edit(s: &mut Browser, view: ViewId) -> &mut TextEdit {
    &mut s
        .page_prompts
        .of_mut(view)
        .crash_review
        .get_or_insert_with(placeholder_review)
        .website_edit
}

pub fn offer(s: &mut Browser, name: String) {
    if let Some(previous) = s.crash_report_offer.replace(name) {
        unsafe {
            ffi_reports::xenon_engine_decline_crash_report_offer(
                XenonStr::borrowed(&previous),
                true,
            )
        }
    }
}

pub fn dismiss_offer(s: &mut Browser) {
    if let Some(name) = s.crash_report_offer.take() {
        unsafe {
            ffi_reports::xenon_engine_decline_crash_report_offer(XenonStr::borrowed(&name), false)
        }
    }
}

pub fn review_offer(s: &mut Browser) {
    if let Some(name) = s.crash_report_offer.take() {
        unsafe { ffi_reports::xenon_engine_review_crash_report(XenonStr::borrowed(&name)) }
    }
}

pub struct ReportContext {
    slots: Arc<Slots>,
}

pub fn install(slots: Arc<Slots>) -> Box<ReportContext> {
    let context = Box::new(ReportContext { slots });
    let callbacks = XenonReportCallbacks {
        context: std::ptr::from_ref::<ReportContext>(&context)
            .cast_mut()
            .cast(),
        crash_report_offered: Some(crash_report_offered),
        crash_review_opened: Some(crash_review_opened),
        crash_review_status: Some(crash_review_status),
        crash_review_text: Some(crash_review_text),
        crash_review_message: Some(crash_review_message),
        crash_review_closed: Some(crash_review_closed),
    };
    unsafe { ffi_reports::xenon_engine_set_report_callbacks(&callbacks) };
    context
}

unsafe fn slots<'a>(context: *mut c_void) -> &'a Slots {
    unsafe { &(*context.cast::<ReportContext>()).slots }
}

unsafe extern "C" fn crash_report_offered(context: *mut c_void, name: XenonStr) {
    let name = text(name);
    unsafe { slots(context) }.update_target(move |browser, _| offer(browser, name));
}

unsafe extern "C" fn crash_review_opened(
    context: *mut c_void,
    view: ViewId,
    review: *const XenonCrashReview,
) {
    let Some(review) = (unsafe { review.as_ref() }) else {
        return;
    };
    let fields = if review.fields.is_null() || review.field_count == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(review.fields, review.field_count) }
            .iter()
            .map(|field| Field {
                label: text(field.label),
                value: text(field.value),
                is_code: field.is_code,
            })
            .collect()
    };
    let message = text(review.message);
    let exit_label = text(review.exit_label);
    let website = Some(text(review.website)).filter(|website| !website.is_empty());
    unsafe { slots(context) }.update_view(view, move |browser, _| {
        review_opened(browser, view, message, exit_label, website, fields)
    });
}

unsafe extern "C" fn crash_review_status(
    context: *mut c_void,
    view: ViewId,
    status: *const XenonCrashReviewStatus,
) {
    let Some(status) = (unsafe { status.as_ref() }) else {
        return;
    };
    let stage = match status.stage {
        ffi_reports::XENON_CRASH_REVIEW_SENDING => Stage::Sending,
        ffi_reports::XENON_CRASH_REVIEW_RETRYING => Stage::Retrying,
        ffi_reports::XENON_CRASH_REVIEW_FAILED_RETRYABLE => Stage::FailedRetryable,
        _ => Stage::Answered,
    };
    let answered_message = text(status.answered_message);
    let status = Status {
        stage,
        title: text(status.title),
        message: text(status.message),
        progress: (status.progress >= 0).then(|| status.progress as f32 / 100.0),
        has_next: status.has_next,
    };
    unsafe { slots(context) }.update_view(view, move |browser, _| {
        if let Some(review) = review_of(browser, view) {
            if stage == Stage::Retrying
                && let Some(current) = review.status.as_mut()
            {
                current.message = status.message;
                return;
            }
            if stage == Stage::Answered {
                review.message = answered_message;
            }
            review.validation = None;
            review.status = Some(status);
        }
    });
}

unsafe extern "C" fn crash_review_text(context: *mut c_void, view: ViewId, report: XenonStr) {
    let report = text(report);
    unsafe { slots(context) }.update_view(view, move |browser, _| {
        if let Some(review) = review_of(browser, view) {
            review.report_text = Some(report);
        }
    });
}

unsafe extern "C" fn crash_review_message(context: *mut c_void, view: ViewId, message: XenonStr) {
    let message = text(message);
    unsafe { slots(context) }.update_view(view, move |browser, _| {
        if let Some(review) = review_of(browser, view) {
            review.validation = Some(message);
            review.status = None;
        }
    });
}

unsafe extern "C" fn crash_review_closed(context: *mut c_void, view: ViewId) {
    unsafe { slots(context) }.update_view(view, move |browser, _| {
        if let Some(prompts) = browser.page_prompts.views_mut().get_mut(&view) {
            prompts.crash_review = None;
        }
    });
}
