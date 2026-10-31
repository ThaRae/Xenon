/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AllOf.h>
#include <AK/AnyOf.h>
#include <AK/CharacterTypes.h>
#include <AK/Enumerate.h>
#include <AK/JsonArray.h>
#include <AK/QuickSort.h>
#include <AK/StringBuilder.h>
#include <UI/Extensions/Manifest.h>
#include <UI/Extensions/MatchPattern.h>

namespace Ladybird::Extensions {

static constexpr auto KNOWN_KEYS = to_array<StringView>({
    "action"sv,
    "author"sv,
    "automation"sv,
    "background"sv,
    "chrome_settings_overrides"sv,
    "chrome_url_overrides"sv,
    "commands"sv,
    "content_scripts"sv,
    "content_security_policy"sv,
    "cross_origin_embedder_policy"sv,
    "cross_origin_opener_policy"sv,
    "declarative_net_request"sv,
    "default_locale"sv,
    "description"sv,
    "devtools_page"sv,
    "differential_fingerprint"sv,
    "event_rules"sv,
    "export"sv,
    "externally_connectable"sv,
    "file_browser_handlers"sv,
    "file_system_provider_capabilities"sv,
    "homepage_url"sv,
    "host_permissions"sv,
    "icons"sv,
    "import"sv,
    "incognito"sv,
    "input_components"sv,
    "key"sv,
    "manifest_version"sv,
    "minimum_chrome_version"sv,
    "nacl_modules"sv,
    "name"sv,
    "oauth2"sv,
    "offline_enabled"sv,
    "omnibox"sv,
    "optional_host_permissions"sv,
    "optional_permissions"sv,
    "options_page"sv,
    "options_ui"sv,
    "permissions"sv,
    "requirements"sv,
    "sandbox"sv,
    "short_name"sv,
    "side_panel"sv,
    "storage"sv,
    "tts_engine"sv,
    "update_url"sv,
    "version"sv,
    "version_name"sv,
    "web_accessible_resources"sv,
});

using ParseError = String;

template<typename... Parameters>
static ParseError error(CheckedFormatString<Parameters...>&& format, Parameters const&... parameters)
{
    return MUST(String::formatted(move(format), parameters...));
}

static String normalized_path(String const& path)
{
    auto view = path.bytes_as_string_view();
    while (view.starts_with('/'))
        view = view.substring_view(1);
    return MUST(String::from_utf8(view));
}

static ErrorOr<Optional<String>, ParseError> optional_string(JsonObject const& object, StringView key, StringView context)
{
    auto value = object.get(key);
    if (!value.has_value())
        return Optional<String> {};
    if (!value->is_string())
        return error("'{}{}' must be a string.", context, key);
    return Optional<String> { value->as_string() };
}

static ErrorOr<Vector<String>, ParseError> string_list(JsonObject const& object, StringView key, StringView context)
{
    Vector<String> list;
    auto value = object.get(key);
    if (!value.has_value())
        return list;
    if (!value->is_array())
        return error("'{}{}' must be a list of strings.", context, key);

    for (auto const& item : value->as_array().values()) {
        if (!item.is_string())
            return error("'{}{}' must be a list of strings.", context, key);
        list.append(item.as_string());
    }
    return list;
}

static ErrorOr<Vector<String>, ParseError> path_list(JsonObject const& object, StringView key, StringView context)
{
    auto list = TRY(string_list(object, key, context));
    for (auto& path : list)
        path = normalized_path(path);
    return list;
}

static ErrorOr<Vector<Icon>, ParseError> parse_icons(JsonValue const& value, StringView key)
{
    Vector<Icon> icons;

    if (value.is_string()) {
        icons.append({ .size = 0, .path = normalized_path(value.as_string()) });
        return icons;
    }
    if (!value.is_object())
        return error("'{}' must be a dictionary of sizes to paths.", key);

    TRY(value.as_object().try_for_each_member([&](String const& size, JsonValue const& path) -> ErrorOr<void, ParseError> {
        auto pixels = size.to_number<u32>();
        if (!pixels.has_value() || *pixels == 0 || !path.is_string())
            return error("'{}' must be a dictionary of sizes to paths.", key);
        icons.append({ .size = *pixels, .path = normalized_path(path.as_string()) });
        return {};
    }));

    quick_sort(icons, [](Icon const& a, Icon const& b) { return a.size < b.size; });
    return icons;
}

static bool is_valid_version(StringView version)
{
    auto parts = version.split_view('.', SplitBehavior::KeepEmpty);
    if (parts.is_empty() || parts.size() > 4)
        return false;

    for (auto part : parts) {
        if (part.is_empty() || part.length() > 5 || !all_of(part, is_ascii_digit))
            return false;
        if (part.length() > 1 && part.starts_with('0'))
            return false;
        if (part.to_number<u32>().value_or(NumericLimits<u32>::max()) > 65535)
            return false;
    }
    return true;
}

static ErrorOr<void, ParseError> validate_extension_pages_csp(StringView policy)
{
    for (auto directive : policy.split_view(';')) {
        auto tokens = directive.trim_whitespace().split_view_if(is_ascii_space);
        if (tokens.is_empty())
            continue;

        auto name = tokens.take_first();
        if (!name.is_one_of_ignoring_ascii_case("script-src"sv, "object-src"sv, "worker-src"sv, "default-src"sv))
            continue;

        for (auto token : tokens) {
            auto is_allowed = token.is_one_of_ignoring_ascii_case("'self'"sv, "'none'"sv, "'wasm-unsafe-eval'"sv)
                || token.starts_with("http://localhost"sv, CaseSensitivity::CaseInsensitive)
                || token.starts_with("http://127.0.0.1"sv);
            if (!is_allowed)
                return error("'content_security_policy.extension_pages': Insecure CSP value \"{}\" in directive '{}'.", token, name);
        }
    }
    return {};
}

static Optional<String> suggested_key_for_this_platform(JsonValue const& suggested_key)
{
    if (suggested_key.is_string())
        return suggested_key.as_string();
    if (!suggested_key.is_object())
        return {};

    auto const& keys = suggested_key.as_object();
#if defined(AK_OS_MACOS)
    if (auto key = keys.get_string("mac"sv); key.has_value())
        return *key;
#elif defined(AK_OS_WINDOWS)
    if (auto key = keys.get_string("windows"sv); key.has_value())
        return *key;
#else
    if (auto key = keys.get_string("linux"sv); key.has_value())
        return *key;
#endif
    if (auto key = keys.get_string("default"sv); key.has_value())
        return *key;
    return {};
}

ErrorOr<Manifest, String> Manifest::parse(JsonObject json)
{
    Manifest manifest;

    auto manifest_version = json.get_i64("manifest_version"sv);
    if (!manifest_version.has_value())
        return error("Required value 'manifest_version' is missing or invalid.");
    if (*manifest_version == 2)
        return error("Manifest version 2 is no longer supported. Xenon only loads Manifest V3 extensions.");
    if (*manifest_version != 3)
        return error("Invalid value for 'manifest_version'. Must be an integer of 3.");

    auto name = TRY(optional_string(json, "name"sv, ""sv));
    if (!name.has_value() || name->is_empty())
        return error("Required value 'name' is missing or invalid.");
    manifest.name = name.release_value();

    auto version = TRY(optional_string(json, "version"sv, ""sv));
    if (!version.has_value() || !is_valid_version(*version))
        return error("Required value 'version' is missing or invalid. It must be between 1-4 dot-separated integers each between 0 and 65535.");
    manifest.version = version.release_value();

    manifest.short_name = TRY(optional_string(json, "short_name"sv, ""sv));
    manifest.version_name = TRY(optional_string(json, "version_name"sv, ""sv));
    manifest.description = TRY(optional_string(json, "description"sv, ""sv));
    manifest.default_locale = TRY(optional_string(json, "default_locale"sv, ""sv));
    manifest.key = TRY(optional_string(json, "key"sv, ""sv));

    if (auto icons = json.get("icons"sv); icons.has_value())
        manifest.icons = TRY(parse_icons(*icons, "icons"sv));

    if (auto action = json.get("action"sv); action.has_value()) {
        if (!action->is_object())
            return error("Invalid value for 'action'.");
        auto const& object = action->as_object();

        Action parsed;
        if (auto popup = TRY(optional_string(object, "default_popup"sv, "action."sv)); popup.has_value() && !popup->is_empty())
            parsed.default_popup = normalized_path(*popup);
        parsed.default_title = TRY(optional_string(object, "default_title"sv, "action."sv));
        if (auto icon = object.get("default_icon"sv); icon.has_value())
            parsed.default_icon = TRY(parse_icons(*icon, "action.default_icon"sv));
        manifest.action = move(parsed);
    }

    if (auto background = json.get_object("background"sv); background.has_value()) {
        if (background->has("scripts"sv) || background->has("page"sv))
            return error("The 'background.scripts' and 'background.page' keys cannot be used with manifest_version 3. Use the 'background.service_worker' key instead.");
        if (auto worker = TRY(optional_string(*background, "service_worker"sv, "background."sv)); worker.has_value())
            manifest.background_service_worker = normalized_path(*worker);
        if (auto type = TRY(optional_string(*background, "type"sv, "background."sv)); type.has_value()) {
            if (!type->is_one_of("classic"sv, "module"sv))
                return error("Invalid value for 'background.type'.");
            manifest.background_is_module = *type == "module"sv;
        }
    }

    if (auto content_scripts = json.get("content_scripts"sv); content_scripts.has_value()) {
        if (!content_scripts->is_array())
            return error("Invalid value for 'content_scripts'.");

        for (auto const& [index, value] : enumerate(content_scripts->as_array().values())) {
            if (!value.is_object())
                return error("Invalid value for 'content_scripts[{}]'.", index);
            auto const& object = value.as_object();
            auto context = MUST(String::formatted("content_scripts[{}].", index));

            ContentScript script;
            script.matches = TRY(string_list(object, "matches"sv, context));
            if (script.matches.is_empty())
                return error("Required value 'content_scripts[{}].matches' is missing or invalid.", index);
            script.exclude_matches = TRY(string_list(object, "exclude_matches"sv, context));
            script.include_globs = TRY(string_list(object, "include_globs"sv, context));
            script.exclude_globs = TRY(string_list(object, "exclude_globs"sv, context));
            script.js = TRY(path_list(object, "js"sv, context));
            script.css = TRY(path_list(object, "css"sv, context));
            if (script.js.is_empty() && script.css.is_empty())
                return error("Required value 'content_scripts[{}].js' or 'content_scripts[{}].css' is missing.", index, index);

            if (auto run_at = TRY(optional_string(object, "run_at"sv, context)); run_at.has_value()) {
                if (*run_at == "document_start"sv)
                    script.run_at = RunAt::DocumentStart;
                else if (*run_at == "document_end"sv)
                    script.run_at = RunAt::DocumentEnd;
                else if (*run_at == "document_idle"sv)
                    script.run_at = RunAt::DocumentIdle;
                else
                    return error("Invalid value for 'content_scripts[{}].run_at'.", index);
            }
            if (auto world = TRY(optional_string(object, "world"sv, context)); world.has_value()) {
                if (*world == "MAIN"sv)
                    script.world = World::Main;
                else if (*world != "ISOLATED"sv)
                    return error("Invalid value for 'content_scripts[{}].world'.", index);
            }
            script.all_frames = object.get_bool("all_frames"sv).value_or(false);
            script.match_about_blank = object.get_bool("match_about_blank"sv).value_or(false);
            script.match_origin_as_fallback = object.get_bool("match_origin_as_fallback"sv).value_or(false);
            manifest.content_scripts.append(move(script));
        }
    }

    if (auto resources = json.get("web_accessible_resources"sv); resources.has_value()) {
        if (!resources->is_array())
            return error("Invalid value for 'web_accessible_resources'.");

        for (auto const& [index, value] : enumerate(resources->as_array().values())) {
            if (!value.is_object())
                return error("Invalid value for 'web_accessible_resources[{}]'. Entries must be objects in manifest_version 3.", index);
            auto const& object = value.as_object();
            auto context = MUST(String::formatted("web_accessible_resources[{}].", index));

            WebAccessibleResources entry;
            entry.resources = TRY(path_list(object, "resources"sv, context));
            entry.matches = TRY(string_list(object, "matches"sv, context));
            entry.extension_ids = TRY(string_list(object, "extension_ids"sv, context));
            entry.use_dynamic_url = object.get_bool("use_dynamic_url"sv).value_or(false);
            if (entry.matches.is_empty() && entry.extension_ids.is_empty())
                return error("Invalid value for 'web_accessible_resources[{}]'. Entry must at least have resources, and one other valid key.", index);
            manifest.web_accessible_resources.append(move(entry));
        }
    }

    if (auto externally_connectable = json.get("externally_connectable"sv); externally_connectable.has_value()) {
        if (!externally_connectable->is_object())
            return error("Invalid value for 'externally_connectable'.");

        ExternallyConnectable parsed;
        auto patterns = TRY(string_list(externally_connectable->as_object(), "matches"sv, "externally_connectable."sv));
        for (auto const& [index, pattern] : enumerate(patterns)) {
            auto parsed_pattern = MatchPattern::parse(pattern);
            if (parsed_pattern.is_error())
                return error("Invalid value for 'externally_connectable.matches[{}]': {}", index, parsed_pattern.error());
            if (parsed_pattern.value().matches_all_urls() || parsed_pattern.value().host().is_empty()) {
                manifest.warnings.append(error("Wildcard domain patterns such as \"<all_urls>\" are not allowed in 'externally_connectable.matches[{}]'.", index));
                continue;
            }
            parsed.matches.append(pattern);
        }
        manifest.externally_connectable = move(parsed);
    }

    if (auto options_ui = json.get_object("options_ui"sv); options_ui.has_value()) {
        if (auto page = TRY(optional_string(*options_ui, "page"sv, "options_ui."sv)); page.has_value())
            manifest.options_page = normalized_path(*page);
    } else if (auto page = TRY(optional_string(json, "options_page"sv, ""sv)); page.has_value()) {
        manifest.options_page = normalized_path(*page);
    }

    if (auto commands = json.get("commands"sv); commands.has_value()) {
        if (!commands->is_object())
            return error("Invalid value for 'commands'.");

        TRY(commands->as_object().try_for_each_member([&](String const& command_name, JsonValue const& value) -> ErrorOr<void, ParseError> {
            if (!value.is_object())
                return error("Invalid value for 'commands.{}'.", command_name);
            auto const& object = value.as_object();

            Command command { .name = command_name, .description = object.get_string("description"sv).copy(), .suggested_key = {} };
            if (auto suggested_key = object.get("suggested_key"sv); suggested_key.has_value())
                command.suggested_key = suggested_key_for_this_platform(*suggested_key);
            manifest.commands.append(move(command));
            return {};
        }));
    }

    manifest.permissions = TRY(string_list(json, "permissions"sv, ""sv));
    manifest.optional_permissions = TRY(string_list(json, "optional_permissions"sv, ""sv));
    manifest.host_permissions = TRY(string_list(json, "host_permissions"sv, ""sv));
    manifest.optional_host_permissions = TRY(string_list(json, "optional_host_permissions"sv, ""sv));

    manifest.extension_pages_csp = MUST(String::from_utf8(DEFAULT_EXTENSION_PAGES_CSP));
    manifest.sandbox_csp = MUST(String::from_utf8(DEFAULT_SANDBOX_CSP));
    if (auto csp = json.get("content_security_policy"sv); csp.has_value()) {
        if (!csp->is_object())
            return error("Invalid value for 'content_security_policy'. It must be a dictionary in manifest_version 3.");
        if (auto policy = TRY(optional_string(csp->as_object(), "extension_pages"sv, "content_security_policy."sv)); policy.has_value()) {
            TRY(validate_extension_pages_csp(*policy));
            manifest.extension_pages_csp = policy.release_value();
        }
        if (auto policy = TRY(optional_string(csp->as_object(), "sandbox"sv, "content_security_policy."sv)); policy.has_value())
            manifest.sandbox_csp = policy.release_value();
    }

    if (auto sandbox = json.get_object("sandbox"sv); sandbox.has_value())
        manifest.sandbox_pages = TRY(path_list(*sandbox, "pages"sv, "sandbox."sv));

    json.for_each_member([&](String const& key, JsonValue const&) {
        if (!key.starts_with('_') && !KNOWN_KEYS.contains_slow(key.bytes_as_string_view()))
            manifest.warnings.append(error("Unrecognized manifest key '{}'.", key));
    });

    manifest.json = move(json);
    return manifest;
}

bool Manifest::has_permission(StringView permission) const
{
    return permissions.contains_slow(permission);
}

Optional<String> best_icon(Vector<Icon> const& icons, u32 size)
{
    if (icons.is_empty())
        return {};
    for (auto const& icon : icons) {
        if (icon.size == 0 || icon.size >= size)
            return icon.path;
    }
    return icons.last().path;
}

bool ContentScript::matches_document(URL::URL const& url, bool is_top_level_frame) const
{
    if (!all_frames && !is_top_level_frame)
        return false;

    if (url.scheme() == "file"sv)
        return false;

    auto matches_any = [&](Vector<String> const& patterns) {
        return any_of(patterns, [&](auto const& pattern) {
            auto parsed = MatchPattern::parse(pattern);
            return !parsed.is_error() && parsed.value().matches(url);
        });
    };
    if (!matches_any(matches) || matches_any(exclude_matches))
        return false;

    auto serialized = url.serialize();
    auto matches_glob = [&](auto const& glob) { return glob_matches(glob, serialized, true); };
    if (!include_globs.is_empty() && !any_of(include_globs, matches_glob))
        return false;
    return !any_of(exclude_globs, matches_glob);
}

bool ExternallyConnectable::matches_document(URL::URL const& url) const
{
    return any_of(matches, [&](auto const& pattern) {
        auto parsed = MatchPattern::parse(pattern);
        return !parsed.is_error() && parsed.value().matches(url);
    });
}

ErrorOr<RegisteredContentScript, String> RegisteredContentScript::from_json(JsonObject const& object)
{
    RegisteredContentScript registered;
    auto id = TRY(optional_string(object, "id"sv, {}));
    if (!id.has_value() || id->is_empty())
        return error("A content script needs an ID.");
    if (id->starts_with('_'))
        return error("Content script's ID '{}' must not start with '_'.", *id);
    registered.id = id.release_value();

    auto context = MUST(String::formatted("content script '{}': ", registered.id));
    auto& script = registered.script;
    script.matches = TRY(string_list(object, "matches"sv, context));
    if (script.matches.is_empty())
        return error("Script with ID '{}' must specify 'matches'.", registered.id);
    script.exclude_matches = TRY(string_list(object, "excludeMatches"sv, context));
    for (auto const& pattern : script.matches) {
        if (auto parsed = MatchPattern::parse(pattern); parsed.is_error())
            return error("Script with ID '{}' has an invalid match pattern '{}': {}", registered.id, pattern, parsed.error());
    }
    for (auto const& pattern : script.exclude_matches) {
        if (auto parsed = MatchPattern::parse(pattern); parsed.is_error())
            return error("Script with ID '{}' has an invalid match pattern '{}': {}", registered.id, pattern, parsed.error());
    }
    script.js = TRY(path_list(object, "js"sv, context));
    script.css = TRY(path_list(object, "css"sv, context));
    if (script.js.is_empty() && script.css.is_empty())
        return error("Script with ID '{}' must specify at least one js or css file.", registered.id);

    if (auto run_at = TRY(optional_string(object, "runAt"sv, context)); run_at.has_value()) {
        if (*run_at == "document_start"sv)
            script.run_at = RunAt::DocumentStart;
        else if (*run_at == "document_end"sv)
            script.run_at = RunAt::DocumentEnd;
        else if (*run_at == "document_idle"sv)
            script.run_at = RunAt::DocumentIdle;
        else
            return error("Script with ID '{}' has an invalid runAt '{}'.", registered.id, *run_at);
    }
    if (auto world = TRY(optional_string(object, "world"sv, context)); world.has_value()) {
        if (*world == "MAIN"sv)
            script.world = World::Main;
        else if (*world != "ISOLATED"sv)
            return error("Script with ID '{}' has an invalid world '{}'.", registered.id, *world);
    }
    script.all_frames = object.get_bool("allFrames"sv).value_or(false);
    script.match_origin_as_fallback = object.get_bool("matchOriginAsFallback"sv).value_or(false);
    registered.persist_across_sessions = object.get_bool("persistAcrossSessions"sv).value_or(true);
    return registered;
}

JsonObject RegisteredContentScript::to_json() const
{
    auto list = [](Vector<String> const& strings) {
        JsonArray array;
        for (auto const& string : strings)
            array.must_append(string);
        return array;
    };

    JsonObject object;
    object.set("id"sv, id);
    object.set("matches"sv, list(script.matches));
    if (!script.exclude_matches.is_empty())
        object.set("excludeMatches"sv, list(script.exclude_matches));
    if (!script.js.is_empty())
        object.set("js"sv, list(script.js));
    if (!script.css.is_empty())
        object.set("css"sv, list(script.css));
    object.set("allFrames"sv, script.all_frames);
    object.set("matchOriginAsFallback"sv, script.match_origin_as_fallback);
    switch (script.run_at) {
    case RunAt::DocumentStart:
        object.set("runAt"sv, "document_start"sv);
        break;
    case RunAt::DocumentEnd:
        object.set("runAt"sv, "document_end"sv);
        break;
    case RunAt::DocumentIdle:
        object.set("runAt"sv, "document_idle"sv);
        break;
    }
    object.set("world"sv, script.world == World::Main ? "MAIN"sv : "ISOLATED"sv);
    object.set("persistAcrossSessions"sv, persist_across_sessions);
    return object;
}

}
