/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <LibURL/URL.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/Forward.h>
#include <UI/BookmarkStore.h>
#include <UI/SessionStore.h>

namespace Ladybird::NewTabPage {

bool is_new_tab_page(URL::URL const&);

bool should_hide_url(URL::URL const&);
bool should_hide_url(URL::URL const&, URL::URL const& new_tab_page_url);

enum class ShowRecentlyClosed {
    No,
    Yes,
};

struct Sources {
    WebView::IsPrivate is_private { WebView::IsPrivate::No };
    WebView::HistoryStore* history_store { nullptr };
    WebView::FaviconStore& favicon_store;
    ReadonlySpan<BookmarkItem> bookmarks;
    Function<Vector<RecentlyClosedSessionUnit>(size_t limit)> recently_closed_units;
};

void populate(WebView::ViewImplementation&, Sources const&, ShowRecentlyClosed);

}
