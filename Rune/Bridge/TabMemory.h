/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/Types.h>

namespace WebView {

class ViewImplementation;

}

namespace Xenon::TabMemory {

struct Usage {
    u64 bytes { 0 };
    bool is_estimate { false };
};

Usage usage(WebView::ViewImplementation const&);

ByteString description(WebView::ViewImplementation const*);
ByteString hibernated_description(Optional<u64> memory_freed);

}
