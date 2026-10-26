/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <LibCore/Directory.h>
#include <LibCore/File.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <LibTest/TestCase.h>
#include <LibURL/Parser.h>
#include <UI/Extensions/Extension.h>
#include <UI/Extensions/MatchPattern.h>
#include <UI/Extensions/Storage.h>

#include <stdlib.h>

using namespace Ladybird::Extensions;

// A directory that's deleted again at the end of the test.
class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path[] = "/tmp/xenon-extension-test-XXXXXX";
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

static JsonObject json(StringView text)
{
    return MUST(JsonValue::from_string(text)).as_object();
}

static ErrorOr<Manifest, String> parse(StringView text)
{
    return Manifest::parse(json(text));
}

static URL::URL url(StringView text)
{
    auto parsed = URL::Parser::basic_parse(text);
    VERIFY(parsed.has_value());
    return parsed.release_value();
}

TEST_CASE(extension_ids_match_chrome)
{
    // From Chromium's crx_file/id_util_unittest.cc.
    EXPECT_EQ(extension_id_from_bytes("test"sv.bytes()), "jpignaibiiemhngfjkcpokkamffknabf"sv);
    EXPECT_EQ(extension_id_from_bytes("_"sv.bytes()), "ncocknphbhhlhkikpnnlmbcnbgdempcd"sv);
    EXPECT_EQ(extension_id_from_bytes("this_string_is_longer_than_a_single_sha256_hash_digest"sv.bytes()), "jimneklojkjdibfkgiiophfhjhbdgcfi"sv);

    EXPECT_EQ(extension_id_from_key("dGVzdA=="sv).value(), "jpignaibiiemhngfjkcpokkamffknabf"sv);
    EXPECT(!extension_id_from_key("not base64!"sv).has_value());

    EXPECT(is_valid_extension_id("jpignaibiiemhngfjkcpokkamffknabf"sv));
    EXPECT(!is_valid_extension_id("jpignaibiiemhngfjkcpokkamffknabz"sv));
    EXPECT(!is_valid_extension_id("jpignaibiiemhngf"sv));
}

TEST_CASE(json_comments_are_allowed)
{
    auto stripped = strip_json_comments(R"({
        // A comment
        "name": "a // not a comment", /* another */
        "url": "http://example.com/*"
    })"sv);
    auto object = json(stripped);
    EXPECT_EQ(object.get_string("name"sv).value(), "a // not a comment"sv);
    EXPECT_EQ(object.get_string("url"sv).value(), "http://example.com/*"sv);
}

TEST_CASE(minimal_manifest)
{
    auto manifest = MUST(parse(R"({ "manifest_version": 3, "name": "Test", "version": "1.0" })"sv));
    EXPECT_EQ(manifest.name, "Test"sv);
    EXPECT_EQ(manifest.version, "1.0"sv);
    EXPECT_EQ(manifest.extension_pages_csp, DEFAULT_EXTENSION_PAGES_CSP);
    EXPECT(!manifest.action.has_value());
    EXPECT(manifest.warnings.is_empty());
}

TEST_CASE(manifest_errors)
{
    EXPECT(parse(R"({ "manifest_version": 2, "name": "Test", "version": "1.0" })"sv).error().contains("no longer supported"sv));
    EXPECT(parse(R"({ "name": "Test", "version": "1.0" })"sv).error().contains("manifest_version"sv));
    EXPECT(parse(R"({ "manifest_version": 3, "version": "1.0" })"sv).error().contains("'name'"sv));

    for (auto version : { "1.0.0.0.0"sv, "1..0"sv, "01.0"sv, "65536"sv, "1.a"sv, ""sv }) {
        auto manifest = MUST(String::formatted(R"({{ "manifest_version": 3, "name": "Test", "version": "{}" }})", version));
        EXPECT(parse(manifest).error().contains("'version'"sv));
    }
    EXPECT(!parse(R"({ "manifest_version": 3, "name": "Test", "version": "65535.0.1.2" })"sv).is_error());

    EXPECT(parse(R"({ "manifest_version": 3, "name": "T", "version": "1", "background": { "scripts": ["a.js"] } })"sv).error().contains("background.service_worker"sv));
    EXPECT(parse(R"({ "manifest_version": 3, "name": "T", "version": "1", "content_scripts": [{ "js": ["a.js"] }] })"sv).error().contains("matches"sv));
    EXPECT(parse(R"({ "manifest_version": 3, "name": "T", "version": "1", "web_accessible_resources": ["a.png"] })"sv).error().contains("must be objects"sv));
}

