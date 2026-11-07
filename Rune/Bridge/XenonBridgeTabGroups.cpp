/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/HashTable.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <LibCore/Directory.h>
#include <LibCore/EventLoop.h>
#include <LibCore/File.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <LibWebView/BrowsingSession.h>
#include <Rune/Bridge/Application.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/TabGroupHooks.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgeTabGroups.h>

namespace Xenon {

static XenonTabGroupCallbacks s_tab_group_callbacks {};
static HashTable<WebView::SessionWindowId> s_windows_with_saved_groups;
static bool s_forgetting_unrestored_groups_scheduled { false };

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

static Optional<ByteString> saved_tab_groups_path()
{
    auto const& session = WebView::Application::default_session();
    if (!session.session_store || !session.session_store->is_persistent())
        return {};
    return LexicalPath::join(WebView::Application::profile().paths().data, "TabGroups.json"sv).string();
}

static String saved_tab_groups_key(WebView::SessionWindowId window_id)
{
    return MUST(String::formatted("window{}", window_id));
}

static JsonObject read_saved_tab_groups()
{
    auto path = saved_tab_groups_path();
    if (!path.has_value() || !FileSystem::exists(*path))
        return {};
    auto file = Core::File::open(*path, Core::File::OpenMode::Read);
    if (file.is_error())
        return {};
    auto contents = file.value()->read_until_eof();
    if (contents.is_error())
        return {};
    auto value = JsonValue::from_string(contents.value());
    if (value.is_error() || !value.value().is_object())
        return {};
    return move(value.value().as_object());
}

static ErrorOr<void> write_json_object(ByteString const& path, JsonObject const& object)
{
    TRY(Core::Directory::create(LexicalPath { path }.parent(), Core::Directory::CreateDirectories::Yes));
    auto temporary_path = ByteString::formatted("{}.tmp", path);
    {
        auto file = TRY(Core::File::open(temporary_path, Core::File::OpenMode::Write));
        TRY(file->write_until_depleted(object.serialized()));
    }
    TRY(Core::System::rename(temporary_path, path));
    return {};
}

static void write_saved_tab_groups(JsonObject const& saved_groups)
{
    auto path = saved_tab_groups_path();
    if (!path.has_value())
        return;
    if (auto result = write_json_object(*path, saved_groups); result.is_error())
        dbgln("Xenon: Unable to save the tab groups to {}: {}", *path, result.error());
}

static void forget_groups_of_windows_not_restored()
{
    if (exchange(s_forgetting_unrestored_groups_scheduled, true))
        return;

    Core::deferred_invoke([] {
        auto* model = Engine::the().model();
        if (!model)
            return;
        auto saved_groups = read_saved_tab_groups();
        JsonObject kept_groups;
        for (auto id : model->windows_by_activation()) {
            auto const* window = model->window(id);
            if (!window || !window->session_window_id.has_value())
                continue;
            auto key = saved_tab_groups_key(*window->session_window_id);
            if (auto value = saved_groups.get(key); value.has_value())
                kept_groups.set(key, *value);
        }
        if (kept_groups.size() != saved_groups.size())
            write_saved_tab_groups(kept_groups);
    });
}

static Vector<SavedTabGroup> saved_groups_of(WebView::SessionWindowId window_id)
{
    Vector<SavedTabGroup> groups;
    auto saved_file = read_saved_tab_groups();
    auto saved_groups = saved_file.get_array(saved_tab_groups_key(window_id));
    if (!saved_groups.has_value())
        return groups;

    saved_groups->for_each([&](JsonValue const& value) {
        if (!value.is_object())
            return;
        auto const& saved = value.as_object();
        SavedTabGroup group;
        if (auto title = saved.get_string("title"sv); title.has_value())
            group.title = *title;
        if (auto color = saved.get_string("color"sv); color.has_value())
            group.color = tab_group_color_from_name(*color).value_or(TabGroupColor::Rose);
        group.collapsed = saved.get_bool("collapsed"sv).value_or(false);
        if (auto tabs = saved.get_array("tabs"sv); tabs.has_value()) {
            tabs->for_each([&](JsonValue const& tab) {
                if (auto id = tab.get_i64(); id.has_value())
                    group.tabs.append(*id);
            });
        }
        groups.append(move(group));
    });
    if (!groups.is_empty())
        s_windows_with_saved_groups.set(window_id);
    return groups;
}

static void save_groups_of(BrowserModel const& model, BrowserModel::Window const& window)
{
    if (window.is_private == WebView::IsPrivate::Yes || !window.session_window_id.has_value())
        return;
    if (window.groups.is_empty() && !s_windows_with_saved_groups.contains(*window.session_window_id))
        return;
    s_windows_with_saved_groups.set(*window.session_window_id);

    JsonArray groups;
    for (auto const& group : window.groups) {
        JsonArray tab_ids;
        for (auto view : group.tabs) {
            if (auto const* tab = model.tab(view); tab && tab->session_tab_id.has_value())
                tab_ids.must_append(*tab->session_tab_id);
        }
        JsonObject saved;
        saved.set("title"sv, group.title);
        saved.set("color"sv, MUST(String::from_utf8(tab_group_color_name(group.color))).to_ascii_lowercase());
        saved.set("collapsed"sv, group.collapsed);
        saved.set("tabs"sv, move(tab_ids));
        groups.must_append(move(saved));
    }

    auto saved_groups = read_saved_tab_groups();
    auto key = saved_tab_groups_key(*window.session_window_id);
    if (groups.is_empty())
        saved_groups.remove(key);
    else
        saved_groups.set(key, move(groups));
    write_saved_tab_groups(saved_groups);
}

static void report_groups_of(BrowserModel::Window const& window)
{
    auto callback = s_tab_group_callbacks.groups_changed;
    if (!callback)
        return;
    Vector<XenonTabGroup> groups;
    groups.ensure_capacity(window.groups.size());
    for (auto const& group : window.groups) {
        groups.unchecked_append({
            .id = group.id,
            .title = xenon_str(group.title),
            .color = static_cast<u32>(to_underlying(group.color)),
            .collapsed = group.collapsed,
            .tabs = group.tabs.data(),
            .tab_count = group.tabs.size(),
        });
    }
    callback(s_tab_group_callbacks.context, window.id, groups.data(), groups.size());
}

void install_tab_group_hooks(BrowserModel& model)
{
    model.set_tab_group_hooks({
        .new_tab_page_url = [] { return Application::settings().new_tab_page_url(); },
        .window_opened = [] { forget_groups_of_windows_not_restored(); },
        .groups_changed = [&model](BrowserModel::Window const& window) {
            report_groups_of(window);
            save_groups_of(model, window);
        },
        .edit_requested = [](BrowserModel::Window const& window, TabGroupId group) {
            if (auto callback = s_tab_group_callbacks.edit_requested)
                callback(s_tab_group_callbacks.context, window.id, group);
        },
        .saved_groups = [](WebView::SessionWindowId window_id) { return saved_groups_of(window_id); },
    });
}

}

