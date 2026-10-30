/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibURL/URL.h>
#include <LibWebView/Forward.h>

namespace Ladybird::NewTabPage {

bool is_new_tab_page(URL::URL const&);

bool should_hide_url(URL::URL const&);
bool should_hide_url(URL::URL const&, URL::URL const& new_tab_page_url);

enum class ShowRecentlyClosed {
    No,
    Yes,
};

void populate(WebView::ViewImplementation&, WebView::BrowsingSession&, ShowRecentlyClosed);

}
