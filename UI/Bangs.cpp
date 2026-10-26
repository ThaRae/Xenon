/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/CharacterTypes.h>
#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <AK/Vector.h>
#include <LibURL/Parser.h>
#include <UI/Bangs.h>

namespace Ladybird::Bangs {

static constexpr char BANG_PREFIX = '!';

// FIXME: Helium resolves DuckDuckGo's full list of 13,000+ bangs. Import that list instead of curating our own.
static constexpr auto s_builtin_bangs = to_array<Bang>({
    // Search engines
    { "g"sv, "Google"sv, "https://www.google.com/search?q=%s"sv },
    { "ddg"sv, "DuckDuckGo"sv, "https://duckduckgo.com/?q=%s"sv },
    { "b"sv, "Bing"sv, "https://www.bing.com/search?q=%s"sv },
    { "k"sv, "Kagi"sv, "https://kagi.com/search?q=%s"sv },
    { "brave"sv, "Brave Search"sv, "https://search.brave.com/search?q=%s"sv },
    { "sp"sv, "Startpage"sv, "https://www.startpage.com/do/search?q=%s"sv },
    { "e"sv, "Ecosia"sv, "https://www.ecosia.org/search?q=%s"sv },
    { "qw"sv, "Qwant"sv, "https://www.qwant.com/?q=%s"sv },
    { "ya"sv, "Yandex"sv, "https://yandex.com/search/?text=%s"sv },
    { "gi"sv, "Google Images"sv, "https://www.google.com/search?tbm=isch&q=%s"sv },
    { "gn"sv, "Google News"sv, "https://news.google.com/search?q=%s"sv },
    { "gs"sv, "Google Scholar"sv, "https://scholar.google.com/scholar?q=%s"sv },
    { "gm"sv, "Google Maps"sv, "https://www.google.com/maps/search/%s"sv },
    { "osm"sv, "OpenStreetMap"sv, "https://www.openstreetmap.org/search?query=%s"sv },

    // Reference
    { "w"sv, "Wikipedia"sv, "https://en.wikipedia.org/wiki/Special:Search?search=%s"sv },
    { "wt"sv, "Wiktionary"sv, "https://en.wiktionary.org/wiki/Special:Search?search=%s"sv },
    { "wa"sv, "Wolfram Alpha"sv, "https://www.wolframalpha.com/input?i=%s"sv },
    { "mw"sv, "Merriam-Webster"sv, "https://www.merriam-webster.com/dictionary/%s"sv },
    { "ud"sv, "Urban Dictionary"sv, "https://www.urbandictionary.com/define.php?term=%s"sv },
    { "gt"sv, "Google Translate"sv, "https://translate.google.com/?sl=auto&tl=en&text=%s"sv },
    { "wb"sv, "Wayback Machine"sv, "https://web.archive.org/web/*/%s"sv },
    { "arxiv"sv, "arXiv"sv, "https://arxiv.org/search/?query=%s&searchtype=all"sv },
    { "imdb"sv, "IMDb"sv, "https://www.imdb.com/find/?q=%s"sv },
    { "gr"sv, "Goodreads"sv, "https://www.goodreads.com/search?q=%s"sv },

    // AI assistants
    { "claude"sv, "Claude"sv, "https://claude.ai/new?q=%s"sv },
    { "cgpt"sv, "ChatGPT"sv, "https://chatgpt.com/?q=%s"sv },
    { "ppx"sv, "Perplexity"sv, "https://www.perplexity.ai/search?q=%s"sv },

    // Development
    { "gh"sv, "GitHub"sv, "https://github.com/search?q=%s"sv },
    { "gl"sv, "GitLab"sv, "https://gitlab.com/search?search=%s"sv },
    { "so"sv, "Stack Overflow"sv, "https://stackoverflow.com/search?q=%s"sv },
    { "mdn"sv, "MDN Web Docs"sv, "https://developer.mozilla.org/en-US/search?q=%s"sv },
    { "caniuse"sv, "Can I use"sv, "https://caniuse.com/?search=%s"sv },
    { "npm"sv, "npm"sv, "https://www.npmjs.com/search?q=%s"sv },
    { "pypi"sv, "PyPI"sv, "https://pypi.org/search/?q=%s"sv },
    { "crates"sv, "crates.io"sv, "https://crates.io/search?q=%s"sv },
    { "rust"sv, "Rust std docs"sv, "https://doc.rust-lang.org/std/?search=%s"sv },
    { "docker"sv, "Docker Hub"sv, "https://hub.docker.com/search?q=%s"sv },
    { "aw"sv, "ArchWiki"sv, "https://wiki.archlinux.org/index.php?search=%s"sv },
    { "hn"sv, "Hacker News"sv, "https://hn.algolia.com/?q=%s"sv },

    // Media and social
    { "yt"sv, "YouTube"sv, "https://www.youtube.com/results?search_query=%s"sv },
    { "ytm"sv, "YouTube Music"sv, "https://music.youtube.com/search?q=%s"sv },
    { "r"sv, "Reddit"sv, "https://www.reddit.com/search/?q=%s"sv },
    { "x"sv, "X"sv, "https://x.com/search?q=%s"sv },
    { "bs"sv, "Bluesky"sv, "https://bsky.app/search?q=%s"sv },
    { "twitch"sv, "Twitch"sv, "https://www.twitch.tv/search?term=%s"sv },
    { "spotify"sv, "Spotify"sv, "https://open.spotify.com/search/%s"sv },
    { "genius"sv, "Genius"sv, "https://genius.com/search?q=%s"sv },
    { "nebula"sv, "Nebula"sv, "https://nebula.tv/search?q=%s"sv },
    { "li"sv, "LinkedIn"sv, "https://www.linkedin.com/search/results/all/?keywords=%s"sv },
    { "pin"sv, "Pinterest"sv, "https://www.pinterest.com/search/pins/?q=%s"sv },

    // Shopping
    { "a"sv, "Amazon"sv, "https://www.amazon.com/s?k=%s"sv },
    { "ebay"sv, "eBay"sv, "https://www.ebay.com/sch/i.html?_nkw=%s"sv },
    { "etsy"sv, "Etsy"sv, "https://www.etsy.com/search?q=%s"sv },
    { "steam"sv, "Steam"sv, "https://store.steampowered.com/search/?term=%s"sv },
    { "yelp"sv, "Yelp"sv, "https://www.yelp.com/search?find_desc=%s"sv },
});

ReadonlySpan<Bang> builtin_bangs()
{
    return s_builtin_bangs.span();
}

Optional<Bang const&> find_bang(StringView trigger)
{
    for (auto const& bang : s_builtin_bangs) {
        if (bang.trigger.equals_ignoring_ascii_case(trigger))
            return bang;
    }
    return {};
}

static Optional<Bang const&> bang_for_word(StringView word)
{
    if (word.length() < 2 || word[0] != BANG_PREFIX)
        return {};
    return find_bang(word.substring_view(1));
}

// The site's front page, which is the query URL up to the end of its host.
static StringView front_page_url(StringView query_url)
{
    auto host_start = query_url.find("://"sv);
    if (!host_start.has_value())
        return query_url;

    auto path_start = query_url.find('/', *host_start + 3);
    return query_url.substring_view(0, path_start.value_or(query_url.length()));
}

Optional<BangNavigation> resolve(StringView input)
{
    auto words = input.split_view_if([](char c) { return is_ascii_space(c); });
    if (words.is_empty())
        return {};

    // NB: Only the first or last word can be a bang, so that a "!" inside a query (as in "what is 5! in binary") stays
    //     part of the query.
    size_t bang_index = 0;
    auto bang = bang_for_word(words.first());
    if (!bang.has_value() && words.size() > 1) {
        bang_index = words.size() - 1;
        bang = bang_for_word(words.last());
    }
    if (!bang.has_value())
        return {};

    words.remove(bang_index);

    Optional<URL::URL> url;
    if (words.is_empty()) {
        url = URL::Parser::basic_parse(front_page_url(bang->query_url));
    } else {
        StringBuilder query;
        query.join(' ', words);

        auto query_url = MUST(String::from_utf8(bang->query_url));
        url = URL::Parser::basic_parse(MUST(query_url.replace("%s"sv, URL::percent_encode(query.string_view()), ReplaceMode::All)));
    }

    if (!url.has_value())
        return {};
    return BangNavigation { .bang = *bang, .url = url.release_value() };
}

}
