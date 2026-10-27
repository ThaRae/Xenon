/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/JsonObject.h>
#include <AK/NonnullRefPtr.h>
#include <AK/RefCounted.h>
#include <AK/String.h>
#include <LibURL/URL.h>
#include <UI/Extensions/Locales.h>
#include <UI/Extensions/Manifest.h>

namespace Ladybird::Extensions {

// The scheme of extension URLs. It's Chrome's, since extensions compare URLs against it.
constexpr auto EXTENSION_SCHEME = "chrome-extension"sv;

// An extension loaded from its directory, which it is served from as chrome-extension://<id>/.
class Extension : public RefCounted<Extension> {
public:
    AK_ALLOC_WITH_KMALLOC;

    // Loads an unpacked extension, validating it as Chrome does. The error says why it can't be loaded.
    static ErrorOr<NonnullRefPtr<Extension>, String> load_unpacked(StringView directory, StringView ui_language);

    String const& id() const { return m_id; }
    ByteString const& root() const { return m_root; }
    Manifest const& manifest() const { return m_manifest; }
    Locales const& locales() const { return m_locales; }

    URL::URL base_url() const;
    URL::URL url_for(StringView path) const;

    // The file a chrome-extension:// URL of this extension names, if it names a file inside the extension's directory.
    ErrorOr<ByteString> resolve(URL::URL const&) const;

    // Whether a page of another origin may load this path: it has to be in web_accessible_resources for that origin.
    bool is_web_accessible(StringView path, URL::Origin const& requester) const;

    bool is_sandboxed_page(StringView path) const;

    // The manifest's permissions, and the optional ones the user granted (see Registry::grant_optional_permissions()).
    Vector<String> permissions() const;
    bool has_permission(StringView) const;
    Vector<String> const& granted_optional_permissions() const { return m_granted_optional_permissions; }
    void set_granted_optional_permissions(Vector<String>);

private:
    Extension(String id, ByteString root, Manifest, Locales);

    String m_id;
    ByteString m_root;
    Manifest m_manifest;
    Locales m_locales;
    Vector<String> m_granted_optional_permissions;
};

// Chrome's extension IDs: the first 128 bits of a SHA-256 hash, as 32 letters from a to p. An extension with a "key" in
// its manifest is identified by the key, and an unpacked one without by its directory's absolute path.
String extension_id_from_bytes(ReadonlyBytes);
Optional<String> extension_id_from_key(StringView base64_key);
String extension_id_for_path(StringView absolute_path);
bool is_valid_extension_id(StringView);

// Reads a manifest.json or messages.json. Chrome allows comments in them, so this does too.
ErrorOr<JsonObject, String> read_extension_json(ByteString const& path, ByteString const& extension_root);
String strip_json_comments(StringView);

StringView mime_type_for_path(StringView path);

}
