/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AnyOf.h>
#include <AK/Base64.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <AK/NeverDestroyed.h>
#include <AK/ScopeGuard.h>
#include <LibCore/Directory.h>
#include <LibCore/File.h>
#include <LibCore/StandardPaths.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <LibGfx/SystemTheme.h>
#include <LibURL/Site.h>
#include <LibWebCommon/WebView/Utilities.h>
#include <LibWebView/Application.h>
#include <LibWebView/HeadlessWebView.h>
#include <LibWebView/RequestServerManager.h>
#include <LibWebView/Utilities.h>
#include <UI/Extensions/CRX.h>
#include <UI/Extensions/ContentScripts.h>
#include <UI/Extensions/MatchPattern.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>

namespace Ladybird::Extensions {

static constexpr auto INSTALLED_DIRECTORY = "Installed"sv;
static constexpr auto STAGING_DIRECTORY = "staging"sv;

static constexpr Web::DevicePixelSize BACKGROUND_PAGE_SIZE { 800, 600 };

Registry& Registry::the()
{
    static NeverDestroyed<Registry> registry;
    return *registry;
}

static bool page_has_permission(WebView::ScriptBridgeEndpoint const& endpoint, StringView permission)
{
    auto extension = Registry::the().extension_for_url(endpoint.url());
    return extension && extension->has_permission(permission);
}

void Registry::register_url_scheme()
{
    WebView::URLSchemeBridge bridge {
        .bootstrap_script = [](WebView::ScriptBridgeEndpoint const& endpoint) { return Router::the().bootstrap_script(endpoint); },
        .on_message = [](WebView::ScriptBridgeEndpoint const& endpoint, String message) { Router::the().on_message(endpoint, message); },
        .may_read_clipboard = [](WebView::ScriptBridgeEndpoint const& endpoint) { return page_has_permission(endpoint, "clipboardRead"sv); },
        .may_write_clipboard = [](WebView::ScriptBridgeEndpoint const& endpoint) { return page_has_permission(endpoint, "clipboardWrite"sv); },
    };

    auto handler = [this](WebView::URLSchemeRequest const& request) { return respond(request); };
    WebView::register_url_scheme(MUST(String::from_utf8(EXTENSION_SCHEME)), move(handler), move(bridge));
}

void Registry::initialize(ByteString profile_data_directory, Vector<ByteString> const& command_line_extensions, String ui_language)
{
    m_extensions_directory = LexicalPath::join(profile_data_directory, "Extensions"sv).string();
    m_ui_language = move(ui_language);
    load_state();
    remove_stale_installed_files();

    for (auto const& directory : m_persisted_directories.span()) {
        if (auto extension = load_unpacked(directory, Persist::No); extension.is_error())
            warnln("Could not load the extension in {}: {}", directory, extension.error());
    }
    for (auto const& [id, version] : HashMap { m_installed_versions }) {
        auto directory = LexicalPath::join(installed_extension_directory(id), version).string();
        if (auto extension = load_unpacked(directory, Persist::No); extension.is_error())
            warnln("Could not load the installed extension {} {}: {}", id, version, extension.error());
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
    stop_background_page(extension->id());
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
    auto installed = m_installed_versions.remove(id);
    if (!removed && !installed)
        return;

    stop_background_page(id);
    if (removed) {
        Router::the().forget_extension(*removed);
        m_extensions.remove_all_matching([&](auto const& loaded) { return loaded->id() == id; });
        m_persisted_directories.remove_all_matching([&](auto const& directory) { return directory == removed->root(); });
    }
    m_known_versions.remove(id);
    m_install_reasons.remove(id);
    m_granted_permissions.remove(id);
    m_registered_content_scripts.remove(id);
    m_controlled_settings.remove_all_matching([&](auto const&, auto const& setting) { return setting.extension_id == id; });
    m_storage.remove_all_matching([&](auto const& key, auto const&) { return key.starts_with(ByteString::formatted("{}/", id)); });
    save_state();

    auto storage = LexicalPath::join(m_extensions_directory, "Storage"sv, id).string();
    if (FileSystem::exists(storage))
        (void)FileSystem::remove(storage, FileSystem::RecursionMode::Allowed);
    if (auto files = installed_extension_directory(id); installed && FileSystem::exists(files))
        (void)FileSystem::remove(files, FileSystem::RecursionMode::Allowed);

    notify_extensions_changed();
}

static ErrorOr<void, String> write_manifest_key(ByteString const& directory, PackedExtension const& packed)
{
    auto path = LexicalPath::join(directory, "manifest.json"sv).string();
    auto manifest = TRY(read_extension_json(path, directory));
    if (auto key = manifest.get_string("key"sv); key.has_value()) {
        if (extension_id_from_key(*key) != packed.id)
            return "Its manifest's key is another extension's."_string;
        return {};
    }

    auto key = encode_base64(packed.public_key);
    if (key.is_error())
        return "There's no memory for its key."_string;
    manifest.set("key"sv, key.release_value());

    auto write = [&]() -> ErrorOr<void> {
        auto file = TRY(Core::File::open(path, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate));
        auto serialized = manifest.serialized();
        TRY(file->write_until_depleted(serialized.bytes()));
        return {};
    };
    if (auto result = write(); result.is_error())
        return MUST(String::formatted("Its manifest can't be written: {}", result.error()));
    return {};
}

ErrorOr<Registry::Installed, String> Registry::install(ReadonlyBytes crx, StringView expected_id, ReadonlyBytes publisher_key_hash)
{
    if (m_extensions_directory.is_empty())
        return "There's no profile to install it in."_string;
    auto packed = TRY(verify_crx(crx, expected_id, publisher_key_hash));

    auto directory = installed_extension_directory(packed.id);
    auto staging = LexicalPath::join(directory, STAGING_DIRECTORY).string();
    auto remove_staging = [&] {
        if (FileSystem::exists(staging))
            (void)FileSystem::remove(staging, FileSystem::RecursionMode::Allowed);
    };
    remove_staging();
    ScopeGuard remove_staging_when_done { remove_staging };

    TRY(unpack_zip(packed.archive, staging));
    TRY(write_manifest_key(staging, packed));
    auto unpacked = TRY(Extension::load_unpacked(staging, m_ui_language));
    auto const& version = unpacked->manifest().version;

    auto running = extension(packed.id);
    auto running_installed_version = running && is_installed(*running) ? Optional<String const&> { running->manifest().version } : Optional<String const&> {};
    if (running_installed_version == version)
        return Installed::Now;

    auto destination = LexicalPath::join(directory, version).string();
    if (FileSystem::exists(destination))
        (void)FileSystem::remove(destination, FileSystem::RecursionMode::Allowed);
    if (auto result = Core::System::rename(staging, destination); result.is_error())
        return MUST(String::formatted("It can't be moved into place: {}", result.error()));

    m_installed_versions.set(packed.id, version);
    if (running && !running_installed_version.has_value())
        m_persisted_directories.remove_all_matching([&](auto const& persisted) { return persisted == running->root(); });
    save_state();

    if (running_installed_version.has_value()) {
        dbgln("Installed {} {} for the next session", packed.id, version);
        return Installed::NextSession;
    }
    (void)TRY(load_unpacked(destination, Persist::No));
    return Installed::Now;
}

Optional<String const&> Registry::installed_version(StringView id) const
{
    return m_installed_versions.get(id);
}

bool Registry::is_installed(Extension const& extension) const
{
    if (!m_installed_versions.contains(extension.id()))
        return false;
    auto directory = FileSystem::real_path(installed_extension_directory(extension.id()));
    return !directory.is_error() && extension.root().starts_with(ByteString::formatted("{}/", directory.value()));
}

ByteString Registry::installed_extension_directory(StringView id) const
{
    return LexicalPath::join(m_extensions_directory, INSTALLED_DIRECTORY, id).string();
}

void Registry::remove_stale_installed_files() const
{
    auto installed = LexicalPath::join(m_extensions_directory, INSTALLED_DIRECTORY).string();
    if (!FileSystem::is_directory(installed))
        return;

    Vector<ByteString> stale;
    auto result = Core::Directory::for_each_entry(installed, Core::DirIterator::SkipParentAndBaseDir, [&](auto const& entry, auto const&) -> ErrorOr<IterationDecision> {
        auto id_directory = LexicalPath::join(installed, entry.name).string();
        auto id = String::from_byte_string(entry.name);
        auto version = id.is_error() ? Optional<String const&> {} : m_installed_versions.get(id.value());
        if (!version.has_value()) {
            stale.append(id_directory);
            return IterationDecision::Continue;
        }
        TRY(Core::Directory::for_each_entry(id_directory, Core::DirIterator::SkipParentAndBaseDir, [&](auto const& version_entry, auto const&) -> ErrorOr<IterationDecision> {
            if (version_entry.name != version->bytes_as_string_view())
                stale.append(LexicalPath::join(id_directory, version_entry.name).string());
            return IterationDecision::Continue;
        }));
        return IterationDecision::Continue;
    });
    if (result.is_error())
        warnln("Extensions: unable to look through {}: {}", installed, result.error());

    for (auto const& path : stale) {
        if (auto removed = FileSystem::remove(path, FileSystem::RecursionMode::Allowed); removed.is_error())
            warnln("Extensions: unable to remove {}: {}", path, removed.error());
    }
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
    return m_persisted_directories.contains_slow(extension.root()) || is_installed(extension);
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
    Router::the().update_navigation_events();
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
    Router::the().update_navigation_events();
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
    Router::the().update_navigation_events();
    m_change_listeners.remove_all_matching([](auto& listener) { return !listener(); });
}

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

    auto view = WebView::HeadlessWebView::create(*m_theme, BACKGROUND_PAGE_SIZE, WebView::IsPrivate::No, WebView::EnableContentBlocker::No);
    view->load(url);
    return view;
}

void Registry::close_hidden_view(NonnullOwnPtr<WebView::HeadlessWebView> view)
{
    if (auto site = URL::Site::serialize_for_partitioning(view->url().origin()); site.has_value())
        WebView::RequestServerManager::the().retire_when_idle(view->session(), *site);

    auto request_close = view->prepare_for_immediate_close();
    request_close();
    Core::deferred_invoke([view = move(view)] { });
}

void Registry::start_background_page(Extension const& extension)
{
    if (!m_can_start_background_pages || !extension.manifest().background_service_worker.has_value())
        return;
    Router::the().background_will_start(extension);
    open_background_page(extension);
}

bool Registry::restart_background_page(Extension const& extension)
{
    if (!m_can_start_background_pages || !extension.manifest().background_service_worker.has_value() || m_background_pages.contains(extension.id()))
        return false;
    open_background_page(extension);
    return m_background_pages.contains(extension.id());
}

void Registry::open_background_page(Extension const& extension)
{
    if (auto view = create_hidden_view(extension.url_for(BACKGROUND_PAGE_PATH)))
        m_background_pages.set(extension.id(), view.release_nonnull());
}

void Registry::stop_idle_background_page(Extension const& extension)
{
    if (auto view = m_background_pages.take(extension.id()); view.has_value())
        close_hidden_view(view.release_value());
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

    bool allowed = false;
    if (request.is_navigation) {
        allowed = !request.navigation_initiator.has_value()
            || request.navigation_initiator->is_same_origin(request.url.origin())
            || extension->is_web_accessible(path, *request.navigation_initiator);
    } else {
        allowed = request.from_same_origin_process
            || any_of(request.requester_origins, [&](auto const& origin) { return extension->is_web_accessible(path, origin); });
    }
    if (!allowed)
        return Error::from_errno(EACCES);

    Vector<HTTP::Header> headers;
    auto add_headers = [&](StringView mime_type, bool is_sandboxed) {
        headers.append(HTTP::Header::isomorphic_encode("Content-Type"sv, mime_type));
        headers.append(HTTP::Header::isomorphic_encode("Cache-Control"sv, "no-cache"sv));
        headers.append(HTTP::Header::isomorphic_encode("X-Content-Type-Options"sv, "nosniff"sv));

        if (mime_type.starts_with("text/html"sv)) {
            auto const& policy = is_sandboxed ? extension->manifest().sandbox_csp : extension->manifest().extension_pages_csp;
            headers.append(HTTP::Header::isomorphic_encode("Content-Security-Policy"sv, policy));
        }
    };

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
    if (auto installed = json.value().get_object("installed"sv); installed.has_value()) {
        installed->for_each_member([&](String const& id, JsonValue const& version) {
            if (is_valid_extension_id(id) && version.is_string() && !version.as_string().is_empty() && !version.as_string().contains('/') && !version.as_string().starts_with('.'))
                m_installed_versions.set(id, version.as_string());
        });
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
    JsonObject installed;
    for (auto const& [id, version] : m_installed_versions)
        installed.set(id, version);
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
    state.set("installed"sv, move(installed));
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
