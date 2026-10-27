/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/HashTable.h>
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

// The kinds of extension contexts, as runtime.getContexts() reports them, and content scripts, which it doesn't.
enum class ContextKind : u8 {
    Tab,
    Popup,
    Background,
    Offscreen,
    ContentScript,
};

// Carries chrome.* between an extension's contexts (its pages, its background worker through its background page, and
// its content scripts' isolated worlds) and the UI process. Each context talks to the router over its script bridge
// (see UI/Resources/extensions/), and the router knows from the bridge, not from the message, which extension, context,
// and frame sent each call. It answers calls, delivers runtime messages and ports between an extension's contexts, and
// sends them events.
class Router {
    AK_MAKE_NONCOPYABLE(Router);

public:
    static Router& the();

    // The script that installs chrome.* in an extension page, or that starts the background worker in its page, before
    // the page's own scripts.
    Optional<String> bootstrap_script(WebView::ScriptBridgeEndpoint const&);
    // The script that installs chrome.* in the extension's background worker.
    String background_worker_script(Extension const&);
    // The script that installs chrome.* in the extension's isolated world in a document, before its content scripts.
    String content_script_bootstrap(NonnullRefPtr<Extension>, WebView::ScriptBridgeEndpoint const& world);
    void on_message(WebView::ScriptBridgeEndpoint const&, String const& message);

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

    // Follows navigations for webNavigation's events while any extension has the permission for them. Call it whenever
    // the set of extensions, or their permissions, changes.
    void update_navigation_events();

private:
    friend class AK::NeverDestroyed<Router>;
    Router();

    struct Context {
        NonnullRefPtr<Extension> extension;
        WebView::ScriptBridgeEndpoint endpoint;
        ContextKind kind { ContextKind::Tab };
    };

    struct PendingMessage {
        WebView::ScriptBridgeEndpoint sender;
        u64 call_id { 0 };
        Vector<WebView::ScriptBridgeEndpoint> awaiting;
        // Whether any receiver had a runtime.onMessage listener.
        bool any_listening { false };
    };

    // A runtime.connect() port. The opener talks to every context that accepted; each of those talks to the opener.
    struct Port {
        String extension_id;
        WebView::ScriptBridgeEndpoint opener;
        Vector<WebView::ScriptBridgeEndpoint> awaiting;
        Vector<WebView::ScriptBridgeEndpoint> receivers;
        // What the opener posted before every context answered, for each one that accepts.
        Vector<String> early_messages;
    };

    Optional<Context&> context_for(WebView::ScriptBridgeEndpoint const&);
    JsonObject config_for(Extension const&, ContextKind, URL::URL const& url);
    JsonObject sender_for(Context const&);
    URL::URL context_url(Context const&) const;
    // Chrome's frame IDs: 0 for a tab's top-level frame, and a number of its own for every other frame.
    i64 frame_id(WebView::ScriptBridgeEndpoint const&);
    i64 frame_id(Web::HTML::CrossProcessId navigable_id, bool is_top_level);
    // An extension's contexts a runtime message or port goes to: its pages and background, or its content scripts in
    // a tab, all of the tab's frames or one.
    Vector<WebView::ScriptBridgeEndpoint> extension_receivers(Context const& sender) const;
    Vector<WebView::ScriptBridgeEndpoint> content_script_receivers(Extension const&, i64 tab_id, Optional<i64> frame_id, Optional<String const&> document_id);

    void handle_call(Context&, u64 id, String const& name, JsonArray const& args);
    ErrorOr<JsonValue, String> handle_storage_call(Context&, String const& name, JsonArray const& args);
    ErrorOr<JsonValue, String> handle_alarms_call(Context&, String const& name, JsonArray const& args);
    ErrorOr<JsonValue, String> handle_permissions_call(Context&, String const& name, JsonArray const& args);
    void request_permissions(Context&, u64 call_id, JsonArray const& args);
    void dispatch_permissions_change(StringView extension_id, StringView event_name, Vector<String> const& permissions);
    ErrorOr<JsonValue, String> handle_privacy_call(Context&, String const& name, JsonArray const& args);
    ErrorOr<JsonValue, String> handle_runtime_call(Context&, String const& name, JsonArray const& args);
    JsonArray contexts_of(Extension const&) const;

    // chrome.scripting, and chrome.webNavigation's frames (UI/Extensions/Scripting.cpp).
    struct Frame {
        WebView::ScriptBridgeEndpoint document;
        i64 frame_id { 0 };
        i64 parent_frame_id { -1 };
    };
    Optional<Vector<Frame>> frames_of_tab(i64 tab_id);
    ErrorOr<Vector<Frame>, String> injection_frames(Extension const&, Optional<JsonObject const&> target);
    void handle_scripting_call(Context&, u64 call_id, String const& name, JsonArray const& args);
    void execute_script(Context&, u64 call_id, JsonObject const& injection);
    void change_css(Context&, u64 call_id, JsonObject const& injection, bool insert);
    ErrorOr<JsonValue, String> handle_registered_content_scripts_call(Context&, String const& name, JsonArray const& args);
    ErrorOr<JsonValue, String> handle_web_navigation_call(Context&, String const& name, JsonArray const& args);
    void did_navigate(WebView::CanonicalNavigable const&, StringView webdriver_bidi_event, URL::URL const&);

    void send_runtime_message(Context&, u64 call_id, JsonValue message, Vector<WebView::ScriptBridgeEndpoint> receivers);
    void send_tab_message(Context&, u64 call_id, JsonArray const& args);
    void handle_response(Context&, u64 message_id, bool handled, bool listening, JsonValue response);
    void finish_pending_message(u64 message_id, Optional<JsonValue> response);

    void open_port(Context&, String const& port_id, String const& name, Vector<WebView::ScriptBridgeEndpoint> receivers);
    void handle_port_accepted(Context&, String const& port_id, bool accepted);
    void handle_port_message(Context&, String const& port_id, JsonValue message);
    void handle_port_disconnect(Context&, String const& port_id);
    void close_port_end(WebView::ScriptBridgeEndpoint const&, String const& port_id, Optional<StringView> error = {});
    void forget_endpoint(WebView::ScriptBridgeEndpoint const&);

    static void reply(WebView::ScriptBridgeEndpoint const&, u64 id, JsonValue result);
    static void reply_with_error(WebView::ScriptBridgeEndpoint const&, u64 id, StringView error);

    Vector<Context> m_contexts;
    HashMap<u64, PendingMessage> m_pending_messages;
    u64 m_next_message_id { 1 };
    HashMap<String, Port> m_ports;
    HashMap<String, Function<void(int, int)>> m_popup_size_listeners;
    HashMap<String, NonnullOwnPtr<Alarms>> m_alarms;
    // Extensions that let their content scripts use storage.session (storage.setAccessLevel()).
    HashTable<String> m_session_storage_open_to_content_scripts;
    HashMap<Web::HTML::CrossProcessId, i64> m_frame_ids;
    i64 m_next_frame_id { 1 };
    // Pages say when they go away, but not always: a crashed process doesn't.
    RefPtr<Core::Timer> m_prune_timer;
};

StringView context_kind_name(ContextKind);
// Chrome's document IDs and context IDs are opaque strings.
String document_id_string(Web::HTML::CrossProcessId document_id);
String document_id_string(WebView::ScriptBridgeEndpoint const&);

}
