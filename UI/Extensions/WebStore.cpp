/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Hex.h>
#include <AK/StringBuilder.h>
#include <LibCore/Environment.h>
#include <LibCrypto/Hash/SHA2.h>
#include <LibURL/Parser.h>
#include <LibWebCommon/Loader/UserAgent.h>
#include <LibXML/Parser/Parser.h>
#include <UI/Download.h>
#include <UI/Extensions/CRX.h>
#include <UI/Extensions/WebStore.h>

namespace Ladybird::Extensions::WebStore {

static constexpr auto UPDATE_URL = "https://clients2.google.com/service/update2/crx"sv;

// For tests, which serve extensions of their own from a local server, signed with a publisher key of their own (given
// as the hex of its SHA-256 hash).
static constexpr auto UPDATE_URL_VARIABLE = "XENON_EXTENSION_STORE_URL"sv;
static constexpr auto PUBLISHER_KEY_HASH_VARIABLE = "XENON_EXTENSION_STORE_PUBLISHER_KEY_HASH"sv;

static constexpr u64 MAXIMUM_UPDATE_CHECK_SIZE = 64 * KiB;
static constexpr u64 MAXIMUM_CRX_SIZE = 256 * MiB;
static constexpr auto UPDATE_CHECK_TIMEOUT = AK::Duration::from_seconds(60);
static constexpr auto CRX_TIMEOUT = AK::Duration::from_seconds(15 * 60);

// The store offers the newest version that runs in the Chrome version asked with, which is the one Xenon's user agent
// names.
static StringView chrome_version()
{
    static constexpr auto prefix = "Chrome/"sv;
    StringView user_agent = Web::default_user_agent;
    auto start = user_agent.find(prefix);
    if (!start.has_value())
        return "150.0.0.0"sv;
    auto version = user_agent.substring_view(*start + prefix.length());
    return version.substring_view(0, version.find(' ').value_or(version.length()));
}

static Optional<ByteString> attribute(XML::Node const& node, StringView name)
{
    for (auto const& [attribute_name, value] : node.as_element().attributes) {
        if (attribute_name == name)
            return value;
    }
    return {};
}

ErrorOr<Optional<Update>, String> parse_update_check(StringView response, StringView id)
{
    XML::Parser parser { response };
    auto document = parser.parse();
    if (document.is_error())
        return "The store's answer isn't XML."_string;

    auto const& root = document.value().root();
    if (!root.is_element() || root.as_element().name != "gupdate"sv)
        return "The store's answer isn't an update check."_string;

    for (auto const& app : root.as_element().children) {
        if (!app->is_element() || app->as_element().name != "app"sv || attribute(*app, "appid"sv) != id)
            continue;
        if (auto status = attribute(*app, "status"sv).value_or({}); status != "ok"sv)
            return MUST(String::formatted("The store doesn't have it ({}).", status));

        for (auto const& update_check : app->as_element().children) {
            if (!update_check->is_element() || update_check->as_element().name != "updatecheck"sv)
                continue;

            auto status = attribute(*update_check, "status"sv).value_or({});
            if (status == "noupdate"sv)
                return Optional<Update> {};
            if (status != "ok"sv)
                return MUST(String::formatted("The store can't offer it ({}).", status));

            auto version = attribute(*update_check, "version"sv);
            auto codebase = attribute(*update_check, "codebase"sv);
            auto url = codebase.has_value() ? URL::Parser::basic_parse(*codebase) : Optional<URL::URL> {};
            if (!version.has_value() || !url.has_value())
                return "The store didn't say where its file is."_string;

            Update update { .version = MUST(String::from_byte_string(*version)), .url = url.release_value(), .size = {}, .sha256 = {} };
            if (auto size = attribute(*update_check, "size"sv); size.has_value())
                update.size = size->to_number<u64>();
            if (auto sha256 = attribute(*update_check, "hash_sha256"sv); sha256.has_value())
                update.sha256 = sha256->to_lowercase();
            return update;
        }
        return "The store's answer has no update check."_string;
    }
    return "The store's answer is about other extensions."_string;
}

static Optional<Vector<u32>> parse_version(StringView version)
{
    Vector<u32> numbers;
    auto parts = version.split_view('.', SplitBehavior::KeepEmpty);
    if (parts.is_empty() || parts.size() > 4)
        return {};
    for (auto part : parts) {
        auto number = part.to_number<u32>();
        if (!number.has_value() || *number > 65535)
            return {};
        numbers.append(*number);
    }
    return numbers;
}

bool is_newer_version(StringView version, StringView than)
{
    auto numbers = parse_version(version);
    auto than_numbers = parse_version(than);
    if (!numbers.has_value() || !than_numbers.has_value())
        return false;

    for (size_t i = 0; i < 4; ++i) {
        auto number = i < numbers->size() ? numbers->at(i) : 0;
        auto than_number = i < than_numbers->size() ? than_numbers->at(i) : 0;
        if (number != than_number)
            return number > than_number;
    }
    return false;
}

ReadonlyBytes publisher_key_hash()
{
    static auto const hash = [] {
        if (auto hex = Core::Environment::get(PUBLISHER_KEY_HASH_VARIABLE); hex.has_value()) {
            if (auto bytes = decode_hex(*hex); !bytes.is_error())
                return bytes.release_value();
            warnln("{} isn't hex; using the Chrome Web Store's key", PUBLISHER_KEY_HASH_VARIABLE);
        }
        return MUST(ByteBuffer::copy(chrome_web_store_publisher_key_hash()));
    }();
    return hash.bytes();
}

void fetch(String id, Optional<String> current_version, Callback callback)
{
    auto base_url = Core::Environment::get(UPDATE_URL_VARIABLE).value_or(UPDATE_URL);
    // The x parameter holds the extension's own parameters, URL-encoded: its ID, the version Xenon has, and "uc" (an
    // update check, rather than a ping).
    auto check_url = URL::Parser::basic_parse(ByteString::formatted("{}?response=updatecheck&acceptformat=crx3&prodversion={}&x=id%3D{}%26v%3D{}%26uc",
        base_url, chrome_version(), id, current_version.has_value() ? current_version->bytes_as_string_view() : "0.0.0.0"sv));
    if (!check_url.has_value()) {
        callback("The store's address isn't valid."_string);
        return;
    }
    auto allows_http = check_url->scheme() == "http"sv;

    Download::start(
        check_url.release_value(), MAXIMUM_UPDATE_CHECK_SIZE, [id = move(id), current_version = move(current_version), allows_http, callback = move(callback)](ErrorOr<ByteBuffer> response) mutable {
            if (response.is_error()) {
                callback(MUST(String::formatted("Xenon couldn't reach the Chrome Web Store: {}.", response.error())));
                return;
            }

            auto update = parse_update_check(StringView { response.value() }, id);
            if (update.is_error()) {
                callback(update.release_error());
                return;
            }
            if (!update.value().has_value() || (current_version.has_value() && !is_newer_version(update.value()->version, *current_version))) {
                callback(Optional<Fetched> {});
                return;
            }

            auto details = update.release_value().release_value();
            if (details.url.scheme() != "https"sv && !(allows_http && details.url.scheme() == "http"sv)) {
                callback("The store's file isn't served over HTTPS."_string);
                return;
            }
            auto maximum_size = min(details.size.value_or(MAXIMUM_CRX_SIZE), MAXIMUM_CRX_SIZE);

            Download::start(
                details.url, maximum_size, [details, callback = move(callback)](ErrorOr<ByteBuffer> crx) mutable {
                    if (crx.is_error()) {
                        callback(MUST(String::formatted("Xenon couldn't download it from the Chrome Web Store: {}.", crx.error())));
                        return;
                    }
                    if (details.size.has_value() && crx.value().size() != *details.size) {
                        callback("The store's file isn't the size the store said."_string);
                        return;
                    }
                    if (details.sha256.has_value() && encode_hex(Crypto::Hash::SHA256::hash(crx.value()).bytes()) != *details.sha256) {
                        callback("The store's file isn't the one the store described."_string);
                        return;
                    }
                    callback(Fetched { .version = details.version, .crx = crx.release_value() });
                },
                CRX_TIMEOUT);
        },
        UPDATE_CHECK_TIMEOUT);
}

}
