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

class MatchPattern {
public:
    static ErrorOr<MatchPattern, String> parse(StringView);

    bool matches(URL::URL const&) const;
    bool matches_origin(URL::Origin const&) const;
    bool contains(MatchPattern const&) const;

    bool matches_all_urls() const { return m_all_urls; }
    bool matches_scheme(StringView) const;
    String const& host() const { return m_host; }
    bool matches_subdomains() const { return m_match_subdomains; }

private:
    MatchPattern() = default;

    bool matches_host(StringView) const;

    bool m_all_urls { false };
    String m_scheme;
    String m_host;
    bool m_match_subdomains { false };
    Optional<u16> m_port;
    bool m_any_port { true };
    String m_path;
};

bool glob_matches(StringView glob, StringView text, bool question_mark_is_wildcard = false);

}
