/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/StandardPaths.h>
#include <LibCore/System.h>
#include <LibTest/TestCase.h>
#include <LibURL/Parser.h>
#include <LibWeb/Farbling/FarblingRNG.h>
#include <LibWebView/FarblingSeedStore.h>
#include <unistd.h>

using WebView::FarblingSeedStore;
using WebView::IsPrivate;

static URL::URL url(StringView serialized)
{
    auto parsed = URL::Parser::basic_parse(serialized);
    VERIFY(parsed.has_value());
    return *parsed;
}

static ByteString unique_path()
{
    static u64 counter = 0;
    return MUST(String::formatted("{}/xenon-farbling-{}-{}.json", Core::StandardPaths::tempfile_directory(), getpid(), ++counter)).to_byte_string();
}

TEST_CASE(two_sites_differ)
{
    auto path = unique_path();
    FarblingSeedStore store(path);
    auto a = store.effective_seed(url("https://site-a.test/"sv), IsPrivate::No);
    auto b = store.effective_seed(url("https://site-b.test/"sv), IsPrivate::No);
    EXPECT(a.enabled());
    EXPECT(b.enabled());
    EXPECT(a != b);
    (void)Core::System::unlink(path);
}

TEST_CASE(same_site_is_stable)
{
    auto path = unique_path();
    FarblingSeedStore store(path);
    auto first = store.effective_seed(url("https://site-a.test/"sv), IsPrivate::No);
    auto second = store.effective_seed(url("https://site-a.test/page"sv), IsPrivate::No);
    EXPECT_EQ(first, second);
    (void)Core::System::unlink(path);
}

TEST_CASE(a_new_store_rotates_the_effective_seed)
{
    auto path = unique_path();
    Web::Farbling::Seed first;
    {
        FarblingSeedStore store(path);
        first = store.effective_seed(url("https://site-a.test/"sv), IsPrivate::No);
    }
    FarblingSeedStore restarted(path);
    auto second = restarted.effective_seed(url("https://site-a.test/"sv), IsPrivate::No);
    EXPECT(first.enabled());
    EXPECT(second.enabled());
    EXPECT(first != second);
    EXPECT_EQ(first.level, second.level);
    (void)Core::System::unlink(path);
}

TEST_CASE(off_returns_no_seed)
{
    auto path = unique_path();
    FarblingSeedStore store(path);
    store.set_level(url("https://site-a.test/"sv), IsPrivate::No, Web::Farbling::Level::Off);
    auto a = store.effective_seed(url("https://site-a.test/"sv), IsPrivate::No);
    auto b = store.effective_seed(url("https://site-b.test/"sv), IsPrivate::No);
    EXPECT_EQ(a.level, Web::Farbling::Level::Off);
    EXPECT_EQ(a.high, 0u);
    EXPECT_EQ(a.low, 0u);
    EXPECT(b.enabled());
    (void)Core::System::unlink(path);
}

TEST_CASE(the_private_map_is_not_persisted)
{
    auto path = unique_path();
    {
        FarblingSeedStore store(path);
        (void)store.effective_seed(url("https://private.test/"sv), IsPrivate::Yes);
        store.set_level(url("https://private.test/"sv), IsPrivate::Yes, Web::Farbling::Level::Off);
        EXPECT(store.persisted_off_sites().is_empty());
    }
    FarblingSeedStore reloaded(path);
    EXPECT(reloaded.persisted_off_sites().is_empty());
    auto seed = reloaded.effective_seed(url("https://private.test/"sv), IsPrivate::No);
    EXPECT(seed.enabled());
    (void)Core::System::unlink(path);
}

TEST_CASE(opaque_origins_stay_off)
{
    auto path = unique_path();
    FarblingSeedStore store(path);
    auto seed = store.effective_seed(url("data:text/html,hi"sv), IsPrivate::No);
    EXPECT_EQ(seed.level, Web::Farbling::Level::Off);
    (void)Core::System::unlink(path);
}

TEST_CASE(prng_is_deterministic_for_the_same_seed_and_key)
{
    Web::Farbling::Seed seed { .level = Web::Farbling::Level::Standard, .high = 0x0123456789abcdefULL, .low = 0xfedcba9876543210ULL };
    Web::Farbling::RNG first(seed);
    Web::Farbling::RNG second(seed);
    EXPECT_EQ(first.integer("canvas"sv), second.integer("canvas"sv));
    EXPECT_EQ(first.integer("canvas"sv), first.integer("canvas"sv));
    EXPECT_NE(first.integer("canvas"sv), first.integer("webgl"sv));
}
