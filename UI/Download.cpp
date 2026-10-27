/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/EventLoop.h>
#include <LibCore/Timer.h>
#include <LibHTTP/Cache/CacheMode.h>
#include <LibHTTP/Cookie/IncludeCredentials.h>
#include <LibRequests/NetworkError.h>
#include <LibRequests/Request.h>
#include <LibRequests/RequestClient.h>
#include <LibURL/Parser.h>
#include <LibWebCommon/Fetch/Infrastructure/HTTP/Statuses.h>
#include <LibWebCommon/Loader/UserAgent.h>
#include <LibWebView/Application.h>
#include <UI/Download.h>

namespace Ladybird {

static constexpr size_t MAXIMUM_REDIRECTS = 5;

void Download::start(URL::URL url, u64 maximum_size, Callback callback, AK::Duration timeout)
{
    auto download = adopt_ref(*new Download(maximum_size, move(callback)));
    download->m_timeout = Core::Timer::create_single_shot(timeout, [download] {
        download->finish(Error::from_string_literal("the download timed out"));
    });
    download->m_timeout->start();
    download->fetch(move(url));
}

Download::Download(u64 maximum_size, Callback callback)
    : m_maximum_size(maximum_size)
    , m_callback(move(callback))
{
}

void Download::fetch(URL::URL url)
{
    auto client = WebView::Application::request_server_client();
    if (client.is_error()) {
        finish(Error::from_string_literal("RequestServer isn't running"));
        return;
    }

    auto request_headers = HTTP::HeaderList::create();
    request_headers->set({ "User-Agent"sv, Web::default_user_agent });

    m_url = move(url);
    m_request = client.value()->start_request("GET"sv, m_url, *request_headers, {}, HTTP::CacheMode::NoStore, HTTP::Cookie::IncludeCredentials::No);
    if (!m_request) {
        finish(Error::from_string_literal("the request didn't start"));
        return;
    }

    // A redirect stops the request it answers, whose callbacks may still come, so each request's are its own.
    auto id = ++m_request_id;
    m_request->set_unbuffered_request_callbacks(
        [self = NonnullRefPtr(*this), id](NonnullRefPtr<HTTP::HeaderList> response_headers, Optional<u32> response_code, Optional<String> const&, Optional<Core::ImmutableBytes>, Optional<u64>, Requests::CacheState) {
            if (id == self->m_request_id)
                self->did_receive_headers(*response_headers, response_code);
        },
        [self = NonnullRefPtr(*this), id](Requests::ResponseData data) {
            if (id == self->m_request_id)
                self->did_receive_data(data.bytes());
        },
        [self = NonnullRefPtr(*this), id](Core::ImmutableBytes data) {
            if (id == self->m_request_id)
                self->did_receive_data(data.bytes());
        },
        [self = NonnullRefPtr(*this), id](u64, Requests::RequestTimingInfo const&, Optional<Requests::NetworkError> network_error) {
            if (id != self->m_request_id)
                return;
            self->m_request_finished = true;
            if (network_error.has_value()) {
                warnln("Unable to download {}: {}", self->m_url, Requests::network_error_to_string(*network_error));
                self->finish(Error::from_string_literal("the download failed"));
                return;
            }
            self->finish(move(self->m_payload));
        });
}

void Download::did_receive_headers(HTTP::HeaderList const& headers, Optional<u32> response_code)
{
    if (response_code.has_value() && Web::Fetch::Infrastructure::is_redirect_status(*response_code)) {
        auto location = headers.get("Location"sv);
        auto url = location.has_value() ? URL::Parser::basic_parse(*location, m_url) : Optional<URL::URL> {};
        auto allowed = url.has_value() && (url->scheme() == "https"sv || (url->scheme() == "http"sv && m_url.scheme() == "http"sv));
        if (!allowed || ++m_redirect_count > MAXIMUM_REDIRECTS) {
            finish(Error::from_string_literal("the download was redirected somewhere it can't go"));
            return;
        }

        ++m_request_id;
        m_request->stop();
        Core::deferred_invoke([self = NonnullRefPtr(*this), url = url.release_value()]() mutable {
            if (!self->m_finished)
                self->fetch(move(url));
        });
        return;
    }

    if (!response_code.has_value() || *response_code < 200 || *response_code >= 300) {
        warnln("Unable to download {}: received HTTP status {}", m_url, response_code.value_or(0));
        finish(Error::from_string_literal("the server didn't send the file"));
    }
}

void Download::did_receive_data(ReadonlyBytes data)
{
    if (m_finished)
        return;
    if (data.size() > m_maximum_size - m_payload.size()) {
        finish(Error::from_string_literal("the file is larger than it should be"));
        return;
    }
    if (m_payload.try_append(data).is_error())
        finish(Error::from_string_literal("there's no memory for the file"));
}

void Download::finish(ErrorOr<ByteBuffer> result)
{
    if (m_finished)
        return;
    m_finished = true;
    m_timeout->stop();

    auto request = move(m_request);
    if (request && !m_request_finished) {
        ++m_request_id;
        request->stop();
    }

    // NB: The request is let go of only after its own callback has returned.
    Core::deferred_invoke([self = NonnullRefPtr(*this), request = move(request), result = move(result)]() mutable {
        request = nullptr;
        self->m_timeout = nullptr;
        self->m_callback(move(result));
    });
}

}
