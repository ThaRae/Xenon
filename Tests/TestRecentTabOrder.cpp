/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <LibTest/TestCase.h>
#include <UI/RecentTabOrder.h>

using namespace Ladybird;

using Order = RecentTabOrder<char>;
using enum Order::Direction;

struct Window {
    void open(char tab, bool activate = true)
    {
        strip.append(tab);
        if (activate)
            this->activate(tab);
    }

    void activate(char tab)
    {
        order.update(strip);
        order.did_activate(tab);
    }

    void close(char tab, Optional<char> next_active = {})
    {
        strip.remove_first_matching([&](auto other) { return other == tab; });
        order.update(strip);
        if (next_active.has_value())
            activate(*next_active);
    }

    void move(char tab, size_t index)
    {
        strip.remove_first_matching([&](auto other) { return other == tab; });
        strip.insert(index, tab);
        order.update(strip);
    }

    char press(Order::Direction direction)
    {
        order.update(strip);
        auto tab = order.step(direction);
        VERIFY(tab.has_value());
        activate(*tab);
        return *tab;
    }

    void release()
    {
        order.update(strip);
        order.end_cycle();
    }

    ByteString recent_first() const { return ByteString { StringView { order.tabs().data(), order.tabs().size() } }; }

    Vector<char> strip;
    Order order;
};

static Window window_with(StringView tabs)
{
    Window window;
    for (auto tab : tabs)
        window.open(tab);
    return window;
}

TEST_CASE(tabs_are_ordered_by_when_they_were_last_activated)
{
    auto window = window_with("ABCD"sv);
    EXPECT_EQ(window.recent_first(), "DCBA"sv);

    window.activate('B');
    EXPECT_EQ(window.recent_first(), "BDCA"sv);
}

TEST_CASE(a_quick_press_flips_between_the_last_two_tabs)
{
    auto window = window_with("ABC"sv);

    EXPECT_EQ(window.press(LessRecent), 'B');
    window.release();
    EXPECT_EQ(window.recent_first(), "BCA"sv);

    EXPECT_EQ(window.press(LessRecent), 'C');
    window.release();
    EXPECT_EQ(window.recent_first(), "CBA"sv);
}

TEST_CASE(holding_ctrl_walks_back_through_recent_tabs_and_wraps_around)
{
    auto window = window_with("ABCD"sv);

    EXPECT_EQ(window.press(LessRecent), 'C');
    EXPECT_EQ(window.press(LessRecent), 'B');
    EXPECT_EQ(window.press(LessRecent), 'A');
    EXPECT_EQ(window.press(LessRecent), 'D');
    EXPECT_EQ(window.press(LessRecent), 'C');
    window.release();
    EXPECT_EQ(window.recent_first(), "CDBA"sv);
}

TEST_CASE(tabs_passed_on_the_way_keep_their_places)
{
    auto window = window_with("ABCD"sv);

    EXPECT_EQ(window.press(LessRecent), 'C');
    EXPECT_EQ(window.recent_first(), "DCBA"sv);
    EXPECT_EQ(window.press(LessRecent), 'B');
    window.release();
    EXPECT_EQ(window.recent_first(), "BDCA"sv);
}

TEST_CASE(shift_walks_the_other_way_from_the_least_recent_tab)
{
    auto window = window_with("ABCD"sv);

    EXPECT_EQ(window.press(MoreRecent), 'A');
    EXPECT_EQ(window.press(MoreRecent), 'B');
    EXPECT_EQ(window.press(LessRecent), 'A');
    window.release();
    EXPECT_EQ(window.recent_first(), "ADCB"sv);
}

TEST_CASE(a_single_tab_has_nowhere_to_go)
{
    auto window = window_with("A"sv);
    EXPECT(!window.order.step(LessRecent).has_value());
    EXPECT(!window.order.step(MoreRecent).has_value());
    EXPECT(!window.order.is_cycling());
}

