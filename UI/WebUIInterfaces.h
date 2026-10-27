/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

namespace Ladybird {

// Gives Ladybird's WebUI pages (about:settings, about:welcome, ...) the messages of Xenon's own features, beside the
// engine's. Call it once, before any page opens.
void install_web_ui_interfaces();

}
