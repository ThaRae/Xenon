/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/HashMap.h>
#include <AK/StdLibExtras.h>
#include <AK/TemporaryChange.h>
#include <Rune/Bridge/BrowserModel.h>

namespace Xenon {

static constexpr Array<StringView, tab_group_color_count> s_tab_group_color_names {
    "Rose"sv,
    "Sky"sv,
    "Amber"sv,
    "Jade"sv,
    "Orchid"sv,
    "Ember"sv,
    "Iris"sv,
    "Moss"sv,
    "Slate"sv,
};

StringView tab_group_color_name(TabGroupColor color)
{
    return s_tab_group_color_names[to_underlying(color)];
}

Optional<TabGroupColor> tab_group_color_from_name(StringView name)
{
    for (size_t index = 0; index < tab_group_color_count; ++index) {
        if (name.equals_ignoring_ascii_case(s_tab_group_color_names[index]))
            return static_cast<TabGroupColor>(index);
    }
    return {};
}

static TabGroup const* group_in(BrowserModel::Window const& window, TabGroupId id)
{
    for (auto const& group : window.groups) {
        if (group.id == id)
            return &group;
    }
    return nullptr;
}

void BrowserModel::set_tab_group_hooks(TabGroupHooks hooks)
{
    m_tab_group_hooks = move(hooks);
}

TabGroup* BrowserModel::find_tab_group(Window& window, TabGroupId id)
{
    return const_cast<TabGroup*>(group_in(window, id));
}

BrowserModel::Window* BrowserModel::find_window_of_tab_group(TabGroupId id)
{
    for (auto& window : m_windows) {
        if (group_in(*window, id))
            return window.ptr();
    }
    return nullptr;
}

TabGroup const* BrowserModel::tab_group(TabGroupId id) const
{
    auto const* window = window_of_tab_group(id);
    return window ? group_in(*window, id) : nullptr;
}

BrowserModel::Window const* BrowserModel::window_of_tab_group(TabGroupId id) const
{
    return const_cast<BrowserModel&>(*this).find_window_of_tab_group(id);
}

Optional<TabGroupId> BrowserModel::group_at(Window const& window, size_t index) const
{
    if (index >= window.tabs.size())
        return {};
    auto const* member = tab(window.tabs[index]);
    if (!member)
        return {};
    return member->group;
}

bool BrowserModel::is_hidden_at(Window const& window, size_t index) const
{
    auto group_id = group_at(window, index);
    if (!group_id.has_value())
        return false;
    auto const* group = group_in(window, *group_id);
    return group && group->collapsed;
}

bool BrowserModel::is_tab_hidden(ViewId view) const
{
    auto const* window = window_of(view);
    if (!window)
        return false;
    auto index = window->index_of(view);
    return index.has_value() && is_hidden_at(*window, *index);
}

size_t BrowserModel::group_tab_count(Window const& window, TabGroupId id) const
{
    size_t count = 0;
    for (size_t index = 0; index < window.tabs.size(); ++index) {
        if (group_at(window, index) == id)
            ++count;
    }
    return count;
}

TabGroupColor BrowserModel::next_tab_group_color(Window const& window) const
{
    Array<size_t, tab_group_color_count> uses {};
    for (auto const& group : window.groups) {
        if (!group.tabs.is_empty())
            ++uses[to_underlying(group.color)];
    }

    size_t color = 0;
    for (size_t index = 1; index < tab_group_color_count; ++index) {
        if (uses[index] < uses[color])
            color = index;
    }
    return static_cast<TabGroupColor>(color);
}

TabGroup& BrowserModel::create_tab_group(Window& window)
{
    TabGroup group;
    group.id = m_next_tab_group_id++;
    group.color = next_tab_group_color(window);
    window.groups.append(move(group));
    return window.groups.last();
}

void BrowserModel::move_tab_quietly(Window& window, ViewId view, size_t index)
{
    auto* moved = find_tab(view);
    if (!moved || window.index_of(view) == index)
        return;
    TemporaryChange moving { m_is_moving_tabs_for_groups, true };
    reorder_tab(window, view, index);
    m_observer.tab_moved(*moved, window.id, index);
    sync_session_tab_order(window);
}

TabGroupId BrowserModel::add_tab_to_new_group(ViewId view)
{
    auto* grouped = find_tab(view);
    if (!grouped)
        return 0;
    if (grouped->pinned)
        set_tab_pinned(view, false);
    remove_tab_from_group(view);

    auto* window = find_window(grouped->window);
    VERIFY(window);
    auto id = create_tab_group(*window).id;
    grouped->group = id;
    update_tab_groups(*window);
    if (m_tab_group_hooks.edit_requested)
        m_tab_group_hooks.edit_requested(*window, id);
    return id;
}

void BrowserModel::add_tab_to_group(ViewId view, TabGroupId id)
{
    auto* grouped = find_tab(view);
    if (!grouped || grouped->group == id)
        return;
    auto* window = find_window(grouped->window);
    VERIFY(window);
    if (!find_tab_group(*window, id))
        return;
    if (grouped->pinned)
        set_tab_pinned(view, false);
    remove_tab_from_group(view);

    auto const* group = find_tab_group(*window, id);
    if (!group || group->tabs.is_empty())
        return;
    auto first = group->first;
    auto last = group->last;
    auto index = window->index_of(view).value();
    move_tab_quietly(*window, view, index < first ? last : last + 1);
    grouped->group = id;
    update_tab_groups(*window);
}

void BrowserModel::remove_tab_from_group(ViewId view)
{
    auto* grouped = find_tab(view);
    if (!grouped || !grouped->group.has_value())
        return;
    auto* window = find_window(grouped->window);
    VERIFY(window);

    auto const* group = find_tab_group(*window, *grouped->group);
    Optional<size_t> first;
    Optional<size_t> last;
    if (group && !group->tabs.is_empty()) {
        first = group->first;
        last = group->last;
    }
    grouped->group.clear();

    if (auto index = window->index_of(view); index.has_value() && first.has_value() && *index > *first && *index < *last)
        move_tab_quietly(*window, view, *last);
    update_tab_groups(*window);
}

void BrowserModel::set_tab_group_title(TabGroupId id, String title)
{
    auto* window = find_window_of_tab_group(id);
    if (!window)
        return;
    auto* group = find_tab_group(*window, id);
    if (group->title == title)
        return;
    group->title = move(title);
    update_tab_groups(*window);
}

void BrowserModel::set_tab_group_color(TabGroupId id, TabGroupColor color)
{
    auto* window = find_window_of_tab_group(id);
    if (!window)
        return;
    auto* group = find_tab_group(*window, id);
    if (group->color == color)
        return;
    group->color = color;
    update_tab_groups(*window);
}

void BrowserModel::open_new_tab_page(Window& window)
{
    auto url = m_tab_group_hooks.new_tab_page_url ? m_tab_group_hooks.new_tab_page_url() : URL::URL {};
    open_tab(window.id, { .url = move(url), .activate = Web::HTML::ActivateTab::Yes });
}

void BrowserModel::set_tab_group_collapsed(TabGroupId id, bool collapsed)
{
    auto* window = find_window_of_tab_group(id);
    if (!window)
        return;
    auto const* group = find_tab_group(*window, id);
    if (group->collapsed == collapsed || group->tabs.is_empty())
        return;

    if (collapsed) {
        auto first = group->first;
        auto last = group->last;
        auto active_index = window->active_tab.has_value() ? window->index_of(*window->active_tab) : Optional<size_t> {};
        if (active_index.has_value() && *active_index >= first && *active_index <= last) {
            Optional<size_t> replacement;
            for (auto index = last + 1; index < window->tabs.size() && !replacement.has_value(); ++index) {
                if (!is_hidden_at(*window, index))
                    replacement = index;
            }
            for (auto index = first; index > 0 && !replacement.has_value(); --index) {
                if (!is_hidden_at(*window, index - 1))
                    replacement = index - 1;
            }

            if (replacement.has_value())
                set_active_tab(*window, window->tabs[*replacement]);
            else
                open_new_tab_page(*window);
        }
    }

    if (auto* changed = find_tab_group(*window, id)) {
        changed->collapsed = collapsed;
        update_tab_groups(*window);
    }
}

void BrowserModel::ungroup_tabs(TabGroupId id)
{
    auto* window = find_window_of_tab_group(id);
    if (!window)
        return;
    for (auto view : window->tabs) {
        if (auto* member = find_tab(view); member && member->group == id)
            member->group.clear();
    }
    update_tab_groups(*window);
}

Vector<ViewId> BrowserModel::prepare_to_close_tab_group(TabGroupId id)
{
    auto* window = find_window_of_tab_group(id);
    if (!window)
        return {};
    auto const* group = find_tab_group(*window, id);
    auto views = group->tabs;
    if (views.is_empty())
        return {};

    auto active_index = window->active_tab.has_value() ? window->index_of(*window->active_tab) : Optional<size_t> {};
    if (views.size() == window->tabs.size())
        open_new_tab_page(*window);
    else if (active_index.has_value() && *active_index >= group->first && *active_index <= group->last)
        set_tab_group_collapsed(id, true);
    return views;
}

Optional<ViewId> BrowserModel::open_new_tab_in_group(TabGroupId id, Optional<ViewId> view)
{
    auto* window = find_window_of_tab_group(id);
    if (!window)
        return {};
    auto const* group = find_tab_group(*window, id);
    if (group->tabs.is_empty())
        return {};

    auto url = m_tab_group_hooks.new_tab_page_url ? m_tab_group_hooks.new_tab_page_url() : URL::URL {};
    auto opened = open_tab(window->id, { .view = view, .url = move(url), .index = group->last + 1, .activate = Web::HTML::ActivateTab::Yes });
    if (auto* member = find_tab(opened); member && find_tab_group(*window, id)) {
        member->group = id;
        update_tab_groups(*window);
    }
    return opened;
}

Optional<WindowId> BrowserModel::move_tab_group_to_new_window(TabGroupId id, Optional<WindowId> new_window_id)
{
    auto* source = find_window_of_tab_group(id);
    if (!source)
        return {};
    auto const* group = find_tab_group(*source, id);
    if (group->tabs.is_empty() || group->tabs.size() == source->tabs.size())
        return {};

    auto views = group->tabs;
    auto title = group->title;
    auto color = group->color;

    auto target_id = open_window(source->is_private, new_window_id);
    Optional<TabGroupId> target_group;
    for (auto view : views) {
        auto* target = find_window(target_id);
        if (!find_tab(view) || !target)
            continue;
        move_tab(view, target_id, target->tabs.size());
        if (!target_group.has_value()) {
            target_group = create_tab_group(*target).id;
            find_tab(view)->group = target_group;
            update_tab_groups(*target);
        } else {
            add_tab_to_group(view, *target_group);
        }
    }

    if (auto* target = find_window(target_id); target && target_group.has_value()) {
        if (auto* moved_group = find_tab_group(*target, *target_group)) {
            moved_group->title = move(title);
            moved_group->color = color;
            update_tab_groups(*target);
        }
    }
    activate_window(target_id);
    return target_id;
}

void BrowserModel::drop_tab(ViewId view, size_t index, Optional<TabGroupId> group)
{
    auto* dropped = find_tab(view);
    if (!dropped)
        return;
    move_tab(view, dropped->window, index);

    dropped = find_tab(view);
    auto* window = dropped ? find_window(dropped->window) : nullptr;
    if (!window)
        return;
    auto dropped_index = window->index_of(view);
    if (!dropped_index.has_value() || dropped->pinned || dropped->group == group)
        return;

    if (!group.has_value())
        dropped->group.clear();
    else if (group_at(*window, *dropped_index - 1) == group || group_at(*window, *dropped_index + 1) == group)
        dropped->group = group;
    update_tab_groups(*window);
}

void BrowserModel::tab_group_did_open_window()
{
    if (m_tab_group_hooks.window_opened)
        m_tab_group_hooks.window_opened();
}

void BrowserModel::tab_group_did_insert_tab(Window& window, size_t index)
{
    if (!m_is_moving_tabs_for_groups && index < window.tabs.size()) {
        auto* inserted = find_tab(window.tabs[index]);
        if (inserted && !inserted->pinned) {
            auto left = group_at(window, index - 1);
            if (left.has_value() && left == group_at(window, index + 1))
                inserted->group = left;
        }
    }
    update_tab_groups(window);
}

void BrowserModel::tab_group_will_remove_tab(Window& window, ViewId view)
{
    auto index = window.index_of(view);
    if (!index.has_value())
        return;

    auto another_tab_shows = false;
    for (size_t other = 0; other < window.tabs.size() && !another_tab_shows; ++other)
        another_tab_shows = other != *index && !is_hidden_at(window, other);

    if (!another_tab_shows && window.tabs.size() > 1) {
        Optional<TabGroupId> nearest;
        for (auto other = *index + 1; other < window.tabs.size() && !nearest.has_value(); ++other)
            nearest = group_at(window, other);
        for (auto other = *index; other > 0 && !nearest.has_value(); --other)
            nearest = group_at(window, other - 1);
        if (nearest.has_value()) {
            if (auto* group = find_tab_group(window, *nearest))
                group->collapsed = false;
        }
    }

    if (auto* removed = find_tab(view))
        removed->group.clear();
}

void BrowserModel::tab_group_did_move_tab(Window& window, size_t index)
{
    if (m_is_moving_tabs_for_groups)
        return;

    if (index < window.tabs.size()) {
        auto* moved = find_tab(window.tabs[index]);
        if (moved && !moved->pinned) {
            auto left = group_at(window, index - 1);
            auto right = group_at(window, index + 1);
            auto group = moved->group;
            if (left.has_value() && left == right)
                moved->group = left;
            else if (group.has_value() && group != left && group != right && group_tab_count(window, *group) > 1)
                moved->group.clear();
        }
    }
    update_tab_groups(window);
}

void BrowserModel::tab_group_did_activate_tab(Window& window, Tab& activated)
{
    if (!activated.group.has_value())
        return;
    if (auto const* group = find_tab_group(window, *activated.group); group && group->collapsed)
        set_tab_group_collapsed(group->id, false);
}

void BrowserModel::tab_group_will_pin_tab(Window&, Tab& pinned)
{
    pinned.group.clear();
}

void BrowserModel::add_opened_tab_to_group(ViewId opener, ViewId opened)
{
    auto const* opener_tab = tab(opener);
    auto const* opened_tab = tab(opened);
    if (!opener_tab || !opener_tab->group.has_value() || !opened_tab || opened_tab->pinned || opened_tab->group.has_value())
        return;
    if (opener_tab->window != opened_tab->window)
        return;
    add_tab_to_group(opened, *opener_tab->group);
}

void BrowserModel::restore_tab_groups(Window& window)
{
    if (!window.session_window_id.has_value() || !m_tab_group_hooks.saved_groups)
        return;
    auto saved_groups = m_tab_group_hooks.saved_groups(*window.session_window_id);
    if (saved_groups.is_empty())
        return;

    HashMap<WebView::SessionTabId, ViewId> views_by_session_tab;
    for (auto view : window.tabs) {
        if (auto const* member = tab(view); member && member->session_tab_id.has_value())
            views_by_session_tab.set(*member->session_tab_id, view);
    }

    for (auto& saved_group : saved_groups) {
        Optional<TabGroupId> group_id;
        for (auto session_tab : saved_group.tabs) {
            auto view = views_by_session_tab.get(session_tab);
            auto* member = view.has_value() ? find_tab(*view) : nullptr;
            if (!member || member->pinned || member->group.has_value())
                continue;
            if (!group_id.has_value()) {
                auto& group = create_tab_group(window);
                group.title = saved_group.title;
                group.color = saved_group.color;
                group.collapsed = saved_group.collapsed;
                group_id = group.id;
            }
            member->group = group_id;
        }
    }
    update_tab_groups(window, true);
}

void BrowserModel::keep_tab_groups_together(Window& window)
{
    auto count = window.tabs.size();
    Vector<Optional<TabGroupId>> groups_at_indices;
    groups_at_indices.ensure_capacity(count);
    for (auto view : window.tabs) {
        auto const* member = tab(view);
        groups_at_indices.unchecked_append(member && !member->pinned ? member->group : Optional<TabGroupId> {});
    }

    struct Run {
        size_t first { 0 };
        size_t length { 0 };
    };
    HashMap<TabGroupId, Run> longest_runs;
    for (size_t index = 0; index < count;) {
        auto group = groups_at_indices[index];
        auto end = index + 1;
        while (end < count && group.has_value() && groups_at_indices[end] == group)
            ++end;
        if (group.has_value()) {
            auto run = longest_runs.get(*group).value_or(Run {});
            if (end - index > run.length)
                longest_runs.set(*group, { index, end - index });
        }
        index = end;
    }

    Vector<TabGroup> groups;
    for (size_t index = 0; index < count; ++index) {
        auto* member = find_tab(window.tabs[index]);
        if (!member)
            continue;
        auto group = groups_at_indices[index];
        auto run = group.has_value() ? longest_runs.get(*group).value_or(Run {}) : Run {};
        if (!group.has_value() || index < run.first || index >= run.first + run.length) {
            member->group.clear();
            continue;
        }
        member->group = group;
        if (!groups.is_empty() && groups.last().id == *group) {
            groups.last().last = index;
            groups.last().tabs.append(member->view);
            continue;
        }
        TabGroup span;
        if (auto const* existing = group_in(window, *group)) {
            span.title = existing->title;
            span.color = existing->color;
            span.collapsed = existing->collapsed;
        }
        span.id = *group;
        span.first = index;
        span.last = index;
        span.tabs.append(member->view);
        groups.append(move(span));
    }
    window.groups = move(groups);
}

void BrowserModel::update_tab_groups(Window& window, bool force_report)
{
    keep_tab_groups_together(window);

    if (window.active_tab.has_value()) {
        if (auto const* active = tab(*window.active_tab); active && active->group.has_value()) {
            if (auto* group = find_tab_group(window, *active->group))
                group->collapsed = false;
        }
    }

    auto const& reported = m_reported_tab_groups.ensure(window.id);
    auto changed = force_report || reported.size() != window.groups.size();
    for (size_t index = 0; !changed && index < reported.size(); ++index)
        changed = !reported[index].has_same_state_as(window.groups[index]);
    if (!changed)
        return;

    m_reported_tab_groups.set(window.id, window.groups);
    if (m_tab_group_hooks.groups_changed)
        m_tab_group_hooks.groups_changed(window);
}

}
