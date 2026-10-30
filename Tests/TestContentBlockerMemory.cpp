/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <AK/Vector.h>
#include <LibTest/TestCase.h>
#include <LibURL/Parser.h>
#include <LibWeb/Loader/ContentBlocker.h>

#if defined(AK_OS_LINUX)
#    include <stdio.h>
#    include <unistd.h>
#endif

#if defined(AK_OS_LINUX)

static Optional<size_t> current_rss_bytes()
{
    auto* file = fopen("/proc/self/statm", "r");
    if (!file)
        return {};
    unsigned long size = 0;
    unsigned long resident = 0;
    auto fields = fscanf(file, "%lu %lu", &size, &resident);
    fclose(file);
    if (fields != 2)
        return {};
    return static_cast<size_t>(resident) * static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

#endif

TEST_CASE(building_the_content_blocker_gives_back_what_building_it_used)
{
#if defined(AK_OS_LINUX)
    StringBuilder rules;
    for (size_t index = 0; index < 60000; ++index)
        rules.appendff("||ads{}.example/{}/banner.js$script\n", index, index);
    for (size_t index = 0; index < 20000; ++index)
        rules.appendff("example.com##.ad-slot-{}\n", index);
    auto rules_string = MUST(rules.to_string());

    auto rss_before = current_rss_bytes();
    EXPECT(rss_before.has_value());
    if (!rss_before.has_value())
        return;

    auto& blocker = Web::ContentBlocker::the();
    MUST(blocker.set_rules_from_bytes(rules_string.bytes_as_string_view().bytes()));
    EXPECT(blocker.has_rules());

    auto rss_after = current_rss_bytes();
    EXPECT(rss_after.has_value());
    if (!rss_after.has_value())
        return;

#    if !defined(HAS_ADDRESS_SANITIZER)
    EXPECT(*rss_after < *rss_before + 24 * 1024 * 1024);
#    endif

    auto source_url = URL::Parser::basic_parse("https://example.com/"sv);
    auto blocked_url = URL::Parser::basic_parse("https://ads7.example/7/banner.js"sv);
    auto allowed_url = URL::Parser::basic_parse("https://example.com/index.html"sv);
    EXPECT(source_url.has_value());
    EXPECT(blocked_url.has_value());
    EXPECT(allowed_url.has_value());
    if (source_url.has_value() && blocked_url.has_value() && allowed_url.has_value()) {
        EXPECT(blocker.is_filtered(*blocked_url, *source_url, Web::ContentBlocker::ResourceType::Script));
        EXPECT(!blocker.is_filtered(*allowed_url, *source_url, Web::ContentBlocker::ResourceType::Script));
    }
#endif
}
