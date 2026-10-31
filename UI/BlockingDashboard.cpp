/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <AK/NeverDestroyed.h>
#include <AK/QuickSort.h>
#include <LibCore/File.h>
#include <LibCore/Resource.h>
#include <LibHTTP/Header.h>
#include <LibWebCommon/Loader/ContentBlocking.h>
#include <LibWebView/Application.h>
#include <LibWebView/URLSchemeHandler.h>
#include <LibWebView/Utilities.h>
#include <UI/BlockingDashboard.h>
#include <UI/CertificateErrors.h>
#include <UI/FilterListUpdates.h>
#include <UI/FilterLists.h>
#include <UI/XenonSettings.h>

namespace Ladybird::BlockingDashboard {

static constexpr auto SCHEME = "xenon"sv;
static constexpr auto DASHBOARD_HOST = "blocking"sv;

static constexpr auto CONTENT_SECURITY_POLICY = "default-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"sv;

static constexpr auto BOOTSTRAP_SCRIPT = R"~~~(
window.xenonBlocking = Object.freeze({
    initialState,
    send(message) {
        bridge.postMessage(JSON.stringify(message));
    },
});
bridge.onmessage = text => {
    window.dispatchEvent(new CustomEvent("xenonblocking", { detail: JSON.parse(text) }));
};
)~~~"sv;

