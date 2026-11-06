/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/Vector.h>
#include <LibTest/TestCase.h>
#include <LibURL/Parser.h>
#include <LibWebView/SessionStore.h>
#include <Rune/Bridge/BrowserModel.h>

using Xenon::BrowserModel;
using Xenon::ViewId;
using Xenon::WindowId;

struct Recorder final : public BrowserModel::Observer {
    Vector<ByteString> events;

    virtual void window_opened(BrowserModel::Window const& window) override { events.append(ByteString::formatted("window_opened {}", window.id)); }
    virtual void window_closed(BrowserModel::Window const& window) override { events.append(ByteString::formatted("window_closed {}", window.id)); }
    virtual void window_activated(BrowserModel::Window const& window) override { events.append(ByteString::formatted("window_activated {}", window.id)); }
    virtual void tab_inserted(BrowserModel::Window const& window, BrowserModel::Tab const& tab, size_t index) override { events.append(ByteString::formatted("tab_inserted {} {} {}", window.id, tab.view, index)); }
    virtual void tab_removed(BrowserModel::Window const& window, BrowserModel::Tab const& tab) override { events.append(ByteString::formatted("tab_removed {} {}", window.id, tab.view)); }
    virtual void tab_moved(BrowserModel::Tab const& tab, WindowId from, size_t index) override { events.append(ByteString::formatted("tab_moved {} {} {} {}", tab.view, from, tab.window, index)); }
    virtual void tab_activated(BrowserModel::Window const& window, BrowserModel::Tab const& tab, Optional<ViewId>) override { events.append(ByteString::formatted("tab_activated {} {}", window.id, tab.view)); }
    virtual void tab_pinned_changed(BrowserModel::Tab const& tab) override { events.append(ByteString::formatted("tab_pinned_changed {} {}", tab.view, tab.pinned)); }
};

struct Fixture {
    Recorder recorder;
    NonnullOwnPtr<WebView::SessionStore> store { WebView::SessionStore::create() };
    NonnullOwnPtr<WebView::SessionStore> private_store { WebView::SessionStore::create() };
    ViewId next_view { 1 };
    WindowId next_window { 100 };
    BrowserModel model {
        recorder,
        BrowserModel::Allocators {
            .view_id = [this] { return next_view++; },
            .window_id = [this] { return next_window++; },
        },
        [this](WebView::IsPrivate is_private) { return is_private == WebView::IsPrivate::Yes ? private_store.ptr() : store.ptr(); },
    };

    ViewId open(WindowId window, StringView url = "https://example.com/"sv, Optional<ViewId> opener = {}, Web::HTML::ActivateTab activate = Web::HTML::ActivateTab::No)
    {
        return model.open_tab(window, { .url = URL::Parser::basic_parse(url).value(), .opener = opener, .activate = activate });
    }

    Vector<ViewId> tabs(WindowId window) const
    {
        return model.window(window)->tabs;
    }

    Optional<ViewId> active(WindowId window) const
    {
        return model.window(window)->active_tab;
    }
};

TEST_CASE(the_first_tab_of_a_window_becomes_active)
{
    Fixture fixture;
    auto window = fixture.model.open_window(WebView::IsPrivate::No);
    auto first = fixture.open(window);
    auto second = fixture.open(window);

    EXPECT_EQ(fixture.tabs(window), (Vector<ViewId> { first, second }));
    EXPECT_EQ(fixture.active(window), first);
    EXPECT_EQ(fixture.model.active_window(), window);
    EXPECT(fixture.model.tab(first)->session_tab_id.has_value());
}

TEST_CASE(tabs_opened_by_a_page_go_right_of_their_opener_and_its_earlier_children)
{
    Fixture fixture;
    auto window = fixture.model.open_window(WebView::IsPrivate::No);
    auto a = fixture.open(window);
    auto b = fixture.open(window);
    auto child = fixture.open(window, "https://example.com/1"sv, a);
    auto second_child = fixture.open(window, "https://example.com/2"sv, a);

    EXPECT_EQ(fixture.tabs(window), (Vector<ViewId> { a, child, second_child, b }));
    EXPECT_EQ(fixture.model.tab(child)->opener, a);
}

TEST_CASE(closing_the_active_tab_activates_its_neighbour_before_it_goes)
{
    Fixture fixture;
    auto window = fixture.model.open_window(WebView::IsPrivate::No);
    auto a = fixture.open(window);
    auto b = fixture.open(window);
    auto c = fixture.open(window);
    fixture.model.activate_tab(b);

    fixture.recorder.events.clear();
    fixture.model.close_tab(b);
    EXPECT_EQ(fixture.recorder.events, (Vector<ByteString> { ByteString::formatted("tab_activated {} {}", window, c), ByteString::formatted("tab_removed {} {}", window, b) }));
    EXPECT_EQ(fixture.active(window), c);

    fixture.model.close_tab(c);
    EXPECT_EQ(fixture.active(window), a);
    EXPECT_EQ(fixture.tabs(window), (Vector<ViewId> { a }));
}

