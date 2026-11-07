/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Vector.h>
#include <LibTest/TestCase.h>
#include <LibURL/Parser.h>
#include <LibWebView/SessionStore.h>
#include <Rune/Bridge/BrowserModel.h>
#include <Rune/Bridge/SplitViews.h>

using Xenon::BrowserModel;
using Xenon::SplitSide;
using Xenon::SplitViews;
using Xenon::ViewId;
using Xenon::WindowId;

struct ChangeCounter final : public SplitViews::Observer {
    size_t changes { 0 };
    virtual void splits_changed() override { ++changes; }
};

struct Fixture {
    BrowserModel::Observer model_observer;
    ChangeCounter counter;
    NonnullOwnPtr<WebView::SessionStore> store { WebView::SessionStore::create() };
    ViewId next_view { 1 };
    WindowId next_window { 100 };
    BrowserModel model {
        model_observer,
        BrowserModel::Allocators {
            .view_id = [this] { return next_view++; },
            .window_id = [this] { return next_window++; },
        },
        [this](WebView::IsPrivate) { return store.ptr(); },
    };
    SplitViews splits { model, counter };
    URL::URL new_tab_url { URL::Parser::basic_parse("about:newtab"sv).value() };

    WindowId window { model.open_window(WebView::IsPrivate::No) };

    ViewId open(Web::HTML::ActivateTab activate = Web::HTML::ActivateTab::No)
    {
        return model.open_tab(window, { .url = URL::Parser::basic_parse("https://example.com/"sv).value(), .activate = activate });
    }

    Vector<ViewId> tabs() const { return model.window(window)->tabs; }
    Optional<ViewId> active() const { return model.window(window)->active_tab; }

    Vector<ViewId> split_pairs() const
    {
        Vector<ViewId> pairs;
        for (auto const& split : splits.splits()) {
            pairs.append(split.left);
            pairs.append(split.right);
        }
        return pairs;
    }
};

TEST_CASE(a_new_split_view_opens_a_tab_beside_the_current_one_and_activates_it)
{
    Fixture fixture;
    auto a = fixture.open();
    auto b = fixture.open();
    auto opened = fixture.splits.open_new_tab_beside(a, SplitSide::Right, fixture.new_tab_url);

    EXPECT_EQ(fixture.tabs(), (Vector<ViewId> { a, opened, b }));
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { a, opened }));
    EXPECT_EQ(fixture.active(), opened);
    EXPECT_EQ(fixture.splits.partner(a), opened);
    EXPECT(fixture.splits.side(opened) == SplitSide::Right);
    EXPECT(fixture.splits.is_shown(a));
    EXPECT(!fixture.splits.is_shown(b));
}

TEST_CASE(splitting_with_the_current_tab_moves_the_other_tab_beside_it)
{
    Fixture fixture;
    auto a = fixture.open();
    auto b = fixture.open();
    auto c = fixture.open();
    fixture.model.activate_tab(c);
    fixture.splits.create(c, a, SplitSide::Right, c);

    EXPECT_EQ(fixture.tabs(), (Vector<ViewId> { b, c, a }));
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { c, a }));
    EXPECT_EQ(fixture.active(), c);
}

TEST_CASE(a_tab_split_with_a_pinned_tab_is_pinned_too)
{
    Fixture fixture;
    auto pinned = fixture.open();
    auto other = fixture.open();
    fixture.model.set_tab_pinned(pinned, true);
    fixture.splits.create(pinned, other, SplitSide::Right, pinned);

    EXPECT(fixture.model.tab(other)->pinned);
    EXPECT_EQ(fixture.tabs(), (Vector<ViewId> { pinned, other }));
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { pinned, other }));
}

TEST_CASE(reversing_swaps_the_tabs_and_their_sides)
{
    Fixture fixture;
    auto a = fixture.open();
    auto b = fixture.open();
    auto c = fixture.open();
    fixture.splits.create(a, b, SplitSide::Right, a);
    fixture.splits.reverse(a);

    EXPECT_EQ(fixture.tabs(), (Vector<ViewId> { b, a, c }));
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { b, a }));
    EXPECT(fixture.splits.side(a) == SplitSide::Right);
}

