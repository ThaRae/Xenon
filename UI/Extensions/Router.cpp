/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <LibWebView/Application.h>
#include <UI/Extensions/Browser.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>

namespace Ladybird::Extensions {

// UI/Resources/extensions/*.js, which the build embeds.
extern String const& chrome_api_source;
extern String const& background_page_source;
extern String const& worker_bridge_source;

static constexpr auto NO_RECEIVER = "Could not establish connection. Receiving end does not exist."sv;
static constexpr auto PRUNE_INTERVAL = AK::Duration::from_seconds(5);

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
    }
    VERIFY_NOT_REACHED();
}

Router& Router::the()
{
    static NeverDestroyed<Router> router;
    return *router;
}

Router::Router()
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

// Chrome's document IDs and context IDs are opaque strings.
static String document_id_string(WebView::URLSchemeBridgeEndpoint const& endpoint)
{
    auto id = endpoint.document_id();
    return MUST(String::formatted("{:x}-{:x}", id.namespace_id, id.local_id));
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
    config.set("permissions"sv, string_array(extension.manifest().permissions));
    config.set("contextType"sv, context_kind_name(kind));
    config.set("isBackground"sv, kind == ContextKind::Background);
    config.set("url"sv, url.serialize());

    // NB: A tab can show the popup's page too, as Bitwarden's popout does; only the popup itself reports its size.
    config.set("isPopup"sv, kind == ContextKind::Popup);
    return config;
}

Optional<String> Router::bootstrap_script(WebView::URLSchemeBridgeEndpoint const& endpoint)
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

Optional<Router::Context&> Router::context_for(WebView::URLSchemeBridgeEndpoint const& endpoint)
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

JsonObject Router::sender_for(Context const& context) const
{
    JsonObject sender;
    sender.set("id"sv, context.extension->id());
    sender.set("url"sv, context_url(context).serialize());
    sender.set("origin"sv, context.endpoint.origin().serialize());
    if (context.kind != ContextKind::Background)
        sender.set("documentId"sv, document_id_string(context.endpoint));
    if (auto view_id = context.endpoint.view_id(); view_id.has_value() && context.kind == ContextKind::Tab) {
        if (auto tab = Browser::tab_json(*view_id, *context.extension); tab.has_value()) {
            sender.set("tab"sv, tab.release_value());
            sender.set("frameId"sv, 0);
        }
    }
    return sender;
}

void Router::prune_closed_contexts()
{
    Vector<WebView::URLSchemeBridgeEndpoint> closed;
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
void Router::forget_endpoint(WebView::URLSchemeBridgeEndpoint const& endpoint)
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
    Vector<WebView::URLSchemeBridgeEndpoint> endpoints;
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
}