TEST_CASE(closing_the_last_tab_closes_its_window_and_activates_the_previous_window)
{
    Fixture fixture;
    auto first_window = fixture.model.open_window(WebView::IsPrivate::No);
    fixture.open(first_window);
    auto second_window = fixture.model.open_window(WebView::IsPrivate::No);
    auto only = fixture.open(second_window);
    EXPECT_EQ(fixture.model.active_window(), second_window);

    fixture.model.close_tab(only);
    EXPECT_EQ(fixture.model.window(second_window), nullptr);
    EXPECT_EQ(fixture.model.tab(only), nullptr);
    EXPECT_EQ(fixture.model.active_window(), first_window);
    EXPECT(fixture.recorder.events.contains_slow(ByteString::formatted("window_closed {}", second_window)));
}

TEST_CASE(pinning_moves_a_tab_to_the_end_of_the_pinned_group_and_new_tabs_stay_out_of_it)
{
    Fixture fixture;
    auto window = fixture.model.open_window(WebView::IsPrivate::No);
    auto a = fixture.open(window);
    auto b = fixture.open(window);
    auto c = fixture.open(window);

    fixture.model.set_tab_pinned(c, true);
    EXPECT_EQ(fixture.tabs(window), (Vector<ViewId> { c, a, b }));
    fixture.model.set_tab_pinned(b, true);
    EXPECT_EQ(fixture.tabs(window), (Vector<ViewId> { c, b, a }));
    EXPECT_EQ(fixture.model.window(window)->pinned_count(fixture.model), 2u);

    auto d = fixture.model.open_tab(window, { .url = URL::Parser::basic_parse("https://example.com/d"sv).value(), .index = 0, .activate = Web::HTML::ActivateTab::No });
    EXPECT_EQ(fixture.tabs(window), (Vector<ViewId> { c, b, d, a }));

    fixture.model.set_tab_pinned(c, false);
    EXPECT_EQ(fixture.tabs(window), (Vector<ViewId> { b, c, d, a }));
    EXPECT(fixture.recorder.events.contains_slow(ByteString::formatted("tab_pinned_changed {} false", c)));
}

TEST_CASE(moving_a_tab_keeps_it_on_its_side_of_the_pinned_group)
{
    Fixture fixture;
    auto window = fixture.model.open_window(WebView::IsPrivate::No);
    auto a = fixture.open(window);
    auto b = fixture.open(window);
    auto c = fixture.open(window);
    fixture.model.set_tab_pinned(a, true);

    fixture.model.move_tab(c, window, 0);
    EXPECT_EQ(fixture.tabs(window), (Vector<ViewId> { a, c, b }));
    fixture.model.move_tab(a, window, 2);
    EXPECT_EQ(fixture.tabs(window), (Vector<ViewId> { a, c, b }));
    fixture.model.move_tab(c, window, 9);
    EXPECT_EQ(fixture.tabs(window), (Vector<ViewId> { a, b, c }));
}

TEST_CASE(moving_a_tab_to_another_window_activates_it_there_and_closes_an_emptied_window)
{
    Fixture fixture;
    auto source = fixture.model.open_window(WebView::IsPrivate::No);
    auto moving = fixture.open(source);
    auto target = fixture.model.open_window(WebView::IsPrivate::No);
    auto resident = fixture.open(target);

    fixture.model.move_tab(moving, target, 0);
    EXPECT_EQ(fixture.tabs(target), (Vector<ViewId> { moving, resident }));
    EXPECT_EQ(fixture.active(target), moving);
    EXPECT_EQ(fixture.model.tab(moving)->window, target);
    EXPECT_EQ(fixture.model.window(source), nullptr);
}

TEST_CASE(a_reopened_tab_comes_back_at_its_place_in_its_window)
{
    Fixture fixture;
    auto window = fixture.model.open_window(WebView::IsPrivate::No);
    auto a = fixture.open(window, "https://a.example/"sv);
    auto b = fixture.open(window, "https://b.example/"sv);
    auto c = fixture.open(window, "https://c.example/"sv);
    (void)a;
    (void)c;

    EXPECT(!fixture.model.can_reopen_closed_tab(WebView::IsPrivate::No));
    fixture.model.close_tab(b);
    EXPECT(fixture.model.can_reopen_closed_tab(WebView::IsPrivate::No));

    EXPECT(fixture.model.reopen_closed_tab({}));
    auto tabs = fixture.tabs(window);
    EXPECT_EQ(tabs.size(), 3u);
    auto reopened = tabs[1];
    EXPECT_EQ(fixture.model.tab(reopened)->initial_url.serialize(), "https://b.example/"sv);
    EXPECT_EQ(fixture.active(window), reopened);
    EXPECT(!fixture.model.reopen_closed_tab({}));
}

