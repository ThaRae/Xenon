/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/BinarySearch.h>
#include <AK/ByteString.h>
#include <AK/CharacterTypes.h>
#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <AK/Vector.h>
#include <LibURL/Parser.h>
#include <UI/BangTable.h>
#include <UI/Bangs.h>

namespace Ladybird::Bangs {

static constexpr char BANG_PREFIX = '!';
static constexpr auto QUERY_PLACEHOLDER = "{{{s}}}"sv;

static constexpr StringView table_string(u32 offset, size_t length)
{
    return { Table::strings + offset, length };
}

size_t bang_count()
{
    return Table::entries.size();
}

Bang bang_at(size_t index)
{
    auto const& entry = Table::entries[index];
    return {
        .trigger = table_string(entry.trigger_offset, entry.trigger_length),
        .name = table_string(entry.name_offset, entry.name_length),
        .query_url = table_string(entry.query_url_offset, entry.query_url_length),
        .snap_domain = table_string(entry.snap_domain_offset, entry.snap_domain_length),
        .format = static_cast<Format>(entry.format),
    };
}

// The table's triggers are lowercase and sorted by their bytes, so this compares the trigger as if it were lowercase.
static int compare_with_entry(StringView trigger, Table::Entry const& entry)
{
    auto entry_trigger = table_string(entry.trigger_offset, entry.trigger_length);
    auto length = min(trigger.length(), entry_trigger.length());
    for (size_t i = 0; i < length; ++i) {
        auto byte = static_cast<u8>(to_ascii_lowercase(static_cast<u8>(trigger[i])));
        auto entry_byte = static_cast<u8>(entry_trigger[i]);
        if (byte != entry_byte)
            return byte < entry_byte ? -1 : 1;
    }
    if (trigger.length() == entry_trigger.length())
        return 0;
    return trigger.length() < entry_trigger.length() ? -1 : 1;
}

Optional<Bang> find_bang(StringView trigger)
{
    size_t index = 0;
    if (!binary_search(Table::entries, trigger, &index, compare_with_entry))
        return {};
    return bang_at(index);
}

Optional<Bang> bang_for_word(StringView word)
{
    if (word.length() < 2 || word[0] != BANG_PREFIX)
        return {};
    return find_bang(word.substring_view(1));
}

Optional<LeadingBang> leading_bang(StringView input)
{
    size_t word_length = 0;
    while (word_length < input.length() && !is_ascii_space(input[word_length]))
        ++word_length;

    // Without the space after it, the bang may still be being typed, so there is no chip yet.
    if (word_length == input.length())
        return {};
    auto bang = bang_for_word(input.substring_view(0, word_length));
    if (!bang.has_value())
        return {};
    return LeadingBang { .bang = *bang, .length = word_length + 1 };
}

// The site's front page, which is the query URL up to the end of its host.
static StringView front_page_url(StringView query_url)
{
    auto host_start = query_url.find("://"sv);
    if (!host_start.has_value())
        return query_url;

    auto after_scheme = query_url.substring_view(*host_start + 3);
    auto host_end = after_scheme.find_any_of("/?#"sv);
    return query_url.substring_view(0, *host_start + 3 + host_end.value_or(after_scheme.length()));
}

Optional<URL::URL> url_for_query(Bang const& bang, StringView query)
{
    if (query.is_empty()) {
        if (has_flag(bang.format, Format::OpenSnapDomain) && !bang.snap_domain.is_empty())
            return URL::Parser::basic_parse(ByteString::formatted("https://{}", bang.snap_domain));
        if (has_flag(bang.format, Format::OpenBasePath)) {
            // A placeholder in the host ("https://{s}.example.com/") leaves no front page to open, so the bang alone
            // doesn't resolve, and the text is searched as typed.
            auto front_page = front_page_url(bang.query_url);
            if (front_page.contains("{{{s}}}"sv))
                return {};
            return URL::Parser::basic_parse(front_page);
        }
    }

    auto space_as_plus = has_flag(bang.format, Format::SpaceToPlus) ? URL::SpaceAsPlus::Yes : URL::SpaceAsPlus::No;
    String encoded_query;
    if (has_flag(bang.format, Format::EncodeQuery)) {
        // NB: The query is one URL component, so it is encoded as one: "&" or "#" left as they are would end it early.
        encoded_query = URL::percent_encode(query, URL::PercentEncodeSet::Component, space_as_plus);
    } else {
        encoded_query = MUST(String::from_utf8(query));
        if (space_as_plus == URL::SpaceAsPlus::Yes)
            encoded_query = MUST(encoded_query.replace(" "sv, "+"sv, ReplaceMode::All));
    }

    auto query_url = MUST(String::from_utf8(bang.query_url));
    return URL::Parser::basic_parse(MUST(query_url.replace(QUERY_PLACEHOLDER, encoded_query, ReplaceMode::All)));
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

    StringBuilder query;
    query.join(' ', words);

    auto url = url_for_query(*bang, query.string_view());
    if (!url.has_value())
        return {};
    return BangNavigation { .bang = *bang, .url = url.release_value() };
}

}
