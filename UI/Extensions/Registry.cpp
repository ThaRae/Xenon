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
#include <LibCore/Directory.h>
#include <LibCore/File.h>
#include <LibCore/StandardPaths.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <LibGfx/SystemTheme.h>
#include <LibWebCommon/WebView/Utilities.h>
#include <LibWebView/Application.h>
#include <LibWebView/HeadlessWebView.h>
#include <LibWebView/Utilities.h>
#include <UI/Extensions/ContentScripts.h>
#include <UI/Extensions/MatchPattern.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>

namespace Ladybird::Extensions {

// The background page's view lays nothing out that anyone sees.
static constexpr Web::DevicePixelSize BACKGROUND_PAGE_SIZE { 800, 600 };

Registry& Registry::the()
{
    static NeverDestroyed<Registry> registry;
    return *registry;
}

void Registry::register_url_scheme()
{
    WebView::URLSchemeBridge bridge {
        .bootstrap_script = [](WebView::ScriptBridgeEndpoint const& endpoint) { return Router::the().bootstrap_script(endpoint); },
        .on_message = [](WebView::ScriptBridgeEndpoint const& endpoint, String message) { Router::the().on_message(endpoint, message); },
    };

    auto handler = [this](WebView::URLSchemeRequest const& request) { return respond(request); };
    WebView::register_url_scheme(MUST(String::from_utf8(EXTENSION_SCHEME)), move(handler), move(bridge));
}

void Registry::initialize(ByteString profile_data_directory, Vector<ByteString> const& command_line_extensions, String ui_language)
{
    m_extensions_directory = LexicalPath::join(profile_data_directory, "Extensions"sv).string();
    m_ui_language = move(ui_language);
    load_state();

    for (auto const& directory : m_persisted_directories.span()) {
        if (auto extension = load_unpacked(directory, Persist::No); extension.is_error())
            warnln("Could not load the extension in {}: {}", directory, extension.error());
    }
    for (auto const& directory : command_line_extensions) {
        if (auto extension = load_unpacked(directory, Persist::No); extension.is_error())
            warnln("Could not load the extension in {}: {}", directory, extension.error());
    }
}

void Registry::start_background_pages()
{
    m_can_start_background_pages = true;
    for (auto const& extension : m_extensions)
        start_background_page(extension);
}

ErrorOr<NonnullRefPtr<Extension>, String> Registry::load_unpacked(StringView directory, Persist persist)
{
    auto extension = TRY(Extension::load_unpacked(directory, m_ui_language));
    for (auto const& warning : extension->manifest().warnings)
        warnln("Extension {} ({}): {}", extension->manifest().name, extension->id(), warning);

    if (persist == Persist::Yes && !m_persisted_directories.contains_slow(extension->root())) {
        m_persisted_directories.append(extension->root());
        save_state();
    }

    add(extension);
    return extension;
}

ErrorOr<NonnullRefPtr<Extension>, String> Registry::reload(StringView id)
{
    auto existing = extension(id);
    if (!existing)
        return "No such extension."_string;
    return load_unpacked(existing->root(), Persist::No);
}

void Registry::add(NonnullRefPtr<Extension> extension)
{
    // Loading an extension that's loaded already reloads it, as Chrome's "Load unpacked" does: its pages and background
    // start over, and it keeps its storage.
    stop_background_page(extension->id());
    // A reloaded extension starts without the content scripts it registered, as an updated one does in Chrome.
    if (m_extensions.remove_all_matching([&](auto const& loaded) { return loaded->id() == extension->id(); }))
        m_registered_content_scripts.remove(extension->id());
    if (auto granted = m_granted_permissions.get(extension->id()); granted.has_value())
        extension->set_granted_optional_permissions(*granted);
    m_extensions.append(extension);

    auto const& version = extension->manifest().version;
    auto known_version = m_known_versions.get(extension->id());
    if (!known_version.has_value())
        m_install_reasons.set(extension->id(), { .reason = "install"sv, .previous_version = {} });
    else if (*known_version != version)
        m_install_reasons.set(extension->id(), { .reason = "update"sv, .previous_version = *known_version });
    m_known_versions.set(extension->id(), version);
    save_state();

    dbgln("Loaded extension {} {} as {}", extension->manifest().name, version, extension->id());
    start_background_page(extension);
    notify_extensions_changed();
}

void Registry::remove(StringView id)
{
    auto removed = extension(id);
    if (!removed)
        return;

    stop_background_page(id);
    Router::the().forget_extension(*removed);
    m_extensions.remove_all_matching([&](auto const& loaded) { return loaded->id() == id; });
    m_persisted_directories.remove_all_matching([&](auto const& directory) { return directory == removed->root(); });
    m_known_versions.remove(removed->id());
    m_install_reasons.remove(removed->id());
    m_granted_permissions.remove(removed->id());
    m_registered_content_scripts.remove(removed->id());
    m_controlled_settings.remove_all_matching([&](auto const&, auto const& setting) { return setting.extension_id == removed->id(); });
    m_storage.remove_all_matching([&](auto const& key, auto const&) { return key.starts_with(ByteString::formatted("{}/", id)); });
    save_state();

    auto storage = LexicalPath::join(m_extensions_directory, "Storage"sv, id).string();
    if (FileSystem::exists(storage))
        (void)FileSystem::remove(storage, FileSystem::RecursionMode::Allowed);

    notify_extensions_changed();
}

RefPtr<Extension> Registry::extension(StringView id) const
{
    for (auto const& extension : m_extensions) {
        if (extension->id() == id)
            return extension;
    }
    return nullptr;
}

bool Registry::is_persisted(Extension const& extension) const
{
    return m_persisted_directories.contains_slow(extension.root());
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
            file = LexicalPath::join(m_extensions_directory, "Storage"sv, extension.id(), ByteString::formatted("{}.json", storage_area_name(kind))).string();

        auto quota = StorageArea::quota_for(kind, extension.has_permission("unlimitedStorage"sv));
        return MUST(StorageArea::create(kind, quota, move(file)));
    });
}

