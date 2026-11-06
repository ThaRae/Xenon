/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibURL/URL.h>
#include <LibWebCommon/HTML/ActivateTab.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/Forward.h>
#include <LibWebView/SessionStore.h>
#include <Rune/Bridge/TabGroup.h>
#include <Rune/Bridge/XenonBridge.h>
#include <UI/RecentTabOrder.h>

namespace Xenon {

using WindowId = XenonWindowId;
using ViewId = XenonViewId;

class BrowserModel {
    AK_MAKE_NONCOPYABLE(BrowserModel);
    AK_MAKE_NONMOVABLE(BrowserModel);

public:
    AK_ALLOC_WITH_KMALLOC;

    using RecentOrder = Ladybird::RecentTabOrder<ViewId>;

    struct Tab {
        AK_ALLOC_WITH_KMALLOC;

        ViewId view { 0 };
        WindowId window { 0 };
        bool pinned { false };
        Optional<ViewId> opener;
        Optional<WebView::SessionTabId> session_tab_id;
        URL::URL initial_url;
        bool deferred { false };
        bool restored_from_session { false };
        bool hibernated { false };
        Optional<WebView::SessionHistorySnapshot> pending_history;
        Optional<TabGroupId> group;

        bool has_initial_url() const { return !initial_url.scheme().is_empty(); }
    };

    struct Window {
        AK_ALLOC_WITH_KMALLOC;

        WindowId id { 0 };
        WebView::IsPrivate is_private { WebView::IsPrivate::No };
        Vector<ViewId> tabs;
        Optional<ViewId> active_tab;
        Optional<WebView::SessionWindowId> session_window_id;
        RecentOrder recent_order;
        Vector<TabGroup> groups;

        size_t pinned_count(BrowserModel const&) const;
        Optional<size_t> index_of(ViewId view) const { return tabs.find_first_index(view); }
    };

    class Observer {
    public:
        virtual ~Observer() = default;

        virtual void window_opened(Window const&) { }
        virtual void window_closed(Window const&) { }
        virtual void window_activated(Window const&) { }
        virtual void tab_inserted(Window const&, Tab const&, size_t) { }
        virtual void tab_removed(Window const&, Tab const&) { }
        virtual void tab_moved(Tab const&, WindowId, size_t) { }
        virtual void tab_activated(Window const&, Tab const&, Optional<ViewId>) { }
        virtual void tab_pinned_changed(Tab const&) { }
        virtual void closed_tabs_changed(bool) { }
        virtual void session_metadata_changed() { }
        virtual void tab_hibernated(Tab const&) { }
    };

    struct NewTab {
        Optional<ViewId> view {};
        URL::URL url {};
        Optional<size_t> index {};
        Optional<ViewId> opener {};
        Web::HTML::ActivateTab activate { Web::HTML::ActivateTab::Yes };
        Optional<WebView::SessionHistorySnapshot> history {};
    };

    struct Allocators {
        Function<ViewId()> view_id;
        Function<WindowId()> window_id;
    };

    using SessionStoreProvider = Function<WebView::SessionStore*(WebView::IsPrivate)>;

    struct TabGroupHooks {
        Function<URL::URL()> new_tab_page_url;
        Function<void()> window_opened;
        Function<void(Window const&)> groups_changed;
        Function<void(Window const&, TabGroupId)> edit_requested;
        Function<Vector<SavedTabGroup>(WebView::SessionWindowId)> saved_groups;
    };

    BrowserModel(Observer&, Allocators, SessionStoreProvider);
    ~BrowserModel();

    WindowId open_window(WebView::IsPrivate, Optional<WindowId> = {});
    WindowId restore_window(WebView::RestoredSessionWindow const&);
    void close_window(WindowId);
    void activate_window(WindowId);

    ViewId open_tab(WindowId, NewTab);
    void activate_tab(ViewId);
    void close_tab(ViewId);
    void move_tab(ViewId, WindowId, size_t index);
    void set_tab_pinned(ViewId, bool pinned, Optional<size_t> pinned_index = {});
    bool reopen_closed_tab(Optional<WindowId> target);

