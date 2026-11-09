/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/WindowLifecycle.h>
#include <Rune/Bridge/XenonBridgeWindows.h>

using Xenon::Engine;
using Xenon::WindowLifecycle;

static void post_to_lifecycle(Function<void(WindowLifecycle&)> command)
{
    Engine::the().post([command = move(command)] {
        command(WindowLifecycle::the());
    });
}

void xenon_engine_set_window_callbacks(XenonWindowCallbacks const* callbacks)
{
    WindowLifecycle::the().set_callbacks(callbacks ? *callbacks : XenonWindowCallbacks {});
}

void xenon_engine_request_close_window(XenonWindowId window)
{
    post_to_lifecycle([window](auto& lifecycle) {
        lifecycle.request_close(window);
    });
}

void xenon_engine_request_quit(void)
{
    post_to_lifecycle([](auto& lifecycle) {
        lifecycle.request_quit();
    });
}

void xenon_engine_answer_stop_downloads(uint64_t request, bool stop)
{
    post_to_lifecycle([request, stop](auto& lifecycle) {
        lifecycle.answer_stop_downloads(request, stop);
    });
}

void xenon_engine_report_window_geometry(XenonWindowId window, XenonWindowGeometry const* geometry)
{
    if (!geometry || geometry->width <= 0 || geometry->height <= 0)
        return;
    WebView::SessionWindowGeometry session_geometry {
        .x = geometry->x,
        .y = geometry->y,
        .width = geometry->width,
        .height = geometry->height,
        .maximized = geometry->maximized,
    };
    post_to_lifecycle([window, session_geometry](auto& lifecycle) {
        lifecycle.geometry_reported(window, session_geometry);
    });
}

void xenon_engine_set_window_exposed(XenonWindowId window, bool exposed)
{
    post_to_lifecycle([window, exposed](auto& lifecycle) {
        lifecycle.set_exposed(window, exposed);
    });
}

void xenon_engine_launch_devtools_client(void)
{
    post_to_lifecycle([](auto& lifecycle) {
        lifecycle.launch_devtools_client();
    });
}

void xenon_engine_disable_devtools(void)
{
    post_to_lifecycle([](auto& lifecycle) {
        lifecycle.disable_devtools();
    });
}
