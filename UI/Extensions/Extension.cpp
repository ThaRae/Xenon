/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AllOf.h>
#include <AK/Base64.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <AK/StringBuilder.h>
#include <LibCore/File.h>
#include <LibCore/MimeData.h>
#include <LibCrypto/Hash/SHA2.h>
#include <LibFileSystem/FileSystem.h>
#include <UI/Extensions/Extension.h>
#include <UI/Extensions/MatchPattern.h>

namespace Ladybird::Extensions {

String extension_id_from_bytes(ReadonlyBytes bytes)
{
    auto digest = Crypto::Hash::SHA256::hash(bytes);

    StringBuilder builder;
    for (auto byte : digest.bytes().slice(0, 16)) {
        builder.append(static_cast<char>('a' + (byte >> 4)));
        builder.append(static_cast<char>('a' + (byte & 0xf)));
    }
    return builder.to_string_without_validation();
}

Optional<String> extension_id_from_key(StringView base64_key)
{
    auto key = decode_base64(base64_key);
    if (key.is_error() || key.value().is_empty())
        return {};
    return extension_id_from_bytes(key.value());
}

String extension_id_for_path(StringView absolute_path)
{
    return extension_id_from_bytes(absolute_path.bytes());
}

bool is_valid_extension_id(StringView id)
{
    return id.length() == 32 && all_of(id, [](char c) { return c >= 'a' && c <= 'p'; });
}

String strip_json_comments(StringView text)
{
    StringBuilder builder;
    bool in_string = false;

    for (size_t i = 0; i < text.length(); ++i) {
        auto c = text[i];
        if (in_string) {
            builder.append(c);
            if (c == '\\' && i + 1 < text.length())
                builder.append(text[++i]);
            else if (c == '"')
                in_string = false;
            continue;
        }

        if (c == '"') {
            in_string = true;
            builder.append(c);
        } else if (c == '/' && i + 1 < text.length() && text[i + 1] == '/') {
            while (i < text.length() && text[i] != '\n')
                ++i;
            builder.append('\n');
        } else if (c == '/' && i + 1 < text.length() && text[i + 1] == '*') {
            auto end = text.find("*/"sv, i + 2);
            i = end.has_value() ? *end + 1 : text.length();
            builder.append(' ');
        } else {
            builder.append(c);
        }
    }
    return builder.to_string_without_validation();
}

ErrorOr<JsonObject, String> read_extension_json(ByteString const& path, ByteString const& extension_root)
{
    auto relative = LexicalPath::relative_path(path, extension_root).value_or(path);

    auto file = Core::File::open(path, Core::File::OpenMode::Read);
    if (file.is_error())
        return MUST(String::formatted("Could not read {}: {}", relative, file.error()));
    auto contents = file.value()->read_until_eof();
    if (contents.is_error())
        return MUST(String::formatted("Could not read {}: {}", relative, contents.error()));

    auto text = StringView { contents.value().bytes() };
    if (text.starts_with("\xEF\xBB\xBF"sv))
        text = text.substring_view(3);

    auto json = JsonValue::from_string(strip_json_comments(text));
    if (json.is_error())
        return MUST(String::formatted("{} isn't valid JSON: {}", relative, json.error()));
    if (!json.value().is_object())
        return MUST(String::formatted("{} must hold a JSON object.", relative));
    return json.release_value().as_object();
}

static void localize_manifest(JsonObject& json, Locales const& locales)
{
    auto localize = [&](JsonObject& object, StringView key) {
        if (auto value = object.get_string(key); value.has_value())
            object.set(key, locales.localize(*value));
    };

    localize(json, "name"sv);
    localize(json, "short_name"sv);
    localize(json, "description"sv);
    if (auto action = json.get_object("action"sv); action.has_value())
        localize(*action, "default_title"sv);
    if (auto commands = json.get_object("commands"sv); commands.has_value()) {
        Vector<String> names;
        commands->for_each_member([&](String const& name, JsonValue const&) { names.append(name); });
        for (auto const& name : names) {
            if (auto command = commands->get_object(name); command.has_value())
                localize(*command, "description"sv);
        }
    }
}

ErrorOr<NonnullRefPtr<Extension>, String> Extension::load_unpacked(StringView directory, StringView ui_language)
{
    auto root = FileSystem::real_path(directory);
    if (root.is_error() || !FileSystem::is_directory(root.value()))
        return MUST(String::formatted("{} isn't a directory.", directory));

    auto manifest_path = LexicalPath::join(root.value(), "manifest.json"sv).string();
    if (!FileSystem::is_regular_file(manifest_path))
        return "Manifest file is missing or unreadable."_string;
    auto json = TRY(read_extension_json(manifest_path, root.value()));

    String id;
    if (auto key = json.get_string("key"sv); key.has_value()) {
        auto id_from_key = extension_id_from_key(*key);
        if (!id_from_key.has_value())
            return "Invalid value for 'key'."_string;
        id = id_from_key.release_value();
    } else
        id = extension_id_for_path(root.value());

    auto default_locale = json.get_string("default_locale"sv).copy();
    auto locales = TRY(Locales::load(root.value(), default_locale, ui_language, id));
    localize_manifest(json, locales);
    auto manifest = TRY(Manifest::parse(move(json)));

    auto require_file = [&](String const& path, StringView what) -> ErrorOr<void, String> {
        auto full_path = LexicalPath::join(root.value(), path.bytes_as_string_view()).string();
        if (!FileSystem::is_regular_file(full_path))
            return MUST(String::formatted("Could not load {} '{}'.", what, path));
        return {};
    };
    for (auto const& icon : manifest.icons)
        TRY(require_file(icon.path, "icon"sv));
    if (manifest.background_service_worker.has_value())
        TRY(require_file(*manifest.background_service_worker, "background script"sv));
    for (auto const& script : manifest.content_scripts) {
        for (auto const& path : script.js)
            TRY(require_file(path, "javascript"sv));
        for (auto const& path : script.css)
            TRY(require_file(path, "css"sv));
    }

    return adopt_ref(*new Extension(move(id), root.release_value(), move(manifest), move(locales)));
}

Extension::Extension(String id, ByteString root, Manifest manifest, Locales locales)
    : m_id(move(id))
    , m_root(move(root))
    , m_manifest(move(manifest))
    , m_locales(move(locales))
{
}

URL::URL Extension::base_url() const
{
    return url_for(""sv);
}

URL::URL Extension::url_for(StringView path) const
{
    while (path.starts_with('/'))
        path = path.substring_view(1);
    auto url = URL::create_with_url_or_path(ByteString::formatted("{}://{}/{}", EXTENSION_SCHEME, m_id, path));
    VERIFY(url.has_value());
    return url.release_value();
}

ErrorOr<ByteString> Extension::resolve(URL::URL const& url) const
{
    if (url.scheme() != EXTENSION_SCHEME || url.serialized_host() != m_id.bytes_as_string_view())
        return Error::from_errno(ENOENT);

    LexicalPath path { m_root };
    size_t segments = 0;
    for (auto segment : url.path_segments()) {
        auto decoded = URL::percent_decode(segment);
        if (decoded.is_empty())
            continue;
        if (decoded == "."sv || decoded == ".."sv || decoded.contains('/') || decoded.contains('\\') || decoded.contains('\0'))
            return Error::from_errno(EACCES);
        path = path.append(decoded);
        ++segments;
    }
    if (segments == 0)
        return Error::from_errno(EISDIR);

    auto real_path = TRY(FileSystem::real_path(path.string()));
    if (!real_path.starts_with(ByteString::formatted("{}/", m_root)))
        return Error::from_errno(EACCES);
    if (!FileSystem::is_regular_file(real_path))
        return Error::from_errno(FileSystem::is_directory(real_path) ? EISDIR : ENOENT);
    return real_path;
}

Vector<String> Extension::permissions() const
{
    auto permissions = m_manifest.permissions;
    for (auto const& permission : m_granted_optional_permissions) {
        if (!permissions.contains_slow(permission))
            permissions.append(permission);
    }
    return permissions;
}

bool Extension::has_permission(StringView permission) const
{
    return m_manifest.has_permission(permission) || m_granted_optional_permissions.contains_slow(permission);
}

void Extension::set_granted_optional_permissions(Vector<String> permissions)
{
    permissions.remove_all_matching([&](auto const& permission) { return !m_manifest.optional_permissions.contains_slow(permission); });
    m_granted_optional_permissions = move(permissions);
}

bool Extension::is_web_accessible(StringView path, URL::Origin const& requester) const
{
    while (path.starts_with('/'))
        path = path.substring_view(1);

    for (auto const& entry : m_manifest.web_accessible_resources) {
        auto origin_matches = any_of(entry.matches, [&](String const& pattern) {
            auto parsed = MatchPattern::parse(pattern);
            return !parsed.is_error() && parsed.value().matches_origin(requester);
        });
        if (!origin_matches && requester.scheme() == EXTENSION_SCHEME)
            origin_matches = entry.extension_ids.contains_slow("*"sv) || entry.extension_ids.contains_slow(requester.host().serialize());
        if (!origin_matches)
            continue;

        if (any_of(entry.resources, [&](String const& resource) { return glob_matches(resource, path); }))
            return true;
    }
    return false;
}

bool Extension::is_sandboxed_page(StringView path) const
{
    while (path.starts_with('/'))
        path = path.substring_view(1);
    return any_of(m_manifest.sandbox_pages, [&](String const& page) { return glob_matches(page, path); });
}

StringView mime_type_for_path(StringView path)
{
    auto mime_type = Core::guess_mime_type_based_on_filename(path);
    if (mime_type == "text/html"sv)
        return "text/html; charset=utf-8"sv;
    if (mime_type == "text/css"sv)
        return "text/css; charset=utf-8"sv;
    if (mime_type == "application/javascript"sv)
        return "text/javascript; charset=utf-8"sv;
    return mime_type;
}

}
