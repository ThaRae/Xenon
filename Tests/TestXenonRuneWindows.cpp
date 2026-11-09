/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/ConditionVariable.h>
#include <AK/HashMap.h>
#include <AK/Mutex.h>
#include <AK/Optional.h>
#include <AK/Time.h>
#include <AK/Vector.h>
#include <LibTest/TestCase.h>
#include <Rune/Bridge/WindowLifecycle.h>
#include <Rune/Bridge/XenonBridge.h>
#include <Rune/Bridge/XenonBridgePage.h>
#include <Rune/Bridge/XenonBridgeWindows.h>

using Xenon::WindowLifecycle;

static constexpr auto event_timeout = AK::Duration::from_seconds(60);

TEST_CASE(a_new_window_comes_back_where_it_was_or_cascades_from_the_last_one)
{
    WebView::SessionWindowGeometry restored { .x = 10, .y = 20, .width = 900, .height = 700, .maximized = true };
    WebView::SessionWindowGeometry previous { .x = 100, .y = 80, .width = 1000, .height = 600, .maximized = false };
    WindowLifecycle::DefaultGeometry fallback { .geometry = { .width = 1200, .height = 900 }, .has_position = false };

    auto from_session = WindowLifecycle::placement_for(restored, previous, fallback);
    EXPECT_EQ(from_session.x, 10);
    EXPECT_EQ(from_session.width, 900);
    EXPECT(from_session.maximized);
    EXPECT(from_session.has_position);

    auto cascaded = WindowLifecycle::placement_for({}, previous, fallback);
    EXPECT_EQ(cascaded.x, 100 + WindowLifecycle::new_window_cascade_offset);
    EXPECT_EQ(cascaded.y, 80 + WindowLifecycle::new_window_cascade_offset);
    EXPECT_EQ(cascaded.height, 600);
    EXPECT(cascaded.has_position);

    auto first = WindowLifecycle::placement_for({}, {}, fallback);
    EXPECT_EQ(first.width, 1200);
    EXPECT_EQ(first.height, 900);
    EXPECT(!first.has_position);
}

TEST_CASE(a_window_asks_its_active_tab_first_when_it_closes)
{
    Xenon::BrowserModel::Window window;
    window.tabs = { 1, 2, 3 };
    window.active_tab = 2;
    EXPECT_EQ(WindowLifecycle::close_order(window), (Vector<Xenon::ViewId> { 2, 1, 3 }));
}

struct WindowEvents {
    Mutex mutex;
    ConditionVariable condition { mutex };

    bool engine_ready { false };
    Vector<XenonWindowId> opened;
    Vector<XenonWindowId> closed;
    HashMap<XenonWindowId, XenonWindowGeometry> placements;
    HashMap<XenonViewId, ByteString> titles;
    size_t raises { 0 };
    Optional<XenonPromptId> before_unload;
    XenonViewId before_unload_view { 0 };
    bool quit_approved { false };
};

static ByteString byte_string_from(XenonStr string)
{
    return ByteString { string.data, string.length };
}

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

template<typename Update>
static void update(void* context, Update update)
{
    auto& events = *static_cast<WindowEvents*>(context);
    MutexLocker locker(events.mutex);
    update(events);
    events.condition.broadcast();
}

template<typename Condition>
static bool wait_until(WindowEvents& events, Condition condition)
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

static void click(XenonViewId view)
{
    XenonMouseEvent event {};
    event.x = 20;
    event.y = 20;
    event.button = XENON_MOUSE_BUTTON_PRIMARY;
    event.type = XENON_MOUSE_EVENT_MOVE;
    xenon_engine_mouse_event(view, &event);
    event.type = XENON_MOUSE_EVENT_DOWN;
    event.buttons = XENON_MOUSE_BUTTON_PRIMARY;
    xenon_engine_mouse_event(view, &event);
    event.type = XENON_MOUSE_EVENT_UP;
    event.buttons = 0;
    xenon_engine_mouse_event(view, &event);
}

