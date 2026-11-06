/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/CharacterTypes.h>
#include <AK/JsonArray.h>
#include <AK/LexicalPath.h>
#include <AK/StringBuilder.h>
#include <LibCore/Directory.h>
#include <LibCore/File.h>
#include <LibCore/StandardPaths.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <UI/XenonSettingsStore.h>

#if defined(AK_OS_MACOS)
#    include <CoreFoundation/CoreFoundation.h>
#endif

namespace Ladybird::XenonSettings {

static constexpr auto SETTINGS_DIRECTORY = "Xenon"sv;
static constexpr auto SETTINGS_FILE = "XenonSettings.json"sv;
static constexpr auto LEGACY_INI_FILE = "Xenon.conf"sv;
static constexpr auto LEGACY_PREFERENCES_APPLICATION_ID = "com.xenon.Xenon"sv;
static constexpr auto QSETTINGS_ROOT_SECTION = "General"sv;
static constexpr auto QSETTINGS_EMPTY_LIST = "@Invalid()"sv;

static constexpr KnownKey KNOWN_KEYS[] = {
    { Keys::compact_toolbar, ValueType::Boolean },
    { Keys::cycle_tabs_in_recently_used_order, ValueType::Boolean },
    { Keys::tab_hibernation, ValueType::Boolean },
    { Keys::tab_hibernation_timeout, ValueType::Integer },
    { Keys::hibernate_on_memory_pressure, ValueType::Boolean },
    { Keys::split_view_drag_and_drop, ValueType::Boolean },
    { Keys::new_tab_page_recently_closed, ValueType::Boolean },
    { Keys::bangs, ValueType::Boolean },
    { Keys::automatic_updates, ValueType::Boolean },
    { Keys::disabled_filter_lists, ValueType::StringList },
    { Keys::show_blocked_request_count, ValueType::Boolean },
    { Keys::blocked_request_total, ValueType::Integer },
};

ReadonlySpan<KnownKey> known_keys()
{
    return KNOWN_KEYS;
}

static Optional<KnownKey const&> known_key(StringView name)
{
    for (auto const& key : KNOWN_KEYS) {
        if (key.name == name)
            return key;
    }
    return {};
}

static Optional<JsonValue> json_from_strings(Vector<String> const& strings, ValueType type)
{
    switch (type) {
    case ValueType::Boolean: {
        if (strings.size() != 1)
            return {};
        auto const& string = strings.first();
        return !(string.is_empty() || string == "0"sv || string.equals_ignoring_ascii_case("false"sv));
    }
    case ValueType::Integer: {
        if (strings.size() != 1)
            return {};
        auto number = strings.first().to_number<i64>();
        if (!number.has_value())
            return {};
        return *number;
    }
    case ValueType::StringList: {
        JsonArray array;
        for (auto const& string : strings)
            array.must_append(string);
        return array;
    }
    }
    VERIFY_NOT_REACHED();
}

static Optional<u32> parse_hex_digits(StringView digits)
{
    if (digits.is_empty())
        return {};
    return digits.to_number<u32>(TrimWhitespace::No, 16);
}

static String unescape_qsettings_key(StringView key)
{
    StringBuilder builder;
    for (size_t i = 0; i < key.length(); ++i) {
        auto character = key[i];
        if (character == '\\') {
            builder.append('/');
            continue;
        }
        if (character == '%') {
            if (i + 6 <= key.length() && key[i + 1] == 'U') {
                if (auto code_point = parse_hex_digits(key.substring_view(i + 2, 4)); code_point.has_value()) {
                    builder.append_code_point(*code_point);
                    i += 5;
                    continue;
                }
            }
            if (i + 3 <= key.length()) {
                if (auto byte = parse_hex_digits(key.substring_view(i + 1, 2)); byte.has_value()) {
                    builder.append_code_point(*byte);
                    i += 2;
                    continue;
                }
            }
        }
        builder.append(character);
    }
    return builder.to_string_without_validation();
}

static Optional<Vector<String>> unescape_qsettings_value(StringView value)
{
    value = value.trim_whitespace();
    if (value == QSETTINGS_EMPTY_LIST)
        return Vector<String> {};
    if (value.starts_with('@') && !value.starts_with("@@"sv))
        return {};

    Vector<String> strings;
    StringBuilder current;
    StringBuilder pending_whitespace;
    bool in_quotes = false;

    auto flush_pending_whitespace = [&] {
        if (!current.is_empty())
            current.append(pending_whitespace.string_view());
        pending_whitespace.clear();
    };
    auto append_text = [&](u32 code_point) {
        flush_pending_whitespace();
        current.append_code_point(code_point);
    };
    auto append_byte = [&](char byte) {
        flush_pending_whitespace();
        current.append(byte);
    };

    auto finish_string = [&] {
        auto string = current.to_string_without_validation();
        if (string.starts_with_bytes("@@"sv))
            string = MUST(string.substring_from_byte_offset(1));
        strings.append(move(string));
        current.clear();
        pending_whitespace.clear();
    };

    for (size_t i = 0; i < value.length(); ++i) {
        auto character = value[i];
        if (character == '\\' && i + 1 < value.length()) {
            auto escaped = value[++i];
            switch (escaped) {
            case 'a':
                append_text('\a');
                break;
            case 'b':
                append_text('\b');
                break;
            case 'f':
                append_text('\f');
                break;
            case 'n':
                append_text('\n');
                break;
            case 'r':
                append_text('\r');
                break;
            case 't':
                append_text('\t');
                break;
            case 'v':
                append_text('\v');
                break;
            case 'x': {
                size_t end = i + 1;
                while (end < value.length() && end - i <= 4 && is_ascii_hex_digit(value[end]))
                    ++end;
                if (auto code_point = parse_hex_digits(value.substring_view(i + 1, end - i - 1)); code_point.has_value()) {
                    append_text(*code_point);
                    i = end - 1;
                }
                break;
            }
            default:
                if (escaped >= '0' && escaped <= '7') {
                    u32 code_point = 0;
                    size_t end = i;
                    while (end < value.length() && end - i < 3 && value[end] >= '0' && value[end] <= '7')
                        code_point = code_point * 8 + (value[end++] - '0');
                    append_text(code_point);
                    i = end - 1;
                } else {
                    append_byte(escaped);
                }
                break;
            }
            continue;
        }
        if (character == '"') {
            if (!in_quotes && !current.is_empty())
                current.append(pending_whitespace.string_view());
            pending_whitespace.clear();
            in_quotes = !in_quotes;
            continue;
        }
        if (in_quotes) {
            current.append(character);
            continue;
        }
        if (character == ',') {
            finish_string();
            continue;
        }
        if (is_ascii_space(character)) {
            pending_whitespace.append(character);
            continue;
        }
        append_byte(character);
    }
    finish_string();
    return strings;
}

JsonObject Store::import_qsettings_ini(StringView contents)
{
    JsonObject values;
    String section;

    for (auto line : contents.lines()) {
        line = line.trim_whitespace();
        if (line.is_empty() || line.starts_with(';') || line.starts_with('#'))
            continue;

        if (line.starts_with('[')) {
            auto end = line.find(']');
            auto name = line.substring_view(1, end.value_or(line.length()) - 1);
            section = name == QSETTINGS_ROOT_SECTION ? String {} : unescape_qsettings_key(name);
            continue;
        }

        auto equals = line.find('=');
        if (!equals.has_value())
            continue;

        auto key = unescape_qsettings_key(line.substring_view(0, *equals).trim_whitespace());
        auto name = section.is_empty() ? key : MUST(String::formatted("{}/{}", section, key));

        auto known = known_key(name);
        if (!known.has_value())
            continue;

        auto strings = unescape_qsettings_value(line.substring_view(*equals + 1));
        if (!strings.has_value())
            continue;
        if (auto value = json_from_strings(*strings, known->type); value.has_value())
            values.set(known->name, value.release_value());
    }
    return values;
}

Optional<JsonObject> Store::import_qsettings_ini_file(ByteString const& path)
{
    if (!FileSystem::exists(path))
        return {};
    auto file = Core::File::open(path, Core::File::OpenMode::Read);
    if (file.is_error())
        return {};
    auto contents = file.value()->read_until_eof();
    if (contents.is_error())
        return {};
    return import_qsettings_ini(StringView { contents.value() });
}

#if defined(AK_OS_MACOS)
static Optional<String> string_from_cf_string(CFStringRef string)
{
    auto length = CFStringGetLength(string);
    auto capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    Vector<char> buffer;
    buffer.resize(capacity);
    if (!CFStringGetCString(string, buffer.data(), capacity, kCFStringEncodingUTF8))
        return {};
    auto result = String::from_utf8(StringView { buffer.data(), __builtin_strlen(buffer.data()) });
    if (result.is_error())
        return {};
    return result.release_value();
}

static CFStringRef create_cf_string(StringView string)
{
    return CFStringCreateWithBytes(nullptr, reinterpret_cast<UInt8 const*>(string.characters_without_null_termination()), static_cast<CFIndex>(string.length()), kCFStringEncodingUTF8, false);
}

static Optional<JsonValue> json_from_preference(CFPropertyListRef value, ValueType type)
{
    auto type_id = CFGetTypeID(value);
    if (type_id == CFBooleanGetTypeID()) {
        if (type != ValueType::Boolean)
            return {};
        return static_cast<bool>(CFBooleanGetValue(static_cast<CFBooleanRef>(value)));
    }
    if (type_id == CFNumberGetTypeID()) {
        i64 number = 0;
        if (!CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &number))
            return {};
        if (type == ValueType::Boolean)
            return number != 0;
        if (type == ValueType::Integer)
            return number;
        return {};
    }
    if (type_id == CFStringGetTypeID()) {
        auto string = string_from_cf_string(static_cast<CFStringRef>(value));
        if (!string.has_value())
            return {};
        return json_from_strings({ string.release_value() }, type);
    }
    if (type_id == CFArrayGetTypeID()) {
        auto array = static_cast<CFArrayRef>(value);
        Vector<String> strings;
        for (CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
            auto element = CFArrayGetValueAtIndex(array, i);
            if (CFGetTypeID(element) != CFStringGetTypeID())
                return {};
            auto string = string_from_cf_string(static_cast<CFStringRef>(element));
            if (!string.has_value())
                return {};
            strings.append(string.release_value());
        }
        return json_from_strings(strings, type);
    }
    return {};
}
#endif

