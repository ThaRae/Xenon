/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <Rune/Bridge/XenonBridge.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XenonReportField {
    XenonStr label;
    XenonStr value;
    bool is_code;
} XenonReportField;

typedef struct XenonCrashReview {
    XenonStr message;
    XenonStr exit_label;
    XenonStr website;
    XenonReportField const* fields;
    size_t field_count;
} XenonCrashReview;

enum XenonCrashReviewStage {
    XENON_CRASH_REVIEW_SENDING,
    XENON_CRASH_REVIEW_RETRYING,
    XENON_CRASH_REVIEW_FAILED_RETRYABLE,
    XENON_CRASH_REVIEW_ANSWERED,
};

typedef struct XenonCrashReviewStatus {
    uint32_t stage;
    XenonStr title;
    XenonStr message;
    int32_t progress;
    bool has_next;
    XenonStr answered_message;
} XenonCrashReviewStatus;

typedef struct XenonReportCallbacks {
    void* context;
    void (*crash_report_offered)(void* context, XenonStr report_name);
    void (*crash_review_opened)(void* context, XenonViewId, XenonCrashReview const*);
    void (*crash_review_status)(void* context, XenonViewId, XenonCrashReviewStatus const*);
    void (*crash_review_text)(void* context, XenonViewId, XenonStr text);
    void (*crash_review_message)(void* context, XenonViewId, XenonStr validation);
    void (*crash_review_closed)(void* context, XenonViewId);
} XenonReportCallbacks;

void xenon_engine_set_report_callbacks(XenonReportCallbacks const*);

void xenon_engine_review_crash_report(XenonStr report_name);
void xenon_engine_decline_crash_report_offer(XenonStr report_name, bool replaced);
void xenon_engine_crash_review_send(XenonViewId, XenonStr description, bool include_website, XenonStr website);
void xenon_engine_crash_review_decline(XenonViewId);
void xenon_engine_crash_review_cancel(XenonViewId);
void xenon_engine_crash_review_next(XenonViewId);
void xenon_engine_crash_review_exit(XenonViewId);
void xenon_engine_crash_review_show_text(XenonViewId);

#ifdef __cplusplus
}
#endif
