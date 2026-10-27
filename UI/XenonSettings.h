/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
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

// Told whenever one of these settings changes, wherever it was changed from, as WebView::SettingsObserver is for
// Ladybird's settings. The features follow about:settings through it, and about:settings follows the menus.
class Observer {
public:
    Observer();
    virtual ~Observer();

    virtual void xenon_settings_changed() = 0;
};

}