Optional<JsonObject> Store::import_qsettings_preferences([[maybe_unused]] StringView application_id)
{
#if defined(AK_OS_MACOS)
    auto cf_application_id = create_cf_string(application_id);
    JsonObject values;
    for (auto const& key : KNOWN_KEYS) {
        CFPropertyListRef value = nullptr;
        for (auto candidate : { ByteString { key.name }, key.name.replace("/"sv, "."sv, ReplaceMode::All) }) {
            auto cf_key = create_cf_string(candidate);
            value = CFPreferencesCopyAppValue(cf_key, cf_application_id);
            CFRelease(cf_key);
            if (value)
                break;
        }
        if (!value)
            continue;
        if (auto json = json_from_preference(value, key.type); json.has_value())
            values.set(key.name, json.release_value());
        CFRelease(value);
    }
    CFRelease(cf_application_id);
    if (values.is_empty())
        return {};
    return values;
#else
    return {};
#endif
}

ByteString Store::default_path()
{
    return LexicalPath::join(Core::StandardPaths::config_directory(), SETTINGS_DIRECTORY, SETTINGS_FILE).string();
}

ByteString Store::legacy_ini_path()
{
    return LexicalPath::join(Core::StandardPaths::config_directory(), SETTINGS_DIRECTORY, LEGACY_INI_FILE).string();
}

