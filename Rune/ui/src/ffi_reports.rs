/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;

use crate::ffi::{XenonStr, XenonViewId};

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonReportField {
    pub label: XenonStr,
    pub value: XenonStr,
    pub is_code: bool,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonCrashReview {
    pub message: XenonStr,
    pub exit_label: XenonStr,
    pub website: XenonStr,
    pub fields: *const XenonReportField,
    pub field_count: usize,
}

pub const XENON_CRASH_REVIEW_SENDING: u32 = 0;
pub const XENON_CRASH_REVIEW_RETRYING: u32 = 1;
pub const XENON_CRASH_REVIEW_FAILED_RETRYABLE: u32 = 2;
pub const XENON_CRASH_REVIEW_ANSWERED: u32 = 3;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct XenonCrashReviewStatus {
    pub stage: u32,
    pub title: XenonStr,
    pub message: XenonStr,
    pub progress: i32,
    pub has_next: bool,
    pub answered_message: XenonStr,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonReportCallbacks {
    pub context: *mut c_void,
    pub crash_report_offered: Option<unsafe extern "C" fn(context: *mut c_void, name: XenonStr)>,
    pub crash_review_opened: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            review: *const XenonCrashReview,
        ),
    >,
    pub crash_review_status: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            status: *const XenonCrashReviewStatus,
        ),
    >,
    pub crash_review_text:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, text: XenonStr)>,
    pub crash_review_message:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, message: XenonStr)>,
    pub crash_review_closed: Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId)>,
}

unsafe extern "C" {
    pub fn xenon_engine_set_report_callbacks(callbacks: *const XenonReportCallbacks);
    pub fn xenon_engine_review_crash_report(name: XenonStr);
    pub fn xenon_engine_decline_crash_report_offer(name: XenonStr, replaced: bool);
    pub fn xenon_engine_crash_review_send(
        view: XenonViewId,
        description: XenonStr,
        include_website: bool,
        website: XenonStr,
    );
    pub fn xenon_engine_crash_review_decline(view: XenonViewId);
    pub fn xenon_engine_crash_review_cancel(view: XenonViewId);
    pub fn xenon_engine_crash_review_next(view: XenonViewId);
    pub fn xenon_engine_crash_review_exit(view: XenonViewId);
    pub fn xenon_engine_crash_review_show_text(view: XenonViewId);
}
