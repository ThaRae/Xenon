/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/EventLoop.h>
#include <LibWebView/Application.h>
#include <LibWebView/Menu.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/SplitViewHost.h>
#include <Rune/Bridge/XenonBridgeSplitView.h>

namespace Xenon {

static XenonSplitViewCallbacks s_split_view_callbacks {};

template<typename Callback, typename... Arguments>
static void notify_split_view(Callback XenonSplitViewCallbacks::* callback, Arguments... arguments)
{
    if (auto function = s_split_view_callbacks.*callback)
        function(s_split_view_callbacks.context, arguments...);
}

static URL::URL new_tab_page_url()
{
    return WebView::Application::settings().new_tab_page_url();
}

SplitViewHost::SplitViewHost(BrowserModel& model)
    : m_model(model)
    , m_splits(model, *this)
{
    report_drag_and_drop();
}

SplitViewHost::~SplitViewHost() = default;

void SplitViewHost::schedule_validation()
{
    if (m_validation_scheduled)
        return;
    m_validation_scheduled = true;
    Core::deferred_invoke([] {
        auto* host = Engine::the().split_views();
        if (!host)
            return;
        host->m_validation_scheduled = false;
        host->m_splits.validate();
    });
}

void SplitViewHost::splits_changed()
{
    Vector<XenonSplit> splits;
    for (auto const& split : m_splits.splits())
        splits.append({ .left = split.left, .right = split.right });
    notify_split_view(&XenonSplitViewCallbacks::splits_changed, static_cast<XenonSplit const*>(splits.data()), splits.size());

    for (auto window : m_model.windows_by_activation())
        Engine::the().sync_shown_tabs(window);
}

void SplitViewHost::xenon_settings_changed()
{
    report_drag_and_drop();
}

void SplitViewHost::report_drag_and_drop()
{
    auto enabled = Ladybird::XenonSettings::split_view_drag_and_drop();
    if (m_reported_drag_and_drop == enabled)
        return;
    m_reported_drag_and_drop = enabled;
    notify_split_view(&XenonSplitViewCallbacks::drag_and_drop_changed, enabled);
}

void SplitViewHost::open_new_tab_beside(ViewId view, SplitSide side)
{
    m_splits.open_new_tab_beside(view, side, new_tab_page_url());
}

void SplitViewHost::drop_tab(ViewId view, SplitSide side)
{
    if (!Ladybird::XenonSettings::split_view_drag_and_drop())
        return;
    m_splits.drop_tab(view, side, new_tab_page_url());
}

void SplitViewHost::open_new_split_view_in_active_window()
{
    auto active_window = m_model.active_window();
    auto const* window = active_window.has_value() ? m_model.window(*active_window) : nullptr;
    if (!window || !window->active_tab.has_value() || m_splits.partner(*window->active_tab).has_value())
        return;
    open_new_tab_beside(*window->active_tab, SplitSide::Right);
}

NonnullRefPtr<WebView::Menu> SplitViewHost::context_menu_for(ViewId view, WebView::Menu& menu)
{
    if (menu.title() != "Link Context Menu"sv || menu.items().is_empty())
        return menu;

    RefPtr<WebView::Action> open_in_new_tab;
    menu.for_each_action([&](WebView::Action& action) {
        if (!open_in_new_tab && action.id() == WebView::ActionID::OpenInNewTab)
            open_in_new_tab = action;
    });
    if (!open_in_new_tab)
        return menu;

    auto side = m_splits.side(view);
    auto text = !side.has_value() ? "Open Link in Split View"sv
        : side == SplitSide::Left ? "Open Link in Right View"sv
                                  : "Open Link in Left View"sv;
    auto action = WebView::Action::create(text, WebView::ActionID::OpenInNewTab, [view, open_in_new_tab = open_in_new_tab.release_nonnull()] {
        if (auto* host = Engine::the().split_views())
            host->open_link_in_split_view(view, *open_in_new_tab);
    });

    auto augmented = WebView::Menu::create(MUST(String::from_utf8(menu.title())));
    for (size_t i = 0; i < menu.items().size(); ++i) {
        menu.items()[i].visit(
            [&](NonnullRefPtr<WebView::Action>& item) { augmented->add_action(item); },
            [&](NonnullRefPtr<WebView::Menu>& submenu) { augmented->add_submenu(submenu); },
            [&](WebView::Separator) { augmented->add_separator(); });
        if (i == 0)
            augmented->add_action(action);
    }
    return augmented;
}

void SplitViewHost::open_link_in_split_view(ViewId view, WebView::Action& open_in_new_tab)
{
    auto const* window = m_model.window_of(view);
    if (!window)
        return;
    auto window_id = window->id;
    auto tabs_before = window->tabs;

    open_in_new_tab.activate();

    window = m_model.window(window_id);
    if (!window || !m_model.tab(view))
        return;
    Optional<ViewId> opened;
    for (auto tab : window->tabs) {
        if (!tabs_before.contains_slow(tab)) {
            opened = tab;
            break;
        }
    }
    if (!opened.has_value())
        return;

    auto side = m_splits.side(view);
    m_splits.create(view, *opened, side.has_value() ? opposite(*side) : SplitSide::Right, view);
}

}

using Xenon::Engine;
using Xenon::SplitSide;

static SplitSide split_side_from(uint32_t side)
{
    return side == XENON_SPLIT_SIDE_LEFT ? SplitSide::Left : SplitSide::Right;
}

static void post_to_split_views(Function<void(Xenon::SplitViewHost&)> command)
{
    Engine::the().post([command = move(command)] {
        if (auto* host = Engine::the().split_views())
            command(*host);
    });
}

void xenon_engine_set_split_view_callbacks(XenonSplitViewCallbacks const* callbacks)
{
    Xenon::s_split_view_callbacks = callbacks ? *callbacks : XenonSplitViewCallbacks {};
}

void xenon_engine_split_view_open_new_tab(XenonViewId beside, uint32_t new_tab_side)
{
    post_to_split_views([beside, side = split_side_from(new_tab_side)](auto& host) {
        host.open_new_tab_beside(beside, side);
    });
}

void xenon_engine_split_view_create(XenonViewId anchor, XenonViewId other, uint32_t other_side, XenonViewId activate)
{
    post_to_split_views([anchor, other, side = split_side_from(other_side), activate](auto& host) {
        host.splits().create(anchor, other, side, activate);
    });
}

void xenon_engine_split_view_reverse(XenonViewId view)
{
    post_to_split_views([view](auto& host) {
        host.splits().reverse(view);
    });
}

void xenon_engine_split_view_separate(XenonViewId view)
{
    post_to_split_views([view](auto& host) {
        (void)host.splits().separate(view);
    });
}

void xenon_engine_split_view_drop_tab(XenonViewId dragged, uint32_t side)
{
    post_to_split_views([dragged, side = split_side_from(side)](auto& host) {
        host.drop_tab(dragged, side);
    });
}
