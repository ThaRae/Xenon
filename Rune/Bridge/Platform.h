/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called by the UI on the main thread. Each acts on the key window, and does nothing where the platform has no
// implementation yet.
bool xenon_platform_is_fullscreen(void);
void xenon_platform_set_fullscreen(bool);
bool xenon_platform_is_always_on_top(void);
void xenon_platform_set_always_on_top(bool);

#ifdef __cplusplus
}
#endif