StringView Store::legacy_preferences_application_id()
{
    return LEGACY_PREFERENCES_APPLICATION_ID;
}

static ErrorOr<JsonObject> read_json_object(ByteString const& path)
{
    auto file = TRY(Core::File::open(path, Core::File::OpenMode::Read));
    auto contents = TRY(file->read_until_eof());
    auto value = TRY(JsonValue::from_string(contents));
    if (!value.is_object())
        return Error::from_string_literal("Xenon's settings are not a JSON object");
    return move(value.as_object());
}

static ErrorOr<void> write_json_object(ByteString const& path, JsonObject const& object)
{
    TRY(Core::Directory::create(LexicalPath { path }.parent(), Core::Directory::CreateDirectories::Yes));
    auto file = TRY(Core::File::open(path, Core::File::OpenMode::Write));
    TRY(file->write_until_depleted(object.serialized()));
    return {};
}

Store::Store(ByteString path, JsonObject values)
    : m_path(move(path))
    , m_values(move(values))
{
}

Store Store::open(ByteString path, Function<Optional<JsonObject>()> const& import_legacy_values)
{
    if (FileSystem::exists(path)) {
        auto values = read_json_object(path);
        if (values.is_error()) {
            warnln("Unable to read Xenon's settings from {}: {}", path, values.error());
            return Store { move(path), {} };
        }
        return Store { move(path), values.release_value() };
    }

    auto legacy_values = import_legacy_values ? import_legacy_values() : Optional<JsonObject> {};
    if (!legacy_values.has_value())
        return Store { move(path), {} };

    Store store { move(path), legacy_values.release_value() };
    if (auto result = store.save(); result.is_error())
        warnln("Unable to save Xenon's imported settings to {}: {}", store.path(), result.error());
    return store;
}

Optional<bool> Store::get_bool(StringView key) const
{
    return m_values.get_bool(key);
}

Optional<i64> Store::get_i64(StringView key) const
{
    return m_values.get_i64(key);
}

Optional<u64> Store::get_u64(StringView key) const
{
    return m_values.get_u64(key);
}

Vector<String> Store::get_strings(StringView key) const
{
    Vector<String> strings;
    auto array = m_values.get_array(key);
    if (!array.has_value())
        return strings;
    array->for_each([&](JsonValue const& value) {
        if (value.is_string())
            strings.append(value.as_string());
    });
    return strings;
}

void Store::set(StringView key, JsonValue value)
{
    m_values.set(key, move(value));
}

ErrorOr<void> Store::save() const
{
    auto temporary_path = ByteString::formatted("{}.tmp", m_path);
    TRY(write_json_object(temporary_path, m_values));
    TRY(Core::System::rename(temporary_path, m_path));
    return {};
}

}
