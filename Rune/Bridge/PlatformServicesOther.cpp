/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebCommon/WebView/DictionaryLookup.h>
#include <LibWebView/SpellChecker.h>
#include <Rune/Bridge/PlatformServices.h>

#if defined(AK_OS_LINUX) && !defined(AK_OS_ANDROID)
#    include <UI/ExternalURLHandler.h>
#    include <gio/gio.h>
#endif

namespace Xenon {

Optional<String> platform_ui_font_family()
{
#if defined(AK_OS_LINUX) && !defined(AK_OS_ANDROID)
    static constexpr auto schema_id = "org.gnome.desktop.interface";
    auto* source = g_settings_schema_source_get_default();
    if (!source)
        return {};
    auto* schema = g_settings_schema_source_lookup(source, schema_id, TRUE);
    if (!schema)
        return {};
    auto has_font_name = g_settings_schema_has_key(schema, "font-name");
    g_settings_schema_unref(schema);
    if (!has_font_name)
        return {};

    auto* settings = g_settings_new(schema_id);
    auto* font_name = g_settings_get_string(settings, "font-name");
    auto description = StringView { font_name, strlen(font_name) }.trim_whitespace();
    if (auto last_space = description.find_last(' '); last_space.has_value() && description.substring_view(*last_space + 1).to_number<double>().has_value())
        description = description.substring_view(0, *last_space).trim_whitespace();
    auto family = String::from_utf8(description);
    g_free(font_name);
    g_object_unref(settings);
    if (family.is_error() || family.value().is_empty())
        return {};
    return family.release_value();
#else
    return {};
#endif
}

OwnPtr<WebView::SpellChecker> create_platform_spell_checker()
{
    return {};
}

void initialize_platform_external_url_handler(Core::EventLoop& event_loop)
{
#if defined(AK_OS_LINUX) && !defined(AK_OS_ANDROID)
    Ladybird::initialize_external_url_handler(event_loop);
#else
    (void)event_loop;
#endif
}

void resolve_platform_external_url_handler(URL::URL const& url, WebView::ExternalURLHandlerCallback callback)
{
#if defined(AK_OS_LINUX) && !defined(AK_OS_ANDROID)
    Ladybird::resolve_external_url_handler(url, "org.xenon.Xenon"_string, move(callback));
#else
    (void)url;
    callback(nullptr);
#endif
}

void show_platform_dictionary_lookup(WebView::DictionaryLookup const&, Gfx::FloatPoint)
{
}

}
