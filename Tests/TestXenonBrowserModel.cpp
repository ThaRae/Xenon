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

template<>
struct AK::Formatter<Xenon::TabGroupColor> : AK::Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, Xenon::TabGroupColor color)
    {
        return AK::Formatter<StringView>::format(builder, Xenon::tab_group_color_name(color));
    }
};

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
    virtual void tab_hibernated(BrowserModel::Tab const& tab) override { events.append(ByteString::formatted("tab_hibernated {}", tab.view)); }
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

TEST_CASE(a_hibernated_tab_keeps_its_history_until_it_loads_again)
{
    Fixture fixture;
    auto window = fixture.model.open_window(WebView::IsPrivate::No);
    auto active = fixture.open(window);
    auto background = fixture.open(window);
    auto session_tab_id = fixture.model.tab(background)->session_tab_id;

    WebView::SessionHistorySnapshot history;
    history.used_steps = { 0, 1 };
    history.current_used_step_index = 1;
    auto later_url = URL::Parser::basic_parse("https://example.com/later"sv).value();

    EXPECT(!fixture.model.hibernate_tab(active, later_url, history));
    EXPECT(!fixture.model.tab(active)->deferred);

    fixture.recorder.events.clear();
    EXPECT(fixture.model.hibernate_tab(background, later_url, history));
    EXPECT_EQ(fixture.recorder.events, (Vector<ByteString> { ByteString::formatted("tab_hibernated {}", background) }));
    auto const* hibernated = fixture.model.tab(background);
    EXPECT(hibernated->deferred);
    EXPECT(hibernated->hibernated);
    EXPECT_EQ(hibernated->initial_url, later_url);
    EXPECT_EQ(hibernated->session_tab_id, session_tab_id);
    EXPECT(!fixture.model.hibernate_tab(background, later_url, history));

    fixture.model.mark_tab_loaded(background);
    EXPECT(!hibernated->deferred);
    EXPECT(!hibernated->hibernated);
    auto restored = fixture.model.take_history_for_load(background);
    EXPECT(restored.has_value());
    EXPECT_EQ(restored->used_steps, (Vector<i32> { 0, 1 }));
    EXPECT_EQ(restored->current_used_step_index, 1u);
    EXPECT(!fixture.model.take_history_for_load(background).has_value());
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

struct GroupFixture : Fixture {
    Vector<ByteString> group_events;
    Vector<Xenon::TabGroupId> edit_requests;
    Vector<Xenon::SavedTabGroup> saved_groups;
    WindowId window { 0 };

    GroupFixture()
    {
        model.set_tab_group_hooks({
            .new_tab_page_url = [] { return URL::Parser::basic_parse("about:newtab"sv).value(); },
            .window_opened = {},
            .groups_changed = [this](BrowserModel::Window const& changed) { group_events.append(ByteString::formatted("groups_changed {} {}", changed.id, changed.groups.size())); },
            .edit_requested = [this](BrowserModel::Window const&, Xenon::TabGroupId group) { edit_requests.append(group); },
            .saved_groups = [this](WebView::SessionWindowId) { return saved_groups; },
        });
        window = model.open_window(WebView::IsPrivate::No);
    }

    Vector<ViewId> open_tabs(size_t count)
    {
        Vector<ViewId> views;
        for (size_t i = 0; i < count; ++i)
            views.append(open(window, ByteString::formatted("https://example.com/{}", i)));
        return views;
    }

    Optional<Xenon::TabGroupId> group_of(ViewId view) const { return model.tab(view)->group; }
    Xenon::TabGroup const& group(Xenon::TabGroupId id) const { return *model.tab_group(id); }
};

TEST_CASE(a_new_group_takes_the_least_used_colour_and_asks_to_be_edited)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(3);

    auto first = fixture.model.add_tab_to_new_group(tabs[0]);
    auto second = fixture.model.add_tab_to_new_group(tabs[2]);
    EXPECT_EQ(fixture.group(first).color, Xenon::TabGroupColor::Rose);
    EXPECT_EQ(fixture.group(second).color, Xenon::TabGroupColor::Sky);
    EXPECT_EQ(fixture.edit_requests, (Vector<Xenon::TabGroupId> { first, second }));
    EXPECT_EQ(fixture.group(first).tabs, (Vector<ViewId> { tabs[0] }));
    EXPECT_EQ(fixture.model.window(fixture.window)->groups.size(), 2u);
    EXPECT(!fixture.group_events.is_empty());
}

