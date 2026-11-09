/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibURL/URL.h>
#include <Rune/Bridge/BrowserModel.h>

namespace Xenon {

enum class SplitSide : u8 {
    Left,
    Right,
};

SplitSide opposite(SplitSide);

class SplitViews {
    AK_MAKE_NONCOPYABLE(SplitViews);
    AK_MAKE_NONMOVABLE(SplitViews);

public:
    AK_ALLOC_WITH_KMALLOC;

    struct Split {
        ViewId left { 0 };
        ViewId right { 0 };
    };

    class Observer {
    public:
        virtual ~Observer() = default;
        virtual void splits_changed() = 0;
    };

    SplitViews(BrowserModel&, Observer&);

    Vector<Split> const& splits() const { return m_splits; }
    Optional<ViewId> partner(ViewId) const;
    Optional<SplitSide> side(ViewId) const;
    Optional<ViewId> tab_at(ViewId, SplitSide) const;
    bool is_shown(ViewId) const;

    void create(ViewId anchor, ViewId other, SplitSide other_side, ViewId tab_to_activate);
    ViewId open_new_tab_beside(ViewId, SplitSide new_tab_side, URL::URL const& new_tab_url, Optional<ViewId> new_view = {});
    void reverse(ViewId);
    Optional<ViewId> separate(ViewId);
    void move_tab(ViewId, size_t index);
    void keep_together(ViewId moved);
    bool can_drop_beside_active_tab(ViewId dragged) const;
    void drop_tab(ViewId dragged, SplitSide, URL::URL const& new_tab_url);
    bool validate();

private:
    Split const* split_for(ViewId) const;
    Split* split_for(ViewId);
    Optional<size_t> index_of(ViewId) const;
    void add(ViewId left, ViewId right);
    void place_beside(ViewId anchor, ViewId, SplitSide);
    void update_order(ViewId);

    BrowserModel& m_model;
    Observer& m_observer;
    Vector<Split> m_splits;
};

}
