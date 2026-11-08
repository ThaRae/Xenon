/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/ConditionVariable.h>
#include <AK/Mutex.h>
#include <AK/Optional.h>
#include <AK/Platform.h>
#include <AK/Time.h>
#include <AK/Vector.h>
#include <LibCore/File.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <LibTest/TestCase.h>
#include <Rune/Bridge/XenonBridge.h>
#include <Rune/Bridge/XenonBridgeDrop.h>
#include <Rune/Bridge/XenonBridgePage.h>
#include <Rune/Bridge/XenonBridgePrompts.h>
#include <Rune/Bridge/XenonBridgeUpdater.h>
#include <stdlib.h>

static constexpr auto event_timeout = AK::Duration::from_seconds(60);

struct ShownDialog {
    XenonPromptId id { 0 };
    u32 kind { 0 };
    ByteString title;
};

struct ShownColorPicker {
    XenonPromptId id { 0 };
    u32 rgb { 0 };
};

struct ShownDatePicker {
    XenonPromptId id { 0 };
    ByteString input_type;
    ByteString value;
    ByteString minimum;
};

struct PromptEvents {
    Mutex mutex;
    ConditionVariable condition { mutex };

    bool engine_ready { false };
    XenonViewId view { 0 };
    Vector<ByteString> titles;
    bool loading { false };

    Optional<ShownDialog> dialog;
    Optional<size_t> find_total;
    Optional<ByteString> find_selection;
    Optional<ShownColorPicker> color_picker;
    Optional<ShownDatePicker> date_picker;
    Optional<ByteString> ready_update;
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
    auto& events = *static_cast<PromptEvents*>(context);
    MutexLocker locker(events.mutex);
    update(events);
    events.condition.broadcast();
}

static XenonUiCallbacks ui_callbacks_for(PromptEvents& events)
{
    XenonUiCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.engine_ready = [](void* context) {
        update(context, [](PromptEvents& events) { events.engine_ready = true; });
    };
    callbacks.view_title_changed = [](void* context, XenonViewId view, XenonStr title) {
        update(context, [&](PromptEvents& events) {
            if (view == events.view)
                events.titles.append(byte_string_from(title));
        });
    };
    callbacks.view_loading_changed = [](void* context, XenonViewId view, bool is_loading) {
        update(context, [&](PromptEvents& events) {
            if (view == events.view)
                events.loading = is_loading;
        });
    };
    return callbacks;
}

static XenonPageCallbacks page_callbacks_for(PromptEvents& events)
{
    XenonPageCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.dialog_opened = [](void* context, XenonViewId, XenonDialog const* dialog) {
        update(context, [&](PromptEvents& events) {
            events.dialog = ShownDialog { dialog->id, dialog->kind, byte_string_from(dialog->title) };
        });
    };
    callbacks.find_result = [](void* context, XenonViewId, size_t, bool has_total, size_t total) {
        update(context, [&](PromptEvents& events) {
            if (has_total)
                events.find_total = total;
        });
    };
    return callbacks;
}

static XenonPromptCallbacks prompt_callbacks_for(PromptEvents& events)
{
    XenonPromptCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.find_selection = [](void* context, XenonViewId, uint64_t request, XenonStr text) {
        update(context, [&](PromptEvents& events) {
            if (request == 42)
                events.find_selection = byte_string_from(text);
        });
    };
    callbacks.color_picker_requested = [](void* context, XenonViewId, XenonPromptId id, uint32_t rgb) {
        update(context, [&](PromptEvents& events) { events.color_picker = ShownColorPicker { id, rgb }; });
    };
    callbacks.date_picker_requested = [](void* context, XenonViewId, XenonDatePicker const* picker) {
        update(context, [&](PromptEvents& events) {
            events.date_picker = ShownDatePicker { picker->id, byte_string_from(picker->input_type), byte_string_from(picker->value), byte_string_from(picker->minimum) };
        });
    };
    return callbacks;
}

static XenonUpdaterCallbacks updater_callbacks_for(PromptEvents& events)
{
    XenonUpdaterCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.status_changed = [](void* context, XenonStr ready_version, bool) {
        update(context, [&](PromptEvents& events) { events.ready_update = byte_string_from(ready_version); });
    };
    return callbacks;
}

template<typename Condition>
static bool wait_until(PromptEvents& events, Condition condition)
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

static bool has_title(PromptEvents& events, StringView title)
{
    return wait_until(events, [&](auto& events) { return events.titles.contains_slow(title); });
}

