/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/ByteString.h>
#include <LibCore/File.h>
#include <LibCrypto/Curves/EdwardsCurve.h>
#include <LibTest/TestCase.h>
#include <UI/FilterListBundle.h>

using namespace Ladybird::FilterLists;

static constexpr auto EMPTY_SHA256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"sv;

static ByteString manifest_with(StringView lists, StringView extra = {})
{
    return ByteString::formatted(R"({{"format": 1, "generated_at": "2026-10-27 05:17 UTC", "lists": [{}]{}}})", lists, extra);
}

static ByteString list(StringView file, bool trusted = false, u64 bytes = 0, StringView sha256 = EMPTY_SHA256)
{
    return ByteString::formatted(R"({{"file": "{}", "bytes": {}, "sha256": "{}", "trusted": {}}})", file, bytes, sha256, trusted);
}

TEST_CASE(manifest_lists_every_file_with_its_size_and_hash)
{
    auto json = manifest_with(ByteString::join(","sv, Vector { list("lists/easylist.txt"sv, false, 12), list("trusted-lists/ublock-filters.txt"sv, true) }),
        ByteString::formatted(R"(, "resources": {{"file": "resources/ublock-resources.json", "bytes": 3, "sha256": "{}"}})", EMPTY_SHA256));
    auto manifest = MUST(parse_bundle_manifest(json));

    EXPECT_EQ(manifest.format, 1u);
    EXPECT_EQ(manifest.generated_at, "2026-10-27 05:17 UTC"sv);
    EXPECT_EQ(manifest.files.size(), 3u);
    EXPECT_EQ(manifest.files[0], (BundleFile { "lists/easylist.txt", 12, EMPTY_SHA256 }));
    EXPECT_EQ(manifest.files[1].path, "trusted-lists/ublock-filters.txt"sv);
    EXPECT_EQ(manifest.files[2].path, "resources/ublock-resources.json"sv);
    EXPECT(manifest.file("lists/easylist.txt"sv).has_value());
    EXPECT(!manifest.file("lists/other.txt"sv).has_value());
}

TEST_CASE(manifest_without_a_format_is_a_bundled_one)
{
    auto manifest = MUST(parse_bundle_manifest(R"({"generated_at": "2026-10-26 18:47 UTC", "lists": [{"file": "lists/a.txt", "bytes": 0, "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}]})"sv));
    EXPECT(!manifest.format.has_value());
}

// Nothing a manifest says can put a file outside the bundle, or let a list run trusted scriptlets it isn't trusted with.
TEST_CASE(manifest_files_stay_in_their_directories)
{
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/../../evil.txt"sv))).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("/etc/hosts.txt"sv))).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/sub/list.txt"sv))).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/.hidden.txt"sv))).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/list.js"sv))).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/"sv))).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("listsx/list.txt"sv))).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("trusted-lists/list.txt"sv, false))).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/list.txt"sv, true))).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/list.txt"sv), R"(, "resources": {"file": "resources/other.json", "bytes": 0, "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"})"sv)).is_error());
}

TEST_CASE(manifest_must_describe_every_file)
{
    EXPECT(parse_bundle_manifest(R"({"format": 1, "generated_at": "2026-10-27 05:17 UTC", "lists": [{"file": "lists/a.txt", "bytes": 0}]})"sv).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/a.txt"sv, false, 0, "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"sv))).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/a.txt"sv, false, 0, "abc"sv))).is_error());
    EXPECT(parse_bundle_manifest(R"({"format": 1, "lists": [{"file": "lists/a.txt", "bytes": 0, "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}]})"sv).is_error());
    EXPECT(parse_bundle_manifest(manifest_with(""sv)).is_error());
    EXPECT(parse_bundle_manifest("[]"sv).is_error());
    EXPECT(parse_bundle_manifest("not json"sv).is_error());
}

TEST_CASE(manifest_stays_within_ladybirds_limits)
{
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/a.txt"sv, false, MAXIMUM_FILE_SIZE))).is_error() == false);
    EXPECT(parse_bundle_manifest(manifest_with(list("lists/a.txt"sv, false, MAXIMUM_FILE_SIZE + 1))).is_error());

    Vector<ByteString> lists;
    for (size_t i = 0; i < 5; ++i)
        lists.append(list(ByteString::formatted("lists/{}.txt", i), false, MAXIMUM_FILE_SIZE));
    EXPECT(parse_bundle_manifest(manifest_with(ByteString::join(","sv, lists))).is_error());

    EXPECT(parse_bundle_manifest(manifest_with(ByteString::join(","sv, Vector { list("lists/a.txt"sv), list("lists/a.txt"sv) }))).is_error());
}

TEST_CASE(signature_is_checked_against_the_key)
{
    Crypto::Curves::Ed25519 curve;
    auto private_key = MUST(curve.generate_private_key());
    auto public_key = MUST(curve.generate_public_key(private_key));
    auto manifest = manifest_with(list("lists/a.txt"sv));
    auto signature_bytes = MUST(curve.sign(private_key, manifest.bytes()));
    auto signature = MUST(encode_base64(signature_bytes));

    EXPECT(bundle_signature_is_valid(manifest.bytes(), signature, public_key));
    // manifest.json.sig may end with a newline.
    EXPECT(bundle_signature_is_valid(manifest.bytes(), ByteString::formatted("{}\n", signature), public_key));

    auto tampered = manifest_with(list("lists/b.txt"sv));
    EXPECT(!bundle_signature_is_valid(tampered.bytes(), signature, public_key));

    auto other_private_key = MUST(curve.generate_private_key());
    auto other_public_key = MUST(curve.generate_public_key(other_private_key));
    EXPECT(!bundle_signature_is_valid(manifest.bytes(), signature, other_public_key));

    EXPECT(!bundle_signature_is_valid(manifest.bytes(), "not base64!"sv, public_key));
    EXPECT(!bundle_signature_is_valid(manifest.bytes(), ""sv, public_key));
    EXPECT(!bundle_signature_is_valid(manifest.bytes(), MUST(encode_base64(public_key)), public_key));
}

TEST_CASE(files_are_checked_by_size_and_hash)
{
    EXPECT_EQ(sha256_hex("abc"sv.bytes()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"sv);

    BundleFile file { "lists/a.txt", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" };
    EXPECT(file_matches(file, "abc"sv.bytes()));
    EXPECT(!file_matches(file, "abd"sv.bytes()));
    EXPECT(!file_matches(file, "abcd"sv.bytes()));
}

// The manifest Xenon ships, which Meta/update-filter-lists.py writes, reads as a bundle.
TEST_CASE(bundled_manifest_reads)
{
    auto file = MUST(Core::File::open(XENON_SOURCE_DIR "/UI/Resources/filter-lists/manifest.json"sv, Core::File::OpenMode::Read));
    auto contents = MUST(file->read_until_eof());
    auto manifest = MUST(parse_bundle_manifest(contents));

    EXPECT(!manifest.files.is_empty());
    EXPECT(manifest.file("resources/ublock-resources.json"sv).has_value());
    EXPECT(manifest.file("trusted-lists/ublock-filters.txt"sv).has_value());
    EXPECT(manifest.file("lists/easylist.txt"sv).has_value());
}