Optional<InstallReason> Registry::take_install_reason(StringView id)
{
    return m_install_reasons.take(MUST(String::from_utf8(id)));
}

void Registry::grant_optional_permissions(StringView id, Vector<String> const& permissions)
{
    auto extension = this->extension(id);
    if (!extension)
        return;

    auto granted = extension->granted_optional_permissions();
    for (auto const& permission : permissions) {
        if (!granted.contains_slow(permission))
            granted.append(permission);
    }
    extension->set_granted_optional_permissions(move(granted));
    m_granted_permissions.set(extension->id(), extension->granted_optional_permissions());
    save_state();
}

void Registry::revoke_optional_permissions(StringView id, Vector<String> const& permissions)
{
    auto extension = this->extension(id);
    if (!extension)
        return;

    auto granted = extension->granted_optional_permissions();
    granted.remove_all_matching([&](auto const& permission) { return permissions.contains_slow(permission); });
    extension->set_granted_optional_permissions(move(granted));
    m_granted_permissions.set(extension->id(), extension->granted_optional_permissions());
    save_state();
}

Optional<Registry::ControlledSetting const&> Registry::controlled_setting(StringView name) const
{
    return m_controlled_settings.get(name);
}

void Registry::set_controlled_setting(String const& name, Extension const& extension, JsonValue value)
{
    m_controlled_settings.set(name, { .extension_id = extension.id(), .value = move(value) });
    save_state();
}

bool Registry::clear_controlled_setting(StringView name, Extension const& extension)
{
    auto setting = m_controlled_settings.get(name);
    if (!setting.has_value() || setting->extension_id != extension.id())
        return false;
    m_controlled_settings.remove(name);
    save_state();
    return true;
}

void Registry::on_extensions_change(Function<bool()> listener)
{
    m_change_listeners.append(move(listener));
}

// An extension's pages and workers fetch from the hosts it has permission for without CORS, as in Chrome. Ports in
// its patterns aren't kept: a pattern reaches its hosts on any port.
void Registry::update_origin_access_allow_list()
{
    Vector<Web::Fetch::Infrastructure::OriginAccessAllowListEntry> allow_list;
    for (auto const& extension : m_extensions) {
        auto origin = extension->base_url().origin();
        for (auto const& permission : extension->manifest().host_permissions) {
            auto pattern = MatchPattern::parse(permission);
            if (pattern.is_error())
                continue;
            for (auto scheme : { "http"_string, "https"_string }) {
                if (!pattern.value().matches_scheme(scheme))
                    continue;
                allow_list.append({
                    .source_origin = origin,
                    .destination_scheme = scheme,
                    .destination_host = pattern.value().host(),
                    .allow_destination_subdomains = pattern.value().matches_subdomains(),
                });
            }
        }
    }
    WebView::Application::the().set_origin_access_allow_list(move(allow_list));
}

