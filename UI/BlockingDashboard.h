/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// The blocking dashboard, at xenon://blocking/, as uBlock Origin's dashboard: the filter lists (each can be turned off),
// my filters (the content blocker's custom filters), and trusted sites (the sites blocking is off for). The popover's
// dashboard button opens it, and so may any page, but no page may frame it.
//
// Its pages are in UI/Resources/blocking, served from the application's resources. They reach the UI process through the
// scheme's script bridge, as JSON messages: each change answers with the dashboard's whole state, and every open
// dashboard gets it again when the blocking settings change elsewhere.
namespace Ladybird::BlockingDashboard {

// Registers the xenon: scheme. Call it before the first renderer starts.
void register_url_scheme();

}
