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
#include <Rune/Bridge/XenonBridgeBookmarks.h>

static constexpr auto event_timeout = AK::Duration::from_seconds(60);

struct ShownItem {
    u32 kind { 0 };
    ByteString id;
    ByteString title;
    ByteString url;
};

struct ShownMenuItem {
    u32 kind { 0 };
    u32 id { 0 };
    ByteString title;
    bool checked { false };
};

struct ShownMenu {
    u64 id { 0 };
    bool at_page_position { false };
    Vector<ShownMenuItem> items;
};

struct ShownDialog {
    u64 id { 0 };
    u32 kind { 0 };
    ByteString url;
    ByteString title;
    ByteString selected_folder_id;
    Vector<ByteString> folder_paths;
    Vector<ByteString> folder_ids;
};

struct BookmarkEvents {
    Mutex mutex;
    ConditionVariable condition { mutex };

    bool engine_ready { false };
    XenonViewId view { 0 };
    Vector<ByteString> titles;
    bool loading { false };

    size_t bookmark_updates { 0 };
    Vector<ShownItem> items;
    Optional<u32> visibility;
    Optional<ShownMenu> menu;
    Optional<ShownDialog> dialog;
};

static ByteString byte_string_from(XenonStr string)
{
    if (!string.data)
        return {};
    return ByteString { string.data, string.length };
}

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

template<typename Update>
static void update(void* context, Update update)
{
    auto& events = *static_cast<BookmarkEvents*>(context);
    MutexLocker locker(events.mutex);
    update(events);
    events.condition.broadcast();
}

static XenonUiCallbacks ui_callbacks_for(BookmarkEvents& events)
{
    XenonUiCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.engine_ready = [](void* context) {
        update(context, [](BookmarkEvents& events) { events.engine_ready = true; });
    };
    callbacks.view_title_changed = [](void* context, XenonViewId view, XenonStr title) {
        update(context, [&](BookmarkEvents& events) {
            if (view == events.view)
                events.titles.append(byte_string_from(title));
        });
    };
    callbacks.view_loading_changed = [](void* context, XenonViewId view, bool is_loading) {
        update(context, [&](BookmarkEvents& events) {
            if (view == events.view)
                events.loading = is_loading;
        });
    };
    return callbacks;
}

static XenonBookmarksCallbacks bookmarks_callbacks_for(BookmarkEvents& events)
{
    XenonBookmarksCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.bookmarks_changed = [](void* context, XenonBookmarkItem const* items, size_t count) {
        update(context, [&](BookmarkEvents& events) {
            events.items.clear();
            for (size_t i = 0; i < count; ++i)
                events.items.append({ items[i].kind, byte_string_from(items[i].id), byte_string_from(items[i].title), byte_string_from(items[i].url) });
            ++events.bookmark_updates;
        });
    };
    callbacks.bar_settings_changed = [](void* context, XenonBookmarksBarSettings const* settings) {
        update(context, [&](BookmarkEvents& events) { events.visibility = settings->visibility; });
    };
    callbacks.context_menu_requested = [](void* context, XenonViewId, XenonMenu const* menu, bool at_page_position) {
        update(context, [&](BookmarkEvents& events) {
            ShownMenu shown { .id = menu->id, .at_page_position = at_page_position, .items = {} };
            for (size_t i = 0; i < menu->item_count; ++i) {
                auto const& item = menu->items[i];
                shown.items.append({ item.kind, item.id, byte_string_from(item.title), item.checked });
            }
            events.menu = move(shown);
        });
    };
    callbacks.dialog_opened = [](void* context, XenonBookmarkDialog const* dialog) {
        update(context, [&](BookmarkEvents& events) {
            ShownDialog shown {
                .id = dialog->id,
                .kind = dialog->kind,
                .url = byte_string_from(dialog->url),
                .title = byte_string_from(dialog->title),
                .selected_folder_id = byte_string_from(dialog->selected_folder_id),
                .folder_paths = {},
                .folder_ids = {},
            };
            for (size_t i = 0; i < dialog->folder_count; ++i) {
                shown.folder_paths.append(byte_string_from(dialog->folders[i].path));
                shown.folder_ids.append(byte_string_from(dialog->folders[i].id));
            }
            events.dialog = move(shown);
        });
    };
    return callbacks;
}

template<typename Condition>
static bool wait_until(BookmarkEvents& events, Condition condition)
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

static Optional<ShownItem> item_titled(BookmarkEvents& events, StringView title)
{
    MutexLocker locker(events.mutex);
    for (auto const& item : events.items) {
        if (item.kind != XENON_BOOKMARK_ITEM_FOLDER_END && item.title == title)
            return item;
    }
    return {};
}

static bool has_item_titled(BookmarkEvents& events, StringView title)
{
    return wait_until(events, [&](auto& events) {
        return events.items.first_matching([&](auto const& item) { return item.kind != XENON_BOOKMARK_ITEM_FOLDER_END && item.title == title; }).has_value();
    });
}