    Optional<ViewId> step_recent_tab(WindowId, RecentOrder::Direction);
    void end_recent_tab_cycle(WindowId);

    void mark_tab_loaded(ViewId);
    bool hibernate_tab(ViewId, URL::URL, WebView::SessionHistorySnapshot);
    Optional<WebView::SessionHistorySnapshot> take_history_for_load(ViewId);
    void application_quitting();

    Window const* window(WindowId) const;
    Tab const* tab(ViewId) const;
    Window const* window_of(ViewId) const;
    Optional<WindowId> active_window() const;
    Vector<WindowId> const& windows_by_activation() const { return m_activation_order; }
    bool can_reopen_closed_tab(WebView::IsPrivate) const;

    void set_tab_group_hooks(TabGroupHooks);
    TabGroup const* tab_group(TabGroupId) const;
    Window const* window_of_tab_group(TabGroupId) const;
    bool is_tab_hidden(ViewId) const;
    TabGroupId add_tab_to_new_group(ViewId);
    void add_tab_to_group(ViewId, TabGroupId);
    void remove_tab_from_group(ViewId);
    void set_tab_group_title(TabGroupId, String);
    void set_tab_group_color(TabGroupId, TabGroupColor);
    void set_tab_group_collapsed(TabGroupId, bool collapsed);
    void ungroup_tabs(TabGroupId);
    Vector<ViewId> prepare_to_close_tab_group(TabGroupId);
    Optional<ViewId> open_new_tab_in_group(TabGroupId, Optional<ViewId> = {});
    Optional<WindowId> move_tab_group_to_new_window(TabGroupId, Optional<WindowId> = {});
    void drop_tab(ViewId, size_t index, Optional<TabGroupId>);

private:
    Window* find_window(WindowId);
    Tab* find_tab(ViewId);
    WebView::SessionStore* store_for(Window const&) const;

    ViewId insert_tab(Window&, NonnullOwnPtr<Tab>, size_t index);
    size_t insertion_index_for(Window const&, NewTab const&) const;
    size_t constrained_index(Window const&, size_t index, bool pinned, bool already_in_window) const;
    void set_active_tab(Window&, ViewId);
    Optional<ViewId> successor_of(Window const&, ViewId) const;
    void detach_tab(Window&, ViewId);
    void reorder_tab(Window&, ViewId, size_t index);
    void sync_session_tab_order(Window const&);
    void report_closed_tabs(Window const&);
    void forget_window(WindowId);

    TabGroup* find_tab_group(Window&, TabGroupId);
    Window* find_window_of_tab_group(TabGroupId);
    Optional<TabGroupId> group_at(Window const&, size_t index) const;
    bool is_hidden_at(Window const&, size_t index) const;
    size_t group_tab_count(Window const&, TabGroupId) const;
    TabGroupColor next_tab_group_color(Window const&) const;
    TabGroup& create_tab_group(Window&);
    void move_tab_quietly(Window&, ViewId, size_t index);
    void tab_group_did_open_window();
    void tab_group_did_insert_tab(Window&, size_t index);
    void tab_group_will_remove_tab(Window&, ViewId);
    void tab_group_did_move_tab(Window&, size_t index);
    void tab_group_did_activate_tab(Window&, Tab&);
    void tab_group_will_pin_tab(Window&, Tab&);
    void add_opened_tab_to_group(ViewId opener, ViewId opened);
    void restore_tab_groups(Window&);
    void keep_tab_groups_together(Window&);
    void update_tab_groups(Window&, bool force_report = false);
    void open_new_tab_page(Window&);

    Observer& m_observer;
    Allocators m_allocators;
    SessionStoreProvider m_session_store_for;

    Vector<NonnullOwnPtr<Window>> m_windows;
    HashMap<ViewId, NonnullOwnPtr<Tab>> m_tabs;
    Vector<WindowId> m_activation_order;

    TabGroupHooks m_tab_group_hooks;
    HashMap<WindowId, Vector<TabGroup>> m_reported_tab_groups;
    TabGroupId m_next_tab_group_id { 1 };
    bool m_is_moving_tabs_for_groups { false };
};

}
