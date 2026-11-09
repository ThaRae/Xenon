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
#include <Rune/Bridge/XenonBridgeAccessibility.h>

static constexpr auto event_timeout = AK::Duration::from_seconds(60);
static constexpr u32 viewport_width = 800;
static constexpr u32 viewport_height = 600;

struct ReceivedNode {
    i64 id { 0 };
    i64 parent_id { -1 };
    Vector<i64> child_ids;
    ByteString role;
    ByteString name;
    ByteString value;
    float x { 0 };
    float y { 0 };
    float width { 0 };
    float height { 0 };
    u32 flags { 0 };
    u8 checked { 0 };
    i32 level { 0 };
};

struct ReceivedTree {
    i64 root_id { -1 };
    i64 focused_id { -1 };
    double zoom { 0 };
    Vector<ReceivedNode> nodes;

    ReceivedNode const* find(StringView role, StringView name) const
    {
        for (auto const& node : nodes) {
            if (node.role == role && node.name == name)
                return &node;
        }
        return nullptr;
    }
};

struct Events {
    Mutex mutex;
    ConditionVariable condition { mutex };

    bool engine_ready { false };
    XenonViewId view { 0 };
    Vector<ByteString> titles;
    bool loading { false };
    size_t trees_received { 0 };
    Optional<ReceivedTree> tree;
    Vector<i64> focus_changes;
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
    callbacks.view_title_changed = [](void* context, XenonViewId view, XenonStr title) {
        update(context, [&](Events& events) {
            if (view == events.view)
                events.titles.append(byte_string_from(title));
        });
    };
    callbacks.view_loading_changed = [](void* context, XenonViewId view, bool is_loading) {
        update(context, [&](Events& events) {
            if (view == events.view)
                events.loading = is_loading;
        });
    };
    return callbacks;
}

static XenonAccessibilityCallbacks accessibility_callbacks_for(Events& events)
{
    XenonAccessibilityCallbacks callbacks {};
    callbacks.context = &events;
    callbacks.tree_changed = [](void* context, XenonViewId view, XenonAccessibilityTree const* tree) {
        ReceivedTree received { tree->root_id, tree->focused_id, tree->zoom, {} };
        for (size_t i = 0; i < tree->node_count; ++i) {
            auto const& node = tree->nodes[i];
            ReceivedNode copy {
                .id = node.id,
                .parent_id = node.parent_id,
                .child_ids = {},
                .role = byte_string_from(node.role),
                .name = byte_string_from(node.name),
                .value = byte_string_from(node.value),
                .x = node.x,
                .y = node.y,
                .width = node.width,
                .height = node.height,
                .flags = node.flags,
                .checked = node.checked,
                .level = node.level,
            };
            for (size_t child = 0; child < node.child_count; ++child)
                copy.child_ids.append(node.child_ids[child]);
            received.nodes.append(move(copy));
        }
        update(context, [&](Events& events) {
            if (view != events.view)
                return;
            ++events.trees_received;
            events.tree = move(received);
        });
    };
    callbacks.focus_changed = [](void* context, XenonViewId view, int64_t node) {
        update(context, [&](Events& events) {
            if (view == events.view)
                events.focus_changes.append(node);
        });
    };
    return callbacks;
}

static bool tree_has(Events& events, StringView role, StringView name)
{
    return wait_until(events, [&](auto& events) { return events.tree.has_value() && events.tree->find(role, name); });
}

static i64 node_id(Events& events, StringView role, StringView name)
{
    MutexLocker locker(events.mutex);
    auto const* node = events.tree->find(role, name);
    return node ? node->id : -1;
}

