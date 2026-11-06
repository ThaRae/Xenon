/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/ConditionVariable.h>
#include <AK/Mutex.h>
#include <AK/Optional.h>
#include <AK/Time.h>
#include <AK/Vector.h>
#include <LibTest/TestCase.h>
#include <Rune/Bridge/XenonBridge.h>

static constexpr auto event_timeout = AK::Duration::from_seconds(60);
static constexpr u32 viewport_width = 800;
static constexpr u32 viewport_height = 600;

struct CenterPixel {
    u8 blue { 0 };
    u8 green { 0 };
    u8 red { 0 };
};

struct EngineEvents {
    Mutex mutex;
    ConditionVariable condition { mutex };

    bool engine_ready { false };
    Optional<int> exit_code;
    XenonViewId expected_view { 0 };
    Vector<ByteString> titles;
    Vector<ByteString> urls;
    Optional<CenterPixel> center_pixel_of_full_frame;
    size_t frame_count { 0 };
    bool loaded { false };
    bool loading { false };
};

static ByteString byte_string_from(XenonStr string)
{
    return ByteString { string.data, string.length };
}

template<typename Update>
static void update(void* context, Update update)
{
    auto& events = *static_cast<EngineEvents*>(context);
    MutexLocker locker(events.mutex);
    update(events);
    events.condition.broadcast();
}

static XenonUiCallbacks callbacks_for(EngineEvents& events)
{
    XenonUiCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.engine_ready = [](void* context) {
        update(context, [](EngineEvents& events) { events.engine_ready = true; });
    };
    callbacks.engine_exited = [](void* context, int exit_code) {
        update(context, [&](EngineEvents& events) { events.exit_code = exit_code; });
    };
    callbacks.view_frame = [](void* context, XenonViewId view, XenonFrame const* frame) {
        update(context, [&](EngineEvents& events) {
            if (view != events.expected_view)
                return;
            ++events.frame_count;
            if (frame->width != viewport_width || frame->height != viewport_height || !frame->bgra)
                return;
            auto const* pixel = frame->bgra + (frame->height / 2) * frame->stride + (frame->width / 2) * 4;
            events.center_pixel_of_full_frame = CenterPixel { pixel[0], pixel[1], pixel[2] };
        });
    };
    callbacks.view_title_changed = [](void* context, XenonViewId view, XenonStr title) {
        update(context, [&](EngineEvents& events) {
            if (view == events.expected_view)
                events.titles.append(byte_string_from(title));
        });
    };
    callbacks.view_url_changed = [](void* context, XenonViewId view, XenonStr url) {
        update(context, [&](EngineEvents& events) {
            if (view == events.expected_view)
                events.urls.append(byte_string_from(url));
        });
    };
    callbacks.view_loading_changed = [](void* context, XenonViewId view, bool is_loading) {
        update(context, [&](EngineEvents& events) {
            if (view != events.expected_view)
                return;
            if (events.loading && !is_loading)
                events.loaded = true;
            events.loading = is_loading;
        });
    };
    return callbacks;
}

template<typename Condition>
static bool wait_until(EngineEvents& events, Condition condition)
{
    auto deadline = MonotonicTime::now() + event_timeout;
    MutexLocker locker(events.mutex);
    while (!condition(events)) {
        auto now = MonotonicTime::now();
        if (now >= deadline)
            return false;
        events.condition.wait_for(deadline - now);
    }
    return true;
}

static bool is_close_to(u8 actual, u8 expected)
{
    return (actual > expected ? actual - expected : expected - actual) <= 2;
}

static bool shows_page_color(Optional<CenterPixel> pixel)
{
    return pixel.has_value() && is_close_to(pixel->red, 0xff) && is_close_to(pixel->green, 0x00) && is_close_to(pixel->blue, 0x50);
}

TEST_CASE(engine_paints_a_page_into_frames_for_the_ui)
{
    EngineEvents events;
    auto callbacks = callbacks_for(events);

    char const* arguments[] = { "TestXenonRuneBridge", "--temporary-profile" };
    EXPECT(xenon_engine_start(&callbacks, 2, arguments));

    static constexpr auto page = "data:text/html,<title>Rune bridge</title><body style=\"margin:0;background:%23ff0050\"></body>"sv;
    {
        MutexLocker locker(events.mutex);
        events.expected_view = xenon_engine_create_view({ page.characters_without_null_termination(), page.length() }, viewport_width, viewport_height, 1.0);
    }
    EXPECT_NE(events.expected_view, 0u);

    EXPECT(wait_until(events, [](auto& events) { return events.engine_ready; }));
    EXPECT(wait_until(events, [](auto& events) { return events.loaded && events.titles.contains_slow("Rune bridge"sv); }));
    EXPECT(wait_until(events, [](auto& events) { return shows_page_color(events.center_pixel_of_full_frame); }));

    {
        MutexLocker locker(events.mutex);
        EXPECT(events.frame_count > 0);
        EXPECT(events.center_pixel_of_full_frame.has_value());
        if (events.center_pixel_of_full_frame.has_value()) {
            auto pixel = *events.center_pixel_of_full_frame;
            if (!shows_page_color(pixel))
                warnln("Center pixel: #{:02x}{:02x}{:02x}", pixel.red, pixel.green, pixel.blue);
            EXPECT(shows_page_color(pixel));
        }
        EXPECT(events.titles.contains_slow("Rune bridge"sv));
        EXPECT(!events.urls.is_empty() && events.urls.last().starts_with("data:text/html"sv));
    }

    xenon_engine_shutdown();

    MutexLocker locker(events.mutex);
    EXPECT_EQ(events.exit_code, Optional<int> { 0 });
}
