/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// Ladybird's session store writes what the tabs show to disk every 30 seconds, and when the browser quits. A crash lost
// whatever changed since then: a tab opened, reopened, or duplicated in that time was saved as about:blank, and came back
// blank, as did the windows reopened after the crash, and so on after every crash that followed. So Xenon has the store
// write a change within a second.
namespace Ladybird::SessionSaving {

// Has the store write what changed soon, along with whatever else changes before then.
void save_soon();

}
