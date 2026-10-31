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
#include <UI/Extensions/NativeMessaging.h>

namespace Ladybird::Extensions {

enum class ContextKind : u8 {
    Tab,
    Popup,
    Background,
    Offscreen,
    ContentScript,
    ExternalPage,
};

class Router {
    AK_MAKE_NONCOPYABLE(Router);

public:
    static Router& the();

    Optional<String> bootstrap_script(WebView::ScriptBridgeEndpoint const&);
    String background_worker_script(Extension const&);
    String content_script_bootstrap(NonnullRefPtr<Extension>, WebView::ScriptBridgeEndpoint const& world);
    String externally_connectable_relay(NonnullRefPtr<Extension>, WebView::ScriptBridgeEndpoint const& world, StringView event_name);
    void on_message(WebView::ScriptBridgeEndpoint const&, String const& message);

    void dispatch_event(Extension const&, StringView name, JsonArray args, Optional<ContextKind> = {});
    void dispatch_event_to_all(StringView name, JsonArray args);

    void set_popup_size_listener(String const& extension_id, Function<void(int width, int height)>);

    void background_will_start(Extension const&);

    void set_background_idle_timeout(AK::Duration timeout) { m_background_idle_timeout = timeout; }

    void forget_extension(Extension const&);

    void prune_closed_contexts();

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
        bool any_listening { false };
    };

    struct Port {
        String extension_id;
        WebView::ScriptBridgeEndpoint opener;
        Vector<WebView::ScriptBridgeEndpoint> awaiting;
        Vector<WebView::ScriptBridgeEndpoint> receivers;
        Vector<String> early_messages;
        RefPtr<NativeHostProcess> native;
    };

    Optional<Context&> context_for(WebView::ScriptBridgeEndpoint const&);
    JsonObject config_for(Extension const&, ContextKind, URL::URL const& url);
    JsonObject sender_for(Context const&);
    URL::URL context_url(Context const&) const;
    i64 frame_id(WebView::ScriptBridgeEndpoint const&);
    i64 frame_id(Web::HTML::CrossProcessId navigable_id, bool is_top_level);
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

    Optional<Vector<Function<void()>>&> waiting_for_background(Context const& sender);
    void background_did_run(String const& extension_id);
    void start_background(Extension const&, bool is_restart);
    void wake_background(Extension const&, StringView event_name);
    void background_was_active(String const& extension_id);
    bool background_is_busy(String const& extension_id) const;
    void stop_idle_background(String const& extension_id);
    void post(WebView::ScriptBridgeEndpoint const&, String const& message);

    void send_runtime_message(Context&, u64 call_id, JsonValue message, Vector<WebView::ScriptBridgeEndpoint> receivers);
    void send_tab_message(Context&, u64 call_id, JsonArray const& args);
    void handle_response(Context&, u64 message_id, bool handled, bool listening, JsonValue response);
    void finish_pending_message(u64 message_id, Optional<JsonValue> response);

    void open_port(Context&, String const& port_id, String const& name, Vector<WebView::ScriptBridgeEndpoint> receivers);
    void open_native_port(Context&, String const& port_id, String const& application);
    void handle_port_accepted(Context&, String const& port_id, bool accepted);
    void handle_port_message(Context&, String const& port_id, JsonValue message);
    void handle_port_disconnect(Context&, String const& port_id);
    void close_port_end(WebView::ScriptBridgeEndpoint const&, String const& port_id, Optional<StringView> error = {});
    void forget_endpoint(WebView::ScriptBridgeEndpoint const&);

    static String event_message(StringView name, JsonArray args);
    static void reply(WebView::ScriptBridgeEndpoint const&, u64 id, JsonValue result);
    static void reply_with_error(WebView::ScriptBridgeEndpoint const&, u64 id, StringView error);

    Vector<Context> m_contexts;
    HashMap<u64, PendingMessage> m_pending_messages;
    u64 m_next_message_id { 1 };
    HashMap<String, Port> m_ports;
    HashMap<String, Function<void(int, int)>> m_popup_size_listeners;
    HashMap<String, NonnullOwnPtr<Alarms>> m_alarms;
    HashTable<String> m_session_storage_open_to_content_scripts;
    HashMap<Web::HTML::CrossProcessId, i64> m_frame_ids;
    i64 m_next_frame_id { 1 };
    RefPtr<Core::Timer> m_prune_timer;
    struct StartingBackground {
        Vector<Function<void()>> waiting;
        NonnullRefPtr<Core::Timer> timeout;
        bool is_restart { false };
    };
    HashMap<String, StartingBackground> m_starting_backgrounds;
    HashMap<String, NonnullRefPtr<Core::Timer>> m_running_backgrounds;
    HashMap<String, HashTable<String>> m_background_listeners;
    AK::Duration m_background_idle_timeout;
};

StringView context_kind_name(ContextKind);
String document_id_string(Web::HTML::CrossProcessId document_id);
String document_id_string(WebView::ScriptBridgeEndpoint const&);

}
