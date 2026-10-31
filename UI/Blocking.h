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

namespace Ladybird::Blocking {

URL::URL dashboard_url();

void count_blocked_requests(WebView::ViewImplementation const&, WebView::ContentBlockingSnapshot const&);

String site_switch_filter(BlockingSiteSwitch, StringView site);
bool site_switch_enabled(BlockingSiteSwitch, StringView site);
void set_site_switch(BlockingSiteSwitch, StringView site, bool enabled);

void start_zapper(WebView::ViewImplementation&);

void connect_popover(Tab&, BlockingPopover&);

}