TEST_CASE(extension_pages_csp_cannot_allow_remote_code)
{
    auto manifest = [](StringView policy) {
        return parse(MUST(String::formatted(R"({{ "manifest_version": 3, "name": "T", "version": "1", "content_security_policy": {{ "extension_pages": "{}" }} }})", policy)));
    };
    EXPECT(manifest("script-src 'self' 'unsafe-eval'; object-src 'self'"sv).error().contains("Insecure CSP value"sv));
    EXPECT(manifest("script-src 'self' https://cdn.example.com"sv).error().contains("https://cdn.example.com"sv));
    EXPECT_EQ(MUST(manifest("script-src 'self' 'wasm-unsafe-eval'; object-src 'self'"sv)).extension_pages_csp, "script-src 'self' 'wasm-unsafe-eval'; object-src 'self'"sv);
    EXPECT(!manifest("script-src 'self' http://localhost:8080; img-src https:"sv).is_error());
}

TEST_CASE(bitwarden_like_manifest)
{
    auto manifest = MUST(parse(R"({
        "manifest_version": 3,
        "name": "Bitwarden Password Manager",
        "version": "2026.10.1",
        "default_locale": "en",
        "icons": { "48": "images/icon48.png", "16": "images/icon16.png", "128": "images/icon128.png" },
        "background": { "service_worker": "background.js" },
        "action": { "default_popup": "popup/index.html", "default_title": "Bitwarden", "default_icon": { "19": "images/icon19.png", "38": "images/icon38.png" } },
        "content_scripts": [
            { "matches": ["*://*/*", "file:///*"], "exclude_matches": ["*://*/*.xml*"], "js": ["content/content-message-handler.js"], "run_at": "document_start" },
            { "matches": ["*://*/*"], "js": ["content/trigger-autofill-script-injection.js"], "css": ["content/autofill.css"], "run_at": "document_start", "all_frames": true }
        ],
        "web_accessible_resources": [{ "resources": ["overlay/menu.html", "popup/fonts/*"], "matches": ["<all_urls>"], "use_dynamic_url": true }],
        "sandbox": { "pages": ["overlay/menu-button.html"] },
        "commands": { "_execute_action": { "suggested_key": { "default": "Ctrl+Shift+Y" } }, "lock_vault": { "description": "Lock the vault" } },
        "permissions": ["storage", "tabs", "unlimitedStorage"],
        "optional_permissions": ["nativeMessaging"],
        "host_permissions": ["https://*/*", "http://*/*"],
        "minimum_chrome_version": "134.0",
        "__chrome__comment": "ignored",
        "not_a_chrome_key": true
    })"sv));

    EXPECT_EQ(manifest.icons.size(), 3u);
    EXPECT_EQ(manifest.icons.first().size, 16u);
    EXPECT_EQ(best_icon(manifest.icons, 32).value(), "images/icon48.png"sv);
    EXPECT_EQ(best_icon(manifest.icons, 256).value(), "images/icon128.png"sv);
    EXPECT_EQ(manifest.background_service_worker.value(), "background.js"sv);
    EXPECT_EQ(manifest.action->default_popup.value(), "popup/index.html"sv);
    EXPECT_EQ(manifest.content_scripts.size(), 2u);
    EXPECT_EQ(manifest.content_scripts[0].run_at, RunAt::DocumentStart);
    EXPECT(!manifest.content_scripts[0].all_frames);
    EXPECT(manifest.content_scripts[1].all_frames);
    EXPECT_EQ(manifest.content_scripts[1].css.first(), "content/autofill.css"sv);
    EXPECT(manifest.web_accessible_resources.first().use_dynamic_url);
    EXPECT_EQ(manifest.sandbox_pages.first(), "overlay/menu-button.html"sv);
    EXPECT_EQ(manifest.commands.size(), 2u);
    EXPECT(manifest.has_permission("unlimitedStorage"sv));
    EXPECT(!manifest.has_permission("nativeMessaging"sv));
    EXPECT_EQ(manifest.warnings.size(), 1u);
    EXPECT(manifest.warnings.first().contains("not_a_chrome_key"sv));
}

