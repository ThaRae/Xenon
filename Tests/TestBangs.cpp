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
    EXPECT_EQ(resolved_url("!w ladybird browser"sv), "https://wikipedia.org/w/index.php?search=ladybird+browser"sv);
}

TEST_CASE(trailing_bang)
{
    EXPECT_EQ(resolved_url("ladybird browser !w"sv), "https://wikipedia.org/w/index.php?search=ladybird+browser"sv);
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

TEST_CASE(query_is_percent_encoded_as_a_component)
{
    EXPECT_EQ(resolved_url("!w salt & pepper"sv), "https://wikipedia.org/w/index.php?search=salt+%26+pepper"sv);
    EXPECT_EQ(resolved_url("!w a=b+c#d"sv), "https://wikipedia.org/w/index.php?search=a%3Db%2Bc%23d"sv);
}

TEST_CASE(input_without_a_known_bang_is_left_alone)
{
    EXPECT(!Bangs::resolve("ladybird browser"sv).has_value());
    EXPECT(!Bangs::resolve("!notabang ladybird"sv).has_value());
    EXPECT(!Bangs::resolve("!"sv).has_value());
    EXPECT(!Bangs::resolve(""sv).has_value());
    EXPECT(!Bangs::resolve("   "sv).has_value());
}

TEST_CASE(bang_in_the_middle_is_part_of_the_query)
{
    EXPECT(!Bangs::resolve("what does !w mean"sv).has_value());
}

TEST_CASE(bangs_from_the_full_list)
{
    EXPECT_EQ(resolved_url("!bsky ladybird"sv), "https://bsky.app/search?q=ladybird"sv);
    EXPECT_EQ(resolved_url("!ecosia trees"sv), "https://www.ecosia.org/search?q=trees"sv);
    EXPECT_EQ(resolved_url("!c++ vector"sv), "https://www.cplusplus.com/search.do?q=vector"sv);
    EXPECT_EQ(resolved_url("!wayback ladybird.org"sv), "https://web.archive.org/web/*/ladybird.org"sv);

    EXPECT_EQ(resolved_url("!wikipedia ladybird"sv), resolved_url("!w ladybird"sv));
    EXPECT_EQ(resolved_url("!github xenon"sv), resolved_url("!gh xenon"sv));
}

TEST_CASE(non_ascii_triggers)
{
    auto navigation = Bangs::resolve("!авито велосипед"sv);
    VERIFY(navigation.has_value());
    EXPECT_EQ(navigation->bang.name, "Avito"sv);
    EXPECT_EQ(resolved_url("!авито велосипед"sv), "https://www.avito.ru/?q=%D0%B2%D0%B5%D0%BB%D0%BE%D1%81%D0%B8%D0%BF%D0%B5%D0%B4"sv);
}

TEST_CASE(xenon_adds_kagi)
{
    EXPECT_EQ(resolved_url("!k ladybird"sv), "https://kagi.com/search?q=ladybird"sv);
    EXPECT_EQ(resolved_url("!kagi"sv), "https://kagi.com/"sv);
}

TEST_CASE(format_flags)
{
    EXPECT_EQ(resolved_url("!caniuse css grid"sv), "https://caniuse.com/?search=css%20grid"sv);

    EXPECT_EQ(resolved_url("!ghrepo LadybirdBrowser/ladybird"sv), "https://github.com/LadybirdBrowser/ladybird"sv);

    EXPECT_EQ(resolved_url("!hn"sv), "https://news.ycombinator.com/"sv);
    EXPECT_EQ(resolved_url("!hn ladybird"sv), "https://hn.algolia.com/?q=ladybird"sv);

    EXPECT_EQ(resolved_url("!oxlint"sv), "https://oxc.rs/docs/guide/usage/linter/rules/"sv);
}

TEST_CASE(every_placeholder_gets_the_query)
{
    EXPECT_EQ(resolved_url("!pagesjaunes plombier"sv), "https://www.pagesjaunes.fr/annuaire/chercherlespros?quoiqui=plombier&monochamp=plombier"sv);
}

TEST_CASE(leading_bang_has_a_chip_once_the_query_starts)
{
    auto chip = Bangs::leading_bang("!w "sv);
    VERIFY(chip.has_value());
    EXPECT_EQ(chip->bang.name, "Wikipedia"sv);
    EXPECT_EQ(chip->length, 3u);

    EXPECT_EQ(Bangs::leading_bang("!w ladybird"sv)->length, 3u);
    EXPECT(!Bangs::leading_bang("!w"sv).has_value());
    EXPECT(!Bangs::leading_bang("!"sv).has_value());
    EXPECT(!Bangs::leading_bang(""sv).has_value());
    EXPECT(!Bangs::leading_bang("!notabang ladybird"sv).has_value());
    EXPECT(!Bangs::leading_bang(" !w ladybird"sv).has_value());
}

TEST_CASE(trailing_bang_has_no_chip)
{
    EXPECT(!Bangs::leading_bang("ladybird browser !w"sv).has_value());
}

TEST_CASE(find_bang)
{
    EXPECT_EQ(Bangs::find_bang("w"sv)->name, "Wikipedia"sv);
    EXPECT_EQ(Bangs::find_bang("W"sv)->name, "Wikipedia"sv);
    EXPECT(!Bangs::find_bang(""sv).has_value());
    EXPECT(!Bangs::find_bang("wikipediaa"sv).has_value());
    EXPECT(!Bangs::find_bang("!w"sv).has_value());

    EXPECT_EQ(Bangs::bang_for_word("!w"sv)->name, "Wikipedia"sv);
    EXPECT(!Bangs::bang_for_word("w"sv).has_value());
    EXPECT(!Bangs::bang_for_word("!"sv).has_value());
}

TEST_CASE(the_list_is_complete)
{
    EXPECT(Bangs::bang_count() > 13'000);
}

TEST_CASE(every_bang_resolves)
{
    for (size_t i = 0; i < Bangs::bang_count(); ++i) {
        auto bang = Bangs::bang_at(i);
        auto found = Bangs::find_bang(bang.trigger);
        VERIFY(found.has_value());
        EXPECT_EQ(found->trigger, bang.trigger);

        auto with_query = Bangs::resolve(ByteString::formatted("!{} xenon", bang.trigger));
        VERIFY(with_query.has_value());
        EXPECT(with_query->url.serialize().contains("xenon"sv));
        EXPECT_EQ(with_query->url.scheme(), "https"sv);

        auto front_page = Bangs::resolve(ByteString::formatted("!{}", bang.trigger));
        if (front_page.has_value())
            EXPECT(!front_page->url.serialize().contains("{{{s}}}"sv));
    }
}

TEST_CASE(placeholder_in_the_host)
{
    EXPECT(!Bangs::resolve("!dauser"sv).has_value());
    EXPECT_EQ(resolved_url("!dauser some-artist"sv), "https://some-artist.deviantart.com/"sv);
}
