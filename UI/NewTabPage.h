/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibURL/URL.h>
#include <LibWebView/Forward.h>

// Xenon's new tab page (UI/Resources/about-pages/newtab.*) is static, so it has no channel to the browser of its own.
// When a tab finishes loading it, the browser hands it what it shows, the user's most visited sites, by calling the
// page's own xenonNewTab.show().
namespace Ladybird::NewTabPage {

bool is_new_tab_page(URL::URL const&);
void populate(WebView::ViewImplementation&, WebView::BrowsingSession&);

}
