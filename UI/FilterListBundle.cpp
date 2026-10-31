/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AllOf.h>
#include <AK/Base64.h>
#include <AK/CharacterTypes.h>
#include <AK/Checked.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/StringBuilder.h>
#include <LibCrypto/Curves/EdwardsCurve.h>
#include <LibCrypto/Hash/SHA2.h>
#include <UI/FilterListBundle.h>

namespace Ladybird::FilterLists {

static constexpr auto RESOURCES_PATH = "resources/ublock-resources.json"sv;

Optional<BundleFile const&> BundleManifest::file(StringView path) const
{
    return files.first_matching([&](auto const& file) { return file.path == path; });
}

static bool is_list_path(StringView path, StringView directory)
{
    if (!path.starts_with(directory) || path.length() <= directory.length() + 1 || path[directory.length()] != '/')
        return false;

    auto name = path.substring_view(directory.length() + 1);
    if (name.starts_with('.') || !name.ends_with(".txt"sv))
        return false;
    return all_of(name, [](char c) { return is_ascii_alphanumeric(c) || c == '.' || c == '-' || c == '_'; });
}

static bool is_sha256_hex(StringView value)
{
    return value.length() == 64 && all_of(value, [](char c) { return is_ascii_digit(c) || (c >= 'a' && c <= 'f'); });
}

static ErrorOr<BundleFile> parse_file(JsonObject const& object)
{
    auto path = object.get_string("file"sv);
    auto size = object.get_u64("bytes"sv);
    auto sha256 = object.get_string("sha256"sv);
    if (!path.has_value() || !size.has_value() || !sha256.has_value())
        return Error::from_string_literal("A file in the manifest has no path, size, or hash");
    if (!is_sha256_hex(*sha256))
        return Error::from_string_literal("A file's hash in the manifest isn't SHA-256 in hex");
    if (*size > MAXIMUM_FILE_SIZE)
        return Error::from_string_literal("A file in the manifest is larger than Ladybird loads");
    return BundleFile { path->to_byte_string(), *size, sha256->to_byte_string() };
}

ErrorOr<BundleManifest> parse_bundle_manifest(StringView json)
{
    auto value = TRY(JsonValue::from_string(json));
    if (!value.is_object())
        return Error::from_string_literal("The manifest isn't a JSON object");
    auto const& object = value.as_object();

    BundleManifest manifest;
    if (auto format = object.get("format"sv); format.has_value()) {
        auto number = format->get_u32();
        if (!number.has_value())
            return Error::from_string_literal("The manifest's format isn't a number");
        manifest.format = *number;
    }

    auto generated_at = object.get_string("generated_at"sv);
    if (!generated_at.has_value() || generated_at->is_empty())
        return Error::from_string_literal("The manifest doesn't say when it was generated");
    manifest.generated_at = generated_at->to_byte_string();

    auto lists = object.get_array("lists"sv);
    if (!lists.has_value() || lists->is_empty())
        return Error::from_string_literal("The manifest has no lists");
    for (auto const& entry : lists->values()) {
        if (!entry.is_object())
            return Error::from_string_literal("A list in the manifest isn't an object");
        auto file = TRY(parse_file(entry.as_object()));
        auto trusted = entry.as_object().get_bool("trusted"sv).value_or(false);
        if (!is_list_path(file.path, trusted ? "trusted-lists"sv : "lists"sv))
            return Error::from_string_literal("A list in the manifest isn't in the directory for its kind of list");
        manifest.files.append(move(file));
    }

    if (auto resources = object.get_object("resources"sv); resources.has_value()) {
        auto file = TRY(parse_file(*resources));
        if (file.path != RESOURCES_PATH)
            return Error::from_string_literal("The manifest's resources aren't where Xenon loads them from");
        manifest.files.append(move(file));
    }

    Checked<u64> total_size = 0;
    for (size_t i = 0; i < manifest.files.size(); ++i) {
        total_size += manifest.files[i].size;
        for (size_t j = 0; j < i; ++j) {
            if (manifest.files[i].path == manifest.files[j].path)
                return Error::from_string_literal("The manifest names a file twice");
        }
    }
    if (total_size.has_overflow() || total_size.value() > MAXIMUM_BUNDLE_SIZE)
        return Error::from_string_literal("The manifest's lists are larger than Ladybird loads");

    return manifest;
}

bool bundle_signature_is_valid(ReadonlyBytes manifest, StringView signature, ReadonlyBytes public_key)
{
    auto signature_bytes = decode_base64(signature.trim_whitespace());
    if (signature_bytes.is_error() || signature_bytes.value().size() != 64 || public_key.size() != 32)
        return false;

    Crypto::Curves::Ed25519 curve;
    auto result = curve.verify(public_key, signature_bytes.value(), manifest);
    return !result.is_error() && result.value();
}

ByteString sha256_hex(ReadonlyBytes bytes)
{
    auto digest = Crypto::Hash::SHA256::hash(bytes);

    StringBuilder builder;
    for (auto byte : digest.bytes())
        builder.appendff("{:02x}", byte);
    return builder.to_byte_string();
}

bool file_matches(BundleFile const& file, ReadonlyBytes contents)
{
    return contents.size() == file.size && sha256_hex(contents) == file.sha256;
}

}