TEST_CASE(adding_a_tab_to_a_group_moves_it_beside_the_group)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(5);
    auto group = fixture.model.add_tab_to_new_group(tabs[2]);
    fixture.model.add_tab_to_group(tabs[4], group);
    EXPECT_EQ(fixture.tabs(fixture.window), (Vector<ViewId> { tabs[0], tabs[1], tabs[2], tabs[4], tabs[3] }));

    fixture.model.add_tab_to_group(tabs[0], group);
    EXPECT_EQ(fixture.tabs(fixture.window), (Vector<ViewId> { tabs[1], tabs[2], tabs[4], tabs[0], tabs[3] }));
    EXPECT_EQ(fixture.group(group).tabs, (Vector<ViewId> { tabs[2], tabs[4], tabs[0] }));
    EXPECT_EQ(fixture.group(group).first, 1u);
    EXPECT_EQ(fixture.group(group).last, 3u);
}

TEST_CASE(removing_a_tab_from_the_middle_of_a_group_moves_it_past_the_group)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(4);
    auto group = fixture.model.add_tab_to_new_group(tabs[0]);
    fixture.model.add_tab_to_group(tabs[1], group);
    fixture.model.add_tab_to_group(tabs[2], group);

    fixture.model.remove_tab_from_group(tabs[1]);
    EXPECT_EQ(fixture.tabs(fixture.window), (Vector<ViewId> { tabs[0], tabs[2], tabs[1], tabs[3] }));
    EXPECT_EQ(fixture.group(group).tabs, (Vector<ViewId> { tabs[0], tabs[2] }));
    EXPECT(!fixture.group_of(tabs[1]).has_value());
}

TEST_CASE(tabs_opened_from_a_grouped_tab_join_its_group)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(3);
    auto group = fixture.model.add_tab_to_new_group(tabs[0]);
    fixture.model.add_tab_to_group(tabs[1], group);

    auto child = fixture.open(fixture.window, "https://example.com/child"sv, tabs[1]);
    EXPECT_EQ(fixture.group_of(child), group);
    EXPECT_EQ(fixture.tabs(fixture.window), (Vector<ViewId> { tabs[0], tabs[1], child, tabs[2] }));

    auto between = fixture.model.open_tab(fixture.window, { .url = URL::Parser::basic_parse("https://example.com/between"sv).value(), .index = 1, .activate = Web::HTML::ActivateTab::No });
    EXPECT_EQ(fixture.group_of(between), group);

    auto outside = fixture.open(fixture.window, "https://example.com/outside"sv);
    EXPECT(!fixture.group_of(outside).has_value());
}

TEST_CASE(moving_a_tab_between_group_members_joins_and_moving_it_away_leaves)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(4);
    auto group = fixture.model.add_tab_to_new_group(tabs[0]);
    fixture.model.add_tab_to_group(tabs[1], group);

    fixture.model.move_tab(tabs[3], fixture.window, 1);
    EXPECT_EQ(fixture.group_of(tabs[3]), group);

    fixture.model.move_tab(tabs[3], fixture.window, 3);
    EXPECT(!fixture.group_of(tabs[3]).has_value());
    EXPECT_EQ(fixture.group(group).tabs, (Vector<ViewId> { tabs[0], tabs[1] }));

    auto single = fixture.model.add_tab_to_new_group(tabs[2]);
    fixture.model.move_tab(tabs[2], fixture.window, 3);
    EXPECT_EQ(fixture.group_of(tabs[2]), single);
}