TEST_CASE(a_closed_window_reopens_whole_in_a_new_window)
{
    Fixture fixture;
    auto first = fixture.model.open_window(WebView::IsPrivate::No);
    fixture.open(first, "https://keep.example/"sv);
    auto closing = fixture.model.open_window(WebView::IsPrivate::No);
    fixture.open(closing, "https://one.example/"sv);
    fixture.open(closing, "https://two.example/"sv);

    fixture.model.close_window(closing);
    EXPECT_EQ(fixture.model.active_window(), first);

    EXPECT(fixture.model.reopen_closed_tab(first));
    auto reopened = fixture.model.active_window().value();
    EXPECT_NE(reopened, first);
    auto tabs = fixture.tabs(reopened);
    EXPECT_EQ(tabs.size(), 2u);
    EXPECT_EQ(fixture.model.tab(tabs[0])->initial_url.serialize(), "https://one.example/"sv);
    EXPECT_EQ(fixture.model.tab(tabs[1])->initial_url.serialize(), "https://two.example/"sv);
}

TEST_CASE(a_restored_window_loads_only_its_active_and_pinned_tabs)
{
    Fixture fixture;
    WebView::RestoredSessionWindow restored {
        .window_id = 42,
        .tabs = {
            { .tab_id = 1, .url = URL::Parser::basic_parse("https://pinned.example/"sv).value(), .pinned = true },
            { .tab_id = 2, .url = URL::Parser::basic_parse("https://later.example/"sv).value(), .pinned = false },
            { .tab_id = 3, .url = URL::Parser::basic_parse("https://active.example/"sv).value(), .pinned = false },
        },
        .active_tab_index = 2,
        .geometry = {},
    };

    auto window = fixture.model.restore_window(restored);
    auto tabs = fixture.tabs(window);
    EXPECT_EQ(tabs.size(), 3u);
    EXPECT_EQ(fixture.model.window(window)->session_window_id, 42);
    EXPECT(!fixture.model.tab(tabs[0])->deferred);
    EXPECT(fixture.model.tab(tabs[0])->pinned);
    EXPECT(fixture.model.tab(tabs[1])->deferred);
    EXPECT(!fixture.model.tab(tabs[2])->deferred);
    EXPECT_EQ(fixture.active(window), tabs[2]);
    EXPECT_EQ(fixture.model.tab(tabs[1])->session_tab_id, 2);

    fixture.model.mark_tab_loaded(tabs[1]);
    EXPECT(!fixture.model.tab(tabs[1])->deferred);
}

TEST_CASE(recent_order_cycles_back_through_the_last_used_tabs)
{
    Fixture fixture;
    auto window = fixture.model.open_window(WebView::IsPrivate::No);
    auto a = fixture.open(window);
    auto b = fixture.open(window);
    auto c = fixture.open(window);
    fixture.model.activate_tab(c);
    fixture.model.activate_tab(b);

    using Direction = BrowserModel::RecentOrder::Direction;
    EXPECT_EQ(fixture.model.step_recent_tab(window, Direction::LessRecent), c);
    EXPECT_EQ(fixture.model.step_recent_tab(window, Direction::LessRecent), a);
    fixture.model.end_recent_tab_cycle(window);
    EXPECT_EQ(fixture.active(window), a);

    EXPECT_EQ(fixture.model.step_recent_tab(window, Direction::LessRecent), b);
    fixture.model.end_recent_tab_cycle(window);
    EXPECT_EQ(fixture.model.step_recent_tab(window, Direction::LessRecent), a);
}

TEST_CASE(private_windows_keep_their_closed_tabs_apart)
{
    Fixture fixture;
    auto normal = fixture.model.open_window(WebView::IsPrivate::No);
    fixture.open(normal);
    auto incognito = fixture.model.open_window(WebView::IsPrivate::Yes);
    fixture.open(incognito, "https://secret.example/"sv);
    auto second = fixture.open(incognito, "https://secret.example/2"sv);

    fixture.model.close_tab(second);
    EXPECT(fixture.model.can_reopen_closed_tab(WebView::IsPrivate::Yes));
    EXPECT(!fixture.model.can_reopen_closed_tab(WebView::IsPrivate::No));

    auto normal_tab = fixture.open(normal);
    fixture.model.move_tab(normal_tab, incognito, 0);
    EXPECT_EQ(fixture.model.tab(normal_tab)->window, normal);
}
