/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/NeverDestroyed.h>
#include <AK/NonnullRefPtr.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibCore/Timer.h>
#include <LibWebView/URLSchemeHandler.h>
#include <UI/Extensions/Alarms.h>
#include <UI/Extensions/Extension.h>

namespace Ladybird::Extensions {

// The kinds of extension contexts, as runtime.getContexts() reports them.
enum class ContextKind : u8 {
    Tab,
    Popup,
    Background,
    Offscreen,
};

// Carries chrome.* between an extension's contexts (its pages, and its background worker through its background page)
// and the UI process. Each context talks to the router over its script bridge (see UI/Resources/extensions/), and the
// router knows from the bridge, not from the message, which extension and context sent each call. It answers calls,
// delivers runtime messages and ports between an extension's contexts, and sends them events.
class Router {
    AK_MAKE_NONCOPYABLE(Router);

public:
    static Router& the();

    // The script that installs chrome.* in an extension page, or that starts the background worker in its page, before
    // the page's own scripts.
    Optional<String> bootstrap_script(WebView::URLSchemeBridgeEndpoint const&);
    // The script that installs chrome.* in the extension's background worker.
    String background_worker_script(Extension const&);
    void on_message(WebView::URLSchemeBridgeEndpoint const&, String const& message);

    // Sends an event to the extension's open contexts, or only to those of one kind.
    void dispatch_event(Extension const&, StringView name, JsonArray args, Optional<ContextKind> = {});
    // Sends an event to every extension, as tabs and windows events go to all of them.
    void dispatch_event_to_all(StringView name, JsonArray args);

    // The open popup of an extension follows the size its page reports.
    void set_popup_size_listener(String const& extension_id, Function<void(int width, int height)>);

    // Closes the extension's contexts' ports and pending messages, and cancels its alarms, as it goes away.
    void forget_extension(Extension const&);

    // A context that may have gone away, such as a popup that closed.
    void prune_closed_contexts();

private:
    friend class AK::NeverDestroyed<Router>;
    Router();

    struct Context {
        NonnullRefPtr<Extension> extension;
        WebView::URLSchemeBridgeEndpoint endpoint;
        ContextKind kind { ContextKind::Tab };
    };

    struct PendingMessage {
        WebView::URLSchemeBridgeEndpoint sender;
        u64 call_id { 0 };
        Vector<WebView::URLSchemeBridgeEndpoint> awaiting;
    };

    // A runtime.connect() port. The opener talks to every context that accepted; each of those talks to the opener.
    struct Port {
        String extension_id;
        WebView::URLSchemeBridgeEndpoint opener;
        Vector<WebView::URLSchemeBridgeEndpoint> awaiting;
        Vector<WebView::URLSchemeBridgeEndpoint> receivers;
        // What the opener posted before every context answered, for each one that accepts.
        Vector<String> early_messages;
    };

    Optional<Context&> context_for(WebView::URLSchemeBridgeEndpoint const&);
    JsonObject config_for(Extension const&, ContextKind, URL::URL const& url);
    JsonObject sender_for(Context const&) const;
    URL::URL context_url(Context const&) const;

    void handle_call(Context&, u64 id, String const& name, JsonArray const& args);
    ErrorOr<JsonValue, String> handle_storage_call(Context&, String const& name, JsonArray const& args);
    ErrorOr<JsonValue, String> handle_alarms_call(Context&, String const& name, JsonArray const& args);
    ErrorOr<JsonValue, String> handle_permissions_call(Context&, String const& name, JsonArray const& args);
    void request_permissions(Context&, u64 call_id, JsonArray const& args);
    void dispatch_permissions_change(StringView extension_id, StringView event_name, Vector<String> const& permissions);
    ErrorOr<JsonValue, String> handle_privacy_call(Context&, String const& name, JsonArray const& args);
    ErrorOr<JsonValue, String> handle_runtime_call(Context&, String const& name, JsonArray const& args);
    JsonArray contexts_of(Extension const&) const;

    void send_runtime_message(Context&, u64 call_id, JsonValue message);
    void handle_response(Context&, u64 message_id, bool handled, JsonValue response);
    void finish_pending_message(u64 message_id, Optional<JsonValue> response);

    void open_port(Context&, String const& port_id, String const& name);
    void handle_port_accepted(Context&, String const& port_id, bool accepted);
    void handle_port_message(Context&, String const& port_id, JsonValue message);
    void handle_port_disconnect(Context&, String const& port_id);
    void close_port_end(WebView::URLSchemeBridgeEndpoint const&, String const& port_id, Optional<StringView> error = {});
    void forget_endpoint(WebView::URLSchemeBridgeEndpoint const&);

    static void reply(WebView::URLSchemeBridgeEndpoint const&, u64 id, JsonValue result);
    static void reply_with_error(WebView::URLSchemeBridgeEndpoint const&, u64 id, StringView error);

    Vector<Context> m_contexts;
    HashMap<u64, PendingMessage> m_pending_messages;
    u64 m_next_message_id { 1 };
    HashMap<String, Port> m_ports;
    HashMap<String, Function<void(int, int)>> m_popup_size_listeners;
    HashMap<String, NonnullOwnPtr<Alarms>> m_alarms;
    // Pages say when they go away, but not always: a crashed process doesn't.
    RefPtr<Core::Timer> m_prune_timer;
};

StringView context_kind_name(ContextKind);

}
