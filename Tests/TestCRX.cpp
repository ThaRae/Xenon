/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Hex.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <LibCore/File.h>
#include <LibFileSystem/FileSystem.h>
#include <LibTest/TestCase.h>
#include <UI/Extensions/CRX.h>
#include <UI/Extensions/Extension.h>
#include <UI/Extensions/WebStore.h>

#include <stdlib.h>

using namespace Ladybird::Extensions;

static ByteBuffer read_fixture(StringView name)
{
    auto file = MUST(Core::File::open(ByteString::formatted("{}/Tests/ExtensionStore/{}", XENON_SOURCE_DIR, name), Core::File::OpenMode::Read));
    return MUST(file->read_until_eof());
}

static String key(StringView name)
{
    auto json = read_fixture("keys.json"sv);
    auto keys = MUST(JsonValue::from_string(StringView { json }));
    return *keys.as_object().get_string(name);
}

static ByteBuffer publisher_key_hash()
{
    return MUST(decode_hex(key("publisherKeyHash"sv)));
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path[] = "/tmp/xenon-crx-test-XXXXXX";
        VERIFY(mkdtemp(path));
        m_path = MUST(FileSystem::real_path(StringView { path, strlen(path) }));
    }

    ~TemporaryDirectory()
    {
        (void)FileSystem::remove(m_path, FileSystem::RecursionMode::Allowed);
    }

    ByteString const& path() const { return m_path; }

private:
    ByteString m_path;
};

TEST_CASE(crx_from_the_store_checks_out)
{
    auto store_key_hash = publisher_key_hash();
    auto crx = read_fixture("bitwarden-1.0.crx"sv);
    auto packed = verify_crx(crx, key("bitwarden"sv), store_key_hash);
    EXPECT(!packed.is_error());
    EXPECT_EQ(packed.value().id, key("bitwarden"sv));
    EXPECT_EQ(extension_id_from_bytes(packed.value().public_key), key("bitwarden"sv));
    EXPECT(packed.value().archive.size() > 0);
}

TEST_CASE(crx_for_another_extension_is_turned_down)
{
    auto store_key_hash = publisher_key_hash();
    auto crx = read_fixture("bitwarden-1.0.crx"sv);
    auto packed = verify_crx(crx, key("proton-pass"sv), store_key_hash);
    EXPECT(packed.is_error());
    EXPECT(packed.error().contains("another extension"sv));
}

TEST_CASE(crx_changed_after_signing_is_turned_down)
{
    auto store_key_hash = publisher_key_hash();
    auto crx = read_fixture("bitwarden-3.0-tampered.crx"sv);
    auto packed = verify_crx(crx, key("bitwarden"sv), store_key_hash);
    EXPECT(packed.is_error());
    EXPECT(packed.error().contains("signatures isn't valid"sv));
}

TEST_CASE(crx_has_to_be_signed_by_the_store)
{
    auto store_key_hash = publisher_key_hash();
    auto unpublished = read_fixture("bitwarden-3.0-unpublished.crx"sv);
    auto packed = verify_crx(unpublished, key("bitwarden"sv), store_key_hash);
    EXPECT(packed.is_error());
    EXPECT(packed.error().contains("Chrome Web Store"sv));

    EXPECT(!verify_crx(unpublished, key("bitwarden"sv), {}).is_error());

    auto published = read_fixture("bitwarden-1.0.crx"sv);
    EXPECT(verify_crx(published, key("bitwarden"sv), chrome_web_store_publisher_key_hash()).is_error());
}

TEST_CASE(only_crx3_files_are_read)
{
    auto crx = read_fixture("bitwarden-1.0.crx"sv);
    auto id = key("bitwarden"sv);

    EXPECT(verify_crx("PK\x03\x04 a zip"sv.bytes(), id, {}).is_error());
    EXPECT(verify_crx(crx.bytes().slice(0, 11), id, {}).is_error());
    EXPECT(verify_crx(crx.bytes().slice(0, 100), id, {}).is_error());

    auto crx2 = MUST(ByteBuffer::copy(crx));
    crx2[4] = 2;
    EXPECT(verify_crx(crx2, id, {}).is_error());

    auto oversized_header = MUST(ByteBuffer::copy(crx));
    oversized_header[11] = 0x7f;
    EXPECT(verify_crx(oversized_header, id, {}).is_error());
}

TEST_CASE(crx_unpacks)
{
    auto store_key_hash = publisher_key_hash();
    TemporaryDirectory directory;
    auto crx = read_fixture("bitwarden-2.0.crx"sv);
    auto packed = MUST(verify_crx(crx, key("bitwarden"sv), store_key_hash));

    auto unpacked = LexicalPath::join(directory.path(), "unpacked"sv).string();
    EXPECT(!unpack_zip(packed.archive, unpacked).is_error());

    auto manifest = MUST(read_extension_json(LexicalPath::join(unpacked, "manifest.json"sv).string(), unpacked));
    EXPECT_EQ(manifest.get_string("version"sv).value(), "2.0"sv);
    EXPECT(FileSystem::is_regular_file(LexicalPath::join(unpacked, "content.js"sv).string()));

    EXPECT(unpack_zip(packed.archive, unpacked).is_error());
}

