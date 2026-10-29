/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>
#include <UI/MemoryPressure.h>

using namespace Ladybird::MemoryPressure;

TEST_CASE(meminfo_parses_total_and_available)
{
    auto sample = parse_meminfo(
        "MemTotal:       16384000 kB\n"
        "MemFree:         1000000 kB\n"
        "MemAvailable:    8192000 kB\n"sv);
    EXPECT(sample.total_bytes.has_value());
    EXPECT(sample.available_bytes.has_value());
    EXPECT_EQ(*sample.total_bytes, 16384000ull * 1024);
    EXPECT_EQ(*sample.available_bytes, 8192000ull * 1024);
}

TEST_CASE(psi_parses_some_avg10)
{
    auto psi = parse_psi_some_avg10("some avg10=12.34 avg60=1.00 avg300=0.10 total=99\nfull avg10=0.00 avg60=0.00 avg300=0.00 total=0\n"sv);
    EXPECT(psi.has_value());
    EXPECT(*psi > 12.3 && *psi < 12.4);
}

TEST_CASE(empty_sample_is_normal)
{
    EXPECT_EQ(level_for({}), Level::Normal);
    EXPECT_EQ(tabs_to_hibernate(Level::Normal, 4), 0u);
}

TEST_CASE(available_fraction_picks_low_and_critical)
{
    Sample low { .available_bytes = 15ull * 1024 * 1024, .total_bytes = 100ull * 1024 * 1024 };
    EXPECT_EQ(level_for(low), Level::Low);
    EXPECT_EQ(tabs_to_hibernate(Level::Low, 4), 1u);
    EXPECT_EQ(tabs_to_hibernate(Level::Low, 0), 0u);

    Sample critical { .available_bytes = 5ull * 1024 * 1024, .total_bytes = 100ull * 1024 * 1024 };
    EXPECT_EQ(level_for(critical), Level::Critical);
    EXPECT_EQ(tabs_to_hibernate(Level::Critical, 4), 4u);
}

TEST_CASE(psi_picks_low_and_critical)
{
    EXPECT_EQ(level_for({ .psi_some_avg10 = 5.0 }), Level::Low);
    EXPECT_EQ(level_for({ .psi_some_avg10 = 15.0 }), Level::Critical);
    EXPECT_EQ(level_for({ .psi_some_avg10 = 4.9 }), Level::Normal);
}

TEST_CASE(critical_wins_when_both_are_set)
{
    Sample sample {
        .psi_some_avg10 = 20,
        .available_bytes = 50ull * 1024 * 1024,
        .total_bytes = 100ull * 1024 * 1024,
    };
    EXPECT_EQ(level_for(sample), Level::Critical);
}
