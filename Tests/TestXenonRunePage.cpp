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
#include <Rune/Bridge/XenonBridgePage.h>

static constexpr auto event_timeout = AK::Duration::from_seconds(60);
static constexpr u32 viewport_width = 800;
static constexpr u32 viewport_height = 600;

struct ShownDialog {
    XenonPromptId id { 0 };
    u32 kind { 0 };
    ByteString message;
    ByteString default_text;
};

struct ShownMenuItem {
    u32 kind { 0 };
    u32 id { 0 };
    ByteString title;
    bool enabled { false };
    bool checked { false };
};

struct ShownMenu {
    XenonPromptId id { 0 };
    Vector<ShownMenuItem> items;
};

struct PageEvents {
    Mutex mutex;
    ConditionVariable condition { mutex };

    bool engine_ready { false };
    XenonViewId view { 0 };
    Vector<ByteString> titles;
    bool loading { false };

    Optional<ShownDialog> dialog;
    Optional<ShownMenu> select_dropdown;
    Optional<ShownMenu> context_menu;
    Optional<size_t> find_total;
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
    auto& events = *static_cast<PageEvents*>(context);
    MutexLocker locker(events.mutex);
    update(events);
    events.condition.broadcast();
}

static ShownMenu shown_menu_from(XenonMenu const& menu)
{
    ShownMenu shown { .id = menu.id, .items = {} };
    for (size_t i = 0; i < menu.item_count; ++i) {
        auto const& item = menu.items[i];
        shown.items.append({ item.kind, item.id, byte_string_from(item.title), item.enabled, item.checked });
    }
    return shown;
}

static XenonUiCallbacks ui_callbacks_for(PageEvents& events)
{
    XenonUiCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.engine_ready = [](void* context) {
        update(context, [](PageEvents& events) { events.engine_ready = true; });
    };
    callbacks.view_title_changed = [](void* context, XenonViewId view, XenonStr title) {
        update(context, [&](PageEvents& events) {
            if (view == events.view)
                events.titles.append(byte_string_from(title));
        });
    };
    callbacks.view_loading_changed = [](void* context, XenonViewId view, bool is_loading) {
        update(context, [&](PageEvents& events) {
            if (view != events.view)
                return;
            events.loading = is_loading;
        });
    };
    return callbacks;
}

static XenonPageCallbacks page_callbacks_for(PageEvents& events)
{
    XenonPageCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.dialog_opened = [](void* context, XenonViewId, XenonDialog const* dialog) {
        update(context, [&](PageEvents& events) {
            events.dialog = ShownDialog { dialog->id, dialog->kind, byte_string_from(dialog->message), byte_string_from(dialog->default_text) };
        });
    };
    callbacks.select_dropdown_requested = [](void* context, XenonViewId, XenonMenu const* menu) {
        update(context, [&](PageEvents& events) { events.select_dropdown = shown_menu_from(*menu); });
    };
    callbacks.context_menu_requested = [](void* context, XenonViewId, XenonMenu const* menu) {
        update(context, [&](PageEvents& events) { events.context_menu = shown_menu_from(*menu); });
    };
    callbacks.find_result = [](void* context, XenonViewId, size_t, bool has_total, size_t total) {
        update(context, [&](PageEvents& events) {
            if (has_total)
                events.find_total = total;
        });
    };
    return callbacks;
}

template<typename Condition>
static bool wait_until(PageEvents& events, Condition condition)
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

static void load(PageEvents& events, StringView url)
{
    {
        MutexLocker locker(events.mutex);
        events.dialog.clear();
        events.select_dropdown.clear();
        events.context_menu.clear();
        events.find_total.clear();
    }
    xenon_engine_load(events.view, xenon_str(url));
}

static void click(XenonViewId view, float x, float y, u32 button)
{
    XenonMouseEvent event {};
    event.x = x;
    event.y = y;
    event.button = button;
    event.type = XENON_MOUSE_EVENT_MOVE;
    xenon_engine_mouse_event(view, &event);
    event.type = XENON_MOUSE_EVENT_DOWN;
    event.buttons = button;
    xenon_engine_mouse_event(view, &event);
    event.type = XENON_MOUSE_EVENT_UP;
    event.buttons = 0;
    xenon_engine_mouse_event(view, &event);
}

static bool has_title(PageEvents& events, StringView title)
{
    return wait_until(events, [&](auto& events) { return events.titles.contains_slow(title); });
}

