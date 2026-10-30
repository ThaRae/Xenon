/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/NeverDestroyed.h>
#include <AK/Vector.h>
#include <AK/WeakPtr.h>
#include <LibCore/Environment.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Resource.h>
#include <LibHTTP/Header.h>
#include <LibURL/Parser.h>
#include <LibURL/URL.h>
#include <LibWebView/Application.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/CanonicalNavigation.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/HSTSStore.h>
#include <LibWebView/RequestServerManager.h>
#include <LibWebView/Utilities.h>
#include <LibWebView/ViewImplementation.h>
#include <UI/CertificateDetails.h>
#include <UI/CertificateErrors.h>

namespace Ladybird::CertificateErrors {

static constexpr auto SCHEME = "xenon"sv;

static constexpr auto CONTENT_SECURITY_POLICY = "default-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"sv;

static constexpr auto PROCEED_BY_ITSELF_VARIABLE = "XENON_TEST_PROCEED_PAST_CERTIFICATE_ERRORS"sv;

static constexpr size_t MAX_REJECTED_CERTIFICATES = 64;

static constexpr auto BOOTSTRAP_SCRIPT = R"~~~(
window.xenonCertificateError = Object.freeze({
    state,
    send(message) {
        bridge.postMessage(JSON.stringify(message));
    },
});
)~~~"sv;

struct RejectedCertificate {
    WeakPtr<WebView::BrowsingSession> session;
    ByteString host;
    u16 port { 0 };
    ByteBuffer certificate;
};

static Vector<RejectedCertificate>& rejected_certificates()
{
    static NeverDestroyed<Vector<RejectedCertificate>> certificates;
    return *certificates;
}

static void did_reject_certificate(WebView::BrowsingSession& session, URL::URL const& url, ByteBuffer const& certificate)
{
    ByteString host { url.serialized_host() };
    auto port = url.port_or_default();

    auto& certificates = rejected_certificates();
    certificates.remove_all_matching([&](auto const& rejected) {
        return rejected.session.is_null() || (rejected.session.ptr() == &session && rejected.host == host && rejected.port == port);
    });
    if (certificates.size() >= MAX_REJECTED_CERTIFICATES)
        certificates.remove(0);
    certificates.append({ session.make_weak_ptr(), move(host), port, certificate });
}

static RejectedCertificate const* rejected_certificate(WebView::BrowsingSession const& session, URL::URL const& url)
{
    auto host = url.serialized_host();
    auto port = url.port_or_default();
    for (auto const& rejected : rejected_certificates()) {
        if (rejected.session.ptr() == &session && rejected.host == host && rejected.port == port)
            return &rejected;
    }
    return nullptr;
}

static URL::URL warning_url(URL::URL const& site)
{
    auto url = MUST(String::formatted("{}://{}/?url={}", SCHEME, PAGE_HOST, URL::percent_encode(site.serialize(), URL::PercentEncodeSet::Component)));
    return URL::Parser::basic_parse(url).release_value();
}

static void show_warning_in_place_of_navigations_to(WebView::BrowsingSession& session, URL::URL const& url)
{
    Vector<u64> view_ids;
    WebView::ViewImplementation::for_each_view([&](WebView::ViewImplementation& view) {
        if (&view.session() != &session || !view.has_traversable())
            return IterationDecision::Continue;
        auto navigation = view.traversable().uncommitted_navigation();
        if (navigation && navigation->url.equals(url, URL::ExcludeFragment::Yes))
            view_ids.append(view.view_id());
        return IterationDecision::Continue;
    });

    for (auto view_id : view_ids) {
        Core::deferred_invoke([view_id, url = warning_url(url)] {
            if (auto view = WebView::ViewImplementation::find_view_by_id(view_id); view.has_value())
                view->load(url);
        });
    }
}

void initialize()
{
    WebView::RequestServerManager::the().on_server_certificate_rejected = [](WebView::BrowsingSession& session, URL::URL const& url, ByteBuffer const& certificate) {
        did_reject_certificate(session, url, certificate);
        show_warning_in_place_of_navigations_to(session, url);
    };
}

static Optional<URL::URL> site_url(URL::URL const& page_url)
{
    auto query = page_url.query();
    if (!query.has_value() || !query->starts_with("url="sv))
        return {};
    auto url = URL::Parser::basic_parse(URL::percent_decode(query->substring_view(4)));
    if (!url.has_value() || url->scheme() != "https"sv || !url->host().has_value())
        return {};
    return url;
}

static String display_host(URL::URL const& url)
{
    if (auto port = url.port(); port.has_value())
        return MUST(String::formatted("{}:{}", url.serialized_host(), *port));
    return MUST(String::from_utf8(url.serialized_host()));
}

static bool is_known_hsts_host(WebView::BrowsingSession& session, URL::URL const& url)
{
    if (!session.hsts_store || !url.host()->is_domain())
        return false;
    return session.hsts_store->is_known_hsts_host(url.serialized_host());
}

static Optional<WebView::ViewImplementation&> view_for(WebView::ScriptBridgeEndpoint const& endpoint)
{
    auto view_id = endpoint.view_id();
    if (!view_id.has_value())
        return {};
    return WebView::ViewImplementation::find_view_by_id(*view_id);
}

