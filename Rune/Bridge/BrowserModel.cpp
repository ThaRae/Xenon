/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Time.h>
#include <Rune/Bridge/BrowserModel.h>

namespace Xenon {

size_t BrowserModel::Window::pinned_count(BrowserModel const& model) const
{
    size_t count = 0;
    for (auto view : tabs) {
        auto const* tab = model.tab(view);
        if (!tab || !tab->pinned)
            break;
        ++count;
    }
    return count;
}

BrowserModel::BrowserModel(Observer& observer, Allocators allocators, SessionStoreProvider session_store_for)
    : m_observer(observer)
    , m_allocators(move(allocators))
    , m_session_store_for(move(session_store_for))
{
}

BrowserModel::~BrowserModel() = default;

BrowserModel::Window* BrowserModel::find_window(WindowId id)
{
    for (auto& window : m_windows) {
        if (window->id == id)
            return window.ptr();
    }
    return nullptr;
}

BrowserModel::Window const* BrowserModel::window(WindowId id) const
{
    return const_cast<BrowserModel&>(*this).find_window(id);
}

BrowserModel::Tab* BrowserModel::find_tab(ViewId view)
{
    auto tab = m_tabs.get(view);
    if (!tab.has_value())
        return nullptr;
    return tab.value();
}

BrowserModel::Tab const* BrowserModel::tab(ViewId view) const
{
    return const_cast<BrowserModel&>(*this).find_tab(view);
}

BrowserModel::Window const* BrowserModel::window_of(ViewId view) const
{
    auto const* found = tab(view);
    return found ? window(found->window) : nullptr;
}

Optional<WindowId> BrowserModel::active_window() const
{
    if (m_activation_order.is_empty())
        return {};
    return m_activation_order.last();
}

WebView::SessionStore* BrowserModel::store_for(Window const& window) const
{
    return m_session_store_for ? m_session_store_for(window.is_private) : nullptr;
}

bool BrowserModel::can_reopen_closed_tab(WebView::IsPrivate is_private) const
{
    auto* store = m_session_store_for ? m_session_store_for(is_private) : nullptr;
    return store && store->has_closed_units();
}

WindowId BrowserModel::open_window(WebView::IsPrivate is_private, Optional<WindowId> id)
{
    auto window = make<Window>();
    window->id = id.has_value() ? *id : m_allocators.window_id();
    window->is_private = is_private;
    if (auto* store = store_for(*window)) {
        if (auto session_window_id = store->window_opened(); session_window_id.is_error())
            dbgln("Xenon: Unable to register the new window with the session store: {}", session_window_id.error());
        else
            window->session_window_id = session_window_id.value();
    }

    auto& opened = *window;
    m_windows.append(move(window));
    m_observer.window_opened(opened);
    activate_window(opened.id);
    return opened.id;
}

WindowId BrowserModel::restore_window(WebView::RestoredSessionWindow const& restored)
{
    auto window = make<Window>();
    window->id = m_allocators.window_id();
    window->session_window_id = restored.window_id;

    auto& opened = *window;
    m_windows.append(move(window));
    m_observer.window_opened(opened);

    if (!restored.tabs.is_empty()) {
        auto active_index = static_cast<size_t>(clamp<i64>(restored.active_tab_index, 0, static_cast<i64>(restored.tabs.size()) - 1));
        Optional<ViewId> active_view;
        for (size_t i = 0; i < restored.tabs.size(); ++i) {
            auto const& restored_tab = restored.tabs[i];
            auto tab = make<Tab>();
            tab->view = m_allocators.view_id();
            tab->pinned = restored_tab.pinned;
            tab->session_tab_id = restored_tab.tab_id;
            tab->initial_url = restored_tab.url;
            tab->restored_from_session = true;
            tab->deferred = i != active_index && !restored_tab.pinned;
            auto view = insert_tab(opened, move(tab), opened.tabs.size());
            if (i == active_index)
                active_view = view;
        }
        if (active_view.has_value())
            set_active_tab(opened, *active_view);
    }

    activate_window(opened.id);
    return opened.id;
}

void BrowserModel::activate_window(WindowId id)
{
    auto* window = find_window(id);
    if (!window)
        return;
    if (!m_activation_order.is_empty() && m_activation_order.last() == id)
        m_activation_order.take_last();
    else
        m_activation_order.remove_first_matching([&](auto other) { return other == id; });
    m_activation_order.append(id);

    if (auto* store = store_for(*window); store && window->session_window_id.has_value())
        store->window_activated(*window->session_window_id);
    m_observer.window_activated(*window);
}

void BrowserModel::close_window(WindowId id)
{
    auto* window = find_window(id);
    if (!window)
        return;

    auto* store = store_for(*window);
    if (store && window->session_window_id.has_value()) {
        auto active_index = window->active_tab.has_value() ? window->index_of(*window->active_tab).value_or(0) : 0;
        WebView::SessionStore::WindowClosing closing {
            .window_id = *window->session_window_id,
            .active_tab_index = static_cast<i64>(active_index),
            .closed_at = UnixDateTime::now(),
        };
        if (auto result = store->window_closing(move(closing)); result.is_error())
            dbgln("Xenon: Unable to record the closing window in the session store: {}", result.error());
    }

    auto was_active = active_window() == id;
    auto is_private = window->is_private;
    m_observer.window_closed(*window);
    forget_window(id);

    if (was_active && !m_activation_order.is_empty())
        activate_window(m_activation_order.last());
    m_observer.closed_tabs_changed(can_reopen_closed_tab(is_private));
}

void BrowserModel::forget_window(WindowId id)
{
    auto* window = find_window(id);
    if (!window)
        return;
    for (auto view : window->tabs)
        m_tabs.remove(view);
    m_activation_order.remove_first_matching([&](auto other) { return other == id; });
    m_windows.remove_first_matching([&](auto const& other) { return other->id == id; });
}

ViewId BrowserModel::insert_tab(Window& window, NonnullOwnPtr<Tab> tab, size_t index)
{
    index = min(index, window.tabs.size());
    tab->window = window.id;
    auto view = tab->view;
    auto& inserted = *tab;
    m_tabs.set(view, move(tab));
    window.tabs.insert(index, view);
    window.recent_order.update(window.tabs);
    m_observer.tab_inserted(window, inserted, index);
    return view;
}

size_t BrowserModel::constrained_index(Window const& window, size_t index, bool pinned, bool already_in_window) const
{
    auto pinned_count = window.pinned_count(*this);
    auto count = window.tabs.size();
    if (already_in_window) {
        if (pinned)
            return clamp(index, size_t { 0 }, pinned_count > 0 ? pinned_count - 1 : size_t { 0 });
        return clamp(index, pinned_count, count > 0 ? count - 1 : size_t { 0 });
    }
    if (pinned)
        return clamp(index, size_t { 0 }, pinned_count);
    return clamp(index, pinned_count, count);
}

size_t BrowserModel::insertion_index_for(Window const& window, NewTab const& new_tab) const
{
    if (new_tab.index.has_value())
        return constrained_index(window, *new_tab.index, false, false);

    if (new_tab.opener.has_value()) {
        if (auto opener_index = window.index_of(*new_tab.opener); opener_index.has_value()) {
            auto index = *opener_index + 1;
            while (index < window.tabs.size()) {
                auto const* sibling = tab(window.tabs[index]);
                if (!sibling || sibling->opener != new_tab.opener)
                    break;
                ++index;
            }
            return constrained_index(window, index, false, false);
        }
    }

    return window.tabs.size();
}

ViewId BrowserModel::open_tab(WindowId window_id, NewTab new_tab)
{
    auto* window = find_window(window_id);
    VERIFY(window);

    auto tab = make<Tab>();
    tab->view = new_tab.view.has_value() ? *new_tab.view : m_allocators.view_id();
    tab->opener = new_tab.opener;
    tab->initial_url = new_tab.url;
    tab->pending_history = move(new_tab.history);

    auto index = insertion_index_for(*window, new_tab);
    auto activates = new_tab.activate == Web::HTML::ActivateTab::Yes || !window->active_tab.has_value();

    if (auto* store = store_for(*window); store && window->session_window_id.has_value()) {
        WebView::SessionStore::TabOpened opened {
            .window_id = window->session_window_id,
            .initial_url = new_tab.url,
            .insertion_index = static_cast<i64>(index),
            .is_active = activates ? WebView::SessionStore::IsActive::Yes : WebView::SessionStore::IsActive::No,
        };
        if (auto session_tab_id = store->tab_opened(move(opened)); session_tab_id.is_error())
            dbgln("Xenon: Unable to register the new tab with the session store: {}", session_tab_id.error());
        else
            tab->session_tab_id = session_tab_id.value();
    }

    auto view = insert_tab(*window, move(tab), index);
    if (activates)
        set_active_tab(*window, view);
    m_observer.session_metadata_changed();
    return view;
}

void BrowserModel::set_active_tab(Window& window, ViewId view)
{
    auto* tab = find_tab(view);
    if (!tab || tab->window != window.id)
        return;

    auto previous = window.active_tab;
    if (previous == view)
        return;

    window.active_tab = view;
    window.recent_order.update(window.tabs);
    window.recent_order.did_activate(view);

    if (auto* store = store_for(window); store && tab->session_tab_id.has_value())
        store->active_tab_changed(*tab->session_tab_id);

    m_observer.tab_activated(window, *tab, previous);
    m_observer.session_metadata_changed();
}

void BrowserModel::activate_tab(ViewId view)
{
    auto* tab = find_tab(view);
    if (!tab)
        return;
    auto* window = find_window(tab->window);
    VERIFY(window);
    set_active_tab(*window, view);
    if (active_window() != window->id)
        activate_window(window->id);
}

Optional<ViewId> BrowserModel::successor_of(Window const& window, ViewId view) const
{
    auto index = window.index_of(view);
    if (!index.has_value())
        return {};
    if (*index + 1 < window.tabs.size())
        return window.tabs[*index + 1];
    if (*index > 0)
        return window.tabs[*index - 1];
    return {};
}

void BrowserModel::detach_tab(Window& window, ViewId view)
{
    if (window.active_tab == view) {
        if (auto successor = successor_of(window, view); successor.has_value())
            set_active_tab(window, *successor);
    }
    window.tabs.remove_first_matching([&](auto other) { return other == view; });
    if (window.active_tab == view)
        window.active_tab.clear();
    window.recent_order.update(window.tabs);
}

void BrowserModel::close_tab(ViewId view)
{
    auto* tab = find_tab(view);
    if (!tab)
        return;
    auto* window = find_window(tab->window);
    VERIFY(window);

    auto* store = store_for(*window);
    if (store && tab->session_tab_id.has_value()) {
        WebView::SessionStore::TabClosed closed {
            .tab_id = *tab->session_tab_id,
            .closed_at = UnixDateTime::now(),
        };
        if (auto result = store->tab_closed(move(closed)); result.is_error())
            dbgln("Xenon: Unable to record the closed tab in the session store: {}", result.error());
    }

    detach_tab(*window, view);
    m_observer.tab_removed(*window, *tab);
    m_tabs.remove(view);

    if (window->tabs.is_empty()) {
        close_window(window->id);
        return;
    }
    m_observer.closed_tabs_changed(can_reopen_closed_tab(window->is_private));
    m_observer.session_metadata_changed();
}

void BrowserModel::reorder_tab(Window& window, ViewId view, size_t index)
{
    window.tabs.remove_first_matching([&](auto other) { return other == view; });
    window.tabs.insert(min(index, window.tabs.size()), view);
}

void BrowserModel::sync_session_tab_order(Window const& window)
{
    auto* store = store_for(window);
    if (!store || !window.session_window_id.has_value())
        return;

    Vector<WebView::SessionTabId> tab_order;
    for (auto view : window.tabs) {
        if (auto const* member = tab(view); member && member->session_tab_id.has_value())
            tab_order.append(*member->session_tab_id);
    }
    store->tab_order_changed({ .window_id = *window.session_window_id, .ordered_tabs = move(tab_order) });
    m_observer.session_metadata_changed();
}

void BrowserModel::move_tab(ViewId view, WindowId target_id, size_t index)
{
    auto* tab = find_tab(view);
    auto* target = find_window(target_id);
    if (!tab || !target)
        return;
    auto* source = find_window(tab->window);
    VERIFY(source);

    if (source == target) {
        auto destination = constrained_index(*target, index, tab->pinned, true);
        if (target->index_of(view) == destination)
            return;
        reorder_tab(*target, view, destination);
        m_observer.tab_moved(*tab, source->id, destination);
        sync_session_tab_order(*target);
        return;
    }

    if (source->is_private != target->is_private)
        return;

    auto destination = constrained_index(*target, index, tab->pinned, false);
    detach_tab(*source, view);
    tab->window = target->id;
    target->tabs.insert(destination, view);
    target->recent_order.update(target->tabs);
    m_observer.tab_moved(*tab, source->id, destination);

    auto* store = store_for(*target);
    if (store && tab->session_tab_id.has_value()) {
        if (target->session_window_id.has_value()) {
            store->tab_moved({ .tab_id = *tab->session_tab_id, .new_window_id = *target->session_window_id, .ordinal = static_cast<i64>(destination) });
            sync_session_tab_order(*target);
        } else {
            store->tab_detached(*tab->session_tab_id);
            tab->session_tab_id.clear();
        }
    }
    sync_session_tab_order(*source);

    set_active_tab(*target, view);
    if (source->tabs.is_empty())
        close_window(source->id);
}

void BrowserModel::set_tab_pinned(ViewId view, bool pinned, Optional<size_t> pinned_index)
{
    auto* tab = find_tab(view);
    if (!tab || tab->pinned == pinned)
        return;
    auto* window = find_window(tab->window);
    VERIFY(window);

    auto index = window->index_of(view).value();
    auto pinned_count = window->pinned_count(*this);
    auto destination = pinned ? clamp(pinned_index.value_or(pinned_count), size_t { 0 }, pinned_count) : pinned_count - 1;
    tab->pinned = pinned;

    if (destination != index) {
        reorder_tab(*window, view, destination);
        m_observer.tab_moved(*tab, window->id, destination);
        sync_session_tab_order(*window);
    }

    if (auto* store = store_for(*window); store && tab->session_tab_id.has_value()) {
        if (auto result = store->tab_pinned_changed(*tab->session_tab_id, pinned); result.is_error())
            dbgln("Xenon: Unable to record the tab's pin in the session store: {}", result.error());
    }
    m_observer.tab_pinned_changed(*tab);
}

bool BrowserModel::reopen_closed_tab(Optional<WindowId> target_id)
{
    if (!target_id.has_value())
        target_id = active_window();
    auto* target = target_id.has_value() ? find_window(*target_id) : nullptr;
    auto is_private = target ? target->is_private : WebView::IsPrivate::No;

    auto* store = m_session_store_for ? m_session_store_for(is_private) : nullptr;
    if (!store)
        return false;

    auto closed_unit = store->take_most_recently_closed();
    if (closed_unit.is_error()) {
        dbgln("Xenon: Unable to reopen the most recently closed session unit: {}", closed_unit.error());
        return false;
    }
    if (!closed_unit.value().has_value())
        return false;
    auto unit = closed_unit.release_value().release_value();

    Window* window = target;
    bool restoring_to_source_window = false;
    if (!unit.was_window && unit.source_window_id.has_value()) {
        for (auto& candidate : m_windows) {
            if (candidate->is_private == is_private && candidate->session_window_id == unit.source_window_id) {
                window = candidate.ptr();
                restoring_to_source_window = true;
                break;
            }
        }
    }
    if (unit.was_window || !window)
        window = find_window(open_window(is_private));
    VERIFY(window);

    Optional<size_t> next_insertion_index;
    if (restoring_to_source_window && unit.source_tab_index.has_value())
        next_insertion_index = static_cast<size_t>(clamp<i64>(*unit.source_tab_index, 0, static_cast<i64>(window->tabs.size())));

    Optional<ViewId> tab_to_activate;
    i64 restored_tab_index = 0;
    for (auto& closed_tab : unit.tabs) {
        auto view = open_tab(window->id, {
                                             .url = closed_tab.active_url,
                                             .index = next_insertion_index.has_value() ? next_insertion_index : Optional<size_t> { window->tabs.size() },
                                             .activate = Web::HTML::ActivateTab::No,
                                             .history = move(closed_tab.history),
                                         });
        if (next_insertion_index.has_value())
            ++*next_insertion_index;
        if (restored_tab_index == unit.active_tab_index)
            tab_to_activate = view;

        if (closed_tab.pinned) {
            Optional<size_t> pinned_index;
            if (restoring_to_source_window && unit.source_tab_index.has_value())
                pinned_index = static_cast<size_t>(max<i64>(*unit.source_tab_index, 0));
            set_tab_pinned(view, true, pinned_index);
        }
        ++restored_tab_index;
    }

    if (tab_to_activate.has_value())
        activate_tab(*tab_to_activate);
    m_observer.closed_tabs_changed(can_reopen_closed_tab(is_private));
    return true;
}

Optional<ViewId> BrowserModel::step_recent_tab(WindowId id, RecentOrder::Direction direction)
{
    auto* window = find_window(id);
    if (!window)
        return {};
    window->recent_order.update(window->tabs);
    auto view = window->recent_order.step(direction);
    if (view.has_value())
        set_active_tab(*window, *view);
    return view;
}

void BrowserModel::end_recent_tab_cycle(WindowId id)
{
    auto* window = find_window(id);
    if (!window)
        return;
    window->recent_order.update(window->tabs);
    window->recent_order.end_cycle();
}

void BrowserModel::mark_tab_loaded(ViewId view)
{
    if (auto* tab = find_tab(view))
        tab->deferred = false;
}

Optional<WebView::SessionHistorySnapshot> BrowserModel::take_history_for_load(ViewId view)
{
    auto* tab = find_tab(view);
    if (!tab)
        return {};
    if (tab->pending_history.has_value())
        return tab->pending_history.release_value();
    if (!tab->restored_from_session || !tab->session_tab_id.has_value())
        return {};
    tab->restored_from_session = false;
    auto const* owner = window(tab->window);
    auto* store = owner ? store_for(*owner) : nullptr;
    if (!store)
        return {};
    return store->load_tab_history(*tab->session_tab_id);
}

void BrowserModel::application_quitting()
{
    if (auto* store = m_session_store_for ? m_session_store_for(WebView::IsPrivate::No) : nullptr)
        store->application_quitting();
}

}