TEST_CASE(moving_one_tab_of_a_split_brings_its_partner)
{
    Fixture fixture;
    auto a = fixture.open();
    auto b = fixture.open();
    auto c = fixture.open();
    auto d = fixture.open();
    fixture.splits.create(a, b, SplitSide::Right, a);
    fixture.splits.move_tab(b, 3);

    EXPECT_EQ(fixture.tabs(), (Vector<ViewId> { c, d, a, b }));
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { a, b }));
}

TEST_CASE(a_tab_moved_between_a_split_passes_over_it)
{
    Fixture fixture;
    auto a = fixture.open();
    auto b = fixture.open();
    auto c = fixture.open();
    fixture.splits.create(b, c, SplitSide::Right, b);
    fixture.splits.move_tab(a, 1);

    EXPECT_EQ(fixture.tabs(), (Vector<ViewId> { b, c, a }));
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { b, c }));
}

TEST_CASE(closing_a_tab_of_a_split_leaves_its_partner_as_a_normal_tab)
{
    Fixture fixture;
    auto a = fixture.open();
    auto b = fixture.open();
    fixture.open();
    fixture.splits.create(a, b, SplitSide::Right, a);
    fixture.model.close_tab(b);

    EXPECT(fixture.splits.validate());
    EXPECT(fixture.splits.splits().is_empty());
    EXPECT(!fixture.splits.partner(a).has_value());
}

TEST_CASE(unpinning_one_tab_of_a_split_separates_it)
{
    Fixture fixture;
    auto a = fixture.open();
    auto b = fixture.open();
    fixture.splits.create(a, b, SplitSide::Right, a);
    fixture.model.set_tab_pinned(b, true);

    EXPECT(fixture.splits.validate());
    EXPECT(fixture.splits.splits().is_empty());
}

TEST_CASE(separating_keeps_both_tabs)
{
    Fixture fixture;
    auto a = fixture.open();
    auto b = fixture.open();
    fixture.splits.create(a, b, SplitSide::Left, b);
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { b, a }));

    auto changes = fixture.counter.changes;
    EXPECT_EQ(fixture.splits.separate(a), b);
    EXPECT_EQ(fixture.counter.changes, changes + 1);
    EXPECT_EQ(fixture.tabs(), (Vector<ViewId> { b, a }));
    EXPECT(!fixture.splits.validate());
}

TEST_CASE(dropping_a_tab_on_a_page_edge_splits_it_with_the_current_tab)
{
    Fixture fixture;
    auto a = fixture.open();
    auto b = fixture.open();
    auto c = fixture.open();
    fixture.model.activate_tab(a);
    fixture.splits.drop_tab(c, SplitSide::Left, fixture.new_tab_url);

    EXPECT_EQ(fixture.tabs(), (Vector<ViewId> { c, a, b }));
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { c, a }));
    EXPECT_EQ(fixture.active(), c);

    EXPECT(!fixture.splits.can_drop_beside_active_tab(b));
    fixture.splits.drop_tab(b, SplitSide::Right, fixture.new_tab_url);
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { c, a }));

    fixture.splits.drop_tab(c, SplitSide::Right, fixture.new_tab_url);
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { a, c }));
    EXPECT_EQ(fixture.tabs(), (Vector<ViewId> { a, c, b }));
}

TEST_CASE(dropping_the_current_tab_opens_a_new_tab_on_the_other_side)
{
    Fixture fixture;
    auto a = fixture.open();
    fixture.splits.drop_tab(a, SplitSide::Right, fixture.new_tab_url);

    EXPECT_EQ(fixture.tabs().size(), 2u);
    auto opened = fixture.tabs()[0];
    EXPECT_EQ(fixture.split_pairs(), (Vector<ViewId> { opened, a }));
    EXPECT_EQ(fixture.active(), opened);
}

TEST_CASE(a_tab_opened_between_a_split_breaks_it)
{
    Fixture fixture;
    auto a = fixture.open();
    auto b = fixture.open();
    fixture.splits.create(a, b, SplitSide::Right, a);
    fixture.model.open_tab(fixture.window, { .url = fixture.new_tab_url, .opener = a });

    EXPECT(fixture.splits.validate());
    EXPECT(fixture.splits.splits().is_empty());
}
