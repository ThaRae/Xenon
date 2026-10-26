/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

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

}
