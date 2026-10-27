/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/Vector.h>

namespace Ladybird {

// The order a window's tabs were last used in, most recent first, which Ctrl+Tab walks through.
//
// Holding Ctrl and pressing Tab walks a cycle: each press steps one tab further back through the order, and Ctrl+Shift+Tab
// steps the other way, so its first press goes to the least recently used tab. The order holds still while a cycle is
// under way, since walking past a tab isn't using it. When the cycle ends, only the tab it ended on moves to the front,
// so a quick Ctrl+Tab flips between the last two tabs.
template<typename TabType>
class RecentTabOrder {
public:
    enum class Direction : u8 {
        // Ctrl+Tab: toward the tabs used longer ago.
        LessRecent,
        // Ctrl+Shift+Tab: toward the tabs used more recently, wrapping around to the least recently used one.
        MoreRecent,
    };

    ReadonlySpan<TabType> tabs() const { return m_tabs; }
    bool is_cycling() const { return m_cycle_position.has_value(); }

    // Brings the order in line with the window's tabs, wherever they sit in its tab strip. Tabs it no longer has drop
    // out. New ones join at the end, in the order given, since they haven't been used yet.
    void update(ReadonlySpan<TabType> window_tabs)
    {
        for (size_t index = m_tabs.size(); index-- > 0;) {
            if (!window_tabs.contains_slow(m_tabs[index]))
                remove_at(index);
        }
        for (auto const& tab : window_tabs) {
            if (!m_tabs.contains_slow(tab))
                m_tabs.append(tab);
        }
        if (m_active_tab.has_value() && !m_tabs.contains_slow(*m_active_tab))
            m_active_tab.clear();
    }

    // The tab became the window's active tab. That makes it the most recently used, unless a cycle is passing through.
    void did_activate(TabType const& tab)
    {
        m_active_tab = tab;
        if (!is_cycling())
            move_to_front(tab);
    }

    // Takes one step from the active tab, starting a cycle if none is under way, and returns the tab to activate.
    Optional<TabType> step(Direction direction)
    {
        if (m_tabs.size() < 2)
            return {};

        if (!is_cycling()) {
            if (m_active_tab.has_value())
                move_to_front(*m_active_tab);
            m_cycle_position = 0;
        }

        // The step always lands on another tab, even when something other than the cycle activated a tab under way.
        for (size_t attempt = 0; attempt < m_tabs.size(); ++attempt) {
            advance(direction);
            if (!m_active_tab.has_value() || m_tabs[*m_cycle_position] != *m_active_tab)
                break;
        }
        return m_tabs[*m_cycle_position];
    }

    // The tab the cycle ended on becomes the most recently used, and the tabs it passed keep their places.
    void end_cycle()
    {
        if (!is_cycling())
            return;
        m_cycle_position.clear();
        m_cycle_tab_was_removed = false;
        if (m_active_tab.has_value())
            move_to_front(*m_active_tab);
    }

private:
    void move_to_front(TabType const& tab)
    {
        m_tabs.remove_first_matching([&](auto const& other) { return other == tab; });
        m_tabs.prepend(tab);
    }

    void remove_at(size_t index)
    {
        m_tabs.remove(index);
        if (!is_cycling())
            return;

        // If the tab the cycle stands on goes, the cycle stands between its neighbors, so the next step reaches one.
        if (index < *m_cycle_position)
            --*m_cycle_position;
        else if (index == *m_cycle_position)
            m_cycle_tab_was_removed = true;
    }

    void advance(Direction direction)
    {
        auto count = m_tabs.size();
        auto position = *m_cycle_position;
        if (direction == Direction::LessRecent)
            position = m_cycle_tab_was_removed ? position : position + 1;
        else
            position = position + count - 1;
        m_cycle_position = position % count;
        m_cycle_tab_was_removed = false;
    }

    // Most recently used first.
    Vector<TabType> m_tabs;
    Optional<TabType> m_active_tab;
    // Where the cycle under way stands in the order, which holds still until it ends.
    Optional<size_t> m_cycle_position;
    bool m_cycle_tab_was_removed { false };
};

}
