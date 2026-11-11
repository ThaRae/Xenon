/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>
#include <UI/Extensions/PopupSize.h>

using namespace Ladybird::Extensions;

TEST_CASE(zero_size_report_is_unusable)
{
    EXPECT(!is_usable_popup_size({ 0, 0 }));
    EXPECT(!popup_size_for({ 0, 0 }, MAXIMUM_POPUP_SIZE).has_value());
}

TEST_CASE(negative_size_report_is_unusable)
{
    EXPECT(!is_usable_popup_size({ -10, 200 }));
    EXPECT(!popup_size_for({ -10, 200 }, MAXIMUM_POPUP_SIZE).has_value());
}

TEST_CASE(tiny_report_is_unusable)
{
    EXPECT(!is_usable_popup_size({ 10, 10 }));
    EXPECT(!popup_size_for({ 10, 10 }, MAXIMUM_POPUP_SIZE).has_value());
}

TEST_CASE(report_at_the_minimum_is_kept)
{
    auto size = popup_size_for(MINIMUM_POPUP_SIZE, MAXIMUM_POPUP_SIZE);
    VERIFY(size.has_value());
    EXPECT_EQ(*size, QSize(25, 25));
}

TEST_CASE(oversized_report_shrinks_to_the_maximum)
{
    auto size = popup_size_for({ 2000, 2000 }, MAXIMUM_POPUP_SIZE);
    VERIFY(size.has_value());
    EXPECT_EQ(*size, QSize(800, 600));
}

TEST_CASE(screen_limit_clamps_below_the_maximum)
{
    auto size = popup_size_for({ 400, 500 }, { 300, 400 });
    VERIFY(size.has_value());
    EXPECT_EQ(*size, QSize(300, 400));
}

TEST_CASE(limit_smaller_than_the_minimum_yields_the_minimum)
{
    auto size = popup_size_for({ 400, 500 }, { 10, 10 });
    VERIFY(size.has_value());
    EXPECT_EQ(*size, QSize(25, 25));
}

TEST_CASE(report_inside_all_limits_is_unchanged)
{
    auto size = popup_size_for({ 400, 500 }, MAXIMUM_POPUP_SIZE);
    VERIFY(size.has_value());
    EXPECT_EQ(*size, QSize(400, 500));
}

TEST_CASE(one_pixel_report_is_unusable)
{
    EXPECT(!is_usable_popup_size({ 1, 1 }));
}