TEST_CASE(match_patterns)
{
    auto matches = [](StringView pattern, StringView target) {
        return MUST(MatchPattern::parse(pattern)).matches(url(target));
    };

    EXPECT(matches("<all_urls>"sv, "https://example.com/"sv));
    EXPECT(matches("<all_urls>"sv, "file:///etc/hosts"sv));
    EXPECT(!matches("<all_urls>"sv, "chrome-extension://abc/page.html"sv));

    EXPECT(matches("*://*/*"sv, "http://example.com/a?b"sv));
    EXPECT(matches("*://*/*"sv, "https://example.com/"sv));
    EXPECT(!matches("*://*/*"sv, "file:///a"sv));

    EXPECT(matches("https://*.example.com/*"sv, "https://example.com/"sv));
    EXPECT(matches("https://*.example.com/*"sv, "https://a.b.example.com/x"sv));
    EXPECT(!matches("https://*.example.com/*"sv, "https://badexample.com/"sv));
    EXPECT(!matches("https://*.example.com/*"sv, "http://example.com/"sv));

    EXPECT(matches("https://example.com/foo*"sv, "https://example.com/foobar"sv));
    EXPECT(!matches("https://example.com/foo*"sv, "https://example.com/bar"sv));
    EXPECT(matches("*://*/*.xml*"sv, "https://example.com/feed.xml?x=1"sv));
    EXPECT(matches("http://localhost/*"sv, "http://localhost:8080/"sv));
    EXPECT(matches("http://localhost:8080/*"sv, "http://localhost:8080/"sv));
    EXPECT(!matches("http://localhost:8080/*"sv, "http://localhost:9090/"sv));
    EXPECT(matches("file:///*"sv, "file:///Users/a.html"sv));

    EXPECT(MatchPattern::parse("https://*foo.com/*"sv).is_error());
    EXPECT(MatchPattern::parse("https://foo.com"sv).is_error());
    EXPECT(MatchPattern::parse("chrome://settings/*"sv).is_error());

    EXPECT(glob_matches("*.example.com"sv, "www.example.com"sv));
    EXPECT(glob_matches("a?c"sv, "abc"sv, true));
    EXPECT(!glob_matches("a?c"sv, "abc"sv, false));
}

static TemporaryDirectory make_extension(StringView manifest)
{
    TemporaryDirectory directory;
    directory.write("manifest.json"sv, manifest);
    return directory;
}

TEST_CASE(load_unpacked_extension)
{
    auto directory = make_extension(R"({
        // Chrome allows comments here.
        "manifest_version": 3,
        "name": "__MSG_extension_name__",
        "description": "__MSG_description__",
        "version": "1.2.3",
        "default_locale": "en",
        "action": { "default_popup": "/popup.html", "default_title": "__MSG_title__" },
        "content_scripts": [{ "matches": ["<all_urls>"], "js": ["content.js"] }]
    })"sv);
    directory.write("popup.html"sv, "<!doctype html>"sv);
    directory.write("content.js"sv, ""sv);
    directory.write("_locales/en/messages.json"sv, R"({
        "extension_name": { "message": "Test Extension" },
        "description": { "message": "Default description" },
        "title": { "message": "Open $name$", "placeholders": { "name": { "content": "the popup" } } },
        "greeting": { "message": "Hello, $who$! It costs $$5.", "placeholders": { "WHO": { "content": "$1" } } }
    })"sv);
    directory.write("_locales/en_GB/messages.json"sv, R"({ "description": { "message": "British description" } })"sv);

    auto extension = MUST(Extension::load_unpacked(directory.path(), "en-GB"sv));
    EXPECT_EQ(extension->id(), extension_id_for_path(directory.path()));
    EXPECT(is_valid_extension_id(extension->id()));
    EXPECT_EQ(extension->manifest().name, "Test Extension"sv);
    EXPECT_EQ(extension->manifest().description.value(), "British description"sv);
    EXPECT_EQ(extension->manifest().action->default_title.value(), "Open the popup"sv);
    EXPECT_EQ(extension->manifest().json.get_string("name"sv).value(), "Test Extension"sv);
    EXPECT_EQ(extension->manifest().action->default_popup.value(), "popup.html"sv);

    auto const& locales = extension->locales();
    EXPECT_EQ(locales.message("GREETING"sv, Array { "world"_string }).value(), "Hello, world! It costs $5."sv);
    EXPECT_EQ(locales.message("greeting"sv).value(), "Hello, ! It costs $5."sv);
    EXPECT(!locales.message("missing"sv).has_value());
    EXPECT_EQ(locales.message("@@extension_id"sv).value(), extension->id());
    EXPECT_EQ(locales.message("@@ui_locale"sv).value(), "en_GB"sv);
    EXPECT_EQ(locales.localize("a __MSG_extension_name__ b __MSG_missing__ c"sv), "a Test Extension b  c"sv);

    EXPECT_EQ(extension->base_url().serialize(), MUST(String::formatted("chrome-extension://{}/", extension->id())));
    EXPECT_EQ(extension->url_for("/popup.html"sv).serialize(), MUST(String::formatted("chrome-extension://{}/popup.html", extension->id())));
}

