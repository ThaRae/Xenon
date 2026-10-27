/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibMain/Main.h>

namespace Ladybird::FilterLists {

// Xenon ships uBlock Origin's filter lists, converted for Ladybird's content blocker by xenon-ublock (see
// UI/Resources/filter-lists, and Meta/update-filter-lists.py). This adds them to the browser's arguments as
// --content-blocker-list paths, which is how Ladybird keeps a list loaded across changes to the blocking settings
// without it being in its own catalog. They apply while content blocking is on.
//
// uBlock Origin's own lists go in as --trusted-content-blocker-list, which lets them use the scriptlets uBO reserves
// for its lists, and uBO's scriptlets and redirect surrogates as --content-blocker-resources. Both options come with
// the content blocker patches in Xenon's patch series (Patches/ladybird).
//
// Newer lists downloaded since (UI/FilterListUpdates.h) replace these once the browser has started.
Main::Arguments with_bundled_filter_lists(Main::Arguments);

// The lists and resources in a bundle's directory, in the order Ladybird gets them: lists/*.txt, trusted-lists/*.txt,
// and resources/ublock-resources.json.
struct ContentBlockers {
    Vector<ByteString> lists;
    Vector<ByteString> trusted_lists;
    Optional<ByteString> resources;
};
ContentBlockers content_blockers_in(StringView directory);

// The bundle that comes with Xenon, in the application's resources.
Optional<ByteString> bundled_lists_directory();

}
