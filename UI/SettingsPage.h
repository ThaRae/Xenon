/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWebView/Forward.h>

// Xenon's about:settings (UI/Resources/about-pages/settings.*) shows Xenon's own settings beside Ladybird's. Ladybird's
// SettingsUI answers the page about Ladybird's settings, and this gives the page interfaces for Xenon's, all named
// "xenon/...": the layout, tab and split view behavior, bangs, filter list updates, extensions, Xenon's own updates, and
// where the profile keeps its data. The page hears "xenon/settings" back whenever any of them changes, from the page or from a menu.
namespace Ladybird::SettingsPage {

// Gives an about:settings page these interfaces, and about:welcome too, whose layout step uses them (see
// UI/WebUIInterfaces.cpp).
void register_interfaces(WebView::WebUI&);

}
