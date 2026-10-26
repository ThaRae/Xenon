/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibMain/Main.h>

namespace Ladybird::FilterLists {

// Xenon ships uBlock Origin's filter lists, converted for Ladybird's content blocker by xenon-ublock (see
// UI/Resources/filter-lists, and Meta/update-filter-lists.py). This adds them to the browser's arguments as
// --content-blocker-list paths, which is how Ladybird keeps a list loaded across changes to the blocking settings
// without it being in its own catalog. They apply while content blocking is on.
Main::Arguments with_bundled_filter_lists(Main::Arguments);

}