TEST_CASE(collapsing_the_active_tabs_group_activates_the_next_shown_tab)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(4);
    auto group = fixture.model.add_tab_to_new_group(tabs[1]);
    fixture.model.add_tab_to_group(tabs[2], group);
    fixture.model.activate_tab(tabs[1]);

    fixture.model.set_tab_group_collapsed(group, true);
    EXPECT(fixture.group(group).collapsed);
    EXPECT_EQ(fixture.active(fixture.window), tabs[3]);
    EXPECT(fixture.model.is_tab_hidden(tabs[1]));

    fixture.model.close_tab(tabs[3]);
    EXPECT_EQ(fixture.active(fixture.window), tabs[0]);

    fixture.model.activate_tab(tabs[2]);
    EXPECT(!fixture.group(group).collapsed);
}

TEST_CASE(collapsing_the_only_shown_tabs_opens_a_new_tab_page)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(2);
    auto group = fixture.model.add_tab_to_new_group(tabs[0]);
    fixture.model.add_tab_to_group(tabs[1], group);

    fixture.model.set_tab_group_collapsed(group, true);
    auto const& window_tabs = fixture.tabs(fixture.window);
    EXPECT_EQ(window_tabs.size(), 3u);
    EXPECT_EQ(fixture.active(fixture.window), window_tabs[2]);
    EXPECT_EQ(fixture.model.tab(window_tabs[2])->initial_url.serialize(), "about:newtab"sv);

    fixture.model.close_tab(window_tabs[2]);
    EXPECT(!fixture.group(group).collapsed);
}

TEST_CASE(pinning_a_grouped_tab_takes_it_out_of_its_group)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(4);
    auto group = fixture.model.add_tab_to_new_group(tabs[1]);
    fixture.model.add_tab_to_group(tabs[2], group);
    fixture.model.add_tab_to_group(tabs[3], group);

    fixture.model.set_tab_pinned(tabs[2], true);
    EXPECT(!fixture.group_of(tabs[2]).has_value());
    EXPECT_EQ(fixture.group(group).tabs, (Vector<ViewId> { tabs[1], tabs[3] }));

    fixture.model.add_tab_to_group(tabs[2], group);
    EXPECT(!fixture.model.tab(tabs[2])->pinned);
    EXPECT_EQ(fixture.group_of(tabs[2]), group);
}

TEST_CASE(closing_a_group_holding_every_tab_opens_a_new_tab_first)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(2);
    auto group = fixture.model.add_tab_to_new_group(tabs[0]);
    fixture.model.add_tab_to_group(tabs[1], group);

    auto closing = fixture.model.prepare_to_close_tab_group(group);
    EXPECT_EQ(closing, (Vector<ViewId> { tabs[0], tabs[1] }));
    EXPECT_EQ(fixture.tabs(fixture.window).size(), 3u);
    for (auto view : closing)
        fixture.model.close_tab(view);
    EXPECT_EQ(fixture.model.tab_group(group), nullptr);
    EXPECT_EQ(fixture.tabs(fixture.window).size(), 1u);
}

TEST_CASE(ungrouping_keeps_the_tabs_where_they_are)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(3);
    auto group = fixture.model.add_tab_to_new_group(tabs[1]);
    fixture.model.add_tab_to_group(tabs[2], group);
    fixture.model.ungroup_tabs(group);
    EXPECT_EQ(fixture.model.tab_group(group), nullptr);
    EXPECT_EQ(fixture.tabs(fixture.window), tabs);
    EXPECT(!fixture.group_of(tabs[1]).has_value());
}

