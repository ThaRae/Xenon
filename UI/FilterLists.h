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

Main::Arguments with_bundled_filter_lists(Main::Arguments);

struct ContentBlockers {
    Vector<ByteString> lists;
    Vector<ByteString> trusted_lists;
    Optional<ByteString> resources;
};
ContentBlockers content_blockers_in(StringView directory);

Optional<ByteString> bundled_lists_directory();

}