static bool proceeds_by_itself()
{
    return WebView::Application::browser_options().headless_mode.has_value() && Core::Environment::get(PROCEED_BY_ITSELF_VARIABLE).has_value();
}

static String page_state(WebView::ScriptBridgeEndpoint const& endpoint)
{
    JsonObject state;
    state.set("proceedsByItself"sv, proceeds_by_itself());

    auto url = site_url(endpoint.url());
    if (!url.has_value())
        return state.serialized();
    state.set("url"sv, url->serialize());
    state.set("host"sv, display_host(*url));

    auto view = view_for(endpoint);
    if (!view.has_value())
        return state.serialized();

    auto& session = view->session();
    auto hsts = is_known_hsts_host(session, *url);
    auto const* rejected = rejected_certificate(session, *url);
    state.set("private"sv, session.is_private() == WebView::IsPrivate::Yes);
    state.set("hsts"sv, hsts);
    state.set("canProceed"sv, rejected && !hsts);
    if (rejected)
        state.set("certificate"sv, describe_rejected_certificate(rejected->certificate.bytes(), *url));
    return state.serialized();
}

bool is_page(WebView::ScriptBridgeEndpoint const& endpoint)
{
    return endpoint.world().is_empty() && endpoint.url().scheme() == SCHEME && endpoint.url().serialized_host() == PAGE_HOST;
}

static Optional<StringView> resource_for_path(StringView path)
{
    if (path.is_empty() || path == "/"sv)
        return "resource://xenon/certificate-error/certificate-error.html"sv;
    if (path == "/certificate-error.css"sv)
        return "resource://xenon/certificate-error/certificate-error.css"sv;
    if (path == "/certificate-error.js"sv)
        return "resource://xenon/certificate-error/certificate-error.js"sv;
    if (path == "/ladybird.css"sv)
        return "resource://ladybird/ladybird.css"sv;
    if (path == "/webui.css"sv)
        return "resource://ladybird/about-pages/webui.css"sv;
    if (path == "/xenon.css"sv)
        return "resource://ladybird/about-pages/xenon.css"sv;
    return {};
}

static StringView mime_type_for(StringView uri)
{
    if (uri.ends_with(".html"sv))
        return "text/html; charset=utf-8"sv;
    if (uri.ends_with(".css"sv))
        return "text/css; charset=utf-8"sv;
    return "text/javascript; charset=utf-8"sv;
}

ErrorOr<WebView::URLSchemeResponse> respond(WebView::URLSchemeRequest const& request)
{
    if (!request.is_navigation && !request.from_same_origin_process)
        return Error::from_errno(EACCES);

    auto uri = resource_for_path(request.url.serialize_path());
    if (!uri.has_value())
        return Error::from_errno(ENOENT);
    auto resource = TRY(Core::Resource::load_from_uri(*uri));

    Vector<HTTP::Header> headers;
    headers.append(HTTP::Header::isomorphic_encode("Content-Type"sv, mime_type_for(*uri)));
    headers.append(HTTP::Header::isomorphic_encode("Cache-Control"sv, "no-cache"sv));
    headers.append(HTTP::Header::isomorphic_encode("X-Content-Type-Options"sv, "nosniff"sv));
    headers.append(HTTP::Header::isomorphic_encode("Content-Security-Policy"sv, CONTENT_SECURITY_POLICY));
    headers.append(HTTP::Header::isomorphic_encode("X-Frame-Options"sv, "DENY"sv));
    return WebView::URLSchemeResponse {
        .file = TRY(WebView::open_local_file_for_renderer(resource->filesystem_path().to_byte_string())),
        .headers = move(headers),
    };
}

Optional<String> bootstrap_script(WebView::ScriptBridgeEndpoint const& endpoint)
{
    if (!is_page(endpoint))
        return {};
    return MUST(String::formatted("const state = {};\n{}", page_state(endpoint), BOOTSTRAP_SCRIPT));
}

void handle_message(WebView::ScriptBridgeEndpoint const& endpoint, String const& message)
{
    if (!is_page(endpoint) || endpoint.navigable().parent_id.has_value())
        return;

    auto json = JsonValue::from_string(message);
    if (json.is_error() || !json.value().is_object())
        return;
    auto name = json.value().as_object().get_string("name"sv);
    if (!name.has_value())
        return;

    auto url = site_url(endpoint.url());
    auto view = view_for(endpoint);
    if (!url.has_value() || !view.has_value())
        return;

    if (*name == "proceed"sv) {
        auto& session = view->session();
        auto const* rejected = rejected_certificate(session, *url);
        if (!rejected || is_known_hsts_host(session, *url))
            return;
        WebView::RequestServerManager::the().add_certificate_exception(session, *url, rejected->certificate);
        view->load(*url, Web::Bindings::NavigationHistoryBehavior::Replace);
    } else if (*name == "leave"sv) {
        view->load(WebView::Application::settings().new_tab_page_url(), Web::Bindings::NavigationHistoryBehavior::Replace);
    }
}

}