static Optional<StringView> resource_for_path(StringView path)
{
    if (path.is_empty() || path == "/"sv)
        return "resource://xenon/blocking/dashboard.html"sv;
    if (path == "/dashboard.css"sv)
        return "resource://xenon/blocking/dashboard.css"sv;
    if (path == "/dashboard.js"sv)
        return "resource://xenon/blocking/dashboard.js"sv;
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

static ErrorOr<WebView::URLSchemeResponse> respond(WebView::URLSchemeRequest const& request)
{
    if (request.url.serialized_host() == CertificateErrors::PAGE_HOST)
        return CertificateErrors::respond(request);
    if (request.url.serialized_host() != DASHBOARD_HOST)
        return Error::from_errno(ENOENT);

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

static ErrorOr<JsonValue> read_manifest(StringView directory)
{
    auto file = TRY(Core::File::open(LexicalPath::join(directory, "manifest.json"sv).string(), Core::File::OpenMode::Read));
    auto contents = TRY(file->read_until_eof());
    return JsonValue::from_string(contents);
}

static JsonObject filter_lists()
{
    JsonObject lists;
    JsonArray items;

    auto directory = FilterLists::Updater::current_directory();
    if (!directory.has_value()) {
        lists.set("items"sv, move(items));
        return lists;
    }

    auto bundled_directory = FilterLists::bundled_lists_directory();
    lists.set("downloaded"sv, !bundled_directory.has_value() || *bundled_directory != *directory);

    auto manifest = read_manifest(*directory);
    if (manifest.is_error() || !manifest.value().is_object()) {
        lists.set("items"sv, move(items));
        return lists;
    }

    auto const& object = manifest.value().as_object();
    if (auto generated_at = object.get_string("generated_at"sv); generated_at.has_value())
        lists.set("generatedAt"sv, *generated_at);

    auto disabled_ids = XenonSettings::disabled_filter_lists();
    if (auto entries = object.get_array("lists"sv); entries.has_value()) {
        for (auto const& entry : entries->values()) {
            if (!entry.is_object())
                continue;
            auto const& list = entry.as_object();
            auto id = list.get_string("id"sv);
            if (!id.has_value())
                continue;

            auto title = list.get_string("title"sv);
            auto group = list.get_string("group"sv);

            JsonObject item;
            item.set("id"sv, *id);
            item.set("title"sv, title.has_value() ? *title : *id);
            item.set("group"sv, group.has_value() ? *group : "default"_string);
            item.set("enabled"sv, !disabled_ids.contains(*id));
            if (auto license = list.get_string("license"sv); license.has_value())
                item.set("license"sv, *license);
            if (auto filters = list.get_object("filters"sv); filters.has_value()) {
                item.set("filters"sv, filters->get_u64("kept"sv).value_or(0));
                item.set("upstreamFilters"sv, filters->get_u64("upstream"sv).value_or(0));
            }
            if (auto upstream = list.get_object("upstream"sv); upstream.has_value()) {
                if (auto homepage = upstream->get_string("homepage"sv); homepage.has_value())
                    item.set("homepage"sv, *homepage);
            }
            items.must_append(move(item));
        }
    }

    lists.set("items"sv, move(items));
    return lists;
}

static String dashboard_state()
{
    auto& settings = WebView::Application::settings();

    JsonObject state;
    state.set("name"sv, "state"_string);
    state.set("enabled"sv, settings.content_blocker_enabled());
    state.set("blockedTotal"sv, XenonSettings::blocked_request_total());
    state.set("customFilters"sv, settings.custom_content_blocker_filters());
    state.set("lists"sv, filter_lists());

    Vector<String> sites;
    for (auto const& site : settings.content_blocker_disabled_sites())
        sites.append(site);
    quick_sort(sites);
    JsonArray trusted_sites;
    for (auto& site : sites)
        trusted_sites.must_append(move(site));
    state.set("trustedSites"sv, move(trusted_sites));

    return state.serialized();
}

class Dashboards final : public WebView::SettingsObserver {
public:
    static Dashboards& the()
    {
        static NeverDestroyed<Dashboards> dashboards;
        return *dashboards;
    }

    void add(WebView::ScriptBridgeEndpoint const& endpoint)
    {
        m_endpoints.remove_all_matching([](auto const& open) { return !open.is_open(); });
        if (!m_endpoints.contains_slow(endpoint))
            m_endpoints.append(endpoint);
    }

    void send_state()
    {
        auto state = dashboard_state();
        m_endpoints.remove_all_matching([](auto const& open) { return !open.is_open(); });
        for (auto const& endpoint : m_endpoints)
            endpoint.post_message(state);
    }

private:
    virtual void content_blocker_settings_changed() override { send_state(); }

    Vector<WebView::ScriptBridgeEndpoint> m_endpoints;
};

static bool is_dashboard(WebView::ScriptBridgeEndpoint const& endpoint)
{
    return endpoint.world().is_empty() && endpoint.url().scheme() == SCHEME && endpoint.url().serialized_host() == DASHBOARD_HOST;
}

static void handle_message(WebView::ScriptBridgeEndpoint const& endpoint, String const& message)
{
    if (CertificateErrors::is_page(endpoint)) {
        CertificateErrors::handle_message(endpoint, message);
        return;
    }
    if (!is_dashboard(endpoint))
        return;

    auto json = JsonValue::from_string(message);
    if (json.is_error() || !json.value().is_object())
        return;
    auto const& object = json.value().as_object();
    auto name = object.get_string("name"sv);
    if (!name.has_value())
        return;

    auto& settings = WebView::Application::settings();
    Dashboards::the().add(endpoint);

    if (*name == "setEnabled"sv) {
        if (auto enabled = object.get_bool("enabled"sv); enabled.has_value())
            settings.set_content_blocker_enabled(*enabled);
    } else if (*name == "setListEnabled"sv) {
        auto id = object.get_string("id"sv);
        auto enabled = object.get_bool("enabled"sv);
        if (id.has_value() && enabled.has_value()) {
            XenonSettings::set_filter_list_enabled(*id, *enabled);
            FilterLists::Updater::reapply_lists();
        }
    } else if (*name == "setCustomFilters"sv) {
        if (auto filters = object.get_string("filters"sv); filters.has_value())
            settings.set_custom_content_blocker_filters(*filters);
    } else if (*name == "setSiteTrusted"sv) {
        auto site = object.get_string("site"sv);
        auto trusted = object.get_bool("trusted"sv);
        if (site.has_value() && trusted.has_value()) {
            if (auto host = Web::canonical_content_blocking_host(*site); host.has_value())
                (void)settings.set_content_blocker_enabled_for_site(*host, !*trusted);
        }
    } else if (*name != "load"sv) {
        return;
    }

    endpoint.post_message(dashboard_state());
}

void register_url_scheme()
{
    WebView::URLSchemeBridge bridge {
        .bootstrap_script = [](WebView::ScriptBridgeEndpoint const& endpoint) -> Optional<String> {
            if (CertificateErrors::is_page(endpoint))
                return CertificateErrors::bootstrap_script(endpoint);
            if (!is_dashboard(endpoint))
                return {};
            return MUST(String::formatted("const initialState = {};\n{}", dashboard_state(), BOOTSTRAP_SCRIPT));
        },
        .on_message = [](WebView::ScriptBridgeEndpoint const& endpoint, String message) { handle_message(endpoint, message); },
        .may_read_clipboard = {},
        .may_write_clipboard = {},
    };
    WebView::register_url_scheme(MUST(String::from_utf8(SCHEME)), respond, move(bridge));
}

}
