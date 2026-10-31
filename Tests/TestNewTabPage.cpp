/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>
#include <LibURL/InternalURLs.h>
#include <LibURL/Parser.h>
#include <UI/NewTabPage.h>

using namespace Ladybird;

static URL::URL parse(StringView url)
{
    return URL::Parser::basic_parse(url).release_value();
}

TEST_CASE(xenons_new_tab_page_hides_its_url)
{
    EXPECT(NewTabPage::should_hide_url(URL::about_newtab(), URL::about_newtab()));
    EXPECT(NewTabPage::should_hide_url(URL::about_newtab(), parse("https://example.com/"sv)));
}

TEST_CASE(an_internal_page_hides_its_url_only_as_the_new_tab_page)
{
    EXPECT(NewTabPage::should_hide_url(URL::about_blank(), URL::about_blank()));
    EXPECT(!NewTabPage::should_hide_url(URL::about_blank(), URL::about_newtab()));
    EXPECT(!NewTabPage::should_hide_url(parse("about:settings"sv), URL::about_newtab()));
}

TEST_CASE(a_web_page_shows_its_url_even_as_the_new_tab_page)
{
    EXPECT(!NewTabPage::should_hide_url(parse("https://example.com/"sv), parse("https://example.com/"sv)));
}
