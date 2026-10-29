/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/JsonObject.h>
#include <AK/LexicalPath.h>
#include <LibCore/Directory.h>
#include <LibCore/File.h>
#include <LibFileSystem/FileSystem.h>
#include <LibTest/TestCase.h>
#include <UI/Extensions/NativeMessaging.h>

#include <stdlib.h>
#include <string.h>

using namespace Ladybird::Extensions;

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path[] = "/tmp/xenon-native-messaging-test-XXXXXX";
        VERIFY(mkdtemp(path));
        m_path = MUST(FileSystem::real_path(StringView { path, strlen(path) }));
    }

    ~TemporaryDirectory()
    {
        (void)FileSystem::remove(m_path, FileSystem::RecursionMode::Allowed);
    }

    ByteString const& path() const { return m_path; }

    ByteString write(StringView relative_path, StringView contents) const
    {
        auto path = LexicalPath::join(m_path, relative_path);
        MUST(Core::Directory::create(path.parent(), Core::Directory::CreateDirectories::Yes));
        auto file = MUST(Core::File::open(path.string(), Core::File::OpenMode::Write | Core::File::OpenMode::Truncate));
        MUST(file->write_until_depleted(contents.bytes()));
        return path.string();
    }

private:
    ByteString m_path;
};

static void use_hosts_directory(ByteString const& path)
{
    VERIFY(setenv("XENON_NATIVE_MESSAGING_HOSTS", path.characters(), 1) == 0);
}

TEST_CASE(host_name_validation)
{
    EXPECT(is_valid_native_host_name("com.apple.passwordmanager"sv));
    EXPECT(is_valid_native_host_name("com.xenon.test"sv));
    EXPECT(is_valid_native_host_name("a"sv));
    EXPECT(!is_valid_native_host_name(""sv));
    EXPECT(!is_valid_native_host_name(".com.apple"sv));
    EXPECT(!is_valid_native_host_name("com.apple."sv));
    EXPECT(!is_valid_native_host_name("com..apple"sv));
    EXPECT(!is_valid_native_host_name("Com.Apple"sv));
    EXPECT(!is_valid_native_host_name("com.apple/passwordmanager"sv));
}

TEST_CASE(native_message_framing)
{
    JsonObject payload;
    payload.set("ping"sv, 1);
    auto encoded = encode_native_message(JsonValue { payload });
    EXPECT(encoded.size() > sizeof(u32));

    ByteBuffer buffer;
    MUST(buffer.try_append(encoded.bytes()));
    auto decoded = MUST(take_native_message(buffer));
    EXPECT(decoded.has_value());
    EXPECT(decoded->is_object());
    EXPECT_EQ(decoded->as_object().get_integer<int>("ping"sv).value(), 1);
    EXPECT_EQ(buffer.size(), 0u);

    ByteBuffer partial;
    MUST(partial.try_append(ReadonlyBytes { encoded.data(), 2 }));
    auto incomplete = MUST(take_native_message(partial));
    EXPECT(!incomplete.has_value());
    EXPECT_EQ(partial.size(), 2u);
}

TEST_CASE(native_message_too_large_is_refused)
{
    u32 size = 1024 * 1024 + 1;
    ByteBuffer buffer;
    MUST(buffer.try_append(ReadonlyBytes { &size, sizeof(size) }));
    auto result = take_native_message(buffer);
    EXPECT(result.is_error());
    EXPECT_EQ(result.error(), NATIVE_HOST_COMMUNICATION_ERROR);
}

TEST_CASE(finds_a_host_that_allows_the_extension)
{
    TemporaryDirectory directory;
    use_hosts_directory(directory.path());
    directory.write("com.xenon.test.json"sv, R"({
        "name": "com.xenon.test",
        "description": "test",
        "path": "/usr/bin/true",
        "type": "stdio",
        "allowed_origins": ["chrome-extension://abcdefghijklmnopabcdefghijklmnop/"]
    })"sv);

    auto host = MUST(find_native_host("com.xenon.test"sv, "abcdefghijklmnopabcdefghijklmnop"sv));
    EXPECT_EQ(host.path, "/usr/bin/true"sv);
}

TEST_CASE(missing_host)
{
    TemporaryDirectory directory;
    use_hosts_directory(directory.path());
    auto result = find_native_host("com.xenon.missing"sv, "abcdefghijklmnopabcdefghijklmnop"sv);
    EXPECT(result.is_error());
    EXPECT_EQ(result.error(), NATIVE_HOST_NOT_FOUND);
}

TEST_CASE(forbidden_origin)
{
    TemporaryDirectory directory;
    use_hosts_directory(directory.path());
    directory.write("com.xenon.test.json"sv, R"({
        "name": "com.xenon.test",
        "description": "test",
        "path": "/usr/bin/true",
        "type": "stdio",
        "allowed_origins": ["chrome-extension://otheridotheridotheridotheridothe/"]
    })"sv);

    auto result = find_native_host("com.xenon.test"sv, "abcdefghijklmnopabcdefghijklmnop"sv);
    EXPECT(result.is_error());
    EXPECT_EQ(result.error(), NATIVE_HOST_FORBIDDEN);
}

TEST_CASE(relative_path_is_refused)
{
    TemporaryDirectory directory;
    use_hosts_directory(directory.path());
    directory.write("com.xenon.test.json"sv, R"({
        "name": "com.xenon.test",
        "path": "true",
        "type": "stdio",
        "allowed_origins": ["chrome-extension://abcdefghijklmnopabcdefghijklmnop/"]
    })"sv);

    auto result = find_native_host("com.xenon.test"sv, "abcdefghijklmnopabcdefghijklmnop"sv);
    EXPECT(result.is_error());
    EXPECT_EQ(result.error(), NATIVE_HOST_NOT_FOUND);
}

TEST_CASE(name_mismatch_is_refused)
{
    TemporaryDirectory directory;
    use_hosts_directory(directory.path());
    directory.write("com.xenon.test.json"sv, R"({
        "name": "com.other.host",
        "path": "/usr/bin/true",
        "type": "stdio",
        "allowed_origins": ["chrome-extension://abcdefghijklmnopabcdefghijklmnop/"]
    })"sv);

    auto result = find_native_host("com.xenon.test"sv, "abcdefghijklmnopabcdefghijklmnop"sv);
    EXPECT(result.is_error());
    EXPECT_EQ(result.error(), NATIVE_HOST_NOT_FOUND);
}
