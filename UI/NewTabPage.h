/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibURL/URL.h>
#include <LibWebView/Forward.h>

// Xenon's new tab page (UI/Resources/about-pages/newtab.*) is static, so it has no channel to the browser of its own.
// When a tab finishes loading it, the browser hands it what it shows, by calling the page's own xenonNewTab.show(): the
// user's most visited sites, the links on the bookmarks bar, and the tabs closed most recently.
namespace Ladybird::NewTabPage {

bool is_new_tab_page(URL::URL const&);

// The address bar leaves out a new tab page's URL, as Chrome's leaves out chrome://newtab, however the tab came to the
// page: Xenon's own, or an internal page such as about:blank that the user picked in its place. A page on the web that
// the user picked shows its URL once they leave the address bar, as any other page does.
bool should_hide_url(URL::URL const&);
bool should_hide_url(URL::URL const&, URL::URL const& new_tab_page_url);

void populate(WebView::ViewImplementation&, WebView::BrowsingSession&);

}
