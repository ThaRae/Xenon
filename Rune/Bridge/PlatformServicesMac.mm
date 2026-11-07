/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AtomicRefCounted.h>
#include <LibWebCommon/WebView/DictionaryLookup.h>
#include <LibWebView/SpellChecker.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/PlatformServices.h>
#include <UI/SpellCheckerMac.h>

#import <Cocoa/Cocoa.h>
#import <UI/DictionaryLookup.h>

namespace Xenon {

static NSString* ns_string_from(StringView string)
{
    return [[NSString alloc] initWithBytes:string.characters_without_null_termination() length:string.length() encoding:NSUTF8StringEncoding];
}

static String string_from(NSString* string)
{
    if (!string)
        return {};
    char const* utf8 = [string UTF8String];
    return MUST(String::from_utf8(StringView { utf8, strlen(utf8) }));
}

Optional<String> platform_ui_font_family()
{
    return ".AppleSystemUIFont"_string;
}

OwnPtr<WebView::SpellChecker> create_platform_spell_checker()
{
    return Ladybird::create_platform_spell_checker();
}

class LaunchCompletion final : public AtomicRefCounted<LaunchCompletion> {
public:
    explicit LaunchCompletion(WebView::ExternalURLHandler::LaunchCallback callback)
        : m_callback(move(callback))
    {
    }

    void complete(bool success)
    {
        if (!m_callback)
            return;
        auto callback = move(m_callback);
        callback(success);
    }

private:
    WebView::ExternalURLHandler::LaunchCallback m_callback;
};

class AppKitExternalURLHandler final : public WebView::ExternalURLHandler {
public:
    AppKitExternalURLHandler(String application_name, bool is_xenon, String application_path)
        : WebView::ExternalURLHandler(move(application_name), is_xenon)
        , m_application_path(move(application_path))
    {
    }

    virtual void launch(URL::URL const& url, LaunchCallback callback) const override
    {
        auto* ns_url = [NSURL URLWithString:ns_string_from(url.serialize())];
        auto* application_url = [NSURL fileURLWithPath:ns_string_from(m_application_path)];
        if (ns_url == nil || application_url == nil) {
            callback(false);
            return;
        }

        auto completion = adopt_ref(*new LaunchCompletion(move(callback)));
        [[NSWorkspace sharedWorkspace] openURLs:@[ ns_url ]
                           withApplicationAtURL:application_url
                                  configuration:[NSWorkspaceOpenConfiguration configuration]
                              completionHandler:^(NSRunningApplication* application, NSError* error) {
                                  auto success = application != nil && error == nil;
                                  Engine::the().post([completion, success] { completion->complete(success); });
                              }];
    }

private:
    String m_application_path;
};

void initialize_platform_external_url_handler(Core::EventLoop&)
{
}

void resolve_platform_external_url_handler(URL::URL const& url, WebView::ExternalURLHandlerCallback callback)
{
    auto* ns_url = [NSURL URLWithString:ns_string_from(url.serialize())];
    if (ns_url == nil) {
        callback(nullptr);
        return;
    }

    auto* application_url = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenURL:ns_url];
    if (application_url == nil) {
        callback(nullptr);
        return;
    }

    auto* application_name = [[NSFileManager defaultManager] displayNameAtPath:[application_url path]];
    if (application_name == nil)
        application_name = [[application_url lastPathComponent] stringByDeletingPathExtension];

    auto* handler_bundle_url = [application_url URLByResolvingSymlinksInPath];
    auto* current_bundle_url = [[[NSBundle mainBundle] bundleURL] URLByResolvingSymlinksInPath];
    bool is_xenon = [handler_bundle_url isEqual:current_bundle_url];

    auto name = application_name == nil ? "another application"_string : string_from(application_name);
    callback(adopt_ref(*new AppKitExternalURLHandler(move(name), is_xenon, string_from([application_url path]))));
}

void show_platform_dictionary_lookup(WebView::DictionaryLookup const& lookup, Gfx::FloatPoint window_point)
{
    auto copy = lookup;
    dispatch_async(dispatch_get_main_queue(), ^{
        auto* window = NSApp.keyWindow ?: NSApp.mainWindow;
        auto* view = window.contentView;
        if (!view)
            return;
        auto y = view.isFlipped ? window_point.y() : view.bounds.size.height - window_point.y();
        Ladybird::show_dictionary_lookup(view, copy, NSMakePoint(window_point.x(), y));
    });
}

}
