/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/EnumBits.h>
#include <AK/Optional.h>
#include <AK/StringView.h>
#include <LibURL/URL.h>

namespace Ladybird::Bangs {

// How a bang puts the query into its URL, as Kagi's list sets it (its "fmt" flags). Most bangs have every flag.
enum class Format : u8 {
    None = 0,
    // Without a query, open the site's front page instead of the query URL with nothing in it.
    OpenBasePath = 1 << 0,
    // Without a query, open the snap domain, the site that the query URL searches through another one.
    OpenSnapDomain = 1 << 1,
    // Percent-encode the query. Some sites take it as it is, for example as a path.
    EncodeQuery = 1 << 2,
    // Encode spaces as "+" rather than "%20".
    SpaceToPlus = 1 << 3,
};
AK_ENUM_BITWISE_OPERATORS(Format);

struct Bang {
    StringView trigger;
    // The site's name, which the address bar shows in its chip while the bang is typed.
    StringView name;
    // The site's search URL, with {{{s}}} where the query goes.
    StringView query_url;
    StringView snap_domain;
    Format format { Format::None };
};

struct BangNavigation {
    Bang bang;
    URL::URL url;
};

// Resolves DuckDuckGo-style !bangs locally, so the query goes straight to the site instead of through a search engine.
// The bang may lead the input ("!w ladybird") or end it ("ladybird !w"). A bang on its own opens the site's front page.
// The bangs are Kagi's list (UI/Resources/bangs, from Meta/update-bangs.py), which shares DuckDuckGo's triggers.
Optional<BangNavigation> resolve(StringView input);

// Where the bang takes a query, or its front page when the query is empty.
Optional<URL::URL> url_for_query(Bang const&, StringView query);

// The bang a word such as "!w" stands for.
Optional<Bang> bang_for_word(StringView word);

// The bang that the input starts with, once the space after it has been typed, so that the query has started. The
// address bar names the bang's site in a chip while it shows. The length is in bytes, and includes that space.
struct LeadingBang {
    Bang bang;
    size_t length;
};
Optional<LeadingBang> leading_bang(StringView input);

Optional<Bang> find_bang(StringView trigger);

size_t bang_count();
Bang bang_at(size_t index);

}