TEST_CASE(extension_with_key_has_its_id)
{
    auto directory = make_extension(R"({ "manifest_version": 3, "name": "T", "version": "1", "key": "dGVzdA==" })"sv);
    EXPECT_EQ(MUST(Extension::load_unpacked(directory.path(), "en-US"sv))->id(), "jpignaibiiemhngfjkcpokkamffknabf"sv);
}

TEST_CASE(load_errors)
{
    TemporaryDirectory empty;
    EXPECT(Extension::load_unpacked(empty.path(), "en"sv).error().contains("Manifest file is missing"sv));

    auto missing_script = make_extension(R"({ "manifest_version": 3, "name": "T", "version": "1", "content_scripts": [{ "matches": ["<all_urls>"], "js": ["missing.js"] }] })"sv);
    EXPECT(Extension::load_unpacked(missing_script.path(), "en"sv).error().contains("missing.js"sv));

    auto missing_locales = make_extension(R"({ "manifest_version": 3, "name": "T", "version": "1", "default_locale": "en" })"sv);
    EXPECT(Extension::load_unpacked(missing_locales.path(), "en"sv).error().contains("_locales"sv));

    auto undeclared_locale = make_extension(R"({ "manifest_version": 3, "name": "T", "version": "1" })"sv);
    undeclared_locale.write("_locales/en/messages.json"sv, "{}"sv);
    EXPECT(Extension::load_unpacked(undeclared_locale.path(), "en"sv).error().contains("default_locale"sv));

    auto invalid_json = make_extension(R"({ "manifest_version": 3, )"sv);
    EXPECT(Extension::load_unpacked(invalid_json.path(), "en"sv).error().contains("valid JSON"sv));
}

TEST_CASE(resolving_urls_stays_inside_the_extension)
{
    auto directory = make_extension(R"({ "manifest_version": 3, "name": "T", "version": "1" })"sv);
    directory.write("popup/index.html"sv, "<!doctype html>"sv);
    directory.write("a b.js"sv, ""sv);
    TemporaryDirectory outside;
    auto secret = outside.write("secret.txt"sv, "secret"sv);
    MUST(Core::System::symlink(secret, LexicalPath::join(directory.path(), "link.txt"sv).string()));
    MUST(Core::System::symlink(LexicalPath::join(directory.path(), "popup/index.html"sv).string(), LexicalPath::join(directory.path(), "inside.html"sv).string()));

    auto extension = MUST(Extension::load_unpacked(directory.path(), "en"sv));
    auto resolve = [&](StringView path) {
        return extension->resolve(url(MUST(String::formatted("chrome-extension://{}{}", extension->id(), path))));
    };

    EXPECT_EQ(MUST(resolve("/popup/index.html"sv)), LexicalPath::join(directory.path(), "popup/index.html"sv).string());
    EXPECT_EQ(MUST(resolve("/popup/index.html?query#fragment"sv)), LexicalPath::join(directory.path(), "popup/index.html"sv).string());
    EXPECT_EQ(MUST(resolve("/a%20b.js"sv)), LexicalPath::join(directory.path(), "a b.js"sv).string());
    EXPECT(!resolve("/inside.html"sv).is_error());

    EXPECT(resolve("/"sv).is_error());
    EXPECT(resolve("/popup"sv).is_error());
    EXPECT(resolve("/missing.html"sv).is_error());
    EXPECT(resolve("/link.txt"sv).is_error());
    EXPECT(resolve("/%2e%2e/secret.txt"sv).is_error());
    EXPECT(resolve("/popup/..%2f..%2fsecret.txt"sv).is_error());
    EXPECT(resolve("/popup%2findex.html"sv).is_error());
    EXPECT(resolve("/..%5c..%5csecret.txt"sv).is_error());
    EXPECT(resolve("/popup/index.html%00"sv).is_error());

    // Another extension's URL, even for a file that exists here.
    EXPECT(extension->resolve(url("chrome-extension://jpignaibiiemhngfjkcpokkamffknabf/popup/index.html"sv)).is_error());
}

