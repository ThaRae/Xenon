/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <Rune/Bridge/XenonBridge.h>

namespace Xenon::ElementPicker {

void confirm_filter(XenonViewId, String const& filter);

}
