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

constexpr auto EXTENSION_SCHEME = "chrome-extension"sv;

class Extension : public RefCounted<Extension> {
public:
    AK_ALLOC_WITH_KMALLOC;

    static ErrorOr<NonnullRefPtr<Extension>, String> load_unpacked(StringView directory, StringView ui_language);

    String const& id() const { return m_id; }
    ByteString const& root() const { return m_root; }
    Manifest const& manifest() const { return m_manifest; }
    Locales const& locales() const { return m_locales; }

    URL::URL base_url() const;
    URL::URL url_for(StringView path) const;

    ErrorOr<ByteString> resolve(URL::URL const&) const;

    bool is_web_accessible(StringView path, URL::Origin const& requester) const;

    bool is_sandboxed_page(StringView path) const;

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

String extension_id_from_bytes(ReadonlyBytes);
Optional<String> extension_id_from_key(StringView base64_key);
String extension_id_for_path(StringView absolute_path);
bool is_valid_extension_id(StringView);

ErrorOr<JsonObject, String> read_extension_json(ByteString const& path, ByteString const& extension_root);
String strip_json_comments(StringView);

StringView mime_type_for_path(StringView path);

}
