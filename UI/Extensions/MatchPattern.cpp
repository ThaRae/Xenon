/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <UI/Extensions/MatchPattern.h>

namespace Ladybird::Extensions {

static constexpr auto ALL_URLS_SCHEMES = to_array<StringView>({ "http"sv, "https"sv, "file"sv, "ftp"sv, "ws"sv, "wss"sv });
static constexpr auto PATTERN_SCHEMES = to_array<StringView>({ "*"sv, "http"sv, "https"sv, "file"sv, "ftp"sv, "ws"sv, "wss"sv });

bool glob_matches(StringView glob, StringView text, bool question_mark_is_wildcard)
{
    // Iterative wildcard matching, backtracking to the last star.
    size_t g = 0;
    size_t t = 0;
    Optional<size_t> star;
    size_t star_text = 0;

    while (t < text.length()) {
        if (g < glob.length() && (glob[g] == text[t] || (question_mark_is_wildcard && glob[g] == '?'))) {
            ++g;
            ++t;
        } else if (g < glob.length() && glob[g] == '*') {
            star = g++;
            star_text = t;
        } else if (star.has_value()) {
            g = *star + 1;
            t = ++star_text;
        } else {
            return false;
        }
    }
    while (g < glob.length() && glob[g] == '*')
        ++g;
    return g == glob.length();
}

ErrorOr<MatchPattern, String> MatchPattern::parse(StringView pattern)
{
    auto invalid = [&](StringView reason) {
        return MUST(String::formatted("Invalid match pattern '{}': {}.", pattern, reason));
    };

    MatchPattern result;
    if (pattern == "<all_urls>"sv) {
        result.m_all_urls = true;
        return result;
    }

    auto separator = pattern.find("://"sv);
    if (!separator.has_value())
        return invalid("Missing scheme separator"sv);

    auto scheme = pattern.substring_view(0, *separator);
    if (!PATTERN_SCHEMES.contains_slow(scheme))
        return invalid("Invalid scheme"sv);
    result.m_scheme = MUST(String::from_utf8(scheme));

    auto rest = pattern.substring_view(*separator + 3);
    auto path_start = rest.find('/');
    if (!path_start.has_value())
        return invalid("Empty path"sv);

    auto host = rest.substring_view(0, *path_start);
    result.m_path = MUST(String::from_utf8(rest.substring_view(*path_start)));

    if (scheme == "file"sv) {
        if (!host.is_empty())
            return invalid("File URLs have no host"sv);
        return result;
    }
    if (host.is_empty())
        return invalid("Empty host"sv);

    // A port, or ":*" for any. Hosts that are IPv6 addresses keep their colons inside brackets.
    if (auto colon = host.find_last(':'); colon.has_value() && !host.ends_with(']')) {
        auto port = host.substring_view(*colon + 1);
        host = host.substring_view(0, *colon);
        if (port != "*"sv) {
            auto number = port.to_number<u16>();
            if (!number.has_value())
                return invalid("Invalid port"sv);
            result.m_port = *number;
            result.m_any_port = false;
        }
    }

    if (host == "*"sv) {
        result.m_match_subdomains = true;
        return result;
    }
    if (host.starts_with("*."sv)) {
        result.m_match_subdomains = true;
        host = host.substring_view(2);
    }
    if (host.contains('*'))
        return invalid("Wildcard in the middle of the host"sv);
    result.m_host = MUST(String::from_utf8(host)).to_ascii_lowercase();
    return result;
}

bool MatchPattern::matches_scheme(StringView scheme) const
{
    if (m_all_urls)
        return ALL_URLS_SCHEMES.contains_slow(scheme);
    if (m_scheme == "*"sv)
        return scheme.is_one_of("http"sv, "https"sv);
    return m_scheme == scheme;
}

bool MatchPattern::matches_host(StringView host) const
{
    if (m_all_urls || m_host.is_empty())
        return true;
    if (host.equals_ignoring_ascii_case(m_host))
        return true;
    return m_match_subdomains && host.length() > m_host.bytes().size() && host.ends_with(m_host, CaseSensitivity::CaseInsensitive)
        && host[host.length() - m_host.bytes().size() - 1] == '.';
}

bool MatchPattern::matches(URL::URL const& url) const
{
    if (!matches_scheme(url.scheme()))
        return false;
    if (m_all_urls)
        return true;

    if (url.scheme() != "file"sv) {
        if (!matches_host(url.serialized_host()))
            return false;
        if (!m_any_port && url.port_or_default() != *m_port)
            return false;
    }

    // The path pattern covers the query too.
    StringBuilder path;
    path.append(url.serialize_path());
    if (auto query = url.query(); query.has_value()) {
        path.append('?');
        path.append(*query);
    }
    return glob_matches(m_path, path.string_view());
}

// https://source.chromium.org/chromium/chromium/src/+/main:extensions/common/url_pattern.cc (URLPattern::Contains)
bool MatchPattern::contains(MatchPattern const& other) const
{
    if (m_all_urls)
        return other.m_all_urls || (other.m_scheme == "*"sv ? true : ALL_URLS_SCHEMES.contains_slow(other.m_scheme));
    if (other.m_all_urls)
        return false;

    if (m_scheme != other.m_scheme && !(m_scheme == "*"sv && other.m_scheme.is_one_of("http"sv, "https"sv)))
        return false;

    if (!m_host.is_empty()) {
        if (other.m_host.is_empty() || (other.m_match_subdomains && !m_match_subdomains) || !matches_host(other.m_host))
            return false;
    }

    if (!m_any_port && (other.m_any_port || other.m_port != m_port))
        return false;

    // Chrome drops the other path's trailing wildcard and matches what's left against this path.
    auto other_path = other.m_path.bytes_as_string_view();
    if (other_path.ends_with('*'))
        other_path = other_path.substring_view(0, other_path.length() - 1);
    return glob_matches(m_path, other_path);
}

bool MatchPattern::matches_origin(URL::Origin const& origin) const
{
    if (origin.is_opaque() || !origin.scheme().has_value())
        return false;
    if (!matches_scheme(*origin.scheme()))
        return false;
    if (m_all_urls)
        return true;
    if (*origin.scheme() == "file"sv)
        return true;

    auto host = origin.host().serialize();
    if (!matches_host(host))
        return false;
    if (!m_any_port) {
        auto port = origin.port().value_or(URL::default_port_for_scheme(*origin.scheme()).value_or(0));
        if (port != *m_port)
            return false;
    }
    return true;
}

}
