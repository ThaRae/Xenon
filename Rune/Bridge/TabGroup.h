/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Types.h>
#include <AK/Vector.h>
#include <LibWebView/SessionStore.h>
#include <Rune/Bridge/XenonBridge.h>

namespace Xenon {

using TabGroupId = u64;

enum class TabGroupColor : u8 {
    Rose,
    Sky,
    Amber,
    Jade,
    Orchid,
    Ember,
    Iris,
    Moss,
    Slate,
};
static constexpr size_t tab_group_color_count = 9;

StringView tab_group_color_name(TabGroupColor);
Optional<TabGroupColor> tab_group_color_from_name(StringView);

struct TabGroup {
    TabGroupId id { 0 };
    String title;
    TabGroupColor color { TabGroupColor::Rose };
    bool collapsed { false };
    size_t first { 0 };
    size_t last { 0 };
    Vector<XenonViewId> tabs;

    size_t tab_count() const { return tabs.size(); }
    bool has_same_state_as(TabGroup const& other) const
    {
        return id == other.id && title == other.title && color == other.color && collapsed == other.collapsed && tabs == other.tabs;
    }
};

struct SavedTabGroup {
    String title;
    TabGroupColor color { TabGroupColor::Rose };
    bool collapsed { false };
    Vector<WebView::SessionTabId> tabs;
};

}
