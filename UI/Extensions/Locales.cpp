/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/CharacterTypes.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <AK/StringBuilder.h>
#include <LibCore/File.h>
#include <LibFileSystem/FileSystem.h>
#include <UI/Extensions/Extension.h>
#include <UI/Extensions/Locales.h>

namespace Ladybird::Extensions {

// Chrome names locale directories with underscores ("en_US"), where BCP 47 uses hyphens ("en-US").
static String locale_directory_name(StringView language)
{
    return MUST(String::from_byte_string(language.replace("-"sv, "_"sv, ReplaceMode::All)));
}

static Vector<String> fallback_locales(StringView ui_language, Optional<String> const& default_locale)
{
    Vector<String> locales;
    auto locale = locale_directory_name(ui_language);
    locales.append(locale);
    if (auto separator = locale.bytes_as_string_view().find('_'); separator.has_value())
        locales.append(MUST(locale.substring_from_byte_offset(0, *separator)));
    if (default_locale.has_value() && !locales.contains_slow(*default_locale))
        locales.append(*default_locale);
    return locales;
}

// Fills in a message's $name$ placeholders (named in any case) from its "placeholders", whose contents may themselves
// hold $1 to $9 for the substitutions. A message without a "message" gives nothing.
static Optional<String> fill_placeholders(JsonObject const& entry)
{
    auto message = entry.get_string("message"sv);
    if (!message.has_value())
        return {};

    auto placeholders = entry.get_object("placeholders"sv);
    if (!placeholders.has_value())
        return *message;

    StringBuilder builder;
    auto view = message->bytes_as_string_view();
    for (size_t i = 0; i < view.length();) {
        // "$$" is a literal dollar sign, which the renderer's substitution step turns into one.
        if (view.substring_view(i).starts_with("$$"sv)) {
            builder.append("$$"sv);
            i += 2;
            continue;
        }
        if (view[i] == '$') {
            auto end = view.find('$', i + 1);
            if (end.has_value()) {
                auto placeholder = view.substring_view(i + 1, *end - i - 1);
                if (!placeholder.is_empty() && all_of(placeholder, [](char c) { return is_ascii_alphanumeric(c) || c == '_' || c == '@'; })) {
                    Optional<String> content;
                    placeholders->for_each_member([&](String const& key, JsonValue const& value) {
                        if (!content.has_value() && key.equals_ignoring_ascii_case(placeholder) && value.is_object())
                            content = value.as_object().get_string("content"sv).copy();
                    });
                    if (content.has_value()) {
                        builder.append(*content);
                        i = *end + 1;
                        continue;
                    }
                }
            }
        }
        builder.append(view[i]);
        ++i;
    }
    return builder.to_string_without_validation();
}

String substitute_message(StringView message, ReadonlySpan<String> substitutions)
{
    StringBuilder builder;
    for (size_t i = 0; i < message.length(); ++i) {
        if (message[i] == '$' && i + 1 < message.length()) {
            auto next = message[i + 1];
            if (next == '$') {
                builder.append('$');
                ++i;
                continue;
            }
            if (next >= '1' && next <= '9') {
                auto index = static_cast<size_t>(next - '1');
                if (index < substitutions.size())
                    builder.append(substitutions[index]);
                ++i;
                continue;
            }
        }
        builder.append(message[i]);
    }
    return builder.to_string_without_validation();
}

ErrorOr<Locales, String> Locales::load(ByteString const& extension_root, Optional<String> const& default_locale, StringView ui_language, StringView extension_id)
{
    Locales locales;
    locales.m_ui_language = MUST(String::from_utf8(ui_language));

    auto locales_directory = LexicalPath::join(extension_root, "_locales"sv).string();
    auto has_locales = FileSystem::is_directory(locales_directory);
    if (has_locales && !default_locale.has_value())
        return "Localization used, but default_locale wasn't specified in the manifest."_string;
    if (default_locale.has_value() && !has_locales)
        return "Default locale was specified, but _locales subtree is missing."_string;

    if (has_locales) {
        auto default_messages = LexicalPath::join(locales_directory, default_locale->bytes_as_string_view(), "messages.json"sv).string();
        if (!FileSystem::is_regular_file(default_messages))
            return MUST(String::formatted("Default locale file _locales/{}/messages.json is missing.", *default_locale));

        // Lower-priority locales first, so that the UI locale's messages replace theirs.
        auto chain = fallback_locales(ui_language, default_locale);
        for (auto const& locale : chain.in_reverse()) {
            auto path = LexicalPath::join(locales_directory, locale.bytes_as_string_view(), "messages.json"sv).string();
            if (!FileSystem::is_regular_file(path))
                continue;

            auto json = TRY(read_extension_json(path, extension_root));
            TRY(json.try_for_each_member([&](String const& name, JsonValue const& entry) -> ErrorOr<void, String> {
                auto message = entry.is_object() ? fill_placeholders(entry.as_object()) : Optional<String> {};
                if (!message.has_value())
                    return MUST(String::formatted("Message '{}' in _locales/{}/messages.json has no \"message\".", name, locale));
                locales.m_messages.set(name.to_ascii_lowercase(), message.release_value());
                return {};
            }));
        }
    }

    auto locale = locale_directory_name(ui_language);
    locales.m_messages.set("@@extension_id"sv, MUST(String::from_utf8(extension_id)));
    locales.m_messages.set("@@ui_locale"sv, locale);
    // FIXME: Right-to-left UI languages.
    locales.m_messages.set("@@bidi_dir"sv, "ltr"_string);
    locales.m_messages.set("@@bidi_reversed_dir"sv, "rtl"_string);
    locales.m_messages.set("@@bidi_start_edge"sv, "left"_string);
    locales.m_messages.set("@@bidi_end_edge"sv, "right"_string);
    return locales;
}

Optional<String> Locales::message(StringView name, ReadonlySpan<String> substitutions) const
{
    auto message = m_messages.get_string(name.to_ascii_lowercase_string());
    if (!message.has_value())
        return {};
    return substitute_message(*message, substitutions);
}

String Locales::localize(StringView text) const
{
    StringBuilder builder;
    while (true) {
        auto start = text.find("__MSG_"sv);
        if (!start.has_value())
            break;
        auto end = text.find("__"sv, *start + 6);
        if (!end.has_value())
            break;

        builder.append(text.substring_view(0, *start));
        auto name = text.substring_view(*start + 6, *end - *start - 6);
        if (auto message = this->message(name); message.has_value())
            builder.append(*message);
        text = text.substring_view(*end + 2);
    }
    builder.append(text);
    return builder.to_string_without_validation();
}

}
