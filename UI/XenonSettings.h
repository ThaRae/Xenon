/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Time.h>
#include <AK/Types.h>

// Settings for Xenon's own features. Settings that Ladybird already has, like vertical tabs, stay in its settings store.
namespace Ladybird::XenonSettings {

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

}
