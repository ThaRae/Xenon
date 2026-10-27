/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/StringView.h>
#include <LibURL/URL.h>
#include <LibWebView/ContentBlocking.h>
#include <LibWebView/Forward.h>
#include <UI/BlockingPopover.h>

namespace Ladybird {

class Tab;

}

// What Xenon adds to Ladybird's content blocking to work like uBlock Origin: the popover's counts, domains, site
// switches, picker and zapper (UI/BlockingPopover.h), and the dashboard (UI/BlockingDashboard.h).
namespace Ladybird::Blocking {

// The blocking dashboard: filter lists, my filters, and trusted sites.
URL::URL dashboard_url();

// Adds what a view's pages block to the count since Xenon was first run. Call it with every snapshot the view reports.
void count_blocked_requests(WebView::ViewImplementation const&, WebView::ContentBlockingSnapshot const&);

// A site switch is a filter in the custom filters: uBlock Origin's "no pop-ups" for a site is *$popup,domain=site, and
// its "no remote fonts" is *$font,domain=site. The engine patches apply both; turning a switch off removes its filter.
String site_switch_filter(BlockingSiteSwitch, StringView site);
bool site_switch_enabled(BlockingSiteSwitch, StringView site);
void set_site_switch(BlockingSiteSwitch, StringView site, bool enabled);

// uBlock Origin's zapper: the next element the user clicks goes from the page until it reloads, without a filter.
void start_zapper(WebView::ViewImplementation&);

// Fills in the popover's counts, domains and switches for its tab, and acts on its controls.
void connect_popover(Tab&, BlockingPopover&);

}
