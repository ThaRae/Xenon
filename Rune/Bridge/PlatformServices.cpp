/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Atomic.h>
#include <LibWebCommon/WebView/DictionaryLookup.h>
#include <Rune/Bridge/PlatformServices.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgePlatform.h>

namespace Xenon {

static Atomic<float> s_page_origin_x { 0 };
static Atomic<float> s_page_origin_y { 0 };

Gfx::FloatPoint page_origin_in_window()
{
    return { s_page_origin_x.load(), s_page_origin_y.load() };
}

void View::install_platform_callbacks()
{
    on_request_dictionary_lookup = [this](WebView::DictionaryLookup const& lookup, Gfx::IntPoint position) {
        auto ratio = max(m_device_pixel_ratio, 0.01);
        auto origin = page_origin_in_window();
        Gfx::FloatPoint window_point { origin.x() + static_cast<float>(position.x() / ratio), origin.y() + static_cast<float>(position.y() / ratio) };
        show_platform_dictionary_lookup(lookup, window_point);
    };
}

}

void xenon_engine_set_page_origin(float x, float y)
{
    Xenon::s_page_origin_x.store(x);
    Xenon::s_page_origin_y.store(y);
}
