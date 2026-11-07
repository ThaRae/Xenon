/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashMap.h>
#include <LibURL/URL.h>
#include <LibWebView/CrashReportReview.h>
#include <LibWebView/CrashReportStore.h>
#include <Rune/Bridge/Application.h>
#include <Rune/Bridge/BrowserModel.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgeReports.h>

namespace Xenon {

static XenonReportCallbacks s_report_callbacks {};

template<typename Callback, typename... Arguments>
static void notify_reports(Callback XenonReportCallbacks::* callback, Arguments... arguments)
{
    if (auto function = s_report_callbacks.*callback)
        function(s_report_callbacks.context, arguments...);
}

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

static constexpr auto crash_screen_message = "This page crashed. You can reload it to try again."sv;
static constexpr auto hidden_crash_message = "Part of Ladybird stopped unexpectedly."sv;

struct CrashReview {
    AK_ALLOC_WITH_KMALLOC;

    NonnullOwnPtr<WebView::CrashReportReview> review;
    bool hidden { false };
    Optional<String> website;
    String last_description;
    Optional<String> last_url;
};

static HashMap<XenonViewId, NonnullOwnPtr<CrashReview>>& reviews()
{
    static HashMap<XenonViewId, NonnullOwnPtr<CrashReview>> reviews;
    return reviews;
}

static Optional<XenonViewId> s_pending_hidden_review_view;
static Optional<ByteString> s_pending_hidden_review_name;

static int progress_percent(WebView::ReportSubmission::Stage stage)
{
    using Stage = WebView::ReportSubmission::Stage;
    switch (stage) {
    case Stage::Preparing:
        return 15;
    case Stage::RequestingChallenge:
        return 40;
    case Stage::SolvingProof:
        return 65;
    case Stage::Sending:
        return 85;
    }
    VERIFY_NOT_REACHED();
}

static void send_status(XenonViewId id, u32 stage, StringView title, StringView message, int progress, bool has_next, bool hidden)
{
    XenonCrashReviewStatus status {
        .stage = stage,
        .title = xenon_str(title),
        .message = xenon_str(message),
        .progress = progress,
        .has_next = has_next,
        .answered_message = xenon_str(hidden ? hidden_crash_message : crash_screen_message),
    };
    notify_reports(&XenonReportCallbacks::crash_review_status, id, static_cast<XenonCrashReviewStatus const*>(&status));
}

static void report_outcome(XenonViewId id, CrashReview& review, StringView title, StringView message, int progress)
{
    send_status(id, XENON_CRASH_REVIEW_ANSWERED, title, message, progress, review.review->has_next_report(), review.hidden);
}

static bool open_review(XenonViewId id, CrashReview& review, Optional<ByteString> const& name)
{
    auto report = review.review->open(name);
    if (report.is_error()) {
        warnln("Xenon: Could not open crash report: {}", report.error());
        return false;
    }

    Vector<XenonReportField> fields;
    for (auto const& field : report.value().fields)
        fields.append({ .label = xenon_str(field.label), .value = xenon_str(field.value), .is_code = field.is_code });

    auto website = review.website.value_or({});
    XenonCrashReview shown {
        .message = xenon_str(review.hidden ? "Part of Ladybird stopped unexpectedly. You can send us a crash report to help fix it."sv : "This page crashed. You can send us a crash report to help fix it."sv),
        .exit_label = xenon_str(review.hidden ? "Close tab"sv : "Reload page"sv),
        .website = xenon_str(website),
        .fields = fields.data(),
        .field_count = fields.size(),
    };
    notify_reports(&XenonReportCallbacks::crash_review_opened, id, static_cast<XenonCrashReview const*>(&shown));
    return true;
}

static void show_review(View& view, Optional<ByteString> const& hidden_report_name)
{
    auto id = view.id();
    if (reviews().contains(id))
        return;
    auto is_hidden = hidden_report_name.has_value();
    auto report_name = is_hidden ? hidden_report_name : view.crash_report_name();
    if (!report_name.has_value())
        return;

    auto review = make<CrashReview>(make<WebView::CrashReportReview>(), is_hidden, is_hidden ? Optional<String> {} : view.crash_report_website(), String {}, Optional<String> {});
    auto& raw_review = *review;
    raw_review.review->on_progress = [id](WebView::ReportSubmission::Stage stage) {
        auto sending = stage == WebView::ReportSubmission::Stage::Sending;
        send_status(id, XENON_CRASH_REVIEW_SENDING, "Sending crash report"sv, sending ? "Sending crash report…"sv : "Preparing report…"sv, progress_percent(stage), false, false);
    };
    raw_review.review->on_retry = [id](String const& reason, u32 retry_number, u32 maximum_retries, u32 delay_seconds) {
        auto message = ByteString::formatted("{} Trying again in {} seconds ({} of {})…", reason, delay_seconds, retry_number, maximum_retries);
        send_status(id, XENON_CRASH_REVIEW_RETRYING, "Sending crash report"sv, message, -1, false, false);
    };
    raw_review.review->on_sent = [id, &raw_review] {
        report_outcome(id, raw_review, "Report sent"sv, "Thank you for helping Ladybird."sv, 100);
    };
    raw_review.review->on_failed = [id, &raw_review](WebView::ReportSubmission::Failure failure, String const& reason) {
        if (failure == WebView::ReportSubmission::Failure::Sending) {
            send_status(id, XENON_CRASH_REVIEW_FAILED_RETRYABLE, "Couldn’t send report"sv, reason, -1, false, raw_review.hidden);
            return;
        }
        report_outcome(id, raw_review, "Couldn’t send report"sv, reason, -1);
    };

    if (!open_review(id, raw_review, report_name))
        return;
    reviews().set(id, move(review));
}

void View::install_report_callbacks()
{
    on_crash_report_saved = [this] {
        show_review(*this, {});
    };
    on_crash_overlay_state_change = [this](bool active) {
        if (active) {
            show_review(*this, {});
            return;
        }
        if (reviews().contains(m_id) && !reviews().get(m_id).value()->hidden) {
            reviews().remove(m_id);
            notify_reports(&XenonReportCallbacks::crash_review_closed, m_id);
        }
    };
}

void View::review_hidden_crash_report(ByteString const& report_name)
{
    show_review(*this, report_name);
}

void offer_crash_report(ByteString const& report_name)
{
    notify_reports(&XenonReportCallbacks::crash_report_offered, xenon_str(report_name));
}

static void with_review(XenonViewId id, Function<void(View&, CrashReview&)> command)
{
    Engine::the().post([id, command = move(command)] {
        auto* view = Engine::the().view(id);
        auto review = reviews().get(id);
        if (!review.has_value())
            return;
        if (!view) {
            reviews().remove(id);
            return;
        }
        command(*view, *review.value());
    });
}

}