void Registry::notify_extensions_changed()
{
    update_origin_access_allow_list();
    ContentScripts::update();
    m_change_listeners.remove_all_matching([](auto& listener) { return !listener(); });
}

// --- Background pages --------------------------------------------------------------------------------------------------

// A view no one sees, for background pages and offscreen documents. A headless view reports itself visible, so its
// page's timers, and its workers, run at full speed.
OwnPtr<WebView::HeadlessWebView> Registry::create_hidden_view(URL::URL const& url)
{
    if (!m_theme.has_value()) {
        auto theme_path = LexicalPath::join(WebView::s_ladybird_resource_root, "themes"sv, "Default.ini"sv);
        auto theme = Gfx::load_system_theme(theme_path.string());
        if (theme.is_error()) {
            warnln("Extensions: can't create hidden views without a theme: {}", theme.error());
            return nullptr;
        }
        m_theme = theme.release_value();
    }

    auto view = WebView::HeadlessWebView::create(*m_theme, BACKGROUND_PAGE_SIZE);
    view->load(url);
    return view;
}

void Registry::close_hidden_view(NonnullOwnPtr<WebView::HeadlessWebView> view)
{
    auto request_close = view->prepare_for_immediate_close();
    request_close();
    // NB: The view goes once its page has had the chance to close.
    Core::deferred_invoke([view = move(view)] { });
}

void Registry::start_background_page(Extension const& extension)
{
    if (!m_can_start_background_pages || !extension.manifest().background_service_worker.has_value())
        return;
    if (auto view = create_hidden_view(extension.url_for(BACKGROUND_PAGE_PATH)))
        m_background_pages.set(extension.id(), view.release_nonnull());
}

void Registry::stop_background_page(StringView id)
{
    auto key = MUST(String::from_utf8(id));
    if (auto view = m_background_pages.take(key); view.has_value())
        close_hidden_view(view.release_value());
    if (auto view = m_offscreen_documents.take(key); view.has_value())
        close_hidden_view(view.release_value());
}

bool Registry::has_offscreen_document(Extension const& extension) const
{
    return m_offscreen_documents.contains(extension.id());
}

void Registry::create_offscreen_document(Extension const& extension, URL::URL const& url)
{
    if (auto view = create_hidden_view(url))
        m_offscreen_documents.set(extension.id(), view.release_nonnull());
}

void Registry::close_offscreen_document(Extension const& extension)
{
    if (auto view = m_offscreen_documents.take(extension.id()); view.has_value())
        close_hidden_view(view.release_value());
}

bool Registry::is_offscreen_view(Extension const& extension, u64 view_id) const
{
    auto view = m_offscreen_documents.get(extension.id());
    return view.has_value() && (*view)->view_id() == view_id;
}

bool Registry::is_hidden_view(u64 view_id) const
{
    for (auto const& views : { &m_background_pages, &m_offscreen_documents }) {
        for (auto const& [id, view] : *views) {
            if (view->view_id() == view_id)
                return true;
        }
    }
    return false;
}

// --- chrome-extension:// -----------------------------------------------------------------------------------------------

// The response body of a generated resource, in an unlinked temporary file, since the renderer reads responses from a
// file descriptor.
static ErrorOr<IPC::File> file_with_contents(StringView contents)
{
    auto pattern = ByteString::formatted("{}/xenon-extension-XXXXXX", Core::StandardPaths::tempfile_directory());
    Vector<char> path;
    path.append(pattern.characters(), pattern.length());
    path.append('\0');
    auto fd = TRY(Core::System::mkstemp(path.span()));
    auto file = IPC::File::adopt_fd(fd);
    TRY(Core::System::unlink(StringView { path.data(), path.size() - 1 }));

    auto stream = TRY(Core::File::adopt_fd(TRY(Core::System::dup(fd)), Core::File::OpenMode::Write));
    TRY(stream->write_until_depleted(contents.bytes()));
    if (::lseek(fd, 0, SEEK_SET) < 0)
        return Error::from_syscall("lseek"sv, errno);
    return file;
}