TEST_CASE(a_new_tab_in_a_group_opens_at_its_end_and_is_active)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(3);
    auto group = fixture.model.add_tab_to_new_group(tabs[0]);
    fixture.model.set_tab_group_title(group, "Work"_string);
    fixture.model.set_tab_group_color(group, Xenon::TabGroupColor::Moss);

    auto opened = fixture.model.open_new_tab_in_group(group);
    EXPECT(opened.has_value());
    EXPECT_EQ(fixture.tabs(fixture.window), (Vector<ViewId> { tabs[0], *opened, tabs[1], tabs[2] }));
    EXPECT_EQ(fixture.group_of(*opened), group);
    EXPECT_EQ(fixture.active(fixture.window), *opened);
    EXPECT_EQ(fixture.group(group).title, "Work"sv);
    EXPECT_EQ(fixture.group(group).color, Xenon::TabGroupColor::Moss);
}

TEST_CASE(moving_a_group_to_a_new_window_keeps_its_title_and_colour)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(3);
    auto group = fixture.model.add_tab_to_new_group(tabs[1]);
    fixture.model.add_tab_to_group(tabs[2], group);
    fixture.model.set_tab_group_title(group, "Research"_string);
    fixture.model.set_tab_group_color(group, Xenon::TabGroupColor::Iris);

    auto moved_to = fixture.model.move_tab_group_to_new_window(group);
    EXPECT(moved_to.has_value());
    EXPECT_EQ(fixture.model.active_window(), *moved_to);
    EXPECT_EQ(fixture.tabs(fixture.window), (Vector<ViewId> { tabs[0] }));
    EXPECT_EQ(fixture.tabs(*moved_to), (Vector<ViewId> { tabs[1], tabs[2] }));
    auto const& groups = fixture.model.window(*moved_to)->groups;
    EXPECT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].title, "Research"sv);
    EXPECT_EQ(groups[0].color, Xenon::TabGroupColor::Iris);
    EXPECT_EQ(groups[0].tabs, (Vector<ViewId> { tabs[1], tabs[2] }));

    EXPECT(!fixture.model.move_tab_group_to_new_window(groups[0].id).has_value());
}

TEST_CASE(dropping_a_tab_at_a_groups_edge_joins_or_leaves_it_as_asked)
{
    GroupFixture fixture;
    auto tabs = fixture.open_tabs(4);
    auto group = fixture.model.add_tab_to_new_group(tabs[1]);
    fixture.model.add_tab_to_group(tabs[2], group);

    fixture.model.drop_tab(tabs[3], 3, group);
    EXPECT_EQ(fixture.group_of(tabs[3]), group);

    fixture.model.drop_tab(tabs[3], 3, {});
    EXPECT(!fixture.group_of(tabs[3]).has_value());

    fixture.model.drop_tab(tabs[0], 0, group);
    EXPECT_EQ(fixture.group_of(tabs[0]), group);
    EXPECT_EQ(fixture.group(group).tabs, (Vector<ViewId> { tabs[0], tabs[1], tabs[2] }));
}

TEST_CASE(a_restored_window_gets_its_saved_groups_back)
{
    GroupFixture fixture;
    fixture.saved_groups.append({ .title = "Saved"_string, .color = Xenon::TabGroupColor::Amber, .collapsed = true, .tabs = { 2, 3 } });
    fixture.saved_groups.append({ .title = "Pinned"_string, .color = Xenon::TabGroupColor::Jade, .collapsed = false, .tabs = { 1 } });
    WebView::RestoredSessionWindow restored {
        .window_id = 42,
        .tabs = {
            { .tab_id = 1, .url = URL::Parser::basic_parse("https://pinned.example/"sv).value(), .pinned = true },
            { .tab_id = 2, .url = URL::Parser::basic_parse("https://two.example/"sv).value(), .pinned = false },
            { .tab_id = 3, .url = URL::Parser::basic_parse("https://three.example/"sv).value(), .pinned = false },
            { .tab_id = 4, .url = URL::Parser::basic_parse("https://four.example/"sv).value(), .pinned = false },
        },
        .active_tab_index = 3,
        .geometry = {},
    };

    fixture.group_events.clear();
    auto window = fixture.model.restore_window(restored);
    auto tabs = fixture.tabs(window);
    auto const& groups = fixture.model.window(window)->groups;
    EXPECT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].title, "Saved"sv);
    EXPECT_EQ(groups[0].color, Xenon::TabGroupColor::Amber);
    EXPECT(groups[0].collapsed);
    EXPECT_EQ(groups[0].tabs, (Vector<ViewId> { tabs[1], tabs[2] }));
    EXPECT(fixture.group_events.contains_slow(ByteString::formatted("groups_changed {} 1", window)));
}

