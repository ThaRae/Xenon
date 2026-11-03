/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Function.h>
#include <UI/DefaultBrowser.h>

#import <Cocoa/Cocoa.h>

namespace Ladybird::DefaultBrowser {

static NSURL* browser_url()
{
    return [NSURL URLWithString:@"https://xenon"];
}

Status status()
{
    auto* handler = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenURL:browser_url()];
    auto* handler_identifier = [[NSBundle bundleWithURL:handler] bundleIdentifier];
    if (!handler_identifier)
        return Status::IsNotDefault;
    return [handler_identifier isEqualToString:[[NSBundle mainBundle] bundleIdentifier]] ? Status::IsDefault
                                                                                         : Status::IsNotDefault;
}

static void make_default_for_scheme(NSString* scheme, NSURL* bundle_url, Function<void()> const& on_done)
{
    [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:bundle_url
                                         toOpenURLsWithScheme:scheme
                                            completionHandler:^(NSError* error) {
                                                if (error)
                                                    NSLog(@"Failed to make Xenon the default handler for %@: %@", scheme, error);
                                                if (on_done)
                                                    dispatch_async(dispatch_get_main_queue(), [on_done] { on_done(); });
                                            }];
}

void request_default(Function<void()> on_done)
{
    auto* bundle_url = [[NSBundle mainBundle] bundleURL];
    make_default_for_scheme(@"http", bundle_url, on_done);
    make_default_for_scheme(@"https", bundle_url, move(on_done));
}

}
