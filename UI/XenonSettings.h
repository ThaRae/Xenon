/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/HashTable.h>
#include <AK/String.h>
#include <AK/Time.h>
#include <AK/Types.h>

// Settings for Xenon's own features. Settings that Ladybird already has, like vertical tabs, stay in its settings store.
namespace Ladybird::XenonSettings {

// Where these settings are kept: a property list in ~/Library/Preferences on macOS, and a file in ~/.config elsewhere.
ByteString location();

enum class ChromeLayout : u8 {
    // The tab strip above the toolbar.
    Classic,
    // The toolbar and the tabs in a single row.
    Compact,
    // Tabs in a sidebar, with the toolbar above the page.
    Vertical,
};

ChromeLayout chrome_layout();
void set_chrome_layout(ChromeLayout);

// Whether horizontal tabs share their row with the toolbar. Vertical tabs leave this setting as it was, so switching
// back to horizontal tabs returns to the layout used before.
bool compact_toolbar();

// Whether Ctrl+Tab switches tabs in the order they were last used, like Helium's "Cycle through tabs in most recently
// used order", rather than in the tab strip's order. On by default.
bool cycle_tabs_in_recently_used_order();
void set_cycle_tabs_in_recently_used_order(bool);

// Whether background tabs left unused for the hibernation timeout give up their WebContent process, as Chromium's Memory
// Saver discards them (UI/TabHibernation.cpp). A hibernated tab loads again when it's selected.
bool tab_hibernation_enabled();
void set_tab_hibernation_enabled(bool);
AK::Duration tab_hibernation_timeout();
void set_tab_hibernation_timeout(AK::Duration);

// Whether holding a tab dragged over the left or right edge of a page opens it in a split view beside the page, like
// Helium's "Allow split view drag and drop". On by default.
bool split_view_drag_and_drop();
void set_split_view_drag_and_drop(bool);

// Whether the address bar resolves !bangs (UI/Bangs.h). On by default. With bangs off, "!w ladybird" is searched for as
// it's typed, like any other text.
bool bangs_enabled();
void set_bangs_enabled(bool);

// Whether Xenon updates itself (UI/Updater.h): it checks for a new version at launch and once a day, downloads it in the
// background, and installs it when Xenon quits. On by default. Off, it updates only when asked to, from the menu.
bool automatic_updates_enabled();
void set_automatic_updates_enabled(bool);

// The ids of the bundled filter lists (UI/Resources/filter-lists/manifest.json) turned off in the blocking dashboard,
// which UI/FilterLists.cpp leaves out of what it gives the content blocker. Every list is on until it's turned off.
HashTable<String> disabled_filter_lists();
void set_filter_list_enabled(StringView id, bool);

// Whether the toolbar's ad block button shows how many requests were blocked on the page, as uBlock Origin's icon badge
// does. Off by default.
bool show_blocked_request_count();
void set_show_blocked_request_count(bool);

// How many requests content blocking has blocked since Xenon was first run, as uBlock Origin counts "since install".
u64 blocked_request_total();
void add_to_blocked_request_total(u64);

// Told whenever one of these settings changes, wherever it was changed from, as WebView::SettingsObserver is for
// Ladybird's settings. The features follow about:settings through it, and about:settings follows the menus and the
// blocking dashboard. The blocked request total, which changes with nearly every page, isn't told.
class Observer {
public:
    Observer();
    virtual ~Observer();

    virtual void xenon_settings_changed() = 0;
};

}
