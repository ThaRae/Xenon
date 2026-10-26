/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <AK/NeverDestroyed.h>
#include <LibWebView/Utilities.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>

namespace Ladybird::Extensions {

Registry& Registry::the()
{
    static NeverDestroyed<Registry> registry;
    return *registry;
}

void Registry::register_url_scheme()
{
    WebView::URLSchemeBridge bridge {
        .bootstrap_script = [](WebView::URLSchemeBridgeEndpoint const& endpoint) { return Router::the().bootstrap_script(endpoint); },
        .on_message = [](WebView::URLSchemeBridgeEndpoint const& endpoint, String message) { Router::the().on_message(endpoint, message); },
    };

    auto handler = [this](WebView::URLSchemeRequest const& request) { return respond(request); };
    WebView::register_url_scheme(MUST(String::from_utf8(EXTENSION_SCHEME)), move(handler), move(bridge));
}

void Registry::initialize(ByteString profile_data_directory, Vector<ByteString> const& unpacked_extensions, String ui_language)
{
    m_storage_directory = LexicalPath::join(profile_data_directory, "Extensions"sv, "Storage"sv).string();
    m_ui_language = move(ui_language);

    for (auto const& directory : unpacked_extensions) {
        if (auto extension = load_unpacked(directory); extension.is_error())
            warnln("Could not load the extension in {}: {}", directory, extension.error());
    }
}

ErrorOr<NonnullRefPtr<Extension>, String> Registry::load_unpacked(StringView directory)
{
    auto extension = TRY(Extension::load_unpacked(directory, m_ui_language));
    for (auto const& warning : extension->manifest().warnings)
        warnln("Extension {} ({}): {}", extension->manifest().name, extension->id(), warning);

    // Loading an unpacked extension again reloads it, as Chrome's "Load unpacked" does.
    m_extensions.remove_all_matching([&](auto const& loaded) { return loaded->id() == extension->id(); });
    m_extensions.append(extension);
    dbgln("Loaded extension {} {} as {}", extension->manifest().name, extension->manifest().version, extension->id());

    if (on_extensions_changed)
        on_extensions_changed();
    return extension;
}

RefPtr<Extension> Registry::extension(StringView id) const
{
    for (auto const& extension : m_extensions) {
        if (extension->id() == id)
            return extension;
    }
    return nullptr;
}

RefPtr<Extension> Registry::extension_for_url(URL::URL const& url) const
{
    if (url.scheme() != EXTENSION_SCHEME)
        return nullptr;
    return extension(url.serialized_host());
}

StorageArea& Registry::storage(Extension const& extension, StorageArea::Kind kind)
{
    auto key = ByteString::formatted("{}/{}", extension.id(), storage_area_name(kind));
    return *m_storage.ensure(key, [&] {
        Optional<ByteString> file;
        if (kind == StorageArea::Kind::Local || kind == StorageArea::Kind::Sync)
            file = LexicalPath::join(m_storage_directory, extension.id(), ByteString::formatted("{}.json", storage_area_name(kind))).string();

        auto quota = StorageArea::quota_for(kind, extension.manifest().has_permission("unlimitedStorage"sv));
        return MUST(StorageArea::create(kind, quota, move(file)));
    });
}

ErrorOr<WebView::URLSchemeResponse> Registry::respond(WebView::URLSchemeRequest const& request) const
{
    auto extension = extension_for_url(request.url);
    if (!extension)
        return Error::from_errno(ENOENT);

    auto path = URL::percent_decode(request.url.serialize_path());

    // An extension's own pages may load any of its files, and the user may open any of its pages. Other pages may only
    // reach what it lists in web_accessible_resources for them, and only by navigating for now.
    // FIXME: Let other pages load web accessible resources as subresources, which needs the request's initiator.
    bool allowed = false;
    if (request.is_navigation) {
        allowed = !request.navigation_initiator.has_value()
            || request.navigation_initiator->is_same_origin(request.url.origin())
            || extension->is_web_accessible(path, *request.navigation_initiator);
    } else {
        allowed = request.from_same_origin_process;
    }
    if (!allowed)
        return Error::from_errno(EACCES);

    auto file_path = TRY(extension->resolve(request.url));
    auto file = TRY(WebView::open_local_file_for_renderer(file_path));

    Vector<HTTP::Header> headers;
    auto mime_type = mime_type_for_path(file_path);
    headers.append(HTTP::Header::isomorphic_encode("Content-Type"sv, mime_type));
    headers.append(HTTP::Header::isomorphic_encode("Cache-Control"sv, "no-cache"sv));
    headers.append(HTTP::Header::isomorphic_encode("X-Content-Type-Options"sv, "nosniff"sv));

    // Extension pages run under MV3's policy, and sandboxed pages under theirs, which also makes their origin opaque.
    if (mime_type.starts_with("text/html"sv)) {
        auto const& policy = extension->is_sandboxed_page(path) ? extension->manifest().sandbox_csp : extension->manifest().extension_pages_csp;
        headers.append(HTTP::Header::isomorphic_encode("Content-Security-Policy"sv, policy));
    }

    return WebView::URLSchemeResponse { .file = move(file), .headers = move(headers) };
}

}
