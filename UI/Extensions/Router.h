/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/JsonArray.h>
#include <AK/JsonValue.h>
#include <AK/NeverDestroyed.h>
#include <AK/NonnullRefPtr.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibWebView/URLSchemeHandler.h>
#include <UI/Extensions/Extension.h>

namespace Ladybird::Extensions {

// Carries chrome.* between extension pages and the UI process. Each page talks to the router over its script bridge
// (see UI/Resources/extensions/chrome-api.js), and the router knows from the bridge, not from the message, which
// extension and page sent each call. It answers calls, delivers runtime messages between an extension's pages, and sends
// them events.
class Router {
    AK_MAKE_NONCOPYABLE(Router);

public:
    static Router& the();

    // The script that installs chrome.* in an extension page, before the page's own scripts.
    Optional<String> bootstrap_script(WebView::URLSchemeBridgeEndpoint const&);
    void on_message(WebView::URLSchemeBridgeEndpoint const&, String const& message);

    // Sends an event to every open page of the extension.
    void dispatch_event(Extension const&, StringView name, JsonArray args);

    // The ID of a runtime message travelling to an extension's pages, and of each delivery of it.
    using MessageId = u64;

private:
    friend class AK::NeverDestroyed<Router>;
    Router() = default;

    struct Context {
        NonnullRefPtr<Extension> extension;
        WebView::URLSchemeBridgeEndpoint endpoint;
    };

    struct PendingMessage {
        WebView::URLSchemeBridgeEndpoint sender;
        u64 call_id { 0 };
        Vector<WebView::URLSchemeBridgeEndpoint> awaiting;
    };

    Optional<Context&> context_for(WebView::URLSchemeBridgeEndpoint const&);
    void prune_closed_contexts();

    void handle_call(Context&, u64 id, String const& name, JsonArray const& args);
    ErrorOr<JsonValue, String> handle_storage_call(Context&, String const& name, JsonArray const& args);
    void send_runtime_message(Context&, u64 call_id, JsonValue message);
    void handle_response(Context&, MessageId, bool handled, JsonValue response);
    void finish_pending_message(MessageId, Optional<JsonValue> response);

    static void reply(WebView::URLSchemeBridgeEndpoint const&, u64 id, JsonValue result);
    static void reply_with_error(WebView::URLSchemeBridgeEndpoint const&, u64 id, StringView error);

    Vector<Context> m_contexts;
    HashMap<MessageId, PendingMessage> m_pending_messages;
    MessageId m_next_message_id { 1 };
};

}