static ShownMenu open_menu(BookmarkEvents& events, StringView id, StringView target_folder_id, StringView parent_folder_id = {})
{
    {
        MutexLocker locker(events.mutex);
        events.menu.clear();
    }
    xenon_engine_show_bookmark_context_menu(events.view, xenon_str(id), xenon_str(target_folder_id), xenon_str(parent_folder_id), 10, 10);
    EXPECT(wait_until(events, [](auto& events) { return events.menu.has_value(); }));
    MutexLocker locker(events.mutex);
    return events.menu.value_or({});
}

static void pick(ShownMenu const& menu, StringView title)
{
    auto item = menu.items.first_matching([&](auto const& item) { return item.kind == XENON_MENU_ITEM_ACTION && item.title == title; });
    EXPECT(item.has_value());
    if (item.has_value())
        xenon_engine_activate_bookmark_menu_item(menu.id, item->id);
}

static ShownDialog wait_for_dialog(BookmarkEvents& events)
{
    EXPECT(wait_until(events, [](auto& events) { return events.dialog.has_value(); }));
    MutexLocker locker(events.mutex);
    auto dialog = events.dialog.value_or({});
    events.dialog.clear();
    return dialog;
}

static Optional<size_t> depth_of(Vector<ShownItem> const& items, StringView title)
{
    size_t depth = 0;
    for (auto const& item : items) {
        if (item.kind == XENON_BOOKMARK_ITEM_FOLDER_END && depth > 0)
            --depth;
        if (item.kind == XENON_BOOKMARK_ITEM_BOOKMARK && item.title == title)
            return depth;
        if (item.kind == XENON_BOOKMARK_ITEM_FOLDER_BEGIN)
            ++depth;
    }
    return {};
}

static size_t bookmark_updates(BookmarkEvents& events)
{
    MutexLocker locker(events.mutex);
    return events.bookmark_updates;
}

