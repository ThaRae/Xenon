/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Rune/Bridge/XenonBridgeWindows.h>

void xenon_platform_attach_window(uint32_t, uint32_t)
{
}

void xenon_platform_bind_window(uint32_t, XenonWindowId)
{
}

bool xenon_platform_window_is_maximized(uint32_t)
{
    return false;
}

bool xenon_platform_window_is_fullscreen(uint32_t)
{
    return false;
}

void xenon_platform_window_request(uint32_t, uint32_t)
{
}
