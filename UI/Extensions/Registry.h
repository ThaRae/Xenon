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

// The page that hosts an extension's background service worker (see UI/Resources/extensions/background-page.js). Chrome
// reserves paths starting with an underscore, and named its MV2 background pages the same way.
constexpr auto BACKGROUND_PAGE_PATH = "/_generated_background_page.html"sv;

// Why runtime.onInstalled fires for an extension this session, if it does.
struct InstallReason {
    StringView reason;
    Optional<String> previous_version;
};

// The extensions this browser has loaded, and what serves them: chrome-extension:// URLs, their storage, and their
// background pages.
class Registry {
    AK_MAKE_NONCOPYABLE(Registry);

public:
    static Registry& the();

    // Registers chrome-extension:// with the engine. Call it before the browser parses its command line, whose URLs may
    // be extension pages, and so before the first renderer starts.
    void register_url_scheme();

    // Loads the extensions loaded in earlier sessions, and the unpacked extensions named on the command line, once the
    // profile is known. Extensions keep their state in the profile's data directory.
    void initialize(ByteString profile_data_directory, Vector<ByteString> const& command_line_extensions, String ui_language);

    // Starts every extension's background, once the browser can create views.
    void start_background_pages();

    enum class Persist : u8 {
        No,
        Yes,
    };
    // Loads an unpacked extension, or reloads it if it's loaded already. One that persists loads again next session,
    // as one loaded from the Extensions dialog does; one from the command line is for this session only.
    ErrorOr<NonnullRefPtr<Extension>, String> load_unpacked(StringView directory, Persist);
    ErrorOr<NonnullRefPtr<Extension>, String> reload(StringView id);
    // Unloads an extension, and deletes its storage, and its files if it was installed.
    void remove(StringView id);

    // Installs an extension from a CRX file once verify_crx() accepts it for the expected ID. It's unpacked into the
    // profile, and loads every session until it's removed. An installed extension that's running keeps the version it
    // has, and the new one takes over next session, as Chrome waits for an extension to go idle to update it.
    enum class Installed : u8 {
        Now,
        NextSession,
    };
    ErrorOr<Installed, String> install(ReadonlyBytes crx, StringView expected_id, ReadonlyBytes publisher_key_hash);
    // The version of an installed extension that loads next session, if the extension was installed rather than loaded
    // unpacked. It's installed even if it didn't load.
    Optional<String const&> installed_version(StringView id) const;
    bool is_installed(Extension const&) const;

    RefPtr<Extension> extension(StringView id) const;
    Vector<NonnullRefPtr<Extension>> const& extensions() const { return m_extensions; }
    bool is_persisted(Extension const&) const;

    // The extension a chrome-extension:// URL belongs to.
    RefPtr<Extension> extension_for_url(URL::URL const&) const;

    StorageArea& storage(Extension const&, StorageArea::Kind);

    // Taken by the background's first start, which reports it as runtime.onInstalled; none means runtime.onStartup.
    Optional<InstallReason> take_install_reason(StringView id);

    // chrome.offscreen: an extension page in a hidden view, at most one per extension.
    bool has_offscreen_document(Extension const&) const;
    void create_offscreen_document(Extension const&, URL::URL const&);
    void close_offscreen_document(Extension const&);
    bool is_offscreen_view(Extension const&, u64 view_id) const;
    // Whether a view is one of the hidden views of extensions: a background page or an offscreen document.
    bool is_hidden_view(u64 view_id) const;

    // Optional permissions the user granted an extension. They last across sessions and reloads, until the extension
    // gives them up or is removed.
    void grant_optional_permissions(StringView id, Vector<String> const&);
    void revoke_optional_permissions(StringView id, Vector<String> const&);

    // Browser settings extensions control through chrome.privacy. The last extension to set one controls it, until it
    // clears it or is removed.
    struct ControlledSetting {
        String extension_id;
        JsonValue value;
    };
    Optional<ControlledSetting const&> controlled_setting(StringView name) const;
    void set_controlled_setting(String const& name, Extension const&, JsonValue);
    // Whether the extension controlled the setting, which it now no longer does.
    bool clear_controlled_setting(StringView name, Extension const&);

    // Content scripts an extension registered with scripting.registerContentScripts(), in order. Those that persist
    // across sessions last until the extension unregisters them, or is reloaded or removed.
    Vector<RegisteredContentScript> const& registered_content_scripts(StringView id) const;
    void set_registered_content_scripts(StringView id, Vector<RegisteredContentScript>);

    // Called whenever the set of extensions changes, until the listener returns false.
    void on_extensions_change(Function<bool()>);

private:
    friend class AK::NeverDestroyed<Registry>;
    Registry() = default;

    ErrorOr<WebView::URLSchemeResponse> respond(WebView::URLSchemeRequest const&) const;
    Optional<String> generated_resource(Extension const&, StringView path) const;

    void add(NonnullRefPtr<Extension>);
    void start_background_page(Extension const&);
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

    // The directories of the unpacked extensions to load every session, the version of each installed extension to load,
    // and each extension's version when last loaded.
    Vector<ByteString> m_persisted_directories;
    HashMap<String, String> m_installed_versions;
    HashMap<String, String> m_known_versions;
    HashMap<String, InstallReason> m_install_reasons;
    // By extension ID, and by setting name.
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
