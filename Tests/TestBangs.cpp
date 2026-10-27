/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/String.h>
#include <LibTest/TestCase.h>
#include <UI/Bangs.h>

using namespace Ladybird;

static ByteString resolved_url(StringView input)
{
    auto navigation = Bangs::resolve(input);
    if (!navigation.has_value())
        return {};
    return navigation->url.serialize().to_byte_string();
}

TEST_CASE(leading_bang)
{
    EXPECT_EQ(resolved_url("!w ladybird browser"sv), "https://en.wikipedia.org/wiki/Special:Search?search=ladybird%20browser"sv);
}

TEST_CASE(trailing_bang)
{
    EXPECT_EQ(resolved_url("ladybird browser !w"sv), "https://en.wikipedia.org/wiki/Special:Search?search=ladybird%20browser"sv);
}

TEST_CASE(bang_alone_opens_the_front_page)
{
    EXPECT_EQ(resolved_url("!gh"sv), "https://github.com/"sv);
    EXPECT_EQ(resolved_url("  !gh  "sv), "https://github.com/"sv);
}

TEST_CASE(trigger_is_case_insensitive)
{
    EXPECT_EQ(resolved_url("!GH xenon"sv), "https://github.com/search?q=xenon"sv);
}

TEST_CASE(resolution_names_the_site)
{
    auto navigation = Bangs::resolve("!yt lofi"sv);
    VERIFY(navigation.has_value());
    EXPECT_EQ(navigation->bang.name, "YouTube"sv);
}

// The query is a URL component, so characters that would end it early or start another parameter must be escaped.
TEST_CASE(query_is_percent_encoded_as_a_component)
{
    EXPECT_EQ(resolved_url("!w salt & pepper"sv), "https://en.wikipedia.org/wiki/Special:Search?search=salt%20%26%20pepper"sv);
    EXPECT_EQ(resolved_url("!w a=b+c#d"sv), "https://en.wikipedia.org/wiki/Special:Search?search=a%3Db%2Bc%23d"sv);
}

TEST_CASE(input_without_a_known_bang_is_left_alone)
{
    EXPECT(!Bangs::resolve("ladybird browser"sv).has_value());
    EXPECT(!Bangs::resolve("!notabang ladybird"sv).has_value());
    EXPECT(!Bangs::resolve("!"sv).has_value());
    EXPECT(!Bangs::resolve(""sv).has_value());
    EXPECT(!Bangs::resolve("   "sv).has_value());
}

// Only the first or last word is a bang, so a "!" in the middle of a query stays part of it.
TEST_CASE(bang_in_the_middle_is_part_of_the_query)
{
    EXPECT(!Bangs::resolve("what does !w mean"sv).has_value());
}

TEST_CASE(every_builtin_bang_resolves)
{
    for (auto const& bang : Bangs::builtin_bangs()) {
        auto with_query = Bangs::resolve(ByteString::formatted("!{} xenon", bang.trigger));
        VERIFY(with_query.has_value());
        EXPECT(with_query->url.serialize().contains("xenon"sv));
        EXPECT_EQ(with_query->url.scheme(), "https"sv);

        auto front_page = Bangs::resolve(ByteString::formatted("!{}", bang.trigger));
        VERIFY(front_page.has_value());
        EXPECT(!front_page->url.serialize().contains("%s"sv));
    }
}
