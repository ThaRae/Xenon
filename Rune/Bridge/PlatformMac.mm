/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Rune/Bridge/Platform.h>

#import <AppKit/AppKit.h>

static NSWindow* browser_window()
{
    if (auto* window = NSApp.keyWindow)
        return window;
    return NSApp.mainWindow;
}

bool xenon_platform_is_fullscreen(void)
{
    auto* window = browser_window();
    return window && (window.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

void xenon_platform_set_fullscreen(bool fullscreen)
{
    auto* window = browser_window();
    if (!window || xenon_platform_is_fullscreen() == fullscreen)
        return;
    [window toggleFullScreen:nil];
}

bool xenon_platform_is_always_on_top(void)
{
    auto* window = browser_window();
    return window && window.level == NSFloatingWindowLevel;
}

void xenon_platform_set_always_on_top(bool always_on_top)
{
    if (auto* window = browser_window())
        window.level = always_on_top ? NSFloatingWindowLevel : NSNormalWindowLevel;
}
