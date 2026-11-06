/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <AK/WeakPtr.h>
#include <LibWebView/Forward.h>
#include <LibWebView/Menu.h>

namespace Xenon {

class AppMenus {
public:
    void show(u32 which);
    void activate_item(u64 menu, u32 item);
    void activate_action(u32 action);

private:
    WebView::Menu& main_menu();

    RefPtr<WebView::Menu> m_main_menu;
    Vector<WeakPtr<WebView::Action>> m_shown_actions;
    u64 m_shown_menu_id { 0 };
    u64 m_next_menu_id { 1 };
};

}