using Xenon::BrowserModel;
using Xenon::Engine;

static void post_to_model(Function<void(BrowserModel&)> command)
{
    Engine::the().post([command = move(command)] {
        if (auto* model = Engine::the().model())
            command(*model);
    });
}

static Optional<Xenon::TabGroupId> group_from(XenonTabGroupId group)
{
    if (group == XENON_TAB_GROUP_NONE)
        return {};
    return group;
}

void xenon_engine_set_tab_group_callbacks(XenonTabGroupCallbacks const* callbacks)
{
    Xenon::s_tab_group_callbacks = callbacks ? *callbacks : XenonTabGroupCallbacks {};
}

void xenon_engine_add_tab_to_new_group(XenonViewId view)
{
    post_to_model([view](BrowserModel& model) { model.add_tab_to_new_group(view); });
}

void xenon_engine_add_tab_to_group(XenonViewId view, XenonTabGroupId group)
{
    post_to_model([view, group](BrowserModel& model) { model.add_tab_to_group(view, group); });
}

void xenon_engine_remove_tab_from_group(XenonViewId view)
{
    post_to_model([view](BrowserModel& model) { model.remove_tab_from_group(view); });
}

void xenon_engine_set_tab_group_title(XenonTabGroupId group, XenonStr title)
{
    auto title_string = String::from_utf8_with_replacement_character(title.data && title.length ? StringView { title.data, title.length } : StringView {});
    post_to_model([group, title = move(title_string)](BrowserModel& model) mutable { model.set_tab_group_title(group, move(title)); });
}

void xenon_engine_set_tab_group_color(XenonTabGroupId group, uint32_t color)
{
    if (color >= Xenon::tab_group_color_count)
        return;
    post_to_model([group, color](BrowserModel& model) { model.set_tab_group_color(group, static_cast<Xenon::TabGroupColor>(color)); });
}

void xenon_engine_set_tab_group_collapsed(XenonTabGroupId group, bool collapsed)
{
    post_to_model([group, collapsed](BrowserModel& model) { model.set_tab_group_collapsed(group, collapsed); });
}

void xenon_engine_ungroup_tabs(XenonTabGroupId group)
{
    post_to_model([group](BrowserModel& model) { model.ungroup_tabs(group); });
}

void xenon_engine_close_tab_group(XenonTabGroupId group)
{
    post_to_model([group](BrowserModel& model) {
        auto views = model.prepare_to_close_tab_group(group);
        for (auto view : views.in_reverse()) {
            if (auto* closing = Engine::the().view(view))
                closing->request_close_with_prompt();
            else
                Engine::the().close_tab(view);
        }
    });
}

void xenon_engine_new_tab_in_group(XenonTabGroupId group)
{
    post_to_model([group](BrowserModel& model) { (void)model.open_new_tab_in_group(group); });
}

void xenon_engine_move_tab_group_to_new_window(XenonTabGroupId group)
{
    post_to_model([group](BrowserModel& model) { (void)model.move_tab_group_to_new_window(group); });
}

void xenon_engine_drop_tab(XenonViewId view, uint32_t index, XenonTabGroupId group)
{
    post_to_model([view, index, group](BrowserModel& model) {
        model.drop_tab(view, index, group_from(group));
        if (auto* split_views = Engine::the().split_views())
            split_views->splits().keep_together(view);
    });
}