TEST_CASE(bookmarks_round_trip_through_the_bookmarks_abi)
{
    BookmarkEvents events;
    auto ui_callbacks = ui_callbacks_for(events);
    auto bookmarks_callbacks = bookmarks_callbacks_for(events);
    xenon_engine_set_bookmarks_callbacks(&bookmarks_callbacks);

    char const* arguments[] = { "TestXenonRuneBookmarks", "--temporary-profile" };
    EXPECT(xenon_engine_start(&ui_callbacks, 2, arguments));
    xenon_engine_bookmarks_start();

    static constexpr auto page = "data:text/html,<title>Bookmarked page</title>"sv;
    {
        MutexLocker locker(events.mutex);
        xenon_engine_set_window_viewport(XENON_WINDOW_NONE, 800, 600, 1.0);
        events.view = xenon_engine_new_tab(XENON_WINDOW_NONE, xenon_str(page), XENON_TAB_INDEX_END, true);
    }
    EXPECT(wait_until(events, [](auto& events) { return events.engine_ready && events.bookmark_updates > 0 && events.visibility.has_value(); }));
    EXPECT(wait_until(events, [](auto& events) { return events.titles.contains_slow("Bookmarked page"sv) && !events.loading; }));
    xenon_engine_activate_tab(events.view);
    xenon_engine_set_focused(events.view, true);

    xenon_engine_activate_action(XENON_ACTION_TOGGLE_BOOKMARK);
    auto star_dialog = wait_for_dialog(events);
    EXPECT_EQ(star_dialog.kind, static_cast<u32>(XENON_BOOKMARK_DIALOG_ADD_BOOKMARK));
    EXPECT_EQ(star_dialog.url, page);
    EXPECT_EQ(star_dialog.title, "Bookmarked page"sv);
    xenon_engine_close_bookmark_dialog(star_dialog.id, false, {}, {}, {});

    auto bar_menu = open_menu(events, {}, {});
    EXPECT(!bar_menu.at_page_position);
    EXPECT(bar_menu.items.first_matching([](auto const& item) { return item.kind == XENON_MENU_ITEM_SUBMENU_BEGIN; }).has_value());
    pick(bar_menu, "New Folder…"sv);
    auto folder_dialog = wait_for_dialog(events);
    EXPECT_EQ(folder_dialog.kind, static_cast<u32>(XENON_BOOKMARK_DIALOG_ADD_FOLDER));
    EXPECT(folder_dialog.folder_paths.is_empty());
    xenon_engine_close_bookmark_dialog(folder_dialog.id, true, {}, xenon_str("Work"sv), {});
    EXPECT(has_item_titled(events, "Work"sv));
    auto folder = item_titled(events, "Work"sv);
    EXPECT(folder.has_value() && folder->kind == XENON_BOOKMARK_ITEM_FOLDER_BEGIN);
    auto folder_id = folder.has_value() ? folder->id : ByteString {};

    pick(open_menu(events, {}, {}), "Add Bookmark…"sv);
    auto add_dialog = wait_for_dialog(events);
    EXPECT_EQ(add_dialog.kind, static_cast<u32>(XENON_BOOKMARK_DIALOG_ADD_BOOKMARK));
    EXPECT_EQ(add_dialog.url, page);
    EXPECT_EQ(add_dialog.title, "Bookmarked page"sv);
    EXPECT_EQ(add_dialog.folder_paths.size(), 1u);
    EXPECT_EQ(add_dialog.folder_paths.first(), "Work"sv);
    auto updates = bookmark_updates(events);
    xenon_engine_close_bookmark_dialog(add_dialog.id, true, xenon_str("https://ladybird.org/"sv), xenon_str("Xenon test"sv), xenon_str(folder_id));
    EXPECT(has_item_titled(events, "Xenon test"sv));
    {
        MutexLocker locker(events.mutex);
        EXPECT(events.bookmark_updates > updates);
        auto folder_begin = events.items.find_first_index_if([&](auto const& item) { return item.kind == XENON_BOOKMARK_ITEM_FOLDER_BEGIN && item.id == folder_id; });
        EXPECT(folder_begin.has_value());
        auto begin = folder_begin.value_or(0);
        EXPECT(begin + 2 < events.items.size());
        if (begin + 2 < events.items.size()) {
            EXPECT_EQ(events.items[begin + 1].url, "https://ladybird.org/"sv);
            EXPECT_EQ(events.items[begin + 2].kind, static_cast<u32>(XENON_BOOKMARK_ITEM_FOLDER_END));
        }
    }
    auto bookmark_id = item_titled(events, "Xenon test"sv).map([](auto const& item) { return item.id; }).value_or({});

    pick(open_menu(events, bookmark_id, {}, folder_id), "Edit…"sv);
    auto edit_dialog = wait_for_dialog(events);
    EXPECT_EQ(edit_dialog.kind, static_cast<u32>(XENON_BOOKMARK_DIALOG_EDIT_BOOKMARK));
    EXPECT_EQ(edit_dialog.url, "https://ladybird.org/"sv);
    EXPECT_EQ(edit_dialog.selected_folder_id, folder_id);
    xenon_engine_close_bookmark_dialog(edit_dialog.id, true, xenon_str("x:rejected"sv), xenon_str("Rejected"sv), xenon_str(folder_id));

    pick(open_menu(events, bookmark_id, {}, folder_id), "Edit…"sv);
    edit_dialog = wait_for_dialog(events);
    xenon_engine_close_bookmark_dialog(edit_dialog.id, true, xenon_str("ladybird.org/news"sv), xenon_str("Xenon news"sv), {});
    EXPECT(wait_until(events, [](auto& events) { return depth_of(events.items, "Xenon news"sv) == Optional<size_t> { 0 }; }));
    {
        MutexLocker locker(events.mutex);
        EXPECT(!events.items.first_matching([](auto const& item) { return item.title == "Rejected"sv; }).has_value());
        auto news = events.items.first_matching([](auto const& item) { return item.title == "Xenon news"sv; });
        EXPECT(news.has_value() && news->url == "https://ladybird.org/news"sv);
    }

    pick(open_menu(events, folder_id, folder_id), "Edit Folder…"sv);
    auto edit_folder_dialog = wait_for_dialog(events);
    EXPECT_EQ(edit_folder_dialog.kind, static_cast<u32>(XENON_BOOKMARK_DIALOG_EDIT_FOLDER));
    EXPECT_EQ(edit_folder_dialog.title, "Work"sv);
    EXPECT(edit_folder_dialog.folder_paths.is_empty());
    xenon_engine_close_bookmark_dialog(edit_folder_dialog.id, false, {}, xenon_str("Ignored"sv), {});

    pick(open_menu(events, bookmark_id, {}), "Delete"sv);
    EXPECT(wait_until(events, [](auto& events) { return !events.items.first_matching([](auto const& item) { return item.title == "Xenon news"sv; }).has_value(); }));
    EXPECT(item_titled(events, "Work"sv).has_value());

    auto show_menu = open_menu(events, {}, {});
    Optional<u32> never;
    for (size_t i = 0, depth = 0; i < show_menu.items.size(); ++i) {
        auto const& item = show_menu.items[i];
        if (item.kind == XENON_MENU_ITEM_SUBMENU_BEGIN)
            ++depth;
        if (depth > 0 && item.kind == XENON_MENU_ITEM_ACTION && item.title.starts_with("Never"sv))
            never = item.id;
    }
    EXPECT(never.has_value());
    xenon_engine_activate_bookmark_menu_item(show_menu.id, never.value_or(0));
    EXPECT(wait_until(events, [](auto& events) { return events.visibility == static_cast<u32>(XENON_BOOKMARKS_BAR_NEVER); }));

    xenon_engine_shutdown();
}