TEST_CASE(restoring_the_active_tabs_group_expands_it)
{
    GroupFixture fixture;
    fixture.saved_groups.append({ .title = {}, .color = Xenon::TabGroupColor::Slate, .collapsed = true, .tabs = { 2 } });
    WebView::RestoredSessionWindow restored {
        .window_id = 43,
        .tabs = {
            { .tab_id = 1, .url = URL::Parser::basic_parse("https://one.example/"sv).value(), .pinned = false },
            { .tab_id = 2, .url = URL::Parser::basic_parse("https://two.example/"sv).value(), .pinned = false },
        },
        .active_tab_index = 1,
        .geometry = {},
    };
    auto window = fixture.model.restore_window(restored);
    EXPECT(!fixture.model.window(window)->groups[0].collapsed);
}

TEST_CASE(tab_group_colours_have_qts_saved_names)
{
    EXPECT_EQ(Xenon::tab_group_color_name(Xenon::TabGroupColor::Orchid), "Orchid"sv);
    EXPECT_EQ(Xenon::tab_group_color_from_name("ember"sv), Xenon::TabGroupColor::Ember);
    EXPECT(!Xenon::tab_group_color_from_name("teal"sv).has_value());
}

TEST_CASE(every_new_or_restored_window_reports_that_it_opened_for_group_cleanup)
{
    Fixture fixture;
    size_t windows_opened = 0;
    fixture.model.set_tab_group_hooks({
        .new_tab_page_url = {},
        .window_opened = [&] { ++windows_opened; },
        .groups_changed = {},
        .edit_requested = {},
        .saved_groups = {},
    });

    fixture.model.open_window(WebView::IsPrivate::No);
    EXPECT_EQ(windows_opened, 1u);

    WebView::RestoredSessionWindow restored {
        .window_id = 44,
        .tabs = { { .tab_id = 1, .url = URL::Parser::basic_parse("https://one.example/"sv).value(), .pinned = false } },
        .active_tab_index = 0,
        .geometry = {},
    };
    fixture.model.restore_window(restored);
    EXPECT_EQ(windows_opened, 2u);
}

TEST_CASE(a_window_comes_back_with_the_geometry_it_was_saved_with)
{
    Fixture fixture;
    WebView::SessionWindowGeometry geometry { .x = 40, .y = 60, .width = 900, .height = 700, .maximized = true };
    WebView::RestoredSessionWindow restored {
        .window_id = 7,
        .tabs = { { .tab_id = 1, .url = URL::Parser::basic_parse("https://restored.example/"sv).value(), .pinned = false } },
        .active_tab_index = 0,
        .geometry = geometry,
    };
    auto window = fixture.model.restore_window(restored);
    EXPECT(fixture.model.window(window)->geometry == geometry);

    auto keep = fixture.model.open_window(WebView::IsPrivate::No);
    fixture.open(keep);
    EXPECT(!fixture.model.window(keep)->geometry.has_value());

    auto closing = fixture.model.open_window(WebView::IsPrivate::No);
    fixture.open(closing, "https://closing.example/"sv);
    WebView::SessionWindowGeometry closing_geometry { .x = 10, .y = 20, .width = 800, .height = 600, .maximized = false };
    fixture.store->window_geometry_changed(*fixture.model.window(closing)->session_window_id, closing_geometry);
    fixture.model.close_window(closing);

    EXPECT(fixture.model.reopen_closed_tab(keep));
    auto reopened = fixture.model.active_window().value();
    EXPECT_NE(reopened, keep);
    EXPECT(fixture.model.window(reopened)->geometry == closing_geometry);
}
