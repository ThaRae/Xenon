/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <LibCore/Forward.h>
#include <LibGfx/Point.h>
#include <LibURL/Forward.h>
#include <LibWebView/ExternalURLHandler.h>
#include <LibWebView/Forward.h>

namespace Xenon {

Optional<String> platform_ui_font_family();
OwnPtr<WebView::SpellChecker> create_platform_spell_checker();
void initialize_platform_external_url_handler(Core::EventLoop&);
void resolve_platform_external_url_handler(URL::URL const&, WebView::ExternalURLHandlerCallback);
void show_platform_dictionary_lookup(WebView::DictionaryLookup const&, Gfx::FloatPoint window_point);
Gfx::FloatPoint page_origin_in_window();

}
