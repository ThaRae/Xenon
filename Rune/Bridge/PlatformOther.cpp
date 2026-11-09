/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Rune/Bridge/Platform.h>

bool xenon_platform_is_fullscreen(void)
{
    return false;
}

void xenon_platform_set_fullscreen(bool)
{
}

bool xenon_platform_is_always_on_top(void)
{
    return false;
}

void xenon_platform_set_always_on_top(bool)
{
}