void Router::on_message(WebView::URLSchemeBridgeEndpoint const& endpoint, String const& text)
{
    auto context = context_for(endpoint);
    if (!context.has_value())
        return;

    auto json = JsonValue::from_string(text);
    if (json.is_error() || !json.value().is_object())
        return;
    auto const& message = json.value().as_object();
    auto type = message.get_string("type"sv);
    if (!type.has_value())
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
            handle_response(*context, *id, message.get_bool("handled"sv).value_or(false), message.get("response"sv).value_or(JsonValue {}));
    } else if (*type == "connect"sv && port_id.has_value()) {
        open_port(*context, *port_id, message.get_string("name"sv).value_or({}));
    } else if (*type == "connected"sv && port_id.has_value()) {
        handle_port_accepted(*context, *port_id, message.get_bool("accepted"sv).value_or(false));
    } else if (*type == "port-message"sv && port_id.has_value()) {
        handle_port_message(*context, *port_id, message.get("message"sv).value_or(JsonValue {}));
    } else if (*type == "port-disconnect"sv && port_id.has_value()) {
        handle_port_disconnect(*context, *port_id);
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

void Router::reply(WebView::URLSchemeBridgeEndpoint const& endpoint, u64 id, JsonValue result)
{
    JsonObject message;
    message.set("type"sv, "reply"sv);
    message.set("id"sv, id);
    message.set("result"sv, move(result));
    endpoint.post_message(message.serialized());
}

void Router::reply_with_error(WebView::URLSchemeBridgeEndpoint const& endpoint, u64 id, StringView error)
{
    JsonObject message;
    message.set("type"sv, "reply"sv);
    message.set("id"sv, id);
    message.set("error"sv, error);
    endpoint.post_message(message.serialized());
}

void Router::handle_call(Context& context, u64 id, String const& name, JsonArray const& args)
{
    if (name == "runtime.sendMessage"sv) {
        send_runtime_message(context, id, args.is_empty() ? JsonValue {} : args.at(0));
        return;
    }

    ErrorOr<JsonValue, String> result = MUST(String::formatted("{} isn't supported yet.", name));
    if (name.starts_with_bytes("storage."sv))
        result = handle_storage_call(context, name, args);
    else if (name.starts_with_bytes("alarms."sv))
        result = handle_alarms_call(context, name, args);
    else if (name.starts_with_bytes("permissions."sv))
        result = handle_permissions_call(context, name, args);
    else if (name.starts_with_bytes("runtime."sv) || name.starts_with_bytes("management."sv) || name.starts_with_bytes("idle."sv) || name.starts_with_bytes("offscreen."sv) || name.starts_with_bytes("commands."sv))
        result = handle_runtime_call(context, name, args);
    else if (name.starts_with_bytes("tabs."sv) || name.starts_with_bytes("windows."sv) || name.starts_with_bytes("action."sv) || name.starts_with_bytes("webNavigation."sv))
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
    if (!extension.manifest().has_permission("storage"sv))
        return "The 'storage' permission is required."_string;

    // FIXME: Honor storage.session's access level once content scripts can reach storage.
    if (name == "storage.setAccessLevel"sv)
        return JsonValue {};

    if (args.is_empty() || !args.at(0).is_string())
        return "Missing storage area."_string;
    auto const& area_name = args.at(0).as_string();

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
    if (!extension.manifest().has_permission("alarms"sv))
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

// Unpacked extensions get every permission they ask for when they load, as in Chrome, and optional permissions only
// when granted.
ErrorOr<JsonValue, String> Router::handle_permissions_call(Context& context, String const& name, JsonArray const& args)
{
    auto const& manifest = context.extension->manifest();

    auto granted = [&] {
        JsonObject all;
        all.set("permissions"sv, string_array(manifest.permissions));
        all.set("origins"sv, string_array(manifest.host_permissions));
        return all;
    };

    if (name == "permissions.getAll"sv)
        return JsonValue { granted() };

    auto request = !args.is_empty() && args.at(0).is_object() ? args.at(0).as_object() : JsonObject {};
    auto requested = [&](StringView key) {
        Vector<String> list;
        if (auto values = request.get_array(key); values.has_value()) {
            for (auto const& value : values->values()) {
                if (value.is_string())
                    list.append(value.as_string());
            }
        }
        return list;
    };

    if (name == "permissions.contains"sv) {
        for (auto const& permission : requested("permissions"sv)) {
            if (!manifest.permissions.contains_slow(permission))
                return JsonValue { false };
        }
        for (auto const& origin : requested("origins"sv)) {
            if (!manifest.host_permissions.contains_slow(origin) && !manifest.host_permissions.contains_slow("<all_urls>"_string))
                return JsonValue { false };
        }
        return JsonValue { true };
    }

    // FIXME: Ask the user for optional permissions (milestone 4).
    if (name == "permissions.request"sv)
        return JsonValue { false };
    if (name == "permissions.remove"sv)
        return JsonValue { false };

    return MUST(String::formatted("{} isn't supported.", name));
}

JsonArray Router::contexts_of(Extension const& extension) const
{
    JsonArray contexts;
    for (auto const& context : m_contexts) {
        if (context.extension->id() != extension.id())
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

        JsonObject details;
        if (auto reason = Registry::the().take_install_reason(extension->id()); reason.has_value()) {
            details.set("reason"sv, reason->reason);
            if (reason->previous_version.has_value())
                details.set("previousVersion"sv, *reason->previous_version);
            JsonArray event_args;
            event_args.must_append(move(details));
            dispatch_event(*extension, "runtime.onInstalled"sv, move(event_args), ContextKind::Background);
        } else {
            dispatch_event(*extension, "runtime.onStartup"sv, {}, ContextKind::Background);
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
        info.set("permissions"sv, string_array(manifest.permissions));
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
        if (!extension->manifest().has_permission("offscreen"sv))
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

void Router::dispatch_event(Extension const& extension, StringView name, JsonArray args, Optional<ContextKind> kind)
{
    prune_closed_contexts();

    JsonObject message;
    message.set("type"sv, "event"sv);
    message.set("name"sv, name);
    message.set("args"sv, move(args));
    auto serialized = message.serialized();

    for (auto const& context : m_contexts) {
        if (context.extension->id() != extension.id())
            continue;
        if (kind.has_value() && context.kind != *kind)
            continue;
        context.endpoint.post_message(serialized);
    }
}

void Router::dispatch_event_to_all(StringView name, JsonArray args)
{
    for (auto const& extension : Registry::the().extensions())
        dispatch_event(extension, name, args);
}

// --- runtime.sendMessage -----------------------------------------------------------------------------------------------

// https://developer.chrome.com/docs/extensions/reference/api/runtime#method-sendMessage
// The message goes to every other context of the extension, and the first answer is the response.
void Router::send_runtime_message(Context& sender, u64 call_id, JsonValue message)
{
    prune_closed_contexts();

    PendingMessage pending { .sender = sender.endpoint, .call_id = call_id, .awaiting = {} };
    auto id = m_next_message_id++;

    JsonObject delivery;
    delivery.set("type"sv, "message"sv);
    delivery.set("id"sv, id);
    delivery.set("message"sv, move(message));
    delivery.set("sender"sv, sender_for(sender));
    auto serialized = delivery.serialized();

    for (auto const& context : m_contexts) {
        if (context.extension->id() != sender.extension->id() || context.endpoint == sender.endpoint)
            continue;
        pending.awaiting.append(context.endpoint);
        context.endpoint.post_message(serialized);
    }

    if (pending.awaiting.is_empty()) {
        reply_with_error(sender.endpoint, call_id, NO_RECEIVER);
        return;
    }
    m_pending_messages.set(id, move(pending));
}

void Router::handle_response(Context& responder, u64 id, bool handled, JsonValue response)
{
    auto pending = m_pending_messages.get(id);
    if (!pending.has_value())
        return;

    auto awaited = pending->awaiting.remove_all_matching([&](auto const& endpoint) { return endpoint == responder.endpoint; });
    if (!awaited)
        return;

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

    JsonObject result;
    result.set("responded"sv, response.has_value());
    result.set("response"sv, response.value_or(JsonValue {}));
    reply(pending->sender, pending->call_id, move(result));
}

// --- runtime.connect ---------------------------------------------------------------------------------------------------

// https://developer.chrome.com/docs/extensions/develop/concepts/messaging#connect
// A port reaches every other context of the extension with a runtime.onConnect listener.
void Router::open_port(Context& opener, String const& port_id, String const& name)
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
    auto serialized = connect.serialized();

    for (auto const& context : m_contexts) {
        if (context.extension->id() != opener.extension->id() || context.endpoint == opener.endpoint)
            continue;
        port.awaiting.append(context.endpoint);
        context.endpoint.post_message(serialized);
    }

    if (port.awaiting.is_empty()) {
        JsonObject disconnect;
        disconnect.set("type"sv, "port-disconnect"sv);
        disconnect.set("port"sv, port_id);
        disconnect.set("error"sv, NO_RECEIVER);
        opener.endpoint.post_message(disconnect.serialized());
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
            receiver.endpoint.post_message(message);
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
        opener.post_message(disconnect.serialized());
    }
}

void Router::handle_port_message(Context& sender, String const& port_id, JsonValue message)
{
    auto port = m_ports.get(port_id);
    if (!port.has_value())
        return;

    JsonObject delivery;
    delivery.set("type"sv, "port-message"sv);
    delivery.set("port"sv, port_id);
    delivery.set("message"sv, move(message));
    auto serialized = delivery.serialized();

    if (sender.endpoint == port->opener) {
        for (auto const& receiver : port->receivers)
            receiver.post_message(serialized);
        if (!port->awaiting.is_empty())
            port->early_messages.append(serialized);
    } else if (port->receivers.contains_slow(sender.endpoint)) {
        port->opener.post_message(serialized);
    }
}

void Router::handle_port_disconnect(Context& context, String const& port_id)
{
    close_port_end(context.endpoint, port_id);
}

// One end of a port closed, or went away. The opener closing closes it for everyone; a receiver closing closes it for
// the opener once no receiver is left.
void Router::close_port_end(WebView::URLSchemeBridgeEndpoint const& endpoint, String const& port_id, Optional<StringView> error)
{
    auto port = m_ports.get(port_id);
    if (!port.has_value())
        return;

    auto disconnect = [&](WebView::URLSchemeBridgeEndpoint const& other) {
        JsonObject message;
        message.set("type"sv, "port-disconnect"sv);
        message.set("port"sv, port_id);
        if (error.has_value())
            message.set("error"sv, *error);
        other.post_message(message.serialized());
    };

    if (endpoint == port->opener) {
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
