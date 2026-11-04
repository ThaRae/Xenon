/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Function.h>
#include <AK/NonnullRefPtr.h>
#include <AK/RefCounted.h>
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

namespace {

struct PendingRequest : public RefCounted<PendingRequest> {
    explicit PendingRequest(Function<void()> on_done)
        : on_done(move(on_done))
    {
    }

    Function<void()> on_done;
    int remaining { 2 };
};

}

static void make_default_for_scheme(NSString* scheme, NSURL* bundle_url, NonnullRefPtr<PendingRequest> pending)
{
    [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:bundle_url
                                         toOpenURLsWithScheme:scheme
                                            completionHandler:^(NSError* error) {
                                                if (error)
                                                    NSLog(@"Failed to make Xenon the default handler for %@: %@", scheme, error);
                                                dispatch_async(dispatch_get_main_queue(), ^{
                                                    if (--pending->remaining == 0 && pending->on_done)
                                                        pending->on_done();
                                                });
                                            }];
}

void request_default(Function<void()> on_done)
{
    auto* bundle_url = [[NSBundle mainBundle] bundleURL];
    auto pending = adopt_ref(*new PendingRequest(move(on_done)));
    make_default_for_scheme(@"http", bundle_url, pending);
    make_default_for_scheme(@"https", bundle_url, pending);
}

}
