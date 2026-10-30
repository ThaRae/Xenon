/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <LibCore/File.h>
#include <LibTest/TestCase.h>
#include <LibURL/Parser.h>
#include <UI/CertificateDetails.h>

using namespace Ladybird;

static ByteBuffer certificate(StringView name)
{
    auto path = ByteString::formatted("{}/Tests/CertificateErrors/{}.der", XENON_SOURCE_DIR, name);
    auto file = MUST(Core::File::open(path, Core::File::OpenMode::Read));
    return MUST(file->read_until_eof());
}

static JsonObject describe(StringView name, StringView url)
{
    auto der_certificate = certificate(name);
    return describe_rejected_certificate(der_certificate, URL::Parser::basic_parse(url).release_value());
}

static Vector<ByteString> strings(JsonObject const& description, StringView key)
{
    Vector<ByteString> values;
    if (auto array = description.get_array(key); array.has_value()) {
        for (auto const& value : array->values())
            values.append(value.as_string().to_byte_string());
    }
    return values;
}

static Vector<ByteString> problems(JsonObject const& description)
{
    return strings(description, "problems"sv);
}

static ByteString string(JsonObject const& description, StringView key)
{
    auto value = description.get_string(key);
    return value.has_value() ? value->to_byte_string() : ByteString {};
}

static i64 milliseconds(JsonObject const& description, StringView key)
{
    return description.get_i64(key).value_or(0);
}

TEST_CASE(self_signed_certificate_for_the_host)
{
    auto description = describe("self-signed"sv, "https://127.0.0.1:8443/"sv);
    EXPECT_EQ(problems(description), (Vector<ByteString> { "selfSigned" }));
    EXPECT_EQ(string(description, "subject"sv), "127.0.0.1"sv);
    EXPECT_EQ(string(description, "issuer"sv), "127.0.0.1"sv);
    EXPECT_EQ(strings(description, "names"sv), (Vector<ByteString> { "127.0.0.1", "::1", "localhost" }));

    EXPECT_EQ(string(description, "fingerprint"sv).length(), 95u);

    EXPECT_EQ(milliseconds(description, "validFrom"sv), 1767225600000);
    EXPECT_EQ(milliseconds(description, "validUntil"sv), 4922899199000);
}

TEST_CASE(self_signed_certificate_by_its_other_names)
{
    EXPECT_EQ(problems(describe("self-signed"sv, "https://localhost/"sv)), (Vector<ByteString> { "selfSigned" }));
    EXPECT_EQ(problems(describe("self-signed"sv, "https://[::1]:8006/"sv)), (Vector<ByteString> { "selfSigned" }));
}

TEST_CASE(self_signed_certificate_for_another_host)
{
    EXPECT_EQ(problems(describe("self-signed"sv, "https://homelab.example/"sv)), (Vector<ByteString> { "selfSigned", "host" }));
    EXPECT_EQ(problems(describe("self-signed"sv, "https://10.0.0.2/"sv)), (Vector<ByteString> { "selfSigned", "host" }));
}

TEST_CASE(self_signed_certificate_for_servers_only)
{
    EXPECT_EQ(problems(describe("self-signed-server"sv, "https://127.0.0.1/"sv)), (Vector<ByteString> { "selfSigned" }));
}

TEST_CASE(certificate_from_an_untrusted_authority)
{
    auto description = describe("untrusted"sv, "https://127.0.0.1/"sv);
    EXPECT_EQ(problems(description), (Vector<ByteString> { "untrusted" }));
    EXPECT_EQ(string(description, "issuer"sv), "Xenon Test Authority"sv);
    EXPECT_EQ(string(description, "issuerOrganization"sv), "Xenon tests"sv);
}

TEST_CASE(expired_certificate)
{
    auto description = describe("expired"sv, "https://127.0.0.1/"sv);
    EXPECT_EQ(problems(description), (Vector<ByteString> { "expired" }));
    EXPECT_EQ(milliseconds(description, "validUntil"sv), 1609459200000);
}

TEST_CASE(certificate_that_is_not_valid_yet)
{
    auto description = describe("not-yet-valid"sv, "https://127.0.0.1/"sv);
    EXPECT_EQ(problems(description), (Vector<ByteString> { "notYetValid" }));
    EXPECT_EQ(milliseconds(description, "validFrom"sv), 4070908800000);
}

TEST_CASE(certificate_for_another_host)
{
    auto description = describe("other-host"sv, "https://127.0.0.1/"sv);
    EXPECT_EQ(problems(description), (Vector<ByteString> { "host" }));
    EXPECT_EQ(strings(description, "names"sv), (Vector<ByteString> { "example.com" }));
    EXPECT_EQ(problems(describe("other-host"sv, "https://example.com/"sv)), (Vector<ByteString> { "untrusted" }));
}

TEST_CASE(certificate_that_does_not_parse)
{
    auto description = describe_rejected_certificate("not a certificate"sv.bytes(), URL::Parser::basic_parse("https://127.0.0.1/"sv).release_value());
    EXPECT_EQ(problems(description), (Vector<ByteString> { "untrusted" }));
    EXPECT(!description.has("subject"sv));
}
