/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AnyOf.h>
#include <AK/Array.h>
#include <AK/StringBuilder.h>
#include <LibWebView/Application.h>
#include <UI/Extensions/Browser.h>
#include <UI/Extensions/MatchPattern.h>
#include <UI/Extensions/NativeMessaging.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>

namespace Ladybird::Extensions {

// UI/Resources/extensions/*.js, which the build embeds.
extern String const& chrome_api_source;
extern String const& background_page_source;
extern String const& worker_bridge_source;
extern String const& externally_connectable_relay_source;

static constexpr auto NO_RECEIVER = "Could not establish connection. Receiving end does not exist."sv;
static constexpr auto PRUNE_INTERVAL = AK::Duration::from_seconds(5);
// How long messages wait for a background that has started to run its script, in case it never does.
static constexpr auto BACKGROUND_START_TIMEOUT = AK::Duration::from_seconds(10);
// How long a background with nothing to do keeps running, as in Chrome.
static constexpr auto BACKGROUND_IDLE_TIMEOUT = AK::Duration::from_seconds(30);

StringView context_kind_name(ContextKind kind)
{
    switch (kind) {
    case ContextKind::Tab:
        return "TAB"sv;
    case ContextKind::Popup:
        return "POPUP"sv;
    case ContextKind::Background:
        return "BACKGROUND"sv;
    case ContextKind::Offscreen:
        return "OFFSCREEN_DOCUMENT"sv;
    case ContextKind::ContentScript:
        return "CONTENT_SCRIPT"sv;
    case ContextKind::ExternalPage:
        return "WEB_PAGE"sv;
    }
    VERIFY_NOT_REACHED();
}

Router& Router::the()
{
    static NeverDestroyed<Router> router;
    return *router;
}

Router::Router()
    : m_background_idle_timeout(BACKGROUND_IDLE_TIMEOUT)
{
    m_prune_timer = Core::Timer::create_repeating(PRUNE_INTERVAL, [this] { prune_closed_contexts(); });
}

static JsonObject platform_info()
{
    JsonObject info;
#if defined(AK_OS_MACOS)
    info.set("os"sv, "mac"sv);
#elif defined(AK_OS_WINDOWS)
    info.set("os"sv, "win"sv);
#else
    info.set("os"sv, "linux"sv);
#endif
#if ARCH(AARCH64)
    info.set("arch"sv, "arm64"sv);
    info.set("nacl_arch"sv, "arm"sv);
#else
    info.set("arch"sv, "x86-64"sv);
    info.set("nacl_arch"sv, "x86-64"sv);
#endif
    return info;
}

static JsonArray string_array(Vector<String> const& strings)
{
    JsonArray array;
    for (auto const& string : strings)
        array.must_append(string);
    return array;
}

String document_id_string(Web::HTML::CrossProcessId id)
{
    return MUST(String::formatted("{:x}-{:x}", id.namespace_id, id.local_id));
}

String document_id_string(WebView::ScriptBridgeEndpoint const& endpoint)
{
    return document_id_string(endpoint.document_id());
}

static JsonArray accept_languages()
{
    return string_array(WebView::Application::settings().languages());
}

JsonObject Router::config_for(Extension const& extension, ContextKind kind, URL::URL const& url)
{
    JsonObject config;
    config.set("id"sv, extension.id());
    config.set("manifest"sv, extension.manifest().json);
    config.set("messages"sv, extension.locales().messages());
    config.set("uiLanguage"sv, extension.locales().ui_language());
    config.set("acceptLanguages"sv, accept_languages());
    config.set("platform"sv, platform_info());
    config.set("permissions"sv, string_array(extension.permissions()));
    config.set("contextType"sv, context_kind_name(kind));
    config.set("isBackground"sv, kind == ContextKind::Background);
    config.set("url"sv, url.serialize());

    // NB: A tab can show the popup's page too, as Bitwarden's popout does; only the popup itself reports its size.
    config.set("isPopup"sv, kind == ContextKind::Popup);
    return config;
}

Optional<String> Router::bootstrap_script(WebView::ScriptBridgeEndpoint const& endpoint)
{
    auto extension = Registry::the().extension_for_url(endpoint.url());
    if (!extension)
        return {};

    prune_closed_contexts();

    auto path = URL::percent_decode(endpoint.url().serialize_path());
    auto view_id = endpoint.view_id();

    // The background page only relays between its worker and the UI process.
    if (path == BACKGROUND_PAGE_PATH && extension->manifest().background_service_worker.has_value()) {
        m_contexts.append({ *extension, endpoint, ContextKind::Background });

        JsonObject config;
        config.set("module"sv, extension->manifest().background_is_module);
        return MUST(String::formatted("const config = {};\n{}", config.serialized(), background_page_source));
    }

    auto kind = ContextKind::Tab;
    if (view_id.has_value() && Registry::the().is_offscreen_view(*extension, *view_id))
        kind = ContextKind::Offscreen;
    else if (view_id.has_value() && Browser::is_popup_view(*view_id))
        kind = ContextKind::Popup;
    m_contexts.append({ *extension, endpoint, kind });

    StringBuilder script;
    script.appendff("const config = {};\n", config_for(*extension, kind, endpoint.url()).serialized());
    script.append(chrome_api_source);
    return script.to_string_without_validation();
}

String Router::background_worker_script(Extension const& extension)
{
    auto worker_url = extension.url_for(*extension.manifest().background_service_worker);

    // NB: One function holds everything, so that nothing but chrome reaches the worker's global scope.
    StringBuilder script;
    script.append("(() => {\n\"use strict\";\n"sv);
    script.append(worker_bridge_source);
    script.append("const bridge = createWorkerBridge();\n"sv);
    script.appendff("const config = {};\n", config_for(extension, ContextKind::Background, worker_url).serialized());
    script.append(chrome_api_source);
    script.append("\n})();\n"sv);
    return script.to_string_without_validation();
}

String Router::content_script_bootstrap(NonnullRefPtr<Extension> extension, WebView::ScriptBridgeEndpoint const& world)
{
    prune_closed_contexts();
    m_contexts.append({ extension, world, ContextKind::ContentScript });

    StringBuilder script;
    script.appendff("const config = {};\n", config_for(*extension, ContextKind::ContentScript, world.url()).serialized());
    script.append(chrome_api_source);
    return script.to_string_without_validation();
}

String Router::externally_connectable_relay(NonnullRefPtr<Extension> extension, WebView::ScriptBridgeEndpoint const& world, StringView event_name)
{
    prune_closed_contexts();
    m_contexts.append({ extension, world, ContextKind::ExternalPage });

    JsonObject config;
    config.set("extensionId"sv, extension->id());
    config.set("eventName"sv, event_name);
    return MUST(String::formatted("const config = {};\n{}", config.serialized(), externally_connectable_relay_source));
}

Optional<Router::Context&> Router::context_for(WebView::ScriptBridgeEndpoint const& endpoint)
{
    for (auto& context : m_contexts) {
        if (context.endpoint == endpoint)
            return context;
    }
    return {};
}

// What a context's messages say about where they came from. The background's are from its worker's script, as in
// Chrome.
URL::URL Router::context_url(Context const& context) const
{
    if (context.kind == ContextKind::Background)
        return context.extension->url_for(*context.extension->manifest().background_service_worker);
    return context.endpoint.url();
}

JsonObject Router::sender_for(Context const& context)
{
    JsonObject sender;
    // A web page's messages have no extension ID, which is how extensions tell them apart.
    if (context.kind != ContextKind::ExternalPage)
        sender.set("id"sv, context.extension->id());
    sender.set("url"sv, context_url(context).serialize());
    sender.set("origin"sv, context.endpoint.origin().serialize());
    if (context.kind != ContextKind::Background) {
        sender.set("documentId"sv, document_id_string(context.endpoint));
        sender.set("documentLifecycle"sv, "active"sv);
    }

    auto is_in_tab = context.kind == ContextKind::Tab || context.kind == ContextKind::ContentScript || context.kind == ContextKind::ExternalPage;
    if (auto view_id = context.endpoint.view_id(); view_id.has_value() && is_in_tab) {
        if (auto tab = Browser::tab_json(*view_id, *context.extension); tab.has_value()) {
            sender.set("tab"sv, tab.release_value());
            sender.set("frameId"sv, frame_id(context.endpoint));
        }
    }
    return sender;
}

i64 Router::frame_id(WebView::ScriptBridgeEndpoint const& endpoint)
{
    auto const& navigable = endpoint.navigable();
    return frame_id(navigable.id, !navigable.parent_id.has_value());
}

i64 Router::frame_id(Web::HTML::CrossProcessId navigable_id, bool is_top_level)
{
    if (is_top_level)
        return 0;
    return m_frame_ids.ensure(navigable_id, [&] { return m_next_frame_id++; });
}

void Router::prune_closed_contexts()
{
    Vector<WebView::ScriptBridgeEndpoint> closed;
    m_contexts.remove_all_matching([&](Context const& context) {
        if (context.endpoint.is_open())
            return false;
        closed.append(context.endpoint);
        return true;
    });
    for (auto const& endpoint : closed)
        forget_endpoint(endpoint);
}

// A context that went away answers no runtime messages, and its ports close.
void Router::forget_endpoint(WebView::ScriptBridgeEndpoint const& endpoint)
{
    Vector<u64> finished;
    for (auto& [id, pending] : m_pending_messages) {
        pending.awaiting.remove_all_matching([&](auto const& awaiting) { return awaiting == endpoint; });
        if (pending.awaiting.is_empty())
            finished.append(id);
    }
    for (auto id : finished)
        finish_pending_message(id, {});

    Vector<String> port_ids;
    for (auto const& [id, port] : m_ports) {
        if (port.opener == endpoint || port.receivers.contains_slow(endpoint) || port.awaiting.contains_slow(endpoint))
            port_ids.append(id);
    }
    for (auto const& id : port_ids)
        close_port_end(endpoint, id);
}

void Router::forget_extension(Extension const& extension)
{
    Vector<WebView::ScriptBridgeEndpoint> endpoints;
    m_contexts.remove_all_matching([&](Context const& context) {
        if (context.extension->id() != extension.id())
            return false;
        endpoints.append(context.endpoint);
        return true;
    });
    for (auto const& endpoint : endpoints)
        forget_endpoint(endpoint);
    m_alarms.remove(extension.id());
    m_popup_size_listeners.remove(extension.id());
    if (auto starting = m_starting_backgrounds.take(extension.id()); starting.has_value())
        starting->timeout->stop();
    if (auto running = m_running_backgrounds.take(extension.id()); running.has_value())
        (*running)->stop();
    m_background_listeners.remove(extension.id());
}

void Router::on_message(WebView::ScriptBridgeEndpoint const& endpoint, String const& text)
{
    auto context = context_for(endpoint);
    if (!context.has_value())
        return;

    if (context->kind == ContextKind::Background)
        background_was_active(context->extension->id());

    auto json = JsonValue::from_string(text);
    if (json.is_error() || !json.value().is_object())
        return;
    auto const& message = json.value().as_object();
    auto type = message.get_string("type"sv);
    if (!type.has_value())
        return;

    // A web page may only message the extension, and use the ports it opened.
    if (context->kind == ContextKind::ExternalPage && !type->is_one_of("call"sv, "connect"sv, "port-message"sv, "port-disconnect"sv, "closing"sv))
        return;

    auto port_id = message.get_string("port"sv);

    if (*type == "call"sv) {
        auto id = message.get_u64("id"sv);
        auto name = message.get_string("name"sv);
        auto args = message.get_array("args"sv);
        if (id.has_value() && name.has_value())
            handle_call(*context, *id, *name, args.has_value() ? *args : JsonArray {});
    } else if (*type == "response"sv) {
        if (auto id = message.get_u64("id"sv); id.has_value())
            handle_response(*context, *id, message.get_bool("handled"sv).value_or(false), message.get_bool("listening"sv).value_or(true), message.get("response"sv).value_or(JsonValue {}));
    } else if (*type == "connect"sv && port_id.has_value()) {
        // A port to a tab (tabs.connect()) goes to the extension's content scripts there; any other port goes to its
        // pages and background (runtime.connect()).
        auto name = message.get_string("name"sv).value_or({});
        auto may_connect_to_tabs = context->kind != ContextKind::ContentScript && context->kind != ContextKind::ExternalPage;
        if (auto tab_id = message.get_integer<i64>("tabId"sv); tab_id.has_value() && may_connect_to_tabs) {
            auto receivers = content_script_receivers(*context->extension, *tab_id, message.get_integer<i64>("frameId"sv), message.get_string("documentId"sv));
            open_port(*context, *port_id, name, move(receivers));
        } else {
            wake_background(*context->extension, context->kind == ContextKind::ExternalPage ? "runtime.onConnectExternal"sv : "runtime.onConnect"sv);
            Function<void()> open = [this, endpoint, port_id = *port_id, name] {
                if (auto opener = context_for(endpoint); opener.has_value())
                    open_port(*opener, port_id, name, extension_receivers(*opener));
            };
            if (auto waiting = waiting_for_background(*context); waiting.has_value())
                waiting->append(move(open));
            else
                open();
        }
    } else if (*type == "connect-native"sv && port_id.has_value()) {
        open_native_port(*context, *port_id, message.get_string("application"sv).value_or({}));
    } else if (*type == "connected"sv && port_id.has_value()) {
        handle_port_accepted(*context, *port_id, message.get_bool("accepted"sv).value_or(false));
    } else if (*type == "port-message"sv && port_id.has_value()) {
        handle_port_message(*context, *port_id, message.get("message"sv).value_or(JsonValue {}));
    } else if (*type == "port-disconnect"sv && port_id.has_value()) {
        handle_port_disconnect(*context, *port_id);
    } else if (*type == "listening"sv && context->kind == ContextKind::Background) {
        auto name = message.get_string("name"sv);
        if (!name.has_value())
            return;
        auto& listeners = m_background_listeners.ensure(context->extension->id());
        if (message.get_bool("listening"sv).value_or(false))
            listeners.set(*name);
        else
            listeners.remove(*name);
    } else if (*type == "closing"sv) {
        // NB: The page is still there for a moment, so it's forgotten once it has gone.
        Core::deferred_invoke([this] { prune_closed_contexts(); });
    } else if (*type == "popup-size"sv) {
        auto width = message.get_i32("width"sv);
        auto height = message.get_i32("height"sv);
        if (auto listener = m_popup_size_listeners.get(context->extension->id()); listener.has_value() && width.has_value() && height.has_value())
            (*listener)(*width, *height);
    }
}

void Router::reply(WebView::ScriptBridgeEndpoint const& endpoint, u64 id, JsonValue result)
{
    JsonObject message;
    message.set("type"sv, "reply"sv);
    message.set("id"sv, id);
    message.set("result"sv, move(result));
    the().post(endpoint, message.serialized());
}

void Router::reply_with_error(WebView::ScriptBridgeEndpoint const& endpoint, u64 id, StringView error)
{
    JsonObject message;
    message.set("type"sv, "reply"sv);
    message.set("id"sv, id);
    message.set("error"sv, error);
    the().post(endpoint, message.serialized());
}

// What Chrome gives content scripts of chrome.*, besides i18n, which needs no calls.
static bool is_available_to_content_scripts(StringView name)
{
    return name == "runtime.sendMessage"sv || name.starts_with("storage."sv);
}

void Router::handle_call(Context& context, u64 id, String const& name, JsonArray const& args)
{
    // NB: A content script's renderer is a web page's, so what it may ask for is checked here, not just left out of
    //     its chrome.*.
    if (context.kind == ContextKind::ContentScript && !is_available_to_content_scripts(name)) {
        reply_with_error(context.endpoint, id, MUST(String::formatted("{} isn't available to content scripts.", name)));
        return;
    }
    if (context.kind == ContextKind::ExternalPage && name != "runtime.sendMessage"sv) {
        reply_with_error(context.endpoint, id, MUST(String::formatted("{} isn't available to web pages.", name)));
        return;
    }

    if (name == "runtime.sendMessage"sv) {
        wake_background(*context.extension, context.kind == ContextKind::ExternalPage ? "runtime.onMessageExternal"sv : "runtime.onMessage"sv);
        auto message = args.is_empty() ? JsonValue {} : args.at(0);
        Function<void()> send = [this, endpoint = context.endpoint, id, message] {
            if (auto sender = context_for(endpoint); sender.has_value())
                send_runtime_message(*sender, id, message, extension_receivers(*sender));
        };
        if (auto waiting = waiting_for_background(context); waiting.has_value())
            waiting->append(move(send));
        else
            send();
        return;
    }
    if (name == "tabs.sendMessage"sv) {
        send_tab_message(context, id, args);
        return;
    }
    if (name.starts_with_bytes("scripting."sv)) {
        handle_scripting_call(context, id, name, args);
        return;
    }
    if (name == "permissions.request"sv) {
        request_permissions(context, id, args);
        return;
    }

    ErrorOr<JsonValue, String> result = MUST(String::formatted("{} isn't supported yet.", name));
    if (name.starts_with_bytes("storage."sv))
        result = handle_storage_call(context, name, args);
    else if (name.starts_with_bytes("alarms."sv))
        result = handle_alarms_call(context, name, args);
    else if (name.starts_with_bytes("permissions."sv))
        result = handle_permissions_call(context, name, args);
    else if (name.starts_with_bytes("privacy."sv))
        result = handle_privacy_call(context, name, args);
    else if (name.starts_with_bytes("runtime."sv) || name.starts_with_bytes("management."sv) || name.starts_with_bytes("idle."sv) || name.starts_with_bytes("offscreen."sv) || name.starts_with_bytes("commands."sv))
        result = handle_runtime_call(context, name, args);
    else if (name.starts_with_bytes("webNavigation."sv))
        result = handle_web_navigation_call(context, name, args);
    else if (name.starts_with_bytes("tabs."sv) || name.starts_with_bytes("windows."sv) || name.starts_with_bytes("action."sv))
        result = Browser::handle_call(*context.extension, context.endpoint.view_id(), name, args);

    if (result.is_error())
        reply_with_error(context.endpoint, id, result.error());
    else
        reply(context.endpoint, id, result.release_value());
}

static Optional<Vector<String>> keys_argument(JsonValue const& value)
{
    if (!value.is_array())
        return {};

    Vector<String> keys;
    for (auto const& key : value.as_array().values()) {
        if (key.is_string())
            keys.append(key.as_string());
    }
    return keys;
}

ErrorOr<JsonValue, String> Router::handle_storage_call(Context& context, String const& name, JsonArray const& args)
{
    auto const& extension = *context.extension;
    if (!extension.has_permission("storage"sv))
        return "The 'storage' permission is required."_string;

    // Content scripts may use storage.session only once the extension lets them, as in Chrome.
    if (name == "storage.setAccessLevel"sv) {
        auto options = !args.is_empty() && args.at(0).is_object() ? args.at(0).as_object() : JsonObject {};
        auto level = options.get_string("accessLevel"sv);
        if (level == "TRUSTED_AND_UNTRUSTED_CONTEXTS"sv)
            m_session_storage_open_to_content_scripts.set(extension.id());
        else if (level == "TRUSTED_CONTEXTS"sv)
            m_session_storage_open_to_content_scripts.remove(extension.id());
        else
            return "storage.setAccessLevel() takes an accessLevel of TRUSTED_CONTEXTS or TRUSTED_AND_UNTRUSTED_CONTEXTS."_string;
        return JsonValue {};
    }

    if (args.is_empty() || !args.at(0).is_string())
        return "Missing storage area."_string;
    auto const& area_name = args.at(0).as_string();
    if (context.kind == ContextKind::ContentScript && area_name == storage_area_name(StorageArea::Kind::Session) && !m_session_storage_open_to_content_scripts.contains(extension.id()))
        return "Access to storage is not allowed from this context."_string;

    Optional<StorageArea::Kind> kind;
    for (auto candidate : { StorageArea::Kind::Local, StorageArea::Kind::Session, StorageArea::Kind::Sync, StorageArea::Kind::Managed }) {
        if (area_name == storage_area_name(candidate))
            kind = candidate;
    }
    if (!kind.has_value())
        return MUST(String::formatted("Unknown storage area '{}'.", area_name));

    auto& area = Registry::the().storage(extension, *kind);
    auto argument = args.size() > 1 ? args.at(1) : JsonValue {};

    auto changed = [&](JsonObject changes) -> JsonValue {
        if (changes.size() > 0) {
            JsonArray event_args;
            event_args.must_append(move(changes));
            event_args.must_append(area_name);
            dispatch_event(extension, "storage.onChanged"sv, move(event_args));
        }
        return JsonValue {};
    };

    if (name == "storage.get"sv)
        return JsonValue { area.get(keys_argument(argument)) };
    if (name == "storage.getKeys"sv)
        return JsonValue { string_array(area.keys()) };
    if (name == "storage.getBytesInUse"sv)
        return JsonValue { static_cast<u64>(area.bytes_in_use(keys_argument(argument))) };
    if (name == "storage.set"sv) {
        if (!argument.is_object())
            return "storage.set() takes an object of items."_string;
        return changed(TRY(area.set(argument.as_object())));
    }
    if (name == "storage.remove"sv)
        return changed(area.remove(keys_argument(argument).value_or({})));
    if (name == "storage.clear"sv)
        return changed(area.clear());

    return MUST(String::formatted("{} isn't supported.", name));
}

ErrorOr<JsonValue, String> Router::handle_alarms_call(Context& context, String const& name, JsonArray const& args)
{
    auto const& extension = *context.extension;
    if (!extension.has_permission("alarms"sv))
        return "The 'alarms' permission is required."_string;

    auto& alarms = *m_alarms.ensure(extension.id(), [&] {
        return make<Alarms>([this, extension = NonnullRefPtr { extension }](JsonObject alarm) {
            JsonArray event_args;
            event_args.must_append(move(alarm));
            dispatch_event(*extension, "alarms.onAlarm"sv, move(event_args));
        });
    });

    auto alarm_name = !args.is_empty() && args.at(0).is_string() ? args.at(0).as_string() : String {};

    if (name == "alarms.create"sv) {
        auto info = args.size() > 1 && args.at(1).is_object() ? args.at(1).as_object() : JsonObject {};
        TRY(alarms.create(alarm_name, info));
        return JsonValue {};
    }
    if (name == "alarms.get"sv) {
        auto alarm = alarms.get(alarm_name);
        return alarm.has_value() ? JsonValue { alarm.release_value() } : JsonValue {};
    }
    if (name == "alarms.getAll"sv) {
        JsonArray all;
        for (auto& alarm : alarms.all())
            all.must_append(move(alarm));
        return JsonValue { move(all) };
    }
    if (name == "alarms.clear"sv)
        return JsonValue { alarms.clear(alarm_name) };
    if (name == "alarms.clearAll"sv)
        return JsonValue { alarms.clear_all() };

    return MUST(String::formatted("{} isn't supported.", name));
}

static Vector<String> string_list(JsonObject const& object, StringView key)
{
    Vector<String> list;
    if (auto values = object.get_array(key); values.has_value()) {
        for (auto const& value : values->values()) {
            if (value.is_string())
                list.append(value.as_string());
        }
    }
    return list;
}

static JsonObject object_argument(JsonArray const& args, size_t index)
{
    return index < args.size() && args.at(index).is_object() ? args.at(index).as_object() : JsonObject {};
}

struct PermissionWarning {
    StringView permission;
    StringView warning;
};

// What Chrome's prompt says a permission lets an extension do. It doesn't ask about those it says nothing about, and
// Xenon asks about any it doesn't know by name.
static Optional<String> permission_warning(StringView permission)
{
    static constexpr Array without_warnings = {
        "activeTab"sv, "alarms"sv, "background"sv, "contextMenus"sv, "cookies"sv, "declarativeContent"sv,
        "declarativeNetRequestWithHostAccess"sv, "dns"sv, "fontSettings"sv, "gcm"sv, "identity"sv, "idle"sv,
        "offscreen"sv, "power"sv, "scripting"sv, "search"sv, "sidePanel"sv, "storage"sv, "system.cpu"sv,
        "system.display"sv, "system.memory"sv, "tts"sv, "unlimitedStorage"sv, "webRequest"sv, "webRequestAuthProvider"sv,
        "webRequestBlocking"sv
    };
    static constexpr Array warnings {
        PermissionWarning { "bookmarks"sv, "Read and change your bookmarks"sv },
        PermissionWarning { "clipboardRead"sv, "Read data you copy and paste"sv },
        PermissionWarning { "clipboardWrite"sv, "Modify data you copy and paste"sv },
        PermissionWarning { "contentSettings"sv, "Change your settings that control websites' access to features such as cookies, JavaScript, plugins, geolocation, microphone, camera etc."sv },
        PermissionWarning { "debugger"sv, "Access the page debugger backend"sv },
        PermissionWarning { "declarativeNetRequest"sv, "Block content on any page"sv },
        PermissionWarning { "declarativeNetRequestFeedback"sv, "Read your browsing history"sv },
        PermissionWarning { "desktopCapture"sv, "Capture content of your screen"sv },
        PermissionWarning { "downloads"sv, "Manage your downloads"sv },
        PermissionWarning { "geolocation"sv, "Detect your physical location"sv },
        PermissionWarning { "history"sv, "Read and change your browsing history"sv },
        PermissionWarning { "management"sv, "Manage your apps, extensions, and themes"sv },
        PermissionWarning { "nativeMessaging"sv, "Communicate with cooperating native applications"sv },
        PermissionWarning { "notifications"sv, "Display notifications"sv },
        PermissionWarning { "pageCapture"sv, "Read and change all your data on all websites"sv },
        PermissionWarning { "privacy"sv, "Change your privacy-related settings"sv },
        PermissionWarning { "proxy"sv, "Read and change all your data on all websites"sv },
        PermissionWarning { "tabCapture"sv, "Read and change all your data on all websites"sv },
        PermissionWarning { "tabGroups"sv, "View and manage your tab groups"sv },
        PermissionWarning { "tabs"sv, "Read your browsing history"sv },
        PermissionWarning { "topSites"sv, "Read a list of your most frequently visited websites"sv },
        PermissionWarning { "webNavigation"sv, "Read your browsing history"sv },
    };

    if (without_warnings.contains_slow(permission))
        return {};
    for (auto const& warning : warnings) {
        if (warning.permission == permission)
            return MUST(String::from_utf8(warning.warning));
    }
    return MUST(String::formatted("Use the \"{}\" permission", permission));
}

// Unpacked extensions get every permission they ask for when they load, as in Chrome, and optional permissions when the
// user grants them.
ErrorOr<JsonValue, String> Router::handle_permissions_call(Context& context, String const& name, JsonArray const& args)
{
    auto& extension = *context.extension;
    auto const& manifest = extension.manifest();

    if (name == "permissions.getAll"sv) {
        JsonObject all;
        all.set("permissions"sv, string_array(extension.permissions()));
        all.set("origins"sv, string_array(manifest.host_permissions));
        return JsonValue { move(all) };
    }

    auto request = object_argument(args, 0);
    auto permissions = string_list(request, "permissions"sv);
    auto origins = string_list(request, "origins"sv);

    if (name == "permissions.contains"sv) {
        for (auto const& permission : permissions) {
            if (!extension.has_permission(permission))
                return JsonValue { false };
        }
        // A pattern is granted when a host permission covers it, as https://*/* covers https://example.com/*.
        for (auto const& origin : origins) {
            auto pattern = MatchPattern::parse(origin);
            if (pattern.is_error())
                return MUST(String::formatted("Invalid value for origin pattern {}: {}", origin, pattern.error()));
            auto is_granted = any_of(manifest.host_permissions, [&](auto const& host_permission) {
                auto granted = MatchPattern::parse(host_permission);
                return !granted.is_error() && granted.value().contains(pattern.value());
            });
            if (!is_granted)
                return JsonValue { false };
        }
        return JsonValue { true };
    }

    if (name == "permissions.remove"sv) {
        for (auto const& permission : permissions) {
            if (manifest.permissions.contains_slow(permission))
                return "You cannot remove required permissions."_string;
        }
        for (auto const& origin : origins) {
            if (manifest.host_permissions.contains_slow(origin))
                return "You cannot remove required permissions."_string;
        }

        Vector<String> removed;
        for (auto const& permission : permissions) {
            if (extension.granted_optional_permissions().contains_slow(permission))
                removed.append(permission);
        }
        if (!removed.is_empty()) {
            Registry::the().revoke_optional_permissions(extension.id(), removed);
            dispatch_permissions_change(extension.id(), "permissions.onRemoved"sv, removed);
        }
        return JsonValue { true };
    }

    return MUST(String::formatted("{} isn't supported.", name));
}

// Asks the user for optional permissions, as Chrome's prompt does, unless the extension has them already or they're
// ones Chrome doesn't ask about. The extension's open contexts get the permissions' APIs before the call returns.
void Router::request_permissions(Context& context, u64 call_id, JsonArray const& args)
{
    auto const& extension = *context.extension;
    auto const& manifest = extension.manifest();
    auto request = object_argument(args, 0);
    auto permissions = string_list(request, "permissions"sv);

    for (auto const& permission : permissions) {
        if (!manifest.permissions.contains_slow(permission) && !manifest.optional_permissions.contains_slow(permission)) {
            reply_with_error(context.endpoint, call_id, MUST(String::formatted("Only permissions specified in the manifest may be requested: '{}'.", permission)));
            return;
        }
    }
    // FIXME: Optional host permissions.
    for (auto const& origin : string_list(request, "origins"sv)) {
        if (!manifest.host_permissions.contains_slow(origin) && !manifest.host_permissions.contains_slow("<all_urls>"_string)) {
            reply_with_error(context.endpoint, call_id, "Optional host permissions aren't supported yet."sv);
            return;
        }
    }

    Vector<String> missing;
    Vector<String> warnings;
    for (auto const& permission : permissions) {
        if (extension.has_permission(permission) || missing.contains_slow(permission))
            continue;
        missing.append(permission);
        if (auto warning = permission_warning(permission); warning.has_value() && !warnings.contains_slow(*warning))
            warnings.append(warning.release_value());
    }

    auto answer = [extension_id = extension.id(), endpoint = context.endpoint, call_id, missing](bool granted) {
        if (granted && !missing.is_empty()) {
            Registry::the().grant_optional_permissions(extension_id, missing);
            Router::the().dispatch_permissions_change(extension_id, "permissions.onAdded"sv, missing);
        }
        reply(endpoint, call_id, JsonValue { granted });
    };
    if (warnings.is_empty()) {
        answer(true);
        return;
    }
    Browser::prompt_for_permissions(extension, context.endpoint.view_id(), warnings, move(answer));
}

void Router::dispatch_permissions_change(StringView extension_id, StringView event_name, Vector<String> const& permissions)
{
    auto extension = Registry::the().extension(extension_id);
    if (!extension)
        return;

    JsonObject change;
    change.set("permissions"sv, string_array(permissions));
    change.set("origins"sv, JsonArray {});
    JsonArray args;
    args.must_append(move(change));
    dispatch_event(*extension, event_name, move(args));
}

// --- chrome.privacy ----------------------------------------------------------------------------------------------------

// The settings Xenon lets extensions control. It has no password manager or autofill of its own, so they're off, and an
// extension that sets one only becomes the one controlling it, which is what password managers check for.
static constexpr Array PRIVACY_SETTINGS = {
    "privacy.services.passwordSavingEnabled"sv,
    "privacy.services.autofillEnabled"sv,
    "privacy.services.autofillAddressEnabled"sv,
    "privacy.services.autofillCreditCardEnabled"sv,
};

static JsonObject setting_details(StringView setting, Extension const& extension)
{
    auto controlled = Registry::the().controlled_setting(setting);

    JsonObject details;
    details.set("value"sv, controlled.has_value() ? controlled->value : JsonValue { false });
    if (!controlled.has_value())
        details.set("levelOfControl"sv, "controllable_by_this_extension"sv);
    else if (controlled->extension_id == extension.id())
        details.set("levelOfControl"sv, "controlled_by_this_extension"sv);
    else
        details.set("levelOfControl"sv, "controlled_by_other_extensions"sv);
    return details;
}

ErrorOr<JsonValue, String> Router::handle_privacy_call(Context& context, String const& name, JsonArray const& args)
{
    auto const& extension = *context.extension;
    if (!extension.has_permission("privacy"sv))
        return "The 'privacy' permission is required."_string;

    auto setting = !args.is_empty() && args.at(0).is_string() ? args.at(0).as_string() : String {};
    if (!PRIVACY_SETTINGS.contains_slow(setting))
        return MUST(String::formatted("{} isn't supported yet.", setting));

    if (name == "privacy.get"sv)
        return JsonValue { setting_details(setting, extension) };

    if (name == "privacy.set"sv) {
        auto value = object_argument(args, 1).get("value"sv);
        if (!value.has_value() || !value->is_bool())
            return "The value has to be a boolean."_string;
        Registry::the().set_controlled_setting(setting, extension, *value);
    } else if (name == "privacy.clear"sv) {
        if (!Registry::the().clear_controlled_setting(setting, extension))
            return JsonValue {};
    } else {
        return MUST(String::formatted("{} isn't supported.", name));
    }

    // Every extension that can see the setting hears it changed, with its own level of control.
    auto event_name = MUST(String::formatted("{}.onChange", setting));
    for (auto const& other : Registry::the().extensions()) {
        if (!other->has_permission("privacy"sv))
            continue;
        JsonArray event_args;
        event_args.must_append(setting_details(setting, *other));
        dispatch_event(*other, event_name, move(event_args));
    }
    return JsonValue {};
}

JsonArray Router::contexts_of(Extension const& extension) const
{
    JsonArray contexts;
    for (auto const& context : m_contexts) {
        if (context.extension->id() != extension.id() || context.kind == ContextKind::ContentScript || context.kind == ContextKind::ExternalPage)
            continue;

        JsonObject json;
        json.set("contextType"sv, context_kind_name(context.kind));
        json.set("contextId"sv, document_id_string(context.endpoint));
        json.set("incognito"sv, false);
        json.set("frameId"sv, context.kind == ContextKind::Background ? -1 : 0);
        json.set("tabId"sv, -1);
        json.set("windowId"sv, -1);
        if (context.kind != ContextKind::Background) {
            json.set("documentId"sv, document_id_string(context.endpoint));
            json.set("documentUrl"sv, context.endpoint.url().serialize());
            json.set("documentOrigin"sv, context.endpoint.origin().serialize());
        }
        if (auto view_id = context.endpoint.view_id(); view_id.has_value() && context.kind == ContextKind::Tab) {
            if (auto ids = Browser::tab_and_window_ids(*view_id); ids.has_value()) {
                json.set("tabId"sv, ids->tab_id);
                json.set("windowId"sv, ids->window_id);
            }
        }
        contexts.must_append(move(json));
    }
    return contexts;
}

ErrorOr<JsonValue, String> Router::handle_runtime_call(Context& context, String const& name, JsonArray const& args)
{
    auto extension = context.extension;

    if (name == "runtime.backgroundReady"sv) {
        if (context.kind != ContextKind::Background)
            return JsonValue {};
        auto starting = m_starting_backgrounds.get(extension->id());
        auto is_restart = starting.has_value() && starting->is_restart;
        Core::deferred_invoke([this, id = extension->id()] { background_did_run(id); });

        // NB: These come before what waited for the background to run its script. A background that started again for
        //     an event is in the same session, and gets no runtime.onStartup.
        JsonObject details;
        if (auto reason = Registry::the().take_install_reason(extension->id()); reason.has_value()) {
            details.set("reason"sv, reason->reason);
            if (reason->previous_version.has_value())
                details.set("previousVersion"sv, *reason->previous_version);
            JsonArray event_args;
            event_args.must_append(move(details));
            post(context.endpoint, event_message("runtime.onInstalled"sv, move(event_args)));
        } else if (!is_restart) {
            post(context.endpoint, event_message("runtime.onStartup"sv, {}));
        }
        return JsonValue {};
    }

    if (name == "runtime.getContexts"sv) {
        prune_closed_contexts();
        auto filter = !args.is_empty() && args.at(0).is_object() ? args.at(0).as_object() : JsonObject {};
        auto types = filter.get_array("contextTypes"sv);

        JsonArray matching;
        for (auto const& candidate : contexts_of(*extension).values()) {
            auto type = candidate.as_object().get_string("contextType"sv);
            if (types.has_value() && !any_of(types->values(), [&](auto const& wanted) { return wanted.is_string() && wanted.as_string() == *type; }))
                continue;
            if (auto url = filter.get_array("documentUrls"sv); url.has_value()) {
                auto document_url = candidate.as_object().get_string("documentUrl"sv);
                if (!document_url.has_value() || !any_of(url->values(), [&](auto const& wanted) { return wanted.is_string() && wanted.as_string() == *document_url; }))
                    continue;
            }
            matching.must_append(candidate);
        }
        return JsonValue { move(matching) };
    }

    if (name == "runtime.reload"sv) {
        // NB: The extension's pages and background go with it, the calling context among them.
        Core::deferred_invoke([id = extension->id()] {
            if (auto result = Registry::the().reload(id); result.is_error())
                warnln("Extensions: unable to reload {}: {}", id, result.error());
        });
        return JsonValue {};
    }

    if (name == "runtime.openOptionsPage"sv) {
        auto const& page = extension->manifest().options_page;
        if (!page.has_value())
            return "Could not create an options page."_string;
        Browser::open_tab(extension->url_for(*page));
        return JsonValue {};
    }

    if (name == "runtime.setUninstallURL"sv)
        return JsonValue {};

    if (name == "management.getSelf"sv) {
        auto const& manifest = extension->manifest();
        JsonObject info;
        info.set("id"sv, extension->id());
        info.set("name"sv, manifest.name);
        info.set("shortName"sv, manifest.short_name.value_or(manifest.name));
        info.set("description"sv, manifest.description.value_or({}));
        info.set("version"sv, manifest.version);
        if (manifest.version_name.has_value())
            info.set("versionName"sv, *manifest.version_name);
        info.set("mayDisable"sv, true);
        info.set("enabled"sv, true);
        info.set("isApp"sv, false);
        info.set("type"sv, "extension"sv);
        info.set("offlineEnabled"sv, false);
        info.set("optionsUrl"sv, manifest.options_page.has_value() ? extension->url_for(*manifest.options_page).serialize() : String {});
        info.set("installType"sv, "development"sv);
        info.set("permissions"sv, string_array(extension->permissions()));
        info.set("hostPermissions"sv, string_array(manifest.host_permissions));
        return JsonValue { move(info) };
    }

    // FIXME: Report the system's idle and locked states.
    if (name == "idle.queryState"sv)
        return JsonValue { "active"sv };
    if (name == "idle.setDetectionInterval"sv)
        return JsonValue {};
    if (name == "idle.getAutoLockDelay"sv)
        return JsonValue { 0 };

    if (name.starts_with_bytes("offscreen."sv)) {
        if (!extension->has_permission("offscreen"sv))
            return "The 'offscreen' permission is required."_string;

        if (name == "offscreen.hasDocument"sv)
            return JsonValue { Registry::the().has_offscreen_document(*extension) };
        if (name == "offscreen.closeDocument"sv) {
            if (!Registry::the().has_offscreen_document(*extension))
                return "No current offscreen document."_string;
            Registry::the().close_offscreen_document(*extension);
            return JsonValue {};
        }
        if (name == "offscreen.createDocument"sv) {
            auto parameters = !args.is_empty() && args.at(0).is_object() ? args.at(0).as_object() : JsonObject {};
            auto url = parameters.get_string("url"sv);
            if (!url.has_value())
                return "offscreen.createDocument() needs a url."_string;
            if (Registry::the().has_offscreen_document(*extension))
                return "Only a single offscreen document may be created."_string;
            Registry::the().create_offscreen_document(*extension, extension->url_for(*url));
            return JsonValue {};
        }
    }

    if (name == "commands.getAll"sv) {
        JsonArray commands;
        for (auto const& command : extension->manifest().commands) {
            JsonObject json;
            json.set("name"sv, command.name);
            json.set("description"sv, command.description.value_or({}));
            json.set("shortcut"sv, command.suggested_key.value_or({}));
            commands.must_append(move(json));
        }
        return JsonValue { move(commands) };
    }

    return MUST(String::formatted("{} isn't supported yet.", name));
}

void Router::set_popup_size_listener(String const& extension_id, Function<void(int width, int height)> listener)
{
    if (listener)
        m_popup_size_listeners.set(extension_id, move(listener));
    else
        m_popup_size_listeners.remove(extension_id);
}

String Router::event_message(StringView name, JsonArray args)
{
    JsonObject message;
    message.set("type"sv, "event"sv);
    message.set("name"sv, name);
    message.set("args"sv, move(args));
    return message.serialized();
}

void Router::dispatch_event(Extension const& extension, StringView name, JsonArray args, Optional<ContextKind> kind)
{
    prune_closed_contexts();

    auto serialized = event_message(name, move(args));

    // The background gets the event once it has run its script, starting again for it if it stopped when idle.
    if (!kind.has_value() || *kind == ContextKind::Background) {
        wake_background(extension, name);
        if (auto starting = m_starting_backgrounds.get(extension.id()); starting.has_value()) {
            starting->waiting.append([this, id = extension.id(), serialized] {
                for (auto const& context : m_contexts) {
                    if (context.extension->id() == id && context.kind == ContextKind::Background)
                        post(context.endpoint, serialized);
                }
            });
        }
    }
    auto background_is_starting = m_starting_backgrounds.contains(extension.id());

    for (auto const& context : m_contexts) {
        if (context.extension->id() != extension.id())
            continue;
        if (kind.has_value() && context.kind != *kind)
            continue;
        if (context.kind == ContextKind::ExternalPage || (context.kind == ContextKind::Background && background_is_starting))
            continue;
        // Content scripts have chrome.storage's events alone, and learn of changed permissions for chrome.storage's sake.
        if (context.kind == ContextKind::ContentScript && !name.starts_with("storage."sv) && !name.starts_with("permissions."sv))
            continue;
        post(context.endpoint, serialized);
    }
}

void Router::dispatch_event_to_all(StringView name, JsonArray args)
{
    for (auto const& extension : Registry::the().extensions())
        dispatch_event(extension, name, args);
}

// --- runtime.sendMessage -----------------------------------------------------------------------------------------------

// runtime.sendMessage() and runtime.connect() reach the extension's pages and background, but not its content scripts
// or the web pages that message it, and never the sender itself.
Vector<WebView::ScriptBridgeEndpoint> Router::extension_receivers(Context const& sender) const
{
    Vector<WebView::ScriptBridgeEndpoint> receivers;
    for (auto const& context : m_contexts) {
        if (context.extension->id() != sender.extension->id() || context.endpoint == sender.endpoint)
            continue;
        if (context.kind == ContextKind::ContentScript || context.kind == ContextKind::ExternalPage)
            continue;
        receivers.append(context.endpoint);
    }
    return receivers;
}

// tabs.sendMessage() and tabs.connect() reach the extension's content scripts in the tab's frames, or in one of them.
Vector<WebView::ScriptBridgeEndpoint> Router::content_script_receivers(Extension const& extension, i64 tab_id, Optional<i64> wanted_frame_id, Optional<String const&> wanted_document_id)
{
    prune_closed_contexts();

    Vector<WebView::ScriptBridgeEndpoint> receivers;
    for (auto const& context : m_contexts) {
        if (context.extension->id() != extension.id() || context.kind != ContextKind::ContentScript)
            continue;
        auto view_id = context.endpoint.view_id();
        if (!view_id.has_value())
            continue;
        if (auto ids = Browser::tab_and_window_ids(*view_id); !ids.has_value() || ids->tab_id != tab_id)
            continue;
        if (wanted_frame_id.has_value() && frame_id(context.endpoint) != *wanted_frame_id)
            continue;
        if (wanted_document_id.has_value() && document_id_string(context.endpoint) != *wanted_document_id)
            continue;
        receivers.append(context.endpoint);
    }
    return receivers;
}

void Router::background_will_start(Extension const& extension)
{
    // NB: Its script registers its listeners again as it runs.
    m_background_listeners.remove(extension.id());
    start_background(extension, false);
}

void Router::start_background(Extension const& extension, bool is_restart)
{
    if (auto running = m_running_backgrounds.take(extension.id()); running.has_value())
        (*running)->stop();

    auto timeout = Core::Timer::create_single_shot(BACKGROUND_START_TIMEOUT, [this, id = extension.id()] { background_did_run(id); });
    timeout->start();
    m_starting_backgrounds.set(extension.id(), { .waiting = {}, .timeout = move(timeout), .is_restart = is_restart });
}

Optional<Vector<Function<void()>>&> Router::waiting_for_background(Context const& sender)
{
    if (sender.kind == ContextKind::Background)
        return {};
    auto starting = m_starting_backgrounds.get(sender.extension->id());
    if (!starting.has_value())
        return {};
    return starting->waiting;
}

void Router::background_did_run(String const& extension_id)
{
    auto starting = m_starting_backgrounds.take(extension_id);
    if (!starting.has_value())
        return;
    starting->timeout->stop();

    auto idle = Core::Timer::create_single_shot(m_background_idle_timeout, [this, extension_id] { stop_idle_background(extension_id); });
    idle->start();
    m_running_backgrounds.set(extension_id, move(idle));

    for (auto& action : starting->waiting)
        action();
}

// https://developer.chrome.com/docs/extensions/develop/concepts/service-workers/lifecycle#idle-shutdown
// Chrome starts a stopped service worker for the events it registered listeners for, and runtime messages and ports
// are events too.
void Router::wake_background(Extension const& extension, StringView event_name)
{
    if (m_starting_backgrounds.contains(extension.id()) || m_running_backgrounds.contains(extension.id()))
        return;
    auto listeners = m_background_listeners.get(extension.id());
    if (!listeners.has_value() || !listeners->contains(event_name))
        return;

    start_background(extension, true);
    if (!Registry::the().restart_background_page(extension)) {
        if (auto starting = m_starting_backgrounds.take(extension.id()); starting.has_value())
            starting->timeout->stop();
    }
}

// Each event, message, and call of a background's keeps it running for another while.
void Router::background_was_active(String const& extension_id)
{
    if (auto idle = m_running_backgrounds.get(extension_id); idle.has_value())
        (*idle)->restart();
}

// A background that owes a runtime message its response, or waits for one, isn't idle.
bool Router::background_is_busy(String const& extension_id) const
{
    auto is_background = [&](WebView::ScriptBridgeEndpoint const& endpoint) {
        return any_of(m_contexts, [&](Context const& context) {
            return context.kind == ContextKind::Background && context.endpoint == endpoint && context.extension->id() == extension_id;
        });
    };
    for (auto const& [id, pending] : m_pending_messages) {
        if (is_background(pending.sender) || any_of(pending.awaiting, is_background))
            return true;
    }
    // An open native messaging port keeps the background running, as Chrome keeps a service worker with one.
    for (auto const& [id, port] : m_ports) {
        if (port.native && is_background(port.opener))
            return true;
    }
    return false;
}

// A background that stopped takes its page's processes with it. Its ports close, as they do when Chrome stops a service
// worker, and its offscreen document stays open.
void Router::stop_idle_background(String const& extension_id)
{
    auto idle = m_running_backgrounds.get(extension_id);
    if (!idle.has_value())
        return;
    if (background_is_busy(extension_id)) {
        (*idle)->restart();
        return;
    }
    m_running_backgrounds.remove(extension_id);

    Vector<WebView::ScriptBridgeEndpoint> endpoints;
    m_contexts.remove_all_matching([&](Context const& context) {
        if (context.kind != ContextKind::Background || context.extension->id() != extension_id)
            return false;
        endpoints.append(context.endpoint);
        return true;
    });
    for (auto const& endpoint : endpoints)
        forget_endpoint(endpoint);

    if (auto extension = Registry::the().extension(extension_id))
        Registry::the().stop_idle_background_page(*extension);
}

void Router::post(WebView::ScriptBridgeEndpoint const& endpoint, String const& message)
{
    endpoint.post_message(message);
    for (auto const& context : m_contexts) {
        if (context.kind == ContextKind::Background && context.endpoint == endpoint) {
            background_was_active(context.extension->id());
            return;
        }
    }
}

// https://developer.chrome.com/docs/extensions/reference/api/runtime#method-sendMessage
// The message goes to each receiver, and the first answer is the response.
void Router::send_runtime_message(Context& sender, u64 call_id, JsonValue message, Vector<WebView::ScriptBridgeEndpoint> receivers)
{
    prune_closed_contexts();

    PendingMessage pending { .sender = sender.endpoint, .call_id = call_id, .awaiting = {} };
    auto id = m_next_message_id++;

    JsonObject delivery;
    delivery.set("type"sv, "message"sv);
    delivery.set("id"sv, id);
    delivery.set("message"sv, move(message));
    delivery.set("sender"sv, sender_for(sender));
    // A web page's messages go to runtime.onMessageExternal.
    delivery.set("external"sv, sender.kind == ContextKind::ExternalPage);
    auto serialized = delivery.serialized();

    for (auto const& receiver : receivers) {
        if (!receiver.is_open())
            continue;
        pending.awaiting.append(receiver);
        post(receiver, serialized);
    }

    if (pending.awaiting.is_empty()) {
        reply_with_error(sender.endpoint, call_id, NO_RECEIVER);
        return;
    }
    m_pending_messages.set(id, move(pending));
}

// https://developer.chrome.com/docs/extensions/reference/api/tabs#method-sendMessage
void Router::send_tab_message(Context& sender, u64 call_id, JsonArray const& args)
{
    if (args.is_empty() || !args.at(0).is_integer<i64>()) {
        reply_with_error(sender.endpoint, call_id, "tabs.sendMessage() needs a tab ID."sv);
        return;
    }
    auto tab_id = args.at(0).as_integer<i64>();
    auto message = args.size() > 1 ? args.at(1) : JsonValue {};
    auto options = args.size() > 2 && args.at(2).is_object() ? args.at(2).as_object() : JsonObject {};

    auto receivers = content_script_receivers(*sender.extension, tab_id, options.get_integer<i64>("frameId"sv), options.get_string("documentId"sv));
    send_runtime_message(sender, call_id, move(message), move(receivers));
}

void Router::handle_response(Context& responder, u64 id, bool handled, bool listening, JsonValue response)
{
    auto pending = m_pending_messages.get(id);
    if (!pending.has_value())
        return;

    auto awaited = pending->awaiting.remove_all_matching([&](auto const& endpoint) { return endpoint == responder.endpoint; });
    if (!awaited)
        return;
    pending->any_listening |= listening;

    if (handled)
        finish_pending_message(id, move(response));
    else if (pending->awaiting.is_empty())
        finish_pending_message(id, {});
}

void Router::finish_pending_message(u64 id, Optional<JsonValue> response)
{
    auto pending = m_pending_messages.take(id);
    if (!pending.has_value())
        return;

    // As in Chrome, a message that no context had a listener for never connected.
    if (!response.has_value() && !pending->any_listening) {
        reply_with_error(pending->sender, pending->call_id, NO_RECEIVER);
        return;
    }

    JsonObject result;
    result.set("responded"sv, response.has_value());
    result.set("response"sv, response.value_or(JsonValue {}));
    reply(pending->sender, pending->call_id, move(result));
}

// --- runtime.connect ---------------------------------------------------------------------------------------------------

// https://developer.chrome.com/docs/extensions/develop/concepts/messaging#connect
// A port reaches each receiver with a runtime.onConnect listener.
void Router::open_port(Context& opener, String const& port_id, String const& name, Vector<WebView::ScriptBridgeEndpoint> receivers)
{
    prune_closed_contexts();
    if (m_ports.contains(port_id))
        return;

    Port port { .extension_id = opener.extension->id(), .opener = opener.endpoint, .awaiting = {}, .receivers = {}, .early_messages = {} };

    JsonObject connect;
    connect.set("type"sv, "connect"sv);
    connect.set("port"sv, port_id);
    connect.set("name"sv, name);
    connect.set("sender"sv, sender_for(opener));
    // A web page's ports go to runtime.onConnectExternal.
    connect.set("external"sv, opener.kind == ContextKind::ExternalPage);
    auto serialized = connect.serialized();

    for (auto const& receiver : receivers) {
        if (!receiver.is_open())
            continue;
        port.awaiting.append(receiver);
        post(receiver, serialized);
    }

    if (port.awaiting.is_empty()) {
        JsonObject disconnect;
        disconnect.set("type"sv, "port-disconnect"sv);
        disconnect.set("port"sv, port_id);
        disconnect.set("error"sv, NO_RECEIVER);
        post(opener.endpoint, disconnect.serialized());
        return;
    }
    m_ports.set(port_id, move(port));
}

void Router::handle_port_accepted(Context& receiver, String const& port_id, bool accepted)
{
    auto port = m_ports.get(port_id);
    if (!port.has_value() || !port->awaiting.remove_first_matching([&](auto const& endpoint) { return endpoint == receiver.endpoint; }))
        return;

    if (accepted) {
        port->receivers.append(receiver.endpoint);
        for (auto const& message : port->early_messages)
            post(receiver.endpoint, message);
    }
    if (port->awaiting.is_empty())
        port->early_messages.clear();

    // A port nothing accepted closes, as one to an extension with no listeners does in Chrome.
    if (port->awaiting.is_empty() && port->receivers.is_empty()) {
        auto opener = port->opener;
        m_ports.remove(port_id);

        JsonObject disconnect;
        disconnect.set("type"sv, "port-disconnect"sv);
        disconnect.set("port"sv, port_id);
        disconnect.set("error"sv, NO_RECEIVER);
        post(opener, disconnect.serialized());
    }
}

void Router::open_native_port(Context& opener, String const& port_id, String const& application)
{
    prune_closed_contexts();
    if (m_ports.contains(port_id))
        return;

    auto disconnect = [&](StringView error) {
        JsonObject message;
        message.set("type"sv, "port-disconnect"sv);
        message.set("port"sv, port_id);
        message.set("error"sv, error);
        post(opener.endpoint, message.serialized());
    };

    if (opener.kind == ContextKind::ContentScript || opener.kind == ContextKind::ExternalPage) {
        disconnect(NATIVE_HOST_FORBIDDEN);
        return;
    }
    if (!opener.extension->has_permission("nativeMessaging"sv)) {
        disconnect(NATIVE_HOST_FORBIDDEN);
        return;
    }

    auto native = NativeHostProcess::start(
        application,
        opener.extension->id(),
        [this, port_id](JsonValue message) {
            auto port = m_ports.get(port_id);
            if (!port.has_value())
                return;
            JsonObject delivery;
            delivery.set("type"sv, "port-message"sv);
            delivery.set("port"sv, port_id);
            delivery.set("message"sv, move(message));
            post(port->opener, delivery.serialized());
        },
        [this, port_id](Optional<String> error) {
            auto port = m_ports.get(port_id);
            if (!port.has_value())
                return;
            auto opener_endpoint = port->opener;
            port->native = nullptr;
            m_ports.remove(port_id);
            JsonObject disconnect;
            disconnect.set("type"sv, "port-disconnect"sv);
            disconnect.set("port"sv, port_id);
            if (error.has_value())
                disconnect.set("error"sv, *error);
            post(opener_endpoint, disconnect.serialized());
        });
    if (native.is_error()) {
        disconnect(native.error());
        return;
    }

    m_ports.set(port_id, Port {
                             .extension_id = opener.extension->id(),
                             .opener = opener.endpoint,
                             .awaiting = {},
                             .receivers = {},
                             .early_messages = {},
                             .native = native.release_value(),
                         });
}

void Router::handle_port_message(Context& sender, String const& port_id, JsonValue message)
{
    auto port = m_ports.get(port_id);
    if (!port.has_value())
        return;

    if (port->native) {
        if (sender.endpoint == port->opener)
            port->native->post_message(message);
        return;
    }

    JsonObject delivery;
    delivery.set("type"sv, "port-message"sv);
    delivery.set("port"sv, port_id);
    delivery.set("message"sv, move(message));
    auto serialized = delivery.serialized();

    if (sender.endpoint == port->opener) {
        for (auto const& receiver : port->receivers)
            post(receiver, serialized);
        if (!port->awaiting.is_empty())
            port->early_messages.append(serialized);
    } else if (port->receivers.contains_slow(sender.endpoint)) {
        post(port->opener, serialized);
    }
}

void Router::handle_port_disconnect(Context& context, String const& port_id)
{
    close_port_end(context.endpoint, port_id);
}

// One end of a port closed, or went away. The opener closing closes it for everyone; a receiver closing closes it for
// the opener once no receiver is left.
void Router::close_port_end(WebView::ScriptBridgeEndpoint const& endpoint, String const& port_id, Optional<StringView> error)
{
    auto port = m_ports.get(port_id);
    if (!port.has_value())
        return;

    auto disconnect = [&](WebView::ScriptBridgeEndpoint const& other) {
        JsonObject message;
        message.set("type"sv, "port-disconnect"sv);
        message.set("port"sv, port_id);
        if (error.has_value())
            message.set("error"sv, *error);
        post(other, message.serialized());
    };

    if (endpoint == port->opener) {
        if (port->native)
            port->native->disconnect();
        for (auto const& receiver : port->receivers)
            disconnect(receiver);
        m_ports.remove(port_id);
        return;
    }

    port->awaiting.remove_all_matching([&](auto const& other) { return other == endpoint; });
    port->receivers.remove_all_matching([&](auto const& other) { return other == endpoint; });
    if (port->receivers.is_empty() && port->awaiting.is_empty()) {
        disconnect(port->opener);
        m_ports.remove(port_id);
    }
}

}
