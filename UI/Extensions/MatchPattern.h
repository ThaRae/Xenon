/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <LibURL/Origin.h>
#include <LibURL/URL.h>

namespace Ladybird::Extensions {

// A Chrome match pattern, like "https://*.example.com/*" or "<all_urls>", as content scripts, host permissions, and web
// accessible resources use them.
// https://developer.chrome.com/docs/extensions/develop/concepts/match-patterns
class MatchPattern {
public:
    static ErrorOr<MatchPattern, String> parse(StringView);

    bool matches(URL::URL const&) const;
    // Whether the pattern matches some URL of the origin, ignoring its path.
    bool matches_origin(URL::Origin const&) const;

    bool matches_all_urls() const { return m_all_urls; }

private:
    MatchPattern() = default;

    bool matches_scheme(StringView) const;
    bool matches_host(StringView) const;

    bool m_all_urls { false };
    // "*" for http and https.
    String m_scheme;
    // "*" for any host, or a host that may start with "*." for its subdomains too.
    String m_host;
    bool m_match_subdomains { false };
    // Any port when empty.
    Optional<u16> m_port;
    bool m_any_port { true };
    String m_path;
};

// Matches text against a glob, where * is any run of characters and, if enabled, ? any one character.
bool glob_matches(StringView glob, StringView text, bool question_mark_is_wildcard = false);

}
