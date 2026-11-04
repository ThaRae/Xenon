/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/Format.h>
#include <AK/ScopeGuard.h>
#include <UI/DefaultBrowser.h>

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

namespace Ladybird::DefaultBrowser {

static constexpr char const* desktop_id = "org.xenon.Xenon.desktop";

static GDesktopAppInfo* desktop_app_info()
{
    return g_desktop_app_info_new(desktop_id);
}

Status status()
{
    auto* app_info = desktop_app_info();
    if (!app_info)
        return Status::Unsupported;
    ScopeGuard release_app_info = [&] {
        g_object_unref(app_info);
    };

    auto* handler = g_app_info_get_default_for_uri_scheme("http");
    if (!handler)
        return Status::IsNotDefault;
    ScopeGuard release_handler = [&] {
        g_object_unref(handler);
    };
    return g_app_info_equal(handler, G_APP_INFO(app_info)) ? Status::IsDefault : Status::IsNotDefault;
}

void request_default(Function<void()> on_done)
{
    auto* app_info = desktop_app_info();
    if (!app_info) {
        if (on_done)
            on_done();
        return;
    }
    ScopeGuard release_app_info = [&] {
        g_object_unref(app_info);
    };

    for (auto content_type : { "x-scheme-handler/http"sv, "x-scheme-handler/https"sv, "text/html"sv, "application/xhtml+xml"sv }) {
        GError* error = nullptr;
        if (g_app_info_set_as_default_for_type(G_APP_INFO(app_info), ByteString { content_type }.characters(), &error))
            continue;
        warnln("Failed to make Xenon the default handler for {}: {}", content_type, error->message);
        g_error_free(error);
    }

    if (on_done)
        on_done();
}

}