TEST_CASE(web_accessible_resources)
{
    auto directory = make_extension(R"({
        "manifest_version": 3, "name": "T", "version": "1",
        "web_accessible_resources": [
            { "resources": ["overlay/*.html", "icon.png"], "matches": ["https://*.example.com/*"] },
            { "resources": ["shared.js"], "matches": [], "extension_ids": ["*"] }
        ],
        "sandbox": { "pages": ["sandboxed.html"] }
    })"sv);
    auto extension = MUST(Extension::load_unpacked(directory.path(), "en"sv));

    auto https_example = url("https://www.example.com/page"sv).origin();
    EXPECT(extension->is_web_accessible("/overlay/menu.html"sv, https_example));
    EXPECT(extension->is_web_accessible("icon.png"sv, https_example));
    EXPECT(!extension->is_web_accessible("popup.html"sv, https_example));
    EXPECT(!extension->is_web_accessible("overlay/menu.html"sv, url("https://evil.com/"sv).origin()));
    EXPECT(!extension->is_web_accessible("shared.js"sv, https_example));

    EXPECT(extension->is_sandboxed_page("/sandboxed.html"sv));
    EXPECT(!extension->is_sandboxed_page("/popup.html"sv));
}

TEST_CASE(storage_changes_and_persistence)
{
    TemporaryDirectory directory;
    auto file = LexicalPath::join(directory.path(), "Storage/abc/local.json"sv).string();

    {
        auto area = MUST(StorageArea::create(StorageArea::Kind::Local, StorageArea::quota_for(StorageArea::Kind::Local, false), file));
        auto changes = MUST(area->set(json(R"({ "a": 1, "b": { "c": [1, 2] } })"sv)));
        EXPECT_EQ(changes.serialized(), R"({"a":{"newValue":1},"b":{"newValue":{"c":[1,2]}}})"sv);

        changes = MUST(area->set(json(R"({ "a": 2, "b": { "c": [1, 2] } })"sv)));
        EXPECT_EQ(changes.serialized(), R"({"a":{"oldValue":1,"newValue":2}})"sv);

        EXPECT_EQ(area->get(Vector { "a"_string, "missing"_string }).serialized(), R"({"a":2})"sv);
        EXPECT_EQ(area->bytes_in_use(Vector { "a"_string }), 2u);
    }

    auto area = MUST(StorageArea::create(StorageArea::Kind::Local, StorageArea::quota_for(StorageArea::Kind::Local, false), file));
    EXPECT_EQ(area->get({}).serialized(), R"({"a":2,"b":{"c":[1,2]}})"sv);

    EXPECT_EQ(area->remove(Vector { "a"_string, "missing"_string }).serialized(), R"({"a":{"oldValue":2}})"sv);
    EXPECT_EQ(area->clear().serialized(), R"({"b":{"oldValue":{"c":[1,2]}}})"sv);
    EXPECT_EQ(area->keys().size(), 0u);
}

TEST_CASE(storage_quotas)
{
    auto sync = MUST(StorageArea::create(StorageArea::Kind::Sync, StorageArea::quota_for(StorageArea::Kind::Sync, false), {}));
    auto big = MUST(String::formatted(R"({{ "big": "{}" }})", MUST(String::repeated('x', 9000))));
    EXPECT(sync->set(json(big)).error().contains("QUOTA_BYTES_PER_ITEM"sv));
    EXPECT_EQ(sync->keys().size(), 0u);

    auto session = MUST(StorageArea::create(StorageArea::Kind::Session, { .bytes = 20, .bytes_per_item = {}, .max_items = {} }, {}));
    EXPECT(!session->set(json(R"({ "a": "0123456789" })"sv)).is_error());
    EXPECT(session->set(json(R"({ "b": "0123456789" })"sv)).error().contains("QUOTA_BYTES"sv));
    EXPECT(!session->set(json(R"({ "a": "01234567890123" })"sv)).is_error());

    auto managed = MUST(StorageArea::create(StorageArea::Kind::Managed, StorageArea::quota_for(StorageArea::Kind::Managed, false), {}));
    EXPECT(managed->set(json(R"({ "a": 1 })"sv)).is_error());
}

TEST_CASE(mime_types)
{
    EXPECT_EQ(mime_type_for_path("popup/index.html"sv), "text/html; charset=utf-8"sv);
    EXPECT_EQ(mime_type_for_path("background.js"sv), "text/javascript; charset=utf-8"sv);
    EXPECT_EQ(mime_type_for_path("sdk.wasm"sv), "application/wasm"sv);
    EXPECT_EQ(mime_type_for_path("fonts/a.woff2"sv), "font/woff2"sv);
}
