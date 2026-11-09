/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <AK/QuickSort.h>
#include <AK/Vector.h>
#include <LibCore/Timer.h>
#include <LibWebView/Application.h>
#include <UI/XenonSettings.h>
#include <UI/XenonSettingsStore.h>

namespace Ladybird::XenonSettings {

static Store& store()
{
    static auto store = Store::open(Store::default_path(), [] {
        if (auto values = Store::import_qsettings_ini_file(Store::legacy_ini_path()); values.has_value())
            return values;
        return Store::import_qsettings_preferences(Store::legacy_preferences_application_id());
    });
    return store;
}

static constexpr auto DEFERRED_SAVE_DELAY = AK::Duration::from_seconds(5);

static void save()
{
    if (auto result = store().save(); result.is_error())
        warnln("Unable to save Xenon's settings to {}: {}", store().path(), result.error());
}

static Core::Timer& deferred_save_timer()
{
    static auto& timer = Core::Timer::create_single_shot(DEFERRED_SAVE_DELAY, [] { save(); }).leak_ref();
    return timer;
}

static void set_value(StringView key, JsonValue value)
{
    store().set(key, move(value));
    deferred_save_timer().stop();
    save();
}

static void set_value_soon(StringView key, JsonValue value)
{
    store().set(key, move(value));
    if (!deferred_save_timer().is_active())
        deferred_save_timer().start();
}

ByteString location()
{
    return store().path();
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

template<typename Callback>
static void notify_observers(Callback callback)
{
    auto observers_to_notify = observers();
    for (auto* observer : observers_to_notify) {
        if (observers().contains_slow(observer))
            callback(*observer);
    }
}

static void notify_observers()
{
    notify_observers([](Observer& observer) { observer.xenon_settings_changed(); });
}

bool compact_toolbar()
{
    return store().get_bool(Keys::compact_toolbar).value_or(false);
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
        set_value(Keys::compact_toolbar, layout == ChromeLayout::Compact);

    auto tab_settings = WebView::Application::settings().tab_settings();
    auto vertical_tabs_enabled = layout == ChromeLayout::Vertical;
    if (tab_settings.vertical_tabs_enabled != vertical_tabs_enabled) {
        tab_settings.vertical_tabs_enabled = vertical_tabs_enabled;
        WebView::Application::settings().set_tab_settings(tab_settings);
    }

    notify_observers([](Observer& observer) { observer.xenon_chrome_layout_changed(); });
    notify_observers();
}

static constexpr auto DEFAULT_TAB_HIBERNATION_TIMEOUT = AK::Duration::from_seconds(2 * 60 * 60);
static constexpr auto MINIMUM_TAB_HIBERNATION_TIMEOUT = AK::Duration::from_seconds(5);
static constexpr i64 TAB_HIBERNATION_TIMEOUT_CHOICES_IN_MINUTES[] = { 15, 30, 60, 2 * 60, 4 * 60, 6 * 60 };

bool tab_hibernation_enabled()
{
    return store().get_bool(Keys::tab_hibernation).value_or(true);
}

void set_tab_hibernation_enabled(bool enabled)
{
    set_value(Keys::tab_hibernation, enabled);
    notify_observers();
}

AK::Duration tab_hibernation_timeout()
{
    auto seconds = store().get_i64(Keys::tab_hibernation_timeout).value_or(DEFAULT_TAB_HIBERNATION_TIMEOUT.to_seconds());
    return max(AK::Duration::from_seconds(seconds), MINIMUM_TAB_HIBERNATION_TIMEOUT);
}

void set_tab_hibernation_timeout(AK::Duration timeout)
{
    set_value(Keys::tab_hibernation_timeout, timeout.to_seconds());
    notify_observers();
}

ReadonlySpan<i64> tab_hibernation_timeout_choices_in_minutes()
{
    return TAB_HIBERNATION_TIMEOUT_CHOICES_IN_MINUTES;
}

bool hibernate_on_memory_pressure()
{
    return store().get_bool(Keys::hibernate_on_memory_pressure).value_or(true);
}

void set_hibernate_on_memory_pressure(bool enabled)
{
    set_value(Keys::hibernate_on_memory_pressure, enabled);
    notify_observers();
}

bool framed_pages()
{
    return store().get_bool(Keys::framed_pages).value_or(true);
}

void set_framed_pages(bool framed)
{
    set_value(Keys::framed_pages, framed);
    notify_observers();
}

bool cycle_tabs_in_recently_used_order()
{
    return store().get_bool(Keys::cycle_tabs_in_recently_used_order).value_or(true);
}

void set_cycle_tabs_in_recently_used_order(bool enabled)
{
    set_value(Keys::cycle_tabs_in_recently_used_order, enabled);
    notify_observers();
}

bool split_view_drag_and_drop()
{
    return store().get_bool(Keys::split_view_drag_and_drop).value_or(true);
}

void set_split_view_drag_and_drop(bool enabled)
{
    set_value(Keys::split_view_drag_and_drop, enabled);
    notify_observers();
}

bool new_tab_page_shows_recently_closed()
{
    return store().get_bool(Keys::new_tab_page_recently_closed).value_or(true);
}

void set_new_tab_page_shows_recently_closed(bool shown)
{
    set_value(Keys::new_tab_page_recently_closed, shown);
    notify_observers();
}

bool bangs_enabled()
{
    return store().get_bool(Keys::bangs).value_or(true);
}

void set_bangs_enabled(bool enabled)
{
    set_value(Keys::bangs, enabled);
    notify_observers();
}

bool fingerprinting_protection()
{
    return WebView::Application::settings().fingerprinting_protection();
}

void set_fingerprinting_protection(bool enabled)
{
    WebView::Application::settings().set_fingerprinting_protection(enabled);
    notify_observers();
}

bool automatic_updates_enabled()
{
    return store().get_bool(Keys::automatic_updates).value_or(true);
}

void set_automatic_updates_enabled(bool enabled)
{
    set_value(Keys::automatic_updates, enabled);
    notify_observers();
}

HashTable<String> disabled_filter_lists()
{
    HashTable<String> ids;
    for (auto& id : store().get_strings(Keys::disabled_filter_lists))
        ids.set(move(id));
    return ids;
}

void set_filter_list_enabled(StringView id, bool enabled)
{
    auto ids = store().get_strings(Keys::disabled_filter_lists);
    ids.remove_all_matching([&](auto const& existing) { return existing == id; });
    if (!enabled)
        ids.append(MUST(String::from_utf8(id)));
    quick_sort(ids);

    JsonArray array;
    for (auto& existing : ids)
        array.must_append(move(existing));
    set_value(Keys::disabled_filter_lists, move(array));
    notify_observers();
}

bool show_blocked_request_count()
{
    return store().get_bool(Keys::show_blocked_request_count).value_or(false);
}

void set_show_blocked_request_count(bool show)
{
    set_value(Keys::show_blocked_request_count, show);
    notify_observers();
}

u64 blocked_request_total()
{
    return store().get_u64(Keys::blocked_request_total).value_or(0);
}

void add_to_blocked_request_total(u64 count)
{
    if (count == 0)
        return;
    set_value_soon(Keys::blocked_request_total, blocked_request_total() + count);
}

}
