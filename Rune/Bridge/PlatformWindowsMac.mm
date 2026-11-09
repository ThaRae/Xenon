/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Rune/Bridge/XenonBridgeWindows.h>

#import <AppKit/AppKit.h>

static constexpr NSTimeInterval unexposed_time_before_hiding_pages = 1.0;

@interface XenonWindowWatcher : NSObject

@property (nonatomic, weak) NSWindow* window;
@property (nonatomic) XenonWindowId modelWindow;
@property (nonatomic) BOOL maximizeWhenAttached;
@property (nonatomic) BOOL hidesPages;
@property (nonatomic) BOOL hasReported;
@property (nonatomic, strong) NSTimer* unexposedTimer;
@property (nonatomic, strong) NSMutableArray* tokens;

- (void)watch:(NSWindow*)window;
- (void)update;

@end

@implementation XenonWindowWatcher

- (instancetype)init
{
    if (self = [super init])
        _tokens = [NSMutableArray array];
    return self;
}

- (void)stopWatching
{
    for (id token in self.tokens)
        [[NSNotificationCenter defaultCenter] removeObserver:token];
    [self.tokens removeAllObjects];
    [self.unexposedTimer invalidate];
    self.unexposedTimer = nil;
}

- (void)watch:(NSWindow*)window
{
    if (self.window == window)
        return;
    [self stopWatching];
    self.window = window;
    __weak XenonWindowWatcher* weak_self = self;
    for (NSNotificationName name in @[
             NSWindowDidMiniaturizeNotification,
             NSWindowDidDeminiaturizeNotification,
             NSWindowDidChangeOcclusionStateNotification,
         ]) {
        id token = [[NSNotificationCenter defaultCenter] addObserverForName:name
                                                                     object:window
                                                                      queue:[NSOperationQueue mainQueue]
                                                                 usingBlock:^(NSNotification*) {
                                                                     [weak_self update];
                                                                 }];
        [self.tokens addObject:token];
    }
    if (self.maximizeWhenAttached) {
        self.maximizeWhenAttached = NO;
        if (!window.isZoomed)
            [window zoom:nil];
    }
    [self update];
}

- (void)setPagesHidden:(BOOL)hidden
{
    if (self.hasReported && self.hidesPages == hidden)
        return;
    self.hidesPages = hidden;
    self.hasReported = YES;
    if (self.modelWindow != XENON_WINDOW_NONE)
        xenon_engine_set_window_exposed(self.modelWindow, !hidden);
}

- (void)update
{
    NSWindow* window = self.window;
    if (!window)
        return;
    if (window.isMiniaturized) {
        [self.unexposedTimer invalidate];
        self.unexposedTimer = nil;
        [self setPagesHidden:YES];
        return;
    }
    if (!window.isVisible || (window.occlusionState & NSWindowOcclusionStateVisible) != 0) {
        [self.unexposedTimer invalidate];
        self.unexposedTimer = nil;
        [self setPagesHidden:NO];
        return;
    }
    if (self.hidesPages || self.unexposedTimer)
        return;
    __weak XenonWindowWatcher* weak_self = self;
    self.unexposedTimer = [NSTimer scheduledTimerWithTimeInterval:unexposed_time_before_hiding_pages
                                                          repeats:NO
                                                            block:^(NSTimer*) {
                                                                XenonWindowWatcher* strong_self = weak_self;
                                                                strong_self.unexposedTimer = nil;
                                                                [strong_self setPagesHidden:YES];
                                                            }];
}

@end

@interface XenonQuitTarget : NSObject
- (void)quit:(id)sender;
@end

@implementation XenonQuitTarget

- (void)quit:(id)sender
{
    xenon_engine_request_quit();
}

@end

static void route_quit_through_the_engine()
{
    static XenonQuitTarget* target = [[XenonQuitTarget alloc] init];
    for (NSMenuItem* top in NSApp.mainMenu.itemArray) {
        for (NSMenuItem* item in top.submenu.itemArray) {
            if (item.action == @selector(terminate:)) {
                item.target = target;
                item.action = @selector(quit:);
            }
        }
    }
}

static NSMutableDictionary<NSNumber*, XenonWindowWatcher*>* watchers()
{
    static NSMutableDictionary<NSNumber*, XenonWindowWatcher*>* watchers = [NSMutableDictionary dictionary];
    return watchers;
}

static XenonWindowWatcher* watcher_for(uint32_t slot)
{
    auto* key = @(slot);
    auto* watcher = watchers()[key];
    if (!watcher) {
        watcher = [[XenonWindowWatcher alloc] init];
        watchers()[key] = watcher;
    }
    return watcher;
}

void xenon_platform_attach_window(uint32_t slot, uint32_t window_number)
{
    auto* window = [NSApp windowWithWindowNumber:window_number];
    if (!window)
        return;
    route_quit_through_the_engine();
    [watcher_for(slot) watch:window];
}

void xenon_platform_bind_window(uint32_t slot, XenonWindowId window)
{
    auto* watcher = watcher_for(slot);
    if (watcher.modelWindow == window)
        return;
    watcher.modelWindow = window;
    watcher.hasReported = NO;
    watcher.maximizeWhenAttached = NO;
    [watcher update];
}

bool xenon_platform_window_is_fullscreen(uint32_t slot)
{
    auto* window = watchers()[@(slot)].window;
    return window && (window.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

bool xenon_platform_window_is_maximized(uint32_t slot)
{
    auto* window = watchers()[@(slot)].window;
    return window && window.isZoomed && !xenon_platform_window_is_fullscreen(slot);
}

void xenon_platform_window_request(uint32_t slot, uint32_t request)
{
    auto* watcher = watcher_for(slot);
    auto* window = watcher.window;
    switch (request) {
    case XENON_WINDOW_FRAME_MAXIMIZE:
        if (!window) {
            watcher.maximizeWhenAttached = YES;
            return;
        }
        if (!window.isZoomed)
            [window zoom:nil];
        return;
    case XENON_WINDOW_FRAME_RESTORE:
        if (!window)
            return;
        if (window.isMiniaturized)
            [window deminiaturize:nil];
        else if (xenon_platform_window_is_fullscreen(slot))
            [window toggleFullScreen:nil];
        else if (window.isZoomed)
            [window zoom:nil];
        return;
    default:
        return;
    }
}
