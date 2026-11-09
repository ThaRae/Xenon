/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Rune/Bridge/XenonBridgeAccessibility.h>

#import <AppKit/AppKit.h>

bool xenon_accessibility_assistive_technology_running(void)
{
    auto* workspace = NSWorkspace.sharedWorkspace;
    return workspace.voiceOverEnabled || workspace.switchControlEnabled;
}