TEST_CASE(tabs_opened_in_the_background_join_the_end)
{
    auto window = window_with("AB"sv);
    window.open('C', false);
    window.open('D', false);

    EXPECT_EQ(window.press(LessRecent), 'A');
    window.release();
    EXPECT_EQ(window.recent_first(), "ABCD"sv);

    EXPECT_EQ(window.press(MoreRecent), 'D');
    window.release();
    EXPECT_EQ(window.recent_first(), "DABC"sv);
}

TEST_CASE(closed_tabs_leave_the_order)
{
    auto window = window_with("ABCD"sv);

    window.close('B');
    EXPECT_EQ(window.recent_first(), "DCA"sv);

    window.close('D', 'A');
    EXPECT_EQ(window.recent_first(), "AC"sv);
    EXPECT_EQ(window.press(LessRecent), 'C');
}

TEST_CASE(moving_a_tab_in_the_strip_leaves_the_order_alone)
{
    auto window = window_with("ABCD"sv);

    window.move('D', 0);
    window.move('A', 3);
    EXPECT_EQ(window.strip, (Vector<char> { 'D', 'B', 'C', 'A' }));
    EXPECT_EQ(window.recent_first(), "DCBA"sv);
    EXPECT_EQ(window.press(LessRecent), 'C');
}

TEST_CASE(tabs_moved_between_windows)
{
    auto first = window_with("ABC"sv);
    auto second = window_with("XY"sv);

    first.close('C', 'B');
    second.open('C');
    EXPECT_EQ(first.recent_first(), "BA"sv);
    EXPECT_EQ(second.recent_first(), "CYX"sv);
    EXPECT_EQ(second.press(LessRecent), 'Y');
}

TEST_CASE(a_tab_opened_during_a_cycle_joins_it_at_the_end)
{
    auto window = window_with("ABC"sv);

    EXPECT_EQ(window.press(LessRecent), 'B');
    window.open('D', false);
    EXPECT_EQ(window.press(LessRecent), 'A');
    EXPECT_EQ(window.press(LessRecent), 'D');
    window.release();
    EXPECT_EQ(window.recent_first(), "DCBA"sv);
}

TEST_CASE(closing_a_tab_behind_the_cycle)
{
    auto window = window_with("ABCD"sv);

    EXPECT_EQ(window.press(LessRecent), 'C');
    EXPECT_EQ(window.press(LessRecent), 'B');
    window.close('D');
    EXPECT_EQ(window.press(LessRecent), 'A');
    window.release();
    EXPECT_EQ(window.recent_first(), "ACB"sv);
}

TEST_CASE(closing_the_tab_the_cycle_stands_on)
{
    auto window = window_with("ABCD"sv);
    EXPECT_EQ(window.press(LessRecent), 'C');
    window.close('C', 'D');
    EXPECT_EQ(window.press(LessRecent), 'B');
    window.release();
    EXPECT_EQ(window.recent_first(), "BDA"sv);

    window = window_with("ABCD"sv);
    EXPECT_EQ(window.press(LessRecent), 'C');
    EXPECT_EQ(window.press(LessRecent), 'B');
    window.close('B', 'A');
    EXPECT_EQ(window.press(MoreRecent), 'C');
    window.release();
    EXPECT_EQ(window.recent_first(), "CDA"sv);
}

TEST_CASE(a_press_always_lands_on_another_tab)
{
    auto window = window_with("ABCD"sv);
    EXPECT_EQ(window.press(LessRecent), 'C');
    window.close('C', 'B');
    EXPECT_EQ(window.press(LessRecent), 'A');

    window = window_with("ABC"sv);
    EXPECT_EQ(window.press(LessRecent), 'B');
    window.activate('A');
    EXPECT_EQ(window.press(LessRecent), 'C');
    window.release();
    EXPECT_EQ(window.recent_first(), "CBA"sv);
}

TEST_CASE(ending_without_a_cycle_changes_nothing)
{
    auto window = window_with("ABC"sv);
    window.release();
    EXPECT_EQ(window.recent_first(), "CBA"sv);
}
