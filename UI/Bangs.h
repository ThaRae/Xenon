/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/StringView.h>
#include <LibURL/URL.h>

namespace Ladybird::Bangs {

struct Bang {
    StringView trigger;
    StringView name;
    // The site's search URL, with %s where the percent-encoded query goes.
    StringView query_url;
};

struct BangNavigation {
    Bang const& bang;
    URL::URL url;
};

// Resolves DuckDuckGo-style !bangs locally, so the query goes straight to the site instead of through a search engine.
// The bang may lead the input ("!w ladybird") or end it ("ladybird !w"). A bang on its own opens the site's front page.
Optional<BangNavigation> resolve(StringView input);

Optional<Bang const&> find_bang(StringView trigger);
ReadonlySpan<Bang> builtin_bangs();

}
