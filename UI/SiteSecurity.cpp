/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AnyOf.h>
#include <AK/HashTable.h>
#include <AK/JsonArray.h>
#include <AK/NeverDestroyed.h>
#include <AK/RefCounted.h>
#include <LibCore/Environment.h>
#include <LibCore/Timer.h>
#include <LibWebView/Application.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/RequestServerManager.h>
#include <LibWebView/ViewImplementation.h>
#include <UI/CertificateDetails.h>
#include <UI/SiteSecurity.h>

namespace Ladybird::SiteSecurity {

static constexpr auto REPORT_VARIABLE = "XENON_TEST_REPORT_SITE_SECURITY"sv;
static constexpr auto REPORT_MARKER = "XENON-SITE-SECURITY-TEST "sv;
static constexpr auto REPORT_INTERVAL = AK::Duration::from_milliseconds(250);

bool applies_to(URL::URL const& url)
{
    return url.host().has_value() && (url.scheme() == "https"sv || url.scheme() == "http"sv);
}

String display_protocol(StringView protocol)
{
    if (protocol == "TLSv1"sv)
        return "TLS 1.0"_string;
    if (protocol.starts_with("TLSv"sv))
        return MUST(String::formatted("TLS {}", protocol.substring_view(4)));
    return MUST(String::from_utf8(protocol));
}

static String display_host(URL::URL const& url)
{
    if (auto port = url.port(); port.has_value())
        return MUST(String::formatted("{}:{}", url.serialized_host(), *port));
    return MUST(String::from_utf8(url.serialized_host()));
}

static Optional<String> chain_name(JsonObject const& certificate)
{
    for (auto key : { "subject"sv, "subjectOrganization"sv }) {
        if (auto name = certificate.get_string(key); name.has_value() && !name->is_empty())
            return *name;
    }
    return {};
}

JsonObject describe(URL::URL const& url, Optional<Requests::SecureConnection> const& connection, bool has_certificate_exception)
{
    JsonObject description;
    description.set("host"sv, display_host(url));

    if (url.scheme() != "https"sv) {
        description.set("state"sv, "notSecure"_string);
        return description;
    }

    auto uses_certificate_exception = has_certificate_exception || (connection.has_value() && connection->uses_certificate_exception);
    description.set("state"sv, uses_certificate_exception ? "acceptedCertificate"_string : "secure"_string);
    if (!connection.has_value())
        return description;

    if (!connection->protocol.is_empty())
        description.set("protocol"sv, display_protocol(connection->protocol));
    if (!connection->cipher.is_empty())
        description.set("cipher"sv, MUST(String::from_byte_string(connection->cipher)));

    JsonArray chain;
    for (size_t i = 0; i < connection->certificate_chain.size(); ++i) {
        auto certificate = describe_certificate(connection->certificate_chain[i].bytes());
        if (!certificate.has_value())
            continue;
        if (auto name = chain_name(*certificate); name.has_value())
            chain.must_append(name.release_value());
        if (i == 0)
            description.set("certificate"sv, certificate.release_value());
    }
    description.set("chain"sv, move(chain));
    return description;
}

bool has_certificate_exception(WebView::BrowsingSession const& session, URL::URL const& url)
{
    auto host = url.serialized_host();
    auto port = url.port_or_default();
    return any_of(session.certificate_exceptions, [&](auto const& exception) {
        return exception.port == port && exception.host == host;
    });
}

void request_description(WebView::ViewImplementation& view, Function<void(JsonObject)> on_description)
{
    auto url = view.url();
    auto has_exception = has_certificate_exception(view.session(), url);
    if (url.scheme() != "https"sv || !url.host().has_value()) {
        on_description(describe(url, {}, has_exception));
        return;
    }

    struct Lookup : public RefCounted<Lookup> {
        URL::URL url;
        bool has_exception { false };
        size_t pending { 1 };
        Optional<Requests::SecureConnection> connection;
        Function<void(JsonObject)> on_description;

        void settle()
        {
            if (--pending == 0)
                on_description(describe(url, connection, has_exception));
        }
    };
    auto lookup = adopt_ref(*new Lookup);
    lookup->url = url;
    lookup->has_exception = has_exception;
    lookup->on_description = move(on_description);

    ByteString host { url.serialized_host() };
    auto port = url.port_or_default();
    auto const* session = &view.session();
    WebView::RequestServerManager::the().for_each_instance([&](WebView::RequestServerInstance& instance) {
        if (&instance.session() != session || instance.is_retired())
            return IterationDecision::Continue;
        ++lookup->pending;
        instance.control_client().retrieve_secure_connection(host, port)->when_resolved([lookup](Optional<Requests::SecureConnection>& connection) {
                                                                            if (connection.has_value() && !lookup->connection.has_value())
                                                                                lookup->connection = move(connection);
                                                                            lookup->settle();
                                                                        })
            .when_rejected([lookup](Error&) { lookup->settle(); });
        return IterationDecision::Continue;
    });
    lookup->settle();
}

static HashTable<String>& reported_pages()
{
    static NeverDestroyed<HashTable<String>> pages;
    return *pages;
}

static void report_loaded_pages()
{
    WebView::ViewImplementation::for_each_view([](WebView::ViewImplementation& view) {
        auto url = view.url();
        if (view.is_loading() || !applies_to(url))
            return IterationDecision::Continue;

        auto key = MUST(String::formatted("{} {}", view.view_id(), url.serialize()));
        if (reported_pages().set(key) != HashSetResult::InsertedNewEntry)
            return IterationDecision::Continue;

        request_description(view, [key, is_secure = url.scheme() == "https"sv](JsonObject description) {
            if (is_secure && !description.has("protocol"sv)) {
                reported_pages().remove(key);
                return;
            }
            dbgln("{}{}", REPORT_MARKER, description.serialized());
        });
        return IterationDecision::Continue;
    });
}

void initialize()
{
    if (!WebView::Application::browser_options().headless_mode.has_value() || !Core::Environment::get(REPORT_VARIABLE).has_value())
        return;

    static NeverDestroyed<NonnullRefPtr<Core::Timer>> timer(Core::Timer::create_repeating(REPORT_INTERVAL, report_loaded_pages));
    (*timer)->start();
}

}