TEST_CASE(archive_entries_stay_inside_their_directory)
{
    auto store_key_hash = publisher_key_hash();
    TemporaryDirectory directory;
    auto crx = read_fixture("escape.crx"sv);
    auto packed = MUST(verify_crx(crx, key("bitwarden"sv), store_key_hash));

    auto unpacked = LexicalPath::join(directory.path(), "unpacked"sv).string();
    auto result = unpack_zip(packed.archive, unpacked);
    EXPECT(result.is_error());
    EXPECT(result.error().contains("../escape.txt"sv));
    EXPECT(!FileSystem::exists(LexicalPath::join(directory.path(), "escape.txt"sv).string()));
}

TEST_CASE(versions_compare_as_chrome_does)
{
    EXPECT(WebStore::is_newer_version("2026.10.1"sv, "2026.10.0"sv));
    EXPECT(WebStore::is_newer_version("1.10"sv, "1.9"sv));
    EXPECT(WebStore::is_newer_version("1.0.0.1"sv, "1"sv));
    EXPECT(!WebStore::is_newer_version("1.0"sv, "1"sv));
    EXPECT(!WebStore::is_newer_version("1.43.0"sv, "1.43.0"sv));
    EXPECT(!WebStore::is_newer_version("1.42.9"sv, "1.43.0"sv));
    EXPECT(!WebStore::is_newer_version("2.x"sv, "1.0"sv));
    EXPECT(!WebStore::is_newer_version("70000"sv, "1.0"sv));
}

static constexpr auto UPDATE_CHECK = R"(<?xml version="1.0" encoding="UTF-8"?><gupdate xmlns="http://www.google.com/update2/response" protocol="2.0" server="prod"><daystart elapsed_days="7239" elapsed_seconds="24616"/><app appid="nngceckbapebfimnlniiiahkandclblb" cohort="1::" cohortname="" status="ok"><updatecheck _esbAllowlist="true" codebase="https://clients2.googleusercontent.com/crx/blobs/abc/NNGCECKBAPEBFIMNLNIIIAHKANDCLBLB_2026_10_0_0.crx" fp="1.f2fe" hash_sha256="F2FEB7FC46D2550BEF9B69AF1CC358AEA26B90A947645F9DE16EC14D9296EF69" protected="0" size="24875732" status="ok" version="2026.10.0"/></app></gupdate>)"sv;

TEST_CASE(update_check_names_the_newest_version)
{
    auto update = WebStore::parse_update_check(UPDATE_CHECK, "nngceckbapebfimnlniiiahkandclblb"sv);
    EXPECT(!update.is_error());
    EXPECT(update.value().has_value());
    EXPECT_EQ(update.value()->version, "2026.10.0"sv);
    EXPECT_EQ(update.value()->url.serialize(), "https://clients2.googleusercontent.com/crx/blobs/abc/NNGCECKBAPEBFIMNLNIIIAHKANDCLBLB_2026_10_0_0.crx"sv);
    EXPECT_EQ(update.value()->size.value(), 24875732u);
    EXPECT_EQ(update.value()->sha256.value(), "f2feb7fc46d2550bef9b69af1cc358aea26b90a947645f9de16ec14d9296ef69"sv);
}

TEST_CASE(update_check_without_a_newer_version)
{
    auto update = WebStore::parse_update_check(R"(<?xml version="1.0" encoding="UTF-8"?><gupdate xmlns="http://www.google.com/update2/response" protocol="2.0" server="prod"><app appid="ghmbeldphafepmbegfdlkpapadhbakde" cohort="1::" cohortname="" status="ok"><updatecheck _esbAllowlist="true" status="noupdate"/></app></gupdate>)"sv, "ghmbeldphafepmbegfdlkpapadhbakde"sv);
    EXPECT(!update.is_error());
    EXPECT(!update.value().has_value());
}

TEST_CASE(update_check_errors)
{
    EXPECT(WebStore::parse_update_check(UPDATE_CHECK, "ghmbeldphafepmbegfdlkpapadhbakde"sv).is_error());
    EXPECT(WebStore::parse_update_check("not xml"sv, "nngceckbapebfimnlniiiahkandclblb"sv).is_error());
    EXPECT(WebStore::parse_update_check(R"(<gupdate><app appid="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" status="error-unknownApplication"/></gupdate>)"sv, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"sv).is_error());
    EXPECT(WebStore::parse_update_check(R"(<gupdate><app appid="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" status="ok"><updatecheck status="ok" version="1.0"/></app></gupdate>)"sv, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"sv).is_error());
}
