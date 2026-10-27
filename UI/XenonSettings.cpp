/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TypeCasts.h>
#include <AK/Vector.h>
#include <LibWebView/Application.h>
#include <UI/BrowserWindow.h>
#include <UI/StringUtils.h>
#include <UI/XenonSettings.h>

#include <QApplication>
#include <QSettings>

namespace Ladybird::XenonSettings {

static constexpr auto COMPACT_TOOLBAR_KEY = "layout/compactToolbar";
static constexpr auto RECENTLY_USED_TAB_ORDER_KEY = "tabs/cycleInRecentlyUsedOrder";

static QSettings& settings()
{
    static QSettings settings { QStringLiteral("Xenon"), QStringLiteral("Xenon") };
    return settings;
}

ByteString location()
{
    return ak_byte_string_from_qstring(settings().fileName());
}

static Vector<Observer*>& observers()
{
    static Vector<Observer*> observers;
    return observers;
}

Observer::Observer()
{
    observers().append(this);
}

Observer::~Observer()
{
    observers().remove_first_matching([this](auto* observer) { return observer == this; });
}

static void notify_observers()
{
    // NB: An observer may come or go while it's told.
    auto observers_to_notify = observers();
    for (auto* observer : observers_to_notify) {
        if (observers().contains_slow(observer))
            observer->xenon_settings_changed();
    }
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
    notify_observers();
}

static constexpr auto TAB_HIBERNATION_KEY = "tabs/hibernation";
// In seconds.
static constexpr auto TAB_HIBERNATION_TIMEOUT_KEY = "tabs/hibernationTimeout";

// Chromium's Memory Saver discards a tab after 2 hours at its most aggressive. Xenon goes by time alone, without Memory
// Saver's other trigger, memory pressure, so it takes that timing.
static constexpr auto DEFAULT_TAB_HIBERNATION_TIMEOUT = AK::Duration::from_seconds(2 * 60 * 60);
static constexpr auto MINIMUM_TAB_HIBERNATION_TIMEOUT = AK::Duration::from_seconds(5);

bool tab_hibernation_enabled()
{
    return settings().value(TAB_HIBERNATION_KEY, true).toBool();
}

void set_tab_hibernation_enabled(bool enabled)
{
    settings().setValue(TAB_HIBERNATION_KEY, enabled);
    notify_observers();
}

AK::Duration tab_hibernation_timeout()
{
    auto seconds = settings().value(TAB_HIBERNATION_TIMEOUT_KEY, static_cast<qlonglong>(DEFAULT_TAB_HIBERNATION_TIMEOUT.to_seconds())).toLongLong();
    return max(AK::Duration::from_seconds(seconds), MINIMUM_TAB_HIBERNATION_TIMEOUT);
}

void set_tab_hibernation_timeout(AK::Duration timeout)
{
    settings().setValue(TAB_HIBERNATION_TIMEOUT_KEY, static_cast<qlonglong>(timeout.to_seconds()));
    notify_observers();
}

bool cycle_tabs_in_recently_used_order()
{
    return settings().value(RECENTLY_USED_TAB_ORDER_KEY, true).toBool();
}

void set_cycle_tabs_in_recently_used_order(bool enabled)
{
    settings().setValue(RECENTLY_USED_TAB_ORDER_KEY, enabled);
    notify_observers();
}

static constexpr auto SPLIT_VIEW_DRAG_AND_DROP_KEY = "splitView/dragAndDrop";

bool split_view_drag_and_drop()
{
    return settings().value(SPLIT_VIEW_DRAG_AND_DROP_KEY, true).toBool();
}

void set_split_view_drag_and_drop(bool enabled)
{
    settings().setValue(SPLIT_VIEW_DRAG_AND_DROP_KEY, enabled);
    notify_observers();
}

static constexpr auto BANGS_KEY = "search/bangs";

bool bangs_enabled()
{
    return settings().value(BANGS_KEY, true).toBool();
}

void set_bangs_enabled(bool enabled)
{
    settings().setValue(BANGS_KEY, enabled);
    notify_observers();
}

static constexpr auto DISABLED_FILTER_LISTS_KEY = "blocking/disabledFilterLists";
static constexpr auto BLOCKED_REQUEST_TOTAL_KEY = "blocking/blockedRequestTotal";

HashTable<String> disabled_filter_lists()
{
    HashTable<String> ids;
    for (auto const& id : settings().value(DISABLED_FILTER_LISTS_KEY).toStringList())
        ids.set(ak_string_from_qstring(id));
    return ids;
}

void set_filter_list_enabled(StringView id, bool enabled)
{
    auto ids = settings().value(DISABLED_FILTER_LISTS_KEY).toStringList();
    auto qid = qstring_from_ak_string(id);
    ids.removeAll(qid);
    if (!enabled)
        ids.append(qid);
    ids.sort();
    settings().setValue(DISABLED_FILTER_LISTS_KEY, ids);
}

u64 blocked_request_total()
{
    return settings().value(BLOCKED_REQUEST_TOTAL_KEY, 0).toULongLong();
}

void add_to_blocked_request_total(u64 count)
{
    if (count == 0)
        return;
    settings().setValue(BLOCKED_REQUEST_TOTAL_KEY, static_cast<qulonglong>(blocked_request_total() + count));
}

}
