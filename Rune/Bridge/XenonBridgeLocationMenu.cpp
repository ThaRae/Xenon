/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/Application.h>
#include <LibWebView/Omnibox.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/XenonBridgeLocationMenu.h>

static XenonLocationMenuCallbacks s_location_menu_callbacks {};

static void describe_paste_and_go(u64 request, String const& clipboard_text)
{
    auto has_search_engine = WebView::Application::settings().search_engine_settings().engine.has_value();
    auto label = WebView::Omnibox::text_for_paste_and_go_action(clipboard_text, has_search_engine);

    if (auto function = s_location_menu_callbacks.paste_and_go_described) {
        auto bytes = label.bytes_as_string_view();
        function(s_location_menu_callbacks.context, request, { bytes.characters_without_null_termination(), bytes.length() });
    }
}

void xenon_engine_set_location_menu_callbacks(XenonLocationMenuCallbacks const* callbacks)
{
    s_location_menu_callbacks = callbacks ? *callbacks : XenonLocationMenuCallbacks {};
}

void xenon_engine_location_describe_paste_and_go(uint64_t request, XenonStr clipboard_text)
{
    StringView text;
    if (clipboard_text.data && clipboard_text.length > 0)
        text = { clipboard_text.data, clipboard_text.length };
    Xenon::Engine::the().post([request, text = String::from_utf8_with_replacement_character(text)] {
        describe_paste_and_go(request, text);
    });
}
