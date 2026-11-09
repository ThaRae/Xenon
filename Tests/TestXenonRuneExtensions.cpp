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
#include <Rune/Bridge/XenonBridge.h>
#include <Rune/Bridge/XenonBridgeExtensions.h>

static constexpr auto event_timeout = AK::Duration::from_seconds(60);

struct Action {
    ByteString extension_id;
    ByteString title;
    ByteString icon_path;
    bool has_popup { false };
};

struct Events {
    Mutex mutex;
    ConditionVariable condition { mutex };

    bool engine_ready { false };
    Optional<int> exit_code;
    XenonViewId tab { 0 };
    bool tab_active { false };
    HashMap<XenonViewId, Vector<Action>> actions;
    Vector<ByteString> extension_names;
    XenonViewId popup { 0 };
    ByteString popup_extension;
    Optional<u32> popup_width;
    size_t popup_frames { 0 };
    Vector<ByteString> popup_titles;
    bool popup_closed { false };
};

static ByteString byte_string_from(XenonStr string)
{
    return ByteString { string.data, string.length };
}

template<typename Update>
static void update(void* context, Update update)
{
    auto& events = *static_cast<Events*>(context);
    MutexLocker locker(events.mutex);
    update(events);
    events.condition.broadcast();
}

template<typename Condition>
static bool wait_until(Events& events, Condition condition)
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

static XenonUiCallbacks ui_callbacks_for(Events& events)
{
    XenonUiCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.engine_ready = [](void* context) {
        update(context, [](Events& events) { events.engine_ready = true; });
    };
    callbacks.engine_exited = [](void* context, int exit_code) {
        update(context, [&](Events& events) { events.exit_code = exit_code; });
    };
    callbacks.view_frame = [](void* context, XenonViewId view, XenonFrame const*) {
        update(context, [&](Events& events) {
            if (view != 0 && view == events.popup)
                ++events.popup_frames;
        });
    };
    callbacks.view_title_changed = [](void* context, XenonViewId view, XenonStr title) {
        update(context, [&](Events& events) {
            if (view != 0 && view == events.popup)
                events.popup_titles.append(byte_string_from(title));
        });
    };
    callbacks.tab_activated = [](void* context, XenonWindowId, XenonViewId view) {
        update(context, [&](Events& events) {
            if (view == events.tab)
                events.tab_active = true;
        });
    };
    return callbacks;
}

static XenonExtensionCallbacks extension_callbacks_for(Events& events)
{
    XenonExtensionCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.actions_changed = [](void* context, XenonViewId tab, XenonExtensionAction const* actions, size_t count) {
        update(context, [&](Events& events) {
            Vector<Action> reported;
            for (size_t i = 0; i < count; ++i)
                reported.append({ byte_string_from(actions[i].extension_id), byte_string_from(actions[i].title), byte_string_from(actions[i].icon_path), actions[i].has_popup });
            events.actions.set(tab, move(reported));
        });
    };
    callbacks.extensions_changed = [](void* context, XenonExtensionInfo const* extensions, size_t count) {
        update(context, [&](Events& events) {
            events.extension_names.clear();
            for (size_t i = 0; i < count; ++i)
                events.extension_names.append(byte_string_from(extensions[i].name));
        });
    };
    callbacks.popup_opened = [](void* context, XenonViewId, XenonStr extension_id, XenonViewId popup, uint32_t, uint32_t) {
        update(context, [&](Events& events) {
            events.popup = popup;
            events.popup_extension = byte_string_from(extension_id);
        });
    };
    callbacks.popup_resized = [](void* context, XenonViewId popup, uint32_t width, uint32_t) {
        update(context, [&](Events& events) {
            if (popup == events.popup)
                events.popup_width = width;
        });
    };
    callbacks.popup_closed = [](void* context, XenonViewId popup) {
        update(context, [&](Events& events) {
            if (popup == events.popup)
                events.popup_closed = true;
        });
    };
    return callbacks;
}

TEST_CASE(an_extension_gets_a_toolbar_action_and_a_popup_view)
{
    Events events;
    auto ui_callbacks = ui_callbacks_for(events);
    auto extension_callbacks = extension_callbacks_for(events);
    xenon_engine_set_extension_callbacks(&extension_callbacks);

    auto extension = ByteString::formatted("{}/Tests/Extensions/basic", XENON_SOURCE_DIR);
    char const* arguments[] = { "TestXenonRuneExtensions", "--temporary-profile", "--load-extension", extension.characters() };
    EXPECT(xenon_engine_start(&ui_callbacks, 4, arguments));

    static constexpr auto page = "data:text/html,<title>Extensions</title>"sv;
    {
        MutexLocker locker(events.mutex);
        xenon_engine_set_window_viewport(XENON_WINDOW_NONE, 800, 600, 1.0);
        events.tab = xenon_engine_new_tab(XENON_WINDOW_NONE, { page.characters_without_null_termination(), page.length() }, XENON_TAB_INDEX_END, true);
    }

    EXPECT(wait_until(events, [](auto& events) { return events.engine_ready && events.tab_active; }));
    EXPECT(wait_until(events, [](auto& events) { return !events.extension_names.is_empty(); }));
    EXPECT(wait_until(events, [](auto& events) { return events.actions.get(events.tab).map([](auto& actions) { return !actions.is_empty(); }).value_or(false); }));

    ByteString extension_id;
    {
        MutexLocker locker(events.mutex);
        EXPECT_EQ(events.extension_names.size(), 1u);
        EXPECT_EQ(events.extension_names.first(), "Xenon test extension"sv);
        auto const& action = events.actions.get(events.tab)->first();
        extension_id = action.extension_id;
        EXPECT(action.has_popup);
        EXPECT(action.icon_path.ends_with("/icon32.png"sv));
        EXPECT(!action.title.is_empty() && !action.title.starts_with("__MSG_"sv));
    }

    xenon_engine_extension_action_clicked(events.tab, { extension_id.characters(), extension_id.length() });
    EXPECT(wait_until(events, [](auto& events) { return events.popup != 0; }));
    EXPECT(wait_until(events, [](auto& events) { return events.popup_frames > 0; }));
    EXPECT(wait_until(events, [](auto& events) { return events.popup_width.has_value(); }));
    EXPECT(wait_until(events, [](auto& events) { return events.popup_titles.contains_slow("Xenon test extension"sv); }));
    {
        MutexLocker locker(events.mutex);
        EXPECT_EQ(events.popup_extension, extension_id);
        auto width = events.popup_width.value_or(0);
        EXPECT(width >= 300 && width <= 340);
    }

    xenon_engine_close_extension_popup(events.popup);
    EXPECT(wait_until(events, [](auto& events) { return events.popup_closed; }));

    xenon_engine_shutdown();
    MutexLocker locker(events.mutex);
    EXPECT_EQ(events.exit_code, Optional<int> { 0 });
}
