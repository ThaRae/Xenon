/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/JsonValue.h>
#include <AK/NeverDestroyed.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/NonnullRefPtr.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibWebView/HeadlessWebView.h>
#include <LibWebView/URLSchemeHandler.h>
#include <UI/Extensions/Extension.h>
#include <UI/Extensions/Storage.h>

namespace Ladybird::Extensions {

constexpr auto BACKGROUND_PAGE_PATH = "/_generated_background_page.html"sv;

struct InstallReason {
    StringView reason;
    Optional<String> previous_version;
};

class Registry {
    AK_MAKE_NONCOPYABLE(Registry);

public:
    static Registry& the();

    void register_url_scheme();

    void initialize(ByteString profile_data_directory, Vector<ByteString> const& command_line_extensions, String ui_language);

    void start_background_pages();
    bool restart_background_page(Extension const&);
    void stop_idle_background_page(Extension const&);

    enum class Persist : u8 {
        No,
        Yes,
    };
    ErrorOr<NonnullRefPtr<Extension>, String> load_unpacked(StringView directory, Persist);
    ErrorOr<NonnullRefPtr<Extension>, String> reload(StringView id);
    void remove(StringView id);

    enum class Installed : u8 {
        Now,
        NextSession,
    };
    ErrorOr<Installed, String> install(ReadonlyBytes crx, StringView expected_id, ReadonlyBytes publisher_key_hash);
    Optional<String const&> installed_version(StringView id) const;
    bool is_installed(Extension const&) const;

    RefPtr<Extension> extension(StringView id) const;
    Vector<NonnullRefPtr<Extension>> const& extensions() const { return m_extensions; }
    bool is_persisted(Extension const&) const;

    RefPtr<Extension> extension_for_url(URL::URL const&) const;

    StorageArea& storage(Extension const&, StorageArea::Kind);

    Optional<InstallReason> take_install_reason(StringView id);

    bool has_offscreen_document(Extension const&) const;
    void create_offscreen_document(Extension const&, URL::URL const&);
    void close_offscreen_document(Extension const&);
    bool is_offscreen_view(Extension const&, u64 view_id) const;
    bool is_hidden_view(u64 view_id) const;

    void grant_optional_permissions(StringView id, Vector<String> const&);
    void revoke_optional_permissions(StringView id, Vector<String> const&);

    struct ControlledSetting {
        String extension_id;
        JsonValue value;
    };
    Optional<ControlledSetting const&> controlled_setting(StringView name) const;
    void set_controlled_setting(String const& name, Extension const&, JsonValue);
    bool clear_controlled_setting(StringView name, Extension const&);

    Vector<RegisteredContentScript> const& registered_content_scripts(StringView id) const;
    void set_registered_content_scripts(StringView id, Vector<RegisteredContentScript>);

    void on_extensions_change(Function<bool()>);

private:
    friend class AK::NeverDestroyed<Registry>;
    Registry() = default;

    ErrorOr<WebView::URLSchemeResponse> respond(WebView::URLSchemeRequest const&) const;
    Optional<String> generated_resource(Extension const&, StringView path) const;

    void add(NonnullRefPtr<Extension>);
    void start_background_page(Extension const&);
    void open_background_page(Extension const&);
    void stop_background_page(StringView id);
    OwnPtr<WebView::HeadlessWebView> create_hidden_view(URL::URL const&);
    static void close_hidden_view(NonnullOwnPtr<WebView::HeadlessWebView>);

    ByteString installed_extension_directory(StringView id) const;
    void remove_stale_installed_files() const;

    void load_state();
    void save_state() const;
    void notify_extensions_changed();
    void update_origin_access_allow_list();

    ByteString m_extensions_directory;
    String m_ui_language;
    Vector<NonnullRefPtr<Extension>> m_extensions;
    HashMap<ByteString, NonnullOwnPtr<StorageArea>> m_storage;

    Vector<ByteString> m_persisted_directories;
    HashMap<String, String> m_installed_versions;
    HashMap<String, String> m_known_versions;
    HashMap<String, InstallReason> m_install_reasons;
    HashMap<String, Vector<String>> m_granted_permissions;
    HashMap<String, ControlledSetting> m_controlled_settings;
    HashMap<String, Vector<RegisteredContentScript>> m_registered_content_scripts;

    bool m_can_start_background_pages { false };
    Optional<Core::AnonymousBuffer> m_theme;
    HashMap<String, NonnullOwnPtr<WebView::HeadlessWebView>> m_background_pages;
    HashMap<String, NonnullOwnPtr<WebView::HeadlessWebView>> m_offscreen_documents;

    Vector<Function<bool()>> m_change_listeners;
};

}
