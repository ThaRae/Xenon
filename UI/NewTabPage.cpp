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
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/HistoryStore.h>
#include <LibWebView/ViewImplementation.h>
#include <UI/NewTabPage.h>

namespace Ladybird::NewTabPage {

static constexpr size_t MAX_TOP_SITES = 8;
static constexpr size_t HISTORY_ENTRIES_CONSIDERED = 500;

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

// The sites visited most, by the history's decaying visit score, one per host so a single busy site can't fill the page.
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
        if (!url.has_value() || !url->scheme().is_one_of("http"sv, "https"sv))
            continue;

        auto host = MUST(String::from_utf8(url->serialized_host()));
        if (hosts.set(host) != HashSetResult::InsertedNewEntry)
            continue;

        JsonObject site;
        site.set("url"sv, entry.url);
        site.set("host"sv, host);
        if (entry.title.has_value())
            site.set("title"sv, *entry.title);
        if (entry.favicon_png.has_value()) {
            if (auto icon = encode_base64(*entry.favicon_png); !icon.is_error())
                site.set("icon"sv, MUST(String::formatted("data:image/png;base64,{}", icon.value())));
        }
        sites.must_append(move(site));

        if (sites.size() == MAX_TOP_SITES)
            break;
    }
    return sites;
}

void populate(WebView::ViewImplementation& view, WebView::BrowsingSession& session)
{
    JsonObject data;
    data.set("isPrivate"sv, session.is_private() == WebView::IsPrivate::Yes);
    data.set("sites"sv, session.history_store ? top_sites(*session.history_store) : JsonArray {});

    // NB: JSON is a JavaScript expression, so the data can be passed as the call's argument as it is.
    view.run_javascript(MUST(String::formatted("window.xenonNewTab?.show({});", data.serialized())));
}

}
