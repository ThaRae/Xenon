/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>
#include <Rune/Bridge/LatestValues.h>

TEST_CASE(only_the_first_value_since_the_last_take_asks_for_a_take)
{
    Xenon::LatestValues<int, int> values;
    EXPECT(values.set(1, 10));
    EXPECT(!values.set(1, 11));
    EXPECT(!values.set(1, 12));
    EXPECT(values.set(2, 20));
    EXPECT_EQ(values.take(1), 12);
    EXPECT_EQ(values.take(1), Optional<int> {});
    EXPECT(values.set(1, 13));
    EXPECT_EQ(values.take(1), 13);
    EXPECT_EQ(values.take(2), 20);
}
