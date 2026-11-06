/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/Input.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridge.h>

using Xenon::Engine;
using Xenon::View;

static StringView string_view_from(XenonStr string)
{
    if (!string.data || string.length == 0)
        return {};
    return { string.data, string.length };
}

static Web::DevicePixelSize viewport_size_from(uint32_t width, uint32_t height)
{
    return { static_cast<int>(max(width, 1u)), static_cast<int>(max(height, 1u)) };
}

static double device_pixel_ratio_from(double device_pixel_ratio)
{
    return device_pixel_ratio > 0 ? device_pixel_ratio : 1.0;
}

static void post_to_view(XenonViewId id, Function<void(View&)> command)
{
    Engine::the().post([id, command = move(command)] {
        if (auto* view = Engine::the().view(id))
            command(*view);
    });
}

bool xenon_engine_start(XenonUiCallbacks const* callbacks, int argc, char const* const* argv)
{
    if (!callbacks)
        return false;
    return Engine::the().start(*callbacks, argc, argv);
}

void xenon_engine_shutdown(void)
{
    Engine::the().shutdown();
}

XenonViewId xenon_engine_create_view(XenonStr url, uint32_t width, uint32_t height, double device_pixel_ratio)
{
    auto& engine = Engine::the();
    auto id = engine.allocate_view_id();
    engine.post([&engine, id, input = ByteString { string_view_from(url) }, size = viewport_size_from(width, height), device_pixel_ratio = device_pixel_ratio_from(device_pixel_ratio)] {
        engine.create_view(id, size, device_pixel_ratio, input);
    });
    return id;
}

void xenon_engine_close_view(XenonViewId id)
{
    Engine::the().post([id] {
        Engine::the().close_view(id);
    });
}

void xenon_engine_load(XenonViewId id, XenonStr input)
{
    post_to_view(id, [input = ByteString { string_view_from(input) }](View& view) {
        view.load_user_input(input);
    });
}

void xenon_engine_go_back(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.go_back();
    });
}

void xenon_engine_go_forward(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.go_forward();
    });
}

void xenon_engine_reload(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.reload();
    });
}

void xenon_engine_stop(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.stop_loading();
    });
}

void xenon_engine_set_viewport(XenonViewId id, uint32_t width, uint32_t height, double device_pixel_ratio)
{
    post_to_view(id, [size = viewport_size_from(width, height), device_pixel_ratio = device_pixel_ratio_from(device_pixel_ratio)](View& view) {
        view.set_viewport(size, device_pixel_ratio);
    });
}

void xenon_engine_set_visible(XenonViewId id, bool visible)
{
    post_to_view(id, [visible](View& view) {
        view.set_visible(visible);
    });
}

void xenon_engine_set_focused(XenonViewId id, bool focused)
{
    post_to_view(id, [id, focused](View& view) {
        Engine::the().set_focused_view(id, focused);
        view.set_focused(focused);
    });
}

void xenon_engine_mouse_event(XenonViewId id, XenonMouseEvent const* event)
{
    if (!event)
        return;
    post_to_view(id, [event = *event](View& view) {
        view.handle_mouse_event(event);
    });
}

void xenon_engine_key_event(XenonViewId id, XenonKeyEvent const* event)
{
    if (!event)
        return;
    auto type = event->type == XENON_KEY_EVENT_UP ? XENON_KEY_EVENT_UP : XENON_KEY_EVENT_DOWN;
    auto key_event = Xenon::key_event_from(type, string_view_from(event->code), string_view_from(event->key), event->modifiers, event->repeat);
    post_to_view(id, [key_event = move(key_event)](View& view) mutable {
        view.handle_key_event(move(key_event));
    });
}

void xenon_engine_insert_text(XenonViewId id, XenonStr text)
{
    post_to_view(id, [text = ByteString { string_view_from(text) }](View& view) {
        view.insert_text(text);
    });
}