Optional<String> Registry::generated_resource(Extension const& extension, StringView path) const
{
    auto const& worker = extension.manifest().background_service_worker;
    if (!worker.has_value())
        return {};

    if (path == BACKGROUND_PAGE_PATH)
        return MUST(String::formatted("<!doctype html>\n<meta charset=\"utf-8\">\n<title>{} background</title>\n", extension.manifest().name));

    // The worker loads the chrome.* API, then the extension's script, then tells the UI process it's running.
    if (path == "/_generated_background_worker.js"sv) {
        if (extension.manifest().background_is_module)
            return MUST(String::formatted("import \"/_generated_chrome_api.js\";\nimport \"/{}\";\nself[Symbol.for(\"xenon.background.ready\")]?.();\n", *worker));
        return MUST(String::formatted("importScripts(\"/_generated_chrome_api.js\", \"/{}\");\nself[Symbol.for(\"xenon.background.ready\")]?.();\n", *worker));
    }

    if (path == "/_generated_chrome_api.js"sv)
        return Router::the().background_worker_script(extension);

    return {};
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

    Vector<HTTP::Header> headers;
    auto add_headers = [&](StringView mime_type, bool is_sandboxed) {
        headers.append(HTTP::Header::isomorphic_encode("Content-Type"sv, mime_type));
        headers.append(HTTP::Header::isomorphic_encode("Cache-Control"sv, "no-cache"sv));
        headers.append(HTTP::Header::isomorphic_encode("X-Content-Type-Options"sv, "nosniff"sv));

        // Extension pages run under MV3's policy, and sandboxed pages under theirs, which also makes their origin opaque.
        if (mime_type.starts_with("text/html"sv)) {
            auto const& policy = is_sandboxed ? extension->manifest().sandbox_csp : extension->manifest().extension_pages_csp;
            headers.append(HTTP::Header::isomorphic_encode("Content-Security-Policy"sv, policy));
        }
    };

    // Xenon's own resources, which only the extension and the browser may reach.
    if (path.starts_with("/_generated_"sv)) {
        bool from_extension_or_browser = request.is_navigation ? !request.navigation_initiator.has_value() : request.from_same_origin_process;
        auto contents = from_extension_or_browser ? generated_resource(*extension, path) : Optional<String> {};
        if (!contents.has_value())
            return Error::from_errno(ENOENT);

        add_headers(mime_type_for_path(path), false);
        return WebView::URLSchemeResponse { .file = TRY(file_with_contents(*contents)), .headers = move(headers) };
    }

    auto file_path = TRY(extension->resolve(request.url));
    add_headers(mime_type_for_path(file_path), extension->is_sandboxed_page(path));
    return WebView::URLSchemeResponse { .file = TRY(WebView::open_local_file_for_renderer(file_path)), .headers = move(headers) };
}

// --- State -------------------------------------------------------------------------------------------------------------

static ErrorOr<JsonObject> read_state_file(ByteString const& path)
{
    if (!FileSystem::exists(path))
        return JsonObject {};
    auto contents = TRY(TRY(Core::File::open(path, Core::File::OpenMode::Read))->read_until_eof());
    auto json = TRY(JsonValue::from_string(contents));
    if (!json.is_object())
        return Error::from_string_literal("not a JSON object");
    return json.as_object();
}

