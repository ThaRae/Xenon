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

// The resident set the kernel reports for this process. PSS is what the browser measurements use, but a test process
// has no shared pages that matter, so RSS says the same thing here.
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

// Building the engine parses the lists into several times what the engine keeps. set_rules_from_bytes() has to give
// that memory back to the OS: a WebContent process builds its engine once and then lives on, so whatever stays
// resident is paid by every tab and every cross-site frame, which is what Xenon's issue #28 measured.
TEST_CASE(building_the_content_blocker_gives_back_what_building_it_used)
{
#if defined(AK_OS_LINUX)
    // Enough filters that building the engine allocates tens of MiB, most of which it does not keep: the engine keeps
    // the filters themselves, a fraction of what tokenizing and the flatbuffer builder used.
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

    // The engine for these lists keeps a few MiB. If building it is still resident beyond that, the pages it used were
    // only reset, not decommitted, which mimalloc's Linux default does not return to the system. Sanitizer builds use the
    // system allocator in place of mimalloc (AK/kmalloc.cpp), so there is nothing of it to measure there.
#    if !defined(HAS_ADDRESS_SANITIZER)
    EXPECT(*rss_after < *rss_before + 24 * 1024 * 1024);
#    endif

    // The engine the test measured is the one that matches requests, not a throwaway.
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
