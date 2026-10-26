/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonObject.h>
#include <AK/StringBuilder.h>
#include <LibWebView/Application.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>

namespace Ladybird::Extensions {

// UI/Resources/extensions/chrome-api.js, which the build embeds.
extern String const& chrome_api_source;

Router& Router::the()
{
    static NeverDestroyed<Router> router;
    return *router;
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

static JsonArray accept_languages()
{
    JsonArray languages;
    for (auto const& language : WebView::Application::settings().languages())
        languages.must_append(language);
    return languages;
}

static JsonArray string_array(Vector<String> const& strings)
{
    JsonArray array;
    for (auto const& string : strings)
        array.must_append(string);
    return array;
}

Optional<String> Router::bootstrap_script(WebView::URLSchemeBridgeEndpoint const& endpoint)
{
    auto extension = Registry::the().extension_for_url(endpoint.url());
    if (!extension)
        return {};

    prune_closed_contexts();
    m_contexts.append({ *extension, endpoint });

    JsonObject config;
    config.set("id"sv, extension->id());
    config.set("manifest"sv, extension->manifest().json);
    config.set("messages"sv, extension->locales().messages());
    config.set("uiLanguage"sv, extension->locales().ui_language());
    config.set("acceptLanguages"sv, accept_languages());
    config.set("platform"sv, platform_info());
    config.set("permissions"sv, string_array(extension->manifest().permissions));

    StringBuilder script;
    script.appendff("const config = {};\n", config.serialized());
    script.append(chrome_api_source);
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

void Router::prune_closed_contexts()
{
    Vector<WebView::URLSchemeBridgeEndpoint> closed;
    m_contexts.remove_all_matching([&](Context const& context) {
        if (context.endpoint.is_open())
            return false;
        closed.append(context.endpoint);
        return true;
    });

    // A page that goes away without answering a runtime message doesn't answer it.
    for (auto const& endpoint : closed) {
        Vector<MessageId> finished;
        for (auto& [id, pending] : m_pending_messages) {
            pending.awaiting.remove_all_matching([&](auto const& awaiting) { return awaiting == endpoint; });
            if (pending.awaiting.is_empty())
                finished.append(id);
        }
        for (auto id : finished)
            finish_pending_message(id, {});
    }
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

    if (*type == "call"sv) {
        auto id = message.get_u64("id"sv);
        auto name = message.get_string("name"sv);
        auto args = message.get_array("args"sv);
        if (id.has_value() && name.has_value())
            handle_call(*context, *id, *name, args.has_value() ? *args : JsonArray {});
    } else if (*type == "response"sv) {
        auto id = message.get_u64("id"sv);
        if (id.has_value())
            handle_response(*context, *id, message.get_bool("handled"sv).value_or(false), message.get("response"sv).value_or(JsonValue {}));
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
    if (name.starts_with_bytes("storage."sv)) {
        auto result = handle_storage_call(context, name, args);
        if (result.is_error())
            reply_with_error(context.endpoint, id, result.error());
        else
            reply(context.endpoint, id, result.release_value());
        return;
    }

    if (name == "runtime.sendMessage"sv) {
        send_runtime_message(context, id, args.is_empty() ? JsonValue {} : args.at(0));
        return;
    }

    reply_with_error(context.endpoint, id, MUST(String::formatted("{} isn't supported yet.", name)));
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

void Router::dispatch_event(Extension const& extension, StringView name, JsonArray args)
{
    prune_closed_contexts();

    JsonObject message;
    message.set("type"sv, "event"sv);
    message.set("name"sv, name);
    message.set("args"sv, move(args));
    auto serialized = message.serialized();

    for (auto const& context : m_contexts) {
        if (context.extension->id() == extension.id())
            context.endpoint.post_message(serialized);
    }
}

// https://developer.chrome.com/docs/extensions/reference/api/runtime#method-sendMessage
// The message goes to every other page of the extension, and the first answer is the response.
void Router::send_runtime_message(Context& sender, u64 call_id, JsonValue message)
{
    prune_closed_contexts();

    auto const& url = sender.endpoint.url();
    JsonObject sender_info;
    sender_info.set("id"sv, sender.extension->id());
    sender_info.set("url"sv, url.serialize());
    sender_info.set("origin"sv, sender.endpoint.origin().serialize());

    PendingMessage pending { .sender = sender.endpoint, .call_id = call_id, .awaiting = {} };
    auto id = m_next_message_id++;

    JsonObject delivery;
    delivery.set("type"sv, "message"sv);
    delivery.set("id"sv, id);
    delivery.set("message"sv, move(message));
    delivery.set("sender"sv, move(sender_info));
    auto serialized = delivery.serialized();

    for (auto const& context : m_contexts) {
        if (context.extension->id() != sender.extension->id() || context.endpoint == sender.endpoint)
            continue;
        pending.awaiting.append(context.endpoint);
        context.endpoint.post_message(serialized);
    }

    if (pending.awaiting.is_empty()) {
        reply_with_error(sender.endpoint, call_id, "Could not establish connection. Receiving end does not exist."sv);
        return;
    }
    m_pending_messages.set(id, move(pending));
}

void Router::handle_response(Context& responder, MessageId id, bool handled, JsonValue response)
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

void Router::finish_pending_message(MessageId id, Optional<JsonValue> response)
{
    auto pending = m_pending_messages.take(id);
    if (!pending.has_value())
        return;

    JsonObject result;
    result.set("responded"sv, response.has_value());
    result.set("response"sv, response.value_or(JsonValue {}));
    reply(pending->sender, pending->call_id, move(result));
}

}