TEST_CASE(page_accessibility_trees_and_actions_cross_the_accessibility_abi)
{
    Events events;
    auto ui_callbacks = ui_callbacks_for(events);
    auto accessibility_callbacks = accessibility_callbacks_for(events);
    xenon_engine_set_accessibility_callbacks(&accessibility_callbacks);

    char const* arguments[] = { "TestXenonRuneAccessibility", "--temporary-profile" };
    EXPECT(xenon_engine_start(&ui_callbacks, 2, arguments));

    static constexpr auto page = "data:text/html,<title>a11y</title><body style='margin:0'>"
                                 "<h2 style='margin:0;height:40px'>Settings</h2>"
                                 "<button style='position:absolute;left:10px;top:50px;width:100px;height:30px' onclick=\"document.title='pressed'\">Save</button>"
                                 "<input aria-label=Name style='position:absolute;left:10px;top:100px;width:200px;height:24px' value=old oninput=\"document.title='typed-'+this.value\">"
                                 "<label style='position:absolute;left:10px;top:150px'><input type=checkbox checked>Remember</label>"
                                 "</body>"sv;
    {
        MutexLocker locker(events.mutex);
        xenon_engine_set_window_viewport(XENON_WINDOW_NONE, viewport_width, viewport_height, 1.0);
        events.view = xenon_engine_new_tab(XENON_WINDOW_NONE, xenon_str(page), XENON_TAB_INDEX_END, true);
    }
    EXPECT_NE(events.view, 0u);
    EXPECT(wait_until(events, [](auto& events) { return events.engine_ready; }));
    EXPECT(wait_until(events, [](auto& events) { return events.titles.contains_slow("a11y"sv) && !events.loading; }));

    {
        MutexLocker locker(events.mutex);
        EXPECT(!events.tree.has_value());
    }

    xenon_engine_set_accessibility_enabled(true);
    EXPECT(tree_has(events, "button"sv, "Save"sv));
    {
        MutexLocker locker(events.mutex);
        auto const& tree = *events.tree;
        EXPECT_EQ(tree.zoom, 1.0);
        auto const* root = tree.nodes.first_matching([&](auto const& node) { return node.id == tree.root_id; }).ptr();
        EXPECT(root);
        if (root) {
            EXPECT_EQ(root->role, "document"sv);
            EXPECT(!root->child_ids.is_empty());
        }

        auto const* heading = tree.find("heading"sv, "Settings"sv);
        EXPECT(heading);
        if (heading)
            EXPECT_EQ(heading->level, 2);

        auto const* button = tree.find("button"sv, "Save"sv);
        EXPECT(button);
        if (button) {
            EXPECT_EQ(button->x, 10.0f);
            EXPECT_EQ(button->y, 50.0f);
            EXPECT_EQ(button->width, 100.0f);
            EXPECT_EQ(button->height, 30.0f);
        }

        auto const* field = tree.find("textbox"sv, "Name"sv);
        EXPECT(field);
        if (field) {
            EXPECT_EQ(field->value, "old"sv);
            EXPECT(field->flags & XENON_ACCESSIBILITY_EDITABLE);
        }

        auto const* checkbox = tree.nodes.first_matching([](auto const& node) { return node.role == "checkbox"sv; }).ptr();
        EXPECT(checkbox);
        if (checkbox)
            EXPECT_EQ(checkbox->checked, static_cast<u8>(XENON_ACCESSIBILITY_TOGGLE_ON));
    }

    auto field = node_id(events, "textbox"sv, "Name"sv);
    xenon_engine_accessibility_action(events.view, field, XENON_ACCESSIBILITY_ACTION_FOCUS);
    EXPECT(wait_until(events, [&](auto& events) { return events.focus_changes.contains_slow(field); }));
    EXPECT(wait_until(events, [&](auto& events) { return events.tree->focused_id == field; }));

    xenon_engine_accessibility_set_value(events.view, field, xenon_str("new value"sv));
    EXPECT(wait_until(events, [](auto& events) { return events.titles.contains_slow("typed-new value"sv); }));
    EXPECT(wait_until(events, [&](auto& events) {
        auto const* node = events.tree->find("textbox"sv, "Name"sv);
        return node && node->value == "new value"sv;
    }));

    xenon_engine_accessibility_action(events.view, node_id(events, "button"sv, "Save"sv), XENON_ACCESSIBILITY_ACTION_PRESS);
    EXPECT(wait_until(events, [](auto& events) { return events.titles.contains_slow("pressed"sv); }));

    size_t trees_before_navigation = 0;
    {
        MutexLocker locker(events.mutex);
        trees_before_navigation = events.trees_received;
    }
    xenon_engine_load(events.view, xenon_str("data:text/html,<title>second</title><a href=%23x>Next page</a>"sv));
    EXPECT(tree_has(events, "link"sv, "Next page"sv));
    {
        MutexLocker locker(events.mutex);
        EXPECT(events.trees_received > trees_before_navigation);
    }

    xenon_engine_shutdown();
}
