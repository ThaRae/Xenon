/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Rune/Bridge/SplitViews.h>

namespace Xenon {

SplitSide opposite(SplitSide side)
{
    return side == SplitSide::Left ? SplitSide::Right : SplitSide::Left;
}

SplitViews::SplitViews(BrowserModel& model, Observer& observer)
    : m_model(model)
    , m_observer(observer)
{
}

SplitViews::Split const* SplitViews::split_for(ViewId view) const
{
    for (auto const& split : m_splits) {
        if (split.left == view || split.right == view)
            return &split;
    }
    return nullptr;
}

SplitViews::Split* SplitViews::split_for(ViewId view)
{
    return const_cast<Split*>(const_cast<SplitViews const&>(*this).split_for(view));
}

Optional<size_t> SplitViews::index_of(ViewId view) const
{
    auto const* window = m_model.window_of(view);
    if (!window)
        return {};
    return window->index_of(view);
}

Optional<ViewId> SplitViews::partner(ViewId view) const
{
    auto const* split = split_for(view);
    if (!split)
        return {};
    return split->left == view ? split->right : split->left;
}

Optional<SplitSide> SplitViews::side(ViewId view) const
{
    auto const* split = split_for(view);
    if (!split)
        return {};
    return split->left == view ? SplitSide::Left : SplitSide::Right;
}

Optional<ViewId> SplitViews::tab_at(ViewId view, SplitSide side) const
{
    auto const* split = split_for(view);
    if (!split)
        return {};
    return side == SplitSide::Left ? split->left : split->right;
}

bool SplitViews::is_shown(ViewId view) const
{
    auto const* window = m_model.window_of(view);
    if (!window || !window->active_tab.has_value())
        return false;
    return *window->active_tab == view || partner(*window->active_tab) == view;
}

void SplitViews::add(ViewId left, ViewId right)
{
    if (left == right || split_for(left) || split_for(right))
        return;
    m_splits.append({ left, right });
    m_observer.splits_changed();
}

Optional<ViewId> SplitViews::separate(ViewId view)
{
    auto partner_view = partner(view);
    m_splits.remove_all_matching([&](auto const& split) {
        return split.left == view || split.right == view;
    });
    if (!partner_view.has_value())
        return {};
    m_observer.splits_changed();
    return partner_view;
}

void SplitViews::place_beside(ViewId anchor, ViewId view, SplitSide side)
{
    auto const* anchor_tab = m_model.tab(anchor);
    auto const* tab = m_model.tab(view);
    if (!anchor_tab || !tab || anchor_tab->window != tab->window)
        return;

    if (tab->pinned != anchor_tab->pinned)
        m_model.set_tab_pinned(view, anchor_tab->pinned);

    auto anchor_index = index_of(anchor);
    auto index = index_of(view);
    if (!anchor_index.has_value() || !index.has_value())
        return;

    auto destination = side == SplitSide::Right ? *anchor_index + 1 : *anchor_index;
    if (*index < *anchor_index)
        --destination;
    if (*index != destination)
        move_tab(view, destination);
}

void SplitViews::create(ViewId anchor, ViewId other, SplitSide other_side, ViewId tab_to_activate)
{
    auto const* anchor_window = m_model.window_of(anchor);
    if (anchor == other || !anchor_window || anchor_window != m_model.window_of(other))
        return;

    (void)separate(anchor);
    (void)separate(other);

    place_beside(anchor, other, other_side);
    if (other_side == SplitSide::Right)
        add(anchor, other);
    else
        add(other, anchor);

    m_model.activate_tab(tab_to_activate);
}

ViewId SplitViews::open_new_tab_beside(ViewId view, SplitSide new_tab_side, URL::URL const& new_tab_url, Optional<ViewId> new_view)
{
    auto const* window = m_model.window_of(view);
    auto index = index_of(view);
    if (!window || !index.has_value())
        return 0;

    auto opened = m_model.open_tab(window->id, { .view = new_view, .url = new_tab_url, .index = *index + 1, .activate = Web::HTML::ActivateTab::No });
    create(view, opened, new_tab_side, opened);
    return opened;
}

void SplitViews::reverse(ViewId view)
{
    auto left = tab_at(view, SplitSide::Left);
    auto right = tab_at(view, SplitSide::Right);
    if (!left.has_value() || !right.has_value())
        return;
    auto left_index = index_of(*left);
    if (!left_index.has_value())
        return;
    move_tab(*right, *left_index);
}

void SplitViews::move_tab(ViewId view, size_t index)
{
    auto const* tab = m_model.tab(view);
    if (!tab)
        return;
    m_model.move_tab(view, tab->window, index);
    keep_together(view);
}

void SplitViews::update_order(ViewId view)
{
    auto* split = split_for(view);
    if (!split)
        return;
    auto left_index = index_of(split->left);
    auto right_index = index_of(split->right);
    if (!left_index.has_value() || !right_index.has_value() || *left_index <= *right_index)
        return;
    swap(split->left, split->right);
    m_observer.splits_changed();
}

void SplitViews::keep_together(ViewId view)
{
    auto const* window = m_model.window_of(view);
    auto index = index_of(view);
    if (!window || !index.has_value())
        return;

    if (auto partner_view = partner(view); partner_view.has_value()) {
        auto partner_index = index_of(*partner_view);
        if (!partner_index.has_value() || m_model.window_of(*partner_view) != window)
            return;
        if (max(*index, *partner_index) - min(*index, *partner_index) == 1)
            update_order(view);
        else
            place_beside(view, *partner_view, *side(*partner_view));
        return;
    }

    if (*index == 0 || *index + 1 >= window->tabs.size())
        return;
    if (partner(window->tabs[*index - 1]) == window->tabs[*index + 1])
        move_tab(view, *index + 1);
}

bool SplitViews::can_drop_beside_active_tab(ViewId dragged) const
{
    auto const* window = m_model.window_of(dragged);
    if (!window || !window->active_tab.has_value())
        return false;
    if (partner(*window->active_tab).has_value())
        return is_shown(dragged);
    return true;
}

void SplitViews::drop_tab(ViewId dragged, SplitSide side, URL::URL const& new_tab_url)
{
    if (!can_drop_beside_active_tab(dragged))
        return;
    auto active = *m_model.window_of(dragged)->active_tab;

    if (is_shown(dragged) && partner(dragged).has_value()) {
        if (this->side(dragged) != side)
            reverse(dragged);
        return;
    }

    if (dragged == active) {
        open_new_tab_beside(dragged, opposite(side), new_tab_url);
        return;
    }

    create(active, dragged, side, dragged);
}

bool SplitViews::validate()
{
    auto changed = false;
    m_splits.remove_all_matching([&](auto const& split) {
        auto const* left = m_model.tab(split.left);
        auto const* right = m_model.tab(split.right);
        auto valid = left && right && left->window == right->window && left->pinned == right->pinned;
        if (valid) {
            auto left_index = index_of(split.left);
            auto right_index = index_of(split.right);
            valid = left_index.has_value() && right_index.has_value() && max(*left_index, *right_index) - min(*left_index, *right_index) == 1;
        }
        changed |= !valid;
        return !valid;
    });

    for (auto& split : m_splits) {
        if (index_of(split.left).value() > index_of(split.right).value()) {
            swap(split.left, split.right);
            changed = true;
        }
    }

    if (changed)
        m_observer.splits_changed();
    return changed;
}

}