TEST_CASE(closing_and_quitting_ask_the_pages_first)
{
    WindowEvents events;

    XenonUiCallbacks ui_callbacks {};
    ui_callbacks.context = &events;
    ui_callbacks.engine_ready = [](void* context) {
        update(context, [](WindowEvents& events) { events.engine_ready = true; });
    };
    ui_callbacks.window_opened = [](void* context, XenonWindowId window, bool) {
        update(context, [&](WindowEvents& events) { events.opened.append(window); });
    };
    ui_callbacks.window_closed = [](void* context, XenonWindowId window) {
        update(context, [&](WindowEvents& events) { events.closed.append(window); });
    };
    ui_callbacks.view_title_changed = [](void* context, XenonViewId view, XenonStr title) {
        update(context, [&](WindowEvents& events) { events.titles.set(view, byte_string_from(title)); });
    };

    XenonPageCallbacks page_callbacks {};
    page_callbacks.context = &events;
    page_callbacks.dialog_opened = [](void* context, XenonViewId view, XenonDialog const* dialog) {
        if (dialog->kind != XENON_DIALOG_BEFORE_UNLOAD)
            return;
        update(context, [&](WindowEvents& events) {
            events.before_unload = dialog->id;
            events.before_unload_view = view;
        });
    };
    xenon_engine_set_page_callbacks(&page_callbacks);

    XenonWindowCallbacks window_callbacks {};
    window_callbacks.context = &events;
    window_callbacks.window_placement = [](void* context, XenonWindowId window, XenonWindowGeometry const* geometry) {
        update(context, [&](WindowEvents& events) { events.placements.set(window, *geometry); });
    };
    window_callbacks.window_raise = [](void* context, XenonWindowId) {
        update(context, [](WindowEvents& events) { ++events.raises; });
    };
    window_callbacks.quit_approved = [](void* context) {
        update(context, [](WindowEvents& events) { events.quit_approved = true; });
    };
    xenon_engine_set_window_callbacks(&window_callbacks);

    char const* arguments[] = { "TestXenonRuneWindows", "--temporary-profile" };
    EXPECT(xenon_engine_start(&ui_callbacks, 2, arguments));
    xenon_engine_set_window_viewport(XENON_WINDOW_NONE, 800, 600, 1.0);
    EXPECT(wait_until(events, [](auto& events) { return events.engine_ready; }));

    auto first = xenon_engine_new_window(false);
    EXPECT(wait_until(events, [&](auto& events) { return events.opened.contains_slow(first) && events.placements.contains(first); }));

    XenonWindowGeometry reported { .x = 50, .y = 60, .width = 900, .height = 700, .maximized = false, .has_position = true };
    xenon_engine_report_window_geometry(first, &reported);
    auto second = xenon_engine_new_window(false);
    EXPECT(wait_until(events, [&](auto& events) { return events.placements.contains(second); }));
    {
        MutexLocker locker(events.mutex);
        auto placement = events.placements.get(second).value();
        EXPECT_EQ(placement.x, 50 + WindowLifecycle::new_window_cascade_offset);
        EXPECT_EQ(placement.y, 60 + WindowLifecycle::new_window_cascade_offset);
        EXPECT_EQ(placement.width, 900);
        EXPECT(placement.has_position);
    }

    static constexpr auto guarded_page = "data:text/html,<title>armed</title><body style='height:100vh'><script>onbeforeunload=e=>{e.preventDefault();e.returnValue='unsaved'};onclick=()=>document.title='clicked'</script>"sv;
    auto guarded = xenon_engine_new_tab(first, xenon_str(guarded_page), XENON_TAB_INDEX_END, true);
    EXPECT(wait_until(events, [&](auto& events) { return events.titles.get(guarded).value_or(ByteString {}) == "armed"sv; }));
    click(guarded);
    EXPECT(wait_until(events, [&](auto& events) { return events.titles.get(guarded).value_or(ByteString {}) == "clicked"sv; }));

    xenon_engine_request_close_window(first);
    EXPECT(wait_until(events, [](auto& events) { return events.before_unload.has_value(); }));
    XenonPromptId prompt = 0;
    {
        MutexLocker locker(events.mutex);
        EXPECT_EQ(events.before_unload_view, guarded);
        EXPECT(events.raises > 0);
        prompt = events.before_unload.release_value();
    }
    xenon_engine_close_dialog(guarded, prompt, false, {});
    EXPECT(wait_until(events, [](auto& events) { return events.raises > 1; }));
    {
        MutexLocker locker(events.mutex);
        EXPECT(!events.closed.contains_slow(first));
    }

    xenon_engine_request_close_window(first);
    EXPECT(wait_until(events, [](auto& events) { return events.before_unload.has_value(); }));
    {
        MutexLocker locker(events.mutex);
        prompt = events.before_unload.release_value();
    }
    xenon_engine_close_dialog(guarded, prompt, true, {});
    EXPECT(wait_until(events, [&](auto& events) { return events.closed.contains_slow(first); }));

    xenon_engine_request_quit();
    EXPECT(wait_until(events, [](auto& events) { return events.quit_approved; }));
    {
        MutexLocker locker(events.mutex);
        EXPECT(!events.closed.contains_slow(second));
    }

    xenon_engine_shutdown();
}
