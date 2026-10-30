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

namespace Ladybird::XenonSettings {

ByteString location();

enum class ChromeLayout : u8 {
    Classic,
    Compact,
    Vertical,
};

ChromeLayout chrome_layout();
void set_chrome_layout(ChromeLayout);

bool compact_toolbar();

bool cycle_tabs_in_recently_used_order();
void set_cycle_tabs_in_recently_used_order(bool);

bool tab_hibernation_enabled();
void set_tab_hibernation_enabled(bool);
AK::Duration tab_hibernation_timeout();
void set_tab_hibernation_timeout(AK::Duration);
bool hibernate_on_memory_pressure();
void set_hibernate_on_memory_pressure(bool);

bool split_view_drag_and_drop();
void set_split_view_drag_and_drop(bool);

bool new_tab_page_shows_recently_closed();
void set_new_tab_page_shows_recently_closed(bool);

bool bangs_enabled();
void set_bangs_enabled(bool);

bool automatic_updates_enabled();
void set_automatic_updates_enabled(bool);

bool fingerprinting_protection();
void set_fingerprinting_protection(bool);

HashTable<String> disabled_filter_lists();
void set_filter_list_enabled(StringView id, bool);

bool show_blocked_request_count();
void set_show_blocked_request_count(bool);

u64 blocked_request_total();
void add_to_blocked_request_total(u64);

class Observer {
public:
    Observer();
    virtual ~Observer();

    virtual void xenon_settings_changed() = 0;
};

}
