/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TypeCasts.h>
#include <LibWebView/Application.h>
#include <UI/BrowserWindow.h>
#include <UI/XenonSettings.h>

#include <QApplication>
#include <QSettings>

namespace Ladybird::XenonSettings {

static constexpr auto COMPACT_TOOLBAR_KEY = "layout/compactToolbar";

static QSettings& settings()
{
    static QSettings settings { QStringLiteral("Xenon"), QStringLiteral("Xenon") };
    return settings;
}

bool compact_toolbar()
{
    return settings().value(COMPACT_TOOLBAR_KEY, false).toBool();
}

ChromeLayout chrome_layout()
{
    if (WebView::Application::settings().tab_settings().vertical_tabs_enabled)
        return ChromeLayout::Vertical;
    return compact_toolbar() ? ChromeLayout::Compact : ChromeLayout::Classic;
}

void set_chrome_layout(ChromeLayout layout)
{
    if (layout != ChromeLayout::Vertical)
        settings().setValue(COMPACT_TOOLBAR_KEY, layout == ChromeLayout::Compact);

    auto tab_settings = WebView::Application::settings().tab_settings();
    auto vertical_tabs_enabled = layout == ChromeLayout::Vertical;
    if (tab_settings.vertical_tabs_enabled != vertical_tabs_enabled) {
        tab_settings.vertical_tabs_enabled = vertical_tabs_enabled;
        WebView::Application::settings().set_tab_settings(tab_settings);
    }

    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* window = as_if<BrowserWindow>(widget))
            window->update_tabs_display();
    }
}

}
