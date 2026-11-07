/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/Application.h>
#include <LibWebView/ViewImplementation.h>
#include <Rune/Bridge/AppMenus.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/ExtensionsHost.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgePage.h>

namespace Xenon {

static void run_ui_command(u32 command)
{
    Engine::the().notify(&XenonUiCallbacks::ui_command, command);
}

WebView::Menu& AppMenus::main_menu()
{
    if (m_main_menu)
        return *m_main_menu;

    auto& application = WebView::Application::the();
    auto& engine = Engine::the();
    m_main_menu = WebView::Menu::create("Xenon"sv);
    auto& menu = *m_main_menu;

    menu.add_action(WebView::Action::create("New Tab"sv, WebView::ActionID::OpenInNewTab, [&engine] {
        engine.new_tab_in_active_window();
    }));
    menu.add_action(WebView::Action::create("New Window"sv, WebView::ActionID::OpenInNewWindow, [&engine] {
        engine.new_window(engine.allocate_window_id(), WebView::IsPrivate::No);
    }));
    menu.add_action(WebView::Action::create("New Private Window"sv, WebView::ActionID::OpenInNewPrivateWindow, [&engine] {
        engine.new_window(engine.allocate_window_id(), WebView::IsPrivate::Yes);
    }));
    menu.add_action(WebView::Action::create("Reopen Closed Tab"sv, WebView::ActionID::OpenInNewTab, [&engine] {
        engine.reopen_closed_tab_in_active_window();
    }));
    menu.add_separator();

    menu.add_submenu(application.history_menu());
    menu.add_submenu(application.bookmarks_menu());
    menu.add_action(application.open_downloads_page_action());
    menu.add_separator();

    menu.add_submenu(application.zoom_menu());
    menu.add_separator();

    menu.add_action(WebView::Action::create("Find in Page…"sv, WebView::ActionID::SearchSelectedText, [] {
        run_ui_command(XENON_UI_COMMAND_FIND);
    }));
    menu.add_separator();

    menu.add_action(application.open_settings_page_action());
    menu.add_action(WebView::Action::create("Extensions"sv, WebView::ActionID::OpenSettingsPage, [] {
        ExtensionsHost::show_extensions();
    }));
    menu.add_submenu(application.inspect_menu());
    menu.add_submenu(application.debug_menu());
    menu.add_separator();

    auto help = WebView::Menu::create("Help"sv);
    help->add_action(WebView::Action::create("About Xenon"sv, WebView::ActionID::OpenAboutPage, [] {
        WebView::Application::the().open_about_page_action().activate();
    }));
    help->add_action(application.report_website_issue_action());
    menu.add_submenu(move(help));
    menu.add_action(WebView::Action::create("Quit Xenon"sv, WebView::ActionID::OpenAboutPage, [] {
        run_ui_command(XENON_UI_COMMAND_QUIT);
    }));
    return menu;
}

void AppMenus::show(u32 which)
{
    if (which != XENON_APP_MENU_MAIN)
        return;

    m_shown_actions.clear();
    Vector<XenonMenuItem> items;
    flatten_menu(main_menu(), items, m_shown_actions);
    drop_trailing_separator(items);

    m_shown_menu_id = m_next_menu_id++;
    XenonMenu shown_menu {
        .id = m_shown_menu_id,
        .x = 0,
        .y = 0,
        .minimum_width = 0,
        .items = items.data(),
        .item_count = items.size(),
    };
    Engine::the().notify(&XenonUiCallbacks::app_menu, which, static_cast<XenonMenu const*>(&shown_menu));
}

void AppMenus::activate_item(u64 menu, u32 item)
{
    if (menu == 0 || menu != m_shown_menu_id || item >= m_shown_actions.size())
        return;
    auto action = m_shown_actions[item].strong_ref();
    if (!action || !action->enabled())
        return;
    if (action->is_checkable())
        action->set_checked(!action->checked());
    action->activate();
}

void AppMenus::activate_action(u32 id)
{
    auto& application = WebView::Application::the();
    auto activate = [](WebView::Action& action) {
        if (!action.enabled())
            return;
        if (action.is_checkable())
            action.set_checked(!action.checked());
        action.activate();
    };
    auto activate_in_menu = [&](WebView::Menu& menu, WebView::ActionID action_id) {
        menu.for_each_action([&](WebView::Action& action) {
            if (action.id() == action_id)
                activate(action);
        });
    };

    switch (id) {
    case XENON_ACTION_ZOOM_IN:
        activate(application.zoom_in_action());
        break;
    case XENON_ACTION_ZOOM_OUT:
        activate(application.zoom_out_action());
        break;
    case XENON_ACTION_RESET_ZOOM:
        if (auto view = application.active_web_view(); view.has_value())
            view->reset_zoom();
        break;
    case XENON_ACTION_RELOAD_BYPASSING_CACHE:
        activate(application.reload_bypassing_cache_action());
        break;
    case XENON_ACTION_VIEW_SOURCE:
        activate(application.view_source_action());
        break;
    case XENON_ACTION_TOGGLE_DEVTOOLS:
        activate_in_menu(application.inspect_menu(), WebView::ActionID::ToggleDevTools);
        break;
    case XENON_ACTION_OPEN_SETTINGS:
        activate(application.open_settings_page_action());
        break;
    case XENON_ACTION_OPEN_ABOUT:
        activate(application.open_about_page_action());
        break;
    case XENON_ACTION_OPEN_DOWNLOADS:
        activate(application.open_downloads_page_action());
        break;
    case XENON_ACTION_VIEW_HISTORY:
        activate_in_menu(application.history_menu(), WebView::ActionID::ViewHistory);
        break;
    case XENON_ACTION_CLEAR_BROWSING_DATA:
        activate_in_menu(application.history_menu(), WebView::ActionID::ClearBrowsingData);
        break;
    case XENON_ACTION_TOGGLE_BOOKMARK:
        if (auto view = application.active_web_view(); view.has_value())
            activate(view->toggle_bookmark_action());
        break;
    case XENON_ACTION_MANAGE_BOOKMARKS:
        activate_in_menu(application.bookmarks_menu(), WebView::ActionID::ManageBookmarks);
        break;
    case XENON_ACTION_BOOKMARK_ALL_TABS:
        activate_in_menu(application.bookmarks_menu(), WebView::ActionID::AddBookmarkAllTabs);
        break;
    case XENON_ACTION_TOGGLE_BOOKMARKS_BAR:
        activate(application.toggle_bookmark_bar_action());
        break;
    case XENON_ACTION_REPORT_WEBSITE_ISSUE:
        activate(application.report_website_issue_action());
        break;
    case XENON_ACTION_FORCE_DARK:
        activate(application.force_dark_action());
        break;
    default:
        break;
    }
}

}
