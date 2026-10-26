/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/NeverDestroyed.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/NonnullRefPtr.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <LibWebView/URLSchemeHandler.h>
#include <UI/Extensions/Extension.h>
#include <UI/Extensions/Storage.h>

namespace Ladybird::Extensions {

// The extensions this browser has loaded, and what serves them: chrome-extension:// URLs, and their storage.
class Registry {
    AK_MAKE_NONCOPYABLE(Registry);

public:
    static Registry& the();

    // Registers chrome-extension:// with the engine. Call it before the browser parses its command line, whose URLs may
    // be extension pages, and so before the first renderer starts.
    void register_url_scheme();

    // Loads the unpacked extensions named on the command line, once the profile is known. Extensions keep their storage in
    // the profile's data directory.
    void initialize(ByteString profile_data_directory, Vector<ByteString> const& unpacked_extensions, String ui_language);

    ErrorOr<NonnullRefPtr<Extension>, String> load_unpacked(StringView directory);

    RefPtr<Extension> extension(StringView id) const;
    Vector<NonnullRefPtr<Extension>> const& extensions() const { return m_extensions; }

    StorageArea& storage(Extension const&, StorageArea::Kind);

    // The extension a chrome-extension:// URL belongs to.
    RefPtr<Extension> extension_for_url(URL::URL const&) const;

    Function<void()> on_extensions_changed;

private:
    friend class AK::NeverDestroyed<Registry>;
    Registry() = default;

    ErrorOr<WebView::URLSchemeResponse> respond(WebView::URLSchemeRequest const&) const;

    ByteString m_storage_directory;
    String m_ui_language;
    Vector<NonnullRefPtr<Extension>> m_extensions;
    HashMap<ByteString, NonnullOwnPtr<StorageArea>> m_storage;
};

}
