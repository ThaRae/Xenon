/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <LibWebView/Forward.h>

namespace Ladybird::SettingsPage {

struct Host {
    Function<void(ByteString const& directory)> open_directory;
    Function<void(ByteString const& file)> reveal_file;
    Function<void()> show_extensions;
    Function<void()> install_update;
};

void set_host(Host);

void application_did_become_active();

void register_interfaces(WebView::WebUI&);

}
