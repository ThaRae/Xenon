/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>

namespace WebView {

class ViewImplementation;

}

namespace Xenon::TabMemory {

ByteString description(WebView::ViewImplementation const*);

}