static void load(PromptEvents& events, StringView url, StringView title)
{
    {
        MutexLocker locker(events.mutex);
        events.titles.clear();
        events.dialog.clear();
        events.find_total.clear();
        events.color_picker.clear();
        events.date_picker.clear();
    }
    xenon_engine_load(events.view, xenon_str(url));
    EXPECT(has_title(events, title));
    EXPECT(wait_until(events, [](auto& events) { return !events.loading; }));
}

static void click(XenonViewId view, float x, float y)
{
    XenonMouseEvent event {};
    event.x = x;
    event.y = y;
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

static void drop_files(XenonViewId view, Vector<ByteString> const& paths)
{
    Vector<XenonStr> raw;
    for (auto const& path : paths)
        raw.append(xenon_str(path));
    xenon_engine_drag_event(view, XENON_DRAG_ENTER, 100, 100, 0, raw.data(), raw.size());
    xenon_engine_drag_event(view, XENON_DRAG_MOVE, 120, 120, 0, nullptr, 0);
    xenon_engine_drag_event(view, XENON_DRAG_DROP, 120, 120, 0, raw.data(), raw.size());
}

static ByteString write_file(ByteString const& directory, StringView name, StringView contents)
{
    auto path = ByteString::formatted("{}/{}", directory, name);
    auto file = MUST(Core::File::open(path, Core::File::OpenMode::Write));
    MUST(file->write_until_depleted(contents.bytes()));
    return path;
}

TEST_CASE(prompts_drops_and_handlers_round_trip_through_their_abis)
{
    char directory_template[] = "/tmp/xenon-rune-prompts-XXXXXX";
    VERIFY(mkdtemp(directory_template));
    ByteString directory { directory_template };
    MUST(Core::System::mkdir(ByteString::formatted("{}/config", directory), 0700));
    MUST(Core::System::mkdir(ByteString::formatted("{}/data", directory), 0700));
    MUST(Core::System::mkdir(ByteString::formatted("{}/data/applications", directory), 0700));
    auto launched = ByteString::formatted("{}/launched", directory);
#if defined(AK_OS_LINUX)
    write_file(directory, "data/applications/xenon-test-handler.desktop"sv,
        ByteString::formatted("[Desktop Entry]\nType=Application\nName=Xenon Test Handler\nExec=/bin/sh -c 'echo \"$1\" > {}' sh %u\nMimeType=x-scheme-handler/xenontest;\nNoDisplay=true\n", launched));
    write_file(directory, "config/mimeapps.list"sv, "[Default Applications]\nx-scheme-handler/xenontest=xenon-test-handler.desktop\n"sv);
#endif
    VERIFY(setenv("XDG_CONFIG_HOME", ByteString::formatted("{}/config", directory).characters(), 1) == 0);
    VERIFY(setenv("XDG_DATA_HOME", ByteString::formatted("{}/data", directory).characters(), 1) == 0);
    VERIFY(setenv("XENON_TEST_UPDATE_READY", "9.9.9", 1) == 0);

    PromptEvents events;
    auto ui_callbacks = ui_callbacks_for(events);
    auto page_callbacks = page_callbacks_for(events);
    auto prompt_callbacks = prompt_callbacks_for(events);
    auto updater_callbacks = updater_callbacks_for(events);
    xenon_engine_set_page_callbacks(&page_callbacks);
    xenon_engine_set_prompt_callbacks(&prompt_callbacks);
    xenon_engine_set_updater_callbacks(&updater_callbacks);

    char const* arguments[] = { "TestXenonRunePrompts", "--temporary-profile" };
    EXPECT(xenon_engine_start(&ui_callbacks, 2, arguments));

    static constexpr auto find_page = "data:text/html,<title>find</title><p id=p>needle hay needle hay needle</p><script>getSelection().setBaseAndExtent(p.firstChild,0,p.firstChild,6)</script>"sv;
    {
        MutexLocker locker(events.mutex);
        xenon_engine_set_window_viewport(XENON_WINDOW_NONE, 800, 600, 1.0);
        events.view = xenon_engine_new_tab(XENON_WINDOW_NONE, xenon_str(find_page), XENON_TAB_INDEX_END, true);
    }
    EXPECT_NE(events.view, 0u);
    EXPECT(wait_until(events, [](auto& events) { return events.engine_ready; }));
    EXPECT(has_title(events, "find"sv));
    EXPECT(wait_until(events, [](auto& events) { return !events.loading; }));

    EXPECT(wait_until(events, [](auto& events) { return events.ready_update.has_value(); }));
    {
        MutexLocker locker(events.mutex);
        EXPECT_EQ(events.ready_update, Optional<ByteString> { "9.9.9"sv });
    }

    xenon_engine_request_find_selection(events.view, 42);
    EXPECT(wait_until(events, [](auto& events) { return events.find_selection.has_value(); }));
    {
        MutexLocker locker(events.mutex);
        EXPECT_EQ(events.find_selection, Optional<ByteString> { "needle"sv });
    }
    xenon_engine_find_in_page(events.view, xenon_str("needle"sv), false, true);
    EXPECT(wait_until(events, [](auto& events) { return events.find_total.has_value(); }));
    {
        MutexLocker locker(events.mutex);
        EXPECT_EQ(events.find_total, Optional<size_t> { 3 });
    }
    xenon_engine_end_find(events.view);

    load(events, "data:text/html,<title>color</title><body style='margin:0'><input type=color value='%233366cc' style='width:200px;height:100px' oninput=\"document.title='input-'+this.value\" onchange=\"document.title='change-'+this.value\">"sv, "color"sv);
    click(events.view, 50, 50);
    if (!wait_until(events, [](auto& events) { return events.color_picker.has_value(); })) {
        FAIL("The colour picker didn't open");
        xenon_engine_shutdown();
        return;
    }
    XenonPromptId color_picker = 0;
    {
        MutexLocker locker(events.mutex);
        color_picker = events.color_picker->id;
        EXPECT_EQ(events.color_picker->rgb, 0x3366ccu);
    }
    xenon_engine_color_picker_update(events.view, color_picker, true, 0xff0000, false);
    EXPECT(has_title(events, "input-#ff0000"sv));
    xenon_engine_color_picker_update(events.view, color_picker, true, 0x00ff00, true);
    EXPECT(has_title(events, "change-#00ff00"sv));

    load(events, "data:text/html,<title>date</title><body style='margin:0'><input type=date value=2026-11-06 min=2026-11-02 style='width:300px;height:60px' onchange=\"document.title='date-'+this.value\">"sv, "date"sv);
    click(events.view, 284, 30);
    if (!wait_until(events, [](auto& events) { return events.date_picker.has_value(); })) {
        FAIL("The date picker didn't open");
        xenon_engine_shutdown();
        return;
    }
    XenonPromptId date_picker = 0;
    {
        MutexLocker locker(events.mutex);
        date_picker = events.date_picker->id;
        EXPECT_EQ(events.date_picker->input_type, "date"sv);
        EXPECT_EQ(events.date_picker->value, "2026-11-06"sv);
        EXPECT_EQ(events.date_picker->minimum, "2026-11-02"sv);
    }
    xenon_engine_close_date_picker(events.view, date_picker, true, xenon_str("2026-11-20"sv));
    EXPECT(has_title(events, "date-2026-11-20"sv));

    auto first_file = write_file(directory, "first.html"sv, "<title>first-dropped</title>"sv);
    auto second_file = write_file(directory, "second.txt"sv, "second"sv);

    load(events, "data:text/html,<title>drop</title><body style='margin:0'><div style='position:fixed;inset:0' ondragover='event.preventDefault()' ondrop=\"event.preventDefault();document.title='dropped-'+[...event.dataTransfer.files].map(f=>f.name).join('+')\"></div>"sv, "drop"sv);
    Vector<ByteString> dropped_files { first_file, second_file };
    drop_files(events.view, dropped_files);
    EXPECT(has_title(events, "dropped-first.html+second.txt"sv));

#if defined(AK_OS_LINUX)
    load(events, "data:text/html,<title>external</title><body style='margin:0'><a href='xenontest:hello' style='display:block;width:300px;height:100px'>open</a>"sv, "external"sv);
    click(events.view, 50, 50);
    if (!wait_until(events, [](auto& events) { return events.dialog.has_value(); })) {
        FAIL("The external application dialog didn't open");
        xenon_engine_shutdown();
        return;
    }
    {
        MutexLocker locker(events.mutex);
        EXPECT_EQ(events.dialog->kind, static_cast<u32>(XENON_DIALOG_EXTERNAL_URL));
        EXPECT_EQ(events.dialog->title, "Open Xenon Test Handler?"sv);
    }
    xenon_engine_close_dialog(events.view, events.dialog->id, false, {});
#endif

    xenon_engine_shutdown();

    EXPECT(!FileSystem::exists(launched));
    MUST(FileSystem::remove(directory, FileSystem::RecursionMode::Allowed));
}
