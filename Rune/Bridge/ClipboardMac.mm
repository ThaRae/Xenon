/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <LibWebView/Utilities.h>
#include <Rune/Bridge/Clipboard.h>

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace Xenon::Clipboard {

struct PasteboardType {
    StringView mime_type;
    NSPasteboardType type;
};

static Array<PasteboardType, 3> pasteboard_types()
{
    return { {
        { "text/plain"sv, NSPasteboardTypeString },
        { "text/html"sv, NSPasteboardTypeHTML },
        { "image/png"sv, NSPasteboardTypePNG },
    } };
}

static NSString* ns_string(StringView string)
{
    return [[NSString alloc] initWithBytes:string.characters_without_null_termination()
                                    length:string.length()
                                  encoding:NSUTF8StringEncoding];
}

static ByteString byte_string(NSData* data)
{
    return ByteString { ReadonlyBytes { static_cast<u8 const*>(data.bytes), data.length } };
}

Utf16String text()
{
    @autoreleasepool {
        NSString* string = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        if (!string)
            return {};
        return Utf16String::from_utf8_with_replacement_character(StringView { string.UTF8String, strlen(string.UTF8String) });
    }
}

void set_text(String const& text)
{
    @autoreleasepool {
        auto* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        [pasteboard setString:ns_string(text) forType:NSPasteboardTypeString];
    }
}

Web::Clipboard::SystemClipboardItem item()
{
    Vector<Web::Clipboard::SystemClipboardRepresentation> representations;
    @autoreleasepool {
        auto* pasteboard = [NSPasteboard generalPasteboard];

        NSArray<NSURL*>* urls = [pasteboard readObjectsForClasses:@[ [NSURL class] ]
                                                          options:@{ NSPasteboardURLReadingFileURLsOnlyKey : @YES }];
        for (NSURL* url in urls) {
            ByteString path { url.path.UTF8String };
            auto file = WebView::create_selected_file(path);
            if (file.is_error()) {
                warnln("Unable to open clipboard file {}: {}", path, file.error());
                continue;
            }
            UTType* type = nil;
            [url getResourceValue:&type forKey:NSURLContentTypeKey error:nil];
            auto mime_type = type.preferredMIMEType ? String::from_utf8_with_replacement_character({ type.preferredMIMEType.UTF8String, strlen(type.preferredMIMEType.UTF8String) })
                                                    : "application/octet-stream"_string;
            representations.empend(move(mime_type), file.release_value());
        }

        if (representations.is_empty()) {
            for (auto const& [mime_type, type] : pasteboard_types()) {
                NSData* data = [pasteboard dataForType:type];
                if (data)
                    representations.empend(MUST(String::from_utf8(mime_type)), byte_string(data));
            }
        }
    }
    return { move(representations) };
}

void set_item(Web::Clipboard::SystemClipboardItem const& item)
{
    @autoreleasepool {
        auto* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        for (auto const& entry : item.system_clipboard_representations) {
            auto const* data = entry.data.get_pointer<ByteString>();
            if (!data)
                continue;
            for (auto const& [mime_type, type] : pasteboard_types()) {
                if (entry.name != mime_type)
                    continue;
                [pasteboard setData:[NSData dataWithBytes:data->characters() length:data->length()] forType:type];
            }
        }
    }
}

}