using Xenon::CrashReview;
using Xenon::Engine;
using Xenon::View;

static StringView string_view_from(XenonStr string)
{
    if (!string.data || string.length == 0)
        return {};
    return { string.data, string.length };
}

void xenon_engine_set_report_callbacks(XenonReportCallbacks const* callbacks)
{
    Xenon::s_report_callbacks = callbacks ? *callbacks : XenonReportCallbacks {};
}

void xenon_engine_review_crash_report(XenonStr report_name)
{
    Engine::the().post([name = ByteString { string_view_from(report_name) }] {
        auto* model = Engine::the().model();
        if (!model || name.is_empty())
            return;
        auto window = model->active_window();
        auto view_id = Engine::the().allocate_view_id();
        Engine::the().new_tab(window.value_or(XENON_WINDOW_NONE), view_id, "about:blank"sv, {}, Web::HTML::ActivateTab::Yes);
        if (auto* view = Engine::the().view(view_id))
            view->review_hidden_crash_report(name);
    });
}

void xenon_engine_decline_crash_report_offer(XenonStr report_name, bool replaced)
{
    Engine::the().post([name = ByteString { string_view_from(report_name) }, replaced] {
        auto& store = WebView::CrashReportStore::the();
        auto result = replaced ? store.mark_seen(name) : store.mark_all_seen();
        if (result.is_error())
            warnln("Xenon: Could not mark crash reports as seen: {}", result.error());
    });
}

void xenon_engine_crash_review_send(XenonViewId id, XenonStr description, bool include_website, XenonStr website)
{
    auto description_text = String::from_utf8_with_replacement_character(string_view_from(description));
    Optional<String> url;
    if (include_website)
        url = String::from_utf8_with_replacement_character(string_view_from(website).trim_whitespace());
    Xenon::with_review(id, [description_text = move(description_text), url = move(url)](View& view, CrashReview& review) {
        Optional<StringView> url_view;
        if (url.has_value())
            url_view = url->bytes_as_string_view();
        if (auto error = WebView::CrashReportReview::validate(description_text, url_view); error.has_value()) {
            Xenon::notify_reports(&XenonReportCallbacks::crash_review_message, view.id(), Xenon::xenon_str(*error));
            return;
        }
        review.last_description = description_text;
        review.last_url = url;
        Xenon::send_status(view.id(), XENON_CRASH_REVIEW_SENDING, "Sending crash report"sv, "Preparing report…"sv, 15, false, review.hidden);
        if (auto error = review.review->send(description_text, url); error.has_value())
            Xenon::report_outcome(view.id(), review, "Couldn’t send report"sv, *error, -1);
    });
}

void xenon_engine_crash_review_decline(XenonViewId id)
{
    Xenon::with_review(id, [](View& view, CrashReview& review) {
        Xenon::report_outcome(view.id(), review, "Report not sent"sv, "This report stays on your device and is not sent."sv, -1);
    });
}

void xenon_engine_crash_review_cancel(XenonViewId id)
{
    Xenon::with_review(id, [](View& view, CrashReview& review) {
        review.review->cancel_sending();
        Xenon::open_review(view.id(), review, {});
    });
}

void xenon_engine_crash_review_next(XenonViewId id)
{
    Xenon::with_review(id, [](View& view, CrashReview& review) {
        review.website.clear();
        if (!Xenon::open_review(view.id(), review, {}))
            xenon_engine_crash_review_exit(view.id());
    });
}

void xenon_engine_crash_review_exit(XenonViewId id)
{
    Xenon::with_review(id, [](View& view, CrashReview& review) {
        auto hidden = review.hidden;
        auto view_id = view.id();
        Xenon::reviews().remove(view_id);
        Xenon::notify_reports(&XenonReportCallbacks::crash_review_closed, view_id);
        if (hidden) {
            if (auto result = WebView::CrashReportStore::the().mark_all_seen(); result.is_error())
                warnln("Xenon: Could not mark crash reports as seen: {}", result.error());
            Engine::the().close_tab(view_id);
            return;
        }
        view.reload();
    });
}

void xenon_engine_crash_review_show_text(XenonViewId id)
{
    Xenon::with_review(id, [](View& view, CrashReview& review) {
        auto text = review.review->report_text();
        if (text.is_error()) {
            warnln("Xenon: Could not read the crash report: {}", text.error());
            Xenon::notify_reports(&XenonReportCallbacks::crash_review_message, view.id(), Xenon::xenon_str("The crash report could not be read."sv));
            return;
        }
        Xenon::notify_reports(&XenonReportCallbacks::crash_review_text, view.id(), Xenon::xenon_str(text.value()));
    });
}
