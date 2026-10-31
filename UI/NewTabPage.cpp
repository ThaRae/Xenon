/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/HashTable.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/QuickSort.h>
#include <LibURL/Parser.h>
#include <LibWebView/Application.h>
#include <LibWebView/BookmarkStore.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/FaviconStore.h>
#include <LibWebView/HistoryStore.h>
#include <LibWebView/SessionStore.h>
#include <LibWebView/ViewImplementation.h>
#include <UI/NewTabPage.h>

namespace Ladybird::NewTabPage {

static constexpr size_t MAX_TOP_SITES = 8;
static constexpr size_t HISTORY_ENTRIES_CONSIDERED = 500;
static constexpr size_t MAX_RECENTLY_CLOSED = 3;
static constexpr size_t RECENTLY_CLOSED_CONSIDERED = 10;
static constexpr size_t MAX_BOOKMARKS_BAR_ITEMS = 8;

bool is_new_tab_page(URL::URL const& url)
{
    return url == URL::URL::about("newtab"sv);
}

bool should_hide_url(URL::URL const& url)
{
    return should_hide_url(url, WebView::Application::settings().new_tab_page_url());
}

bool should_hide_url(URL::URL const& url, URL::URL const& new_tab_page_url)
{
    if (is_new_tab_page(url))
        return true;
    return url.scheme() == "about"sv && url == new_tab_page_url;
}

static Optional<String> favicon_data_url(ReadonlyBytes favicon_png)
{
    auto icon = encode_base64(favicon_png);
    if (icon.is_error())
        return {};
    return MUST(String::formatted("data:image/png;base64,{}", icon.value()));
}

static Optional<String> web_host(URL::URL const& url)
{
    if (!url.scheme().is_one_of("http"sv, "https"sv))
        return {};
    return MUST(String::from_utf8(url.serialized_host()));
}

static JsonArray top_sites(WebView::HistoryStore& history)
{
    auto entries = history.list_entries({}, 0, HISTORY_ENTRIES_CONSIDERED);
    quick_sort(entries, [](auto const& a, auto const& b) {
        if (a.decayed_visit_score != b.decayed_visit_score)
            return a.decayed_visit_score > b.decayed_visit_score;
        return a.visit_count > b.visit_count;
    });

    HashTable<String> hosts;
    JsonArray sites;
    for (auto const& entry : entries) {
        auto url = URL::Parser::basic_parse(entry.url);
        if (!url.has_value())
            continue;

        auto host = web_host(*url);
        if (!host.has_value() || hosts.set(*host) != HashSetResult::InsertedNewEntry)
            continue;

        JsonObject site;
        site.set("url"sv, entry.url);
        site.set("host"sv, host.release_value());
        if (entry.title.has_value())
            site.set("title"sv, *entry.title);
        if (entry.favicon_png.has_value()) {
            if (auto icon = favicon_data_url(*entry.favicon_png); icon.has_value())
                site.set("icon"sv, icon.release_value());
        }
        sites.must_append(move(site));

        if (sites.size() == MAX_TOP_SITES)
            break;
    }
    return sites;
}

static JsonArray recently_closed(WebView::BrowsingSession& session)
{
    JsonArray closed;
    if (!session.session_store)
        return closed;

    for (auto const& unit : session.session_store->recently_closed_units(RECENTLY_CLOSED_CONSIDERED)) {
        auto host = web_host(unit.active_url);
        if (!host.has_value())
            continue;

        JsonObject item;
        item.set("url"sv, unit.active_url.serialize());
        item.set("host"sv, host.release_value());
        item.set("closedAt"sv, unit.closed_at.milliseconds_since_epoch());
        if (unit.was_window)
            item.set("tabCount"sv, static_cast<u64>(unit.tab_count));
        if (session.history_store) {
            if (auto entry = session.history_store->entry_for_url(unit.active_url); entry.has_value()) {
                if (entry->title.has_value())
                    item.set("title"sv, *entry->title);
                if (entry->favicon_png.has_value()) {
                    if (auto icon = favicon_data_url(*entry->favicon_png); icon.has_value())
                        item.set("icon"sv, icon.release_value());
                }
            }
        }
        closed.must_append(move(item));

        if (closed.size() == MAX_RECENTLY_CLOSED)
            break;
    }
    return closed;
}

static JsonArray bookmarks_bar(WebView::BrowsingSession& session)
{
    JsonArray bookmarks;
    for (auto const& item : WebView::Application::bookmark_store().root_items()) {
        if (!item.is_bookmark())
            continue;

        auto const& bookmark = item.bookmark();
        auto host = web_host(bookmark.url);
        if (!host.has_value())
            continue;

        JsonObject site;
        site.set("url"sv, bookmark.url.serialize());
        site.set("host"sv, host.release_value());
        if (bookmark.title.has_value())
            site.set("title"sv, *bookmark.title);
        if (bookmark.favicon_hash.has_value() && session.favicon_store) {
            if (auto png = session.favicon_store->favicon_png(*bookmark.favicon_hash); png.has_value()) {
                if (auto icon = favicon_data_url(*png); icon.has_value())
                    site.set("icon"sv, icon.release_value());
            }
        }
        bookmarks.must_append(move(site));

        if (bookmarks.size() == MAX_BOOKMARKS_BAR_ITEMS)
            break;
    }
    return bookmarks;
}

void populate(WebView::ViewImplementation& view, WebView::BrowsingSession& session, ShowRecentlyClosed show_recently_closed)
{
    auto is_private = session.is_private() == WebView::IsPrivate::Yes;

    JsonObject data;
    data.set("isPrivate"sv, is_private);
    data.set("sites"sv, session.history_store ? top_sites(*session.history_store) : JsonArray {});
    if (!is_private) {
        data.set("bookmarksBar"sv, bookmarks_bar(session));
        if (show_recently_closed == ShowRecentlyClosed::Yes)
            data.set("recentlyClosed"sv, recently_closed(session));
    }

    view.run_javascript(MUST(String::formatted("window.xenonNewTab?.show({});", data.serialized())));
}

}
