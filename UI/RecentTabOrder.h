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

template<typename TabType>
class RecentTabOrder {
public:
    enum class Direction : u8 {
        LessRecent,
        MoreRecent,
    };

    ReadonlySpan<TabType> tabs() const { return m_tabs; }
    bool is_cycling() const { return m_cycle_position.has_value(); }

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

    void did_activate(TabType const& tab)
    {
        m_active_tab = tab;
        if (!is_cycling())
            move_to_front(tab);
    }

    Optional<TabType> step(Direction direction)
    {
        if (m_tabs.size() < 2)
            return {};

        if (!is_cycling()) {
            if (m_active_tab.has_value())
                move_to_front(*m_active_tab);
            m_cycle_position = 0;
        }

        for (size_t attempt = 0; attempt < m_tabs.size(); ++attempt) {
            advance(direction);
            if (!m_active_tab.has_value() || m_tabs[*m_cycle_position] != *m_active_tab)
                break;
        }
        return m_tabs[*m_cycle_position];
    }

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

    Vector<TabType> m_tabs;
    Optional<TabType> m_active_tab;
    Optional<size_t> m_cycle_position;
    bool m_cycle_tab_was_removed { false };
};

}