void Registry::load_state()
{
    auto path = LexicalPath::join(m_extensions_directory, "extensions.json"sv).string();
    auto json = read_state_file(path);
    if (json.is_error()) {
        warnln("Extensions: unable to read {}: {}", path, json.error());
        return;
    }

    if (auto directories = json.value().get_array("unpacked"sv); directories.has_value()) {
        for (auto const& directory : directories->values()) {
            if (directory.is_string())
                m_persisted_directories.append(directory.as_string().to_byte_string());
        }
    }
    if (auto versions = json.value().get_object("versions"sv); versions.has_value()) {
        versions->for_each_member([&](String const& id, JsonValue const& version) {
            if (version.is_string())
                m_known_versions.set(id, version.as_string());
        });
    }
    if (auto granted = json.value().get_object("granted"sv); granted.has_value()) {
        granted->for_each_member([&](String const& id, JsonValue const& permissions) {
            if (!permissions.is_array())
                return;
            Vector<String> list;
            for (auto const& permission : permissions.as_array().values()) {
                if (permission.is_string())
                    list.append(permission.as_string());
            }
            m_granted_permissions.set(id, move(list));
        });
    }
    if (auto content_scripts = json.value().get_object("contentScripts"sv); content_scripts.has_value()) {
        content_scripts->for_each_member([&](String const& id, JsonValue const& scripts) {
            if (!scripts.is_array())
                return;
            Vector<RegisteredContentScript> list;
            for (auto const& script : scripts.as_array().values()) {
                if (!script.is_object())
                    continue;
                if (auto registered = RegisteredContentScript::from_json(script.as_object()); !registered.is_error())
                    list.append(registered.release_value());
            }
            m_registered_content_scripts.set(id, move(list));
        });
    }
    if (auto settings = json.value().get_object("settings"sv); settings.has_value()) {
        settings->for_each_member([&](String const& name, JsonValue const& setting) {
            if (!setting.is_object())
                return;
            auto extension_id = setting.as_object().get_string("extension"sv);
            auto value = setting.as_object().get("value"sv);
            if (extension_id.has_value() && value.has_value())
                m_controlled_settings.set(name, { .extension_id = *extension_id, .value = *value });
        });
    }
}

Vector<RegisteredContentScript> const& Registry::registered_content_scripts(StringView id) const
{
    static NeverDestroyed<Vector<RegisteredContentScript>> none;
    if (auto scripts = m_registered_content_scripts.get(id); scripts.has_value())
        return *scripts;
    return *none;
}

void Registry::set_registered_content_scripts(StringView id, Vector<RegisteredContentScript> scripts)
{
    if (scripts.is_empty())
        m_registered_content_scripts.remove(id);
    else
        m_registered_content_scripts.set(MUST(String::from_utf8(id)), move(scripts));
    save_state();
    ContentScripts::update();
}

void Registry::save_state() const
{
    if (m_extensions_directory.is_empty())
        return;

    JsonArray directories;
    for (auto const& directory : m_persisted_directories)
        directories.must_append(MUST(String::from_byte_string(directory)));
    JsonObject versions;
    for (auto const& [id, version] : m_known_versions)
        versions.set(id, version);

    JsonObject granted;
    for (auto const& [id, permissions] : m_granted_permissions) {
        JsonArray list;
        for (auto const& permission : permissions)
            list.must_append(permission);
        granted.set(id, move(list));
    }
    JsonObject settings;
    for (auto const& [name, setting] : m_controlled_settings) {
        JsonObject json;
        json.set("extension"sv, setting.extension_id);
        json.set("value"sv, setting.value);
        settings.set(name, move(json));
    }

    JsonObject content_scripts;
    for (auto const& [id, scripts] : m_registered_content_scripts) {
        JsonArray list;
        for (auto const& script : scripts) {
            if (script.persist_across_sessions)
                list.must_append(script.to_json());
        }
        if (!list.is_empty())
            content_scripts.set(id, move(list));
    }

    JsonObject state;
    state.set("unpacked"sv, move(directories));
    state.set("versions"sv, move(versions));
    state.set("granted"sv, move(granted));
    state.set("contentScripts"sv, move(content_scripts));
    state.set("settings"sv, move(settings));

    auto write = [&]() -> ErrorOr<void> {
        TRY(Core::Directory::create(m_extensions_directory, Core::Directory::CreateDirectories::Yes));
        auto path = LexicalPath::join(m_extensions_directory, "extensions.json"sv).string();
        auto temporary = ByteString::formatted("{}.tmp", path);
        auto file = TRY(Core::File::open(temporary, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate, 0600));
        auto serialized = state.serialized();
        TRY(file->write_until_depleted(serialized.bytes()));
        file->close();
        TRY(Core::System::rename(temporary, path));
        return {};
    };
    if (auto result = write(); result.is_error())
        warnln("Extensions: unable to save the list of extensions: {}", result.error());
}

}