TEST_CASE(page_prompts_round_trip_through_the_page_abi)
{
    PageEvents events;
    auto ui_callbacks = ui_callbacks_for(events);
    auto page_callbacks = page_callbacks_for(events);
    xenon_engine_set_page_callbacks(&page_callbacks);

    char const* arguments[] = { "TestXenonRunePage", "--temporary-profile" };
    EXPECT(xenon_engine_start(&ui_callbacks, 2, arguments));

    static constexpr auto confirm_page = "data:text/html,<title>start</title><script>setTimeout(()=>{document.title=confirm('Proceed?')?'confirmed':'declined'},10)</script>"sv;
    {
        MutexLocker locker(events.mutex);
        xenon_engine_set_window_viewport(XENON_WINDOW_NONE, viewport_width, viewport_height, 1.0);
        events.view = xenon_engine_new_tab(XENON_WINDOW_NONE, xenon_str(confirm_page), XENON_TAB_INDEX_END, true);
    }
    EXPECT_NE(events.view, 0u);
    EXPECT(wait_until(events, [](auto& events) { return events.engine_ready; }));

    EXPECT(wait_until(events, [](auto& events) { return events.dialog.has_value(); }));
    {
        MutexLocker locker(events.mutex);
        EXPECT_EQ(events.dialog->kind, static_cast<u32>(XENON_DIALOG_CONFIRM));
        EXPECT_EQ(events.dialog->message, "Proceed?"sv);
    }
    xenon_engine_close_dialog(events.view, events.dialog->id, true, {});
    EXPECT(has_title(events, "confirmed"sv));

    load(events, "data:text/html,<title>start</title><script>setTimeout(()=>{document.title='name-'+prompt('Name?','default')},10)</script>"sv);
    EXPECT(wait_until(events, [](auto& events) { return events.dialog.has_value(); }));
    {
        MutexLocker locker(events.mutex);
        EXPECT_EQ(events.dialog->kind, static_cast<u32>(XENON_DIALOG_PROMPT));
        EXPECT_EQ(events.dialog->default_text, "default"sv);
    }
    xenon_engine_close_dialog(events.view, events.dialog->id, true, xenon_str("Rune"sv));
    EXPECT(has_title(events, "name-Rune"sv));

    load(events, "data:text/html,<title>select</title><body style='margin:0'><select style='width:300px;height:40px' onchange=\"document.title='picked-'+this.value\"><option value=a>Apple</option><optgroup label=Citrus><option value=l>Lemon</option></optgroup></select>"sv);
    EXPECT(has_title(events, "select"sv));
    EXPECT(wait_until(events, [](auto& events) { return !events.loading; }));
    click(events.view, 100, 20, XENON_MOUSE_BUTTON_PRIMARY);
    EXPECT(wait_until(events, [](auto& events) { return events.select_dropdown.has_value(); }));
    Optional<u32> lemon;
    XenonPromptId dropdown = 0;
    {
        MutexLocker locker(events.mutex);
        dropdown = events.select_dropdown->id;
        for (auto const& item : events.select_dropdown->items) {
            if (item.kind == XENON_MENU_ITEM_ACTION && item.title == "Lemon"sv)
                lemon = item.id;
        }
        EXPECT(events.select_dropdown->items.first_matching([](auto const& item) { return item.kind == XENON_MENU_ITEM_HEADING && item.title == "Citrus"sv; }).has_value());
    }
    EXPECT(lemon.has_value());
    xenon_engine_close_select_dropdown(events.view, dropdown, true, lemon.value_or(0));
    EXPECT(has_title(events, "picked-l"sv));

    click(events.view, 500, 400, XENON_MOUSE_BUTTON_SECONDARY);
    EXPECT(wait_until(events, [](auto& events) { return events.context_menu.has_value(); }));
    {
        MutexLocker locker(events.mutex);
        EXPECT(events.context_menu->items.first_matching([](auto const& item) { return item.kind == XENON_MENU_ITEM_ACTION && item.title == "Reload"sv; }).has_value());
        EXPECT(events.context_menu->items.first().kind != XENON_MENU_ITEM_SEPARATOR);
        EXPECT(events.context_menu->items.last().kind != XENON_MENU_ITEM_SEPARATOR);
    }

    load(events, "data:text/html,<title>find</title><p>needle hay needle hay needle</p>"sv);
    EXPECT(has_title(events, "find"sv));
    EXPECT(wait_until(events, [](auto& events) { return !events.loading; }));
    xenon_engine_find_in_page(events.view, xenon_str("needle"sv), false, false);
    EXPECT(wait_until(events, [](auto& events) { return events.find_total.has_value(); }));
    {
        MutexLocker locker(events.mutex);
        EXPECT_EQ(events.find_total, Optional<size_t> { 3 });
    }

    xenon_engine_shutdown();
}
