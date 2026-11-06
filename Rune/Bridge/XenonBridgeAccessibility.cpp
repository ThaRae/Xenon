/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/Math.h>
#include <AK/NumericLimits.h>
#include <AK/Vector.h>
#include <AK/WeakPtr.h>
#include <LibCore/Timer.h>
#include <LibWebView/Menu.h>
#include <Rune/Bridge/Accessibility.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgeAccessibility.h>

namespace Xenon {

static constexpr auto refresh_delay = AK::Duration::from_milliseconds(150);

static XenonAccessibilityCallbacks s_callbacks {};
static bool s_enabled { false };
static HashMap<XenonViewId, WeakPtr<View>> s_views;
static HashMap<XenonViewId, NonnullRefPtr<Core::Timer>> s_refresh_timers;

class ZoomObserver final : public WebView::Action::Observer {
public:
    explicit ZoomObserver(Function<void()> on_change)
        : m_on_change(move(on_change))
    {
    }

    virtual void on_text_changed(WebView::Action&) override { m_on_change(); }

private:
    Function<void()> m_on_change;
};

static XenonStr xenon_str(String const& string)
{
    auto bytes = string.bytes_as_string_view();
    return { bytes.characters_without_null_termination(), bytes.length() };
}

static double number_or_nan(Optional<double> value)
{
    return value.value_or(AK::NaN<double>);
}

static u8 toggle_from(WebView::AccessibilityNodeData::CheckedState state)
{
    using State = WebView::AccessibilityNodeData::CheckedState;
    switch (state) {
    case State::Unchecked:
        return XENON_ACCESSIBILITY_TOGGLE_OFF;
    case State::Checked:
        return XENON_ACCESSIBILITY_TOGGLE_ON;
    case State::Mixed:
        return XENON_ACCESSIBILITY_TOGGLE_MIXED;
    case State::NotApplicable:
        break;
    }
    return XENON_ACCESSIBILITY_TOGGLE_NONE;
}

static u8 toggle_from(WebView::AccessibilityNodeData::PressedState state)
{
    using State = WebView::AccessibilityNodeData::PressedState;
    switch (state) {
    case State::NotPressed:
        return XENON_ACCESSIBILITY_TOGGLE_OFF;
    case State::Pressed:
        return XENON_ACCESSIBILITY_TOGGLE_ON;
    case State::Mixed:
        return XENON_ACCESSIBILITY_TOGGLE_MIXED;
    case State::NotApplicable:
        break;
    }
    return XENON_ACCESSIBILITY_TOGGLE_NONE;
}

static u8 expanded_from(WebView::AccessibilityNodeData::ExpandedState state)
{
    using State = WebView::AccessibilityNodeData::ExpandedState;
    switch (state) {
    case State::Collapsed:
        return XENON_ACCESSIBILITY_EXPANDED_COLLAPSED;
    case State::Expanded:
        return XENON_ACCESSIBILITY_EXPANDED_EXPANDED;
    case State::NotApplicable:
        break;
    }
    return XENON_ACCESSIBILITY_EXPANDED_NONE;
}

static u32 flags_of(WebView::AccessibilityNodeData const& node)
{
    u32 flags = 0;
    auto set = [&](bool on, u32 flag) {
        if (on)
            flags |= flag;
    };
    set(node.is_focused, XENON_ACCESSIBILITY_FOCUSED);
    set(node.is_disabled, XENON_ACCESSIBILITY_DISABLED);
    set(node.is_selected, XENON_ACCESSIBILITY_SELECTED);
    set(node.is_editable, XENON_ACCESSIBILITY_EDITABLE);
    set(node.is_multi_line, XENON_ACCESSIBILITY_MULTI_LINE);
    set(node.is_password, XENON_ACCESSIBILITY_PASSWORD);
    set(node.is_read_only, XENON_ACCESSIBILITY_READ_ONLY);
    set(node.is_required, XENON_ACCESSIBILITY_REQUIRED);
    set(node.is_invalid, XENON_ACCESSIBILITY_INVALID);
    set(node.is_visited, XENON_ACCESSIBILITY_VISITED);
    set(node.is_multi_selectable, XENON_ACCESSIBILITY_MULTI_SELECTABLE);
    set(node.value_numeric.has_value(), XENON_ACCESSIBILITY_HAS_NUMERIC_VALUE);
    return flags;
}

static XenonAccessibilityNode xenon_node_from(WebView::AccessibilityNodeData const& node)
{
    return {
        .id = node.id,
        .parent_id = node.parent_id,
        .child_ids = node.child_ids.data(),
        .child_count = node.child_ids.size(),
        .role = xenon_str(node.role),
        .name = xenon_str(node.name),
        .description = xenon_str(node.description),
        .value = xenon_str(node.value),
        .url = xenon_str(node.url),
        .live = xenon_str(node.live),
        .keybinding = xenon_str(node.keybinding),
        .x = static_cast<float>(node.bounds.x()),
        .y = static_cast<float>(node.bounds.y()),
        .width = static_cast<float>(node.bounds.width()),
        .height = static_cast<float>(node.bounds.height()),
        .flags = flags_of(node),
        .checked = toggle_from(node.checked_state),
        .pressed = toggle_from(node.pressed_state),
        .expanded = expanded_from(node.expanded_state),
        .level = node.heading_level > 0 ? node.heading_level : node.hierarchical_level,
        .value_now = number_or_nan(node.value_numeric),
        .value_min = number_or_nan(node.value_minimum),
        .value_max = number_or_nan(node.value_maximum),
        .value_step = number_or_nan(node.value_step),
        .caret_offset = node.caret_offset,
        .selection_start = node.selection_start,
        .selection_end = node.selection_end,
    };
}

static void report_tree(View& view, Vector<WebView::AccessibilityNodeData> const& nodes)
{
    if (!s_enabled || !s_callbacks.tree_changed)
        return;

    Vector<XenonAccessibilityNode> xenon_nodes;
    xenon_nodes.ensure_capacity(nodes.size());
    i64 root_id = -1;
    i64 focused_id = -1;
    for (auto const& node : nodes) {
        if (node.parent_id == -1 && root_id == -1)
            root_id = node.id;
        if (node.is_focused)
            focused_id = node.id;
        xenon_nodes.unchecked_append(xenon_node_from(node));
    }

    XenonAccessibilityTree tree {
        .root_id = root_id,
        .focused_id = focused_id,
        .zoom = view.zoom_level(),
        .nodes = xenon_nodes.data(),
        .node_count = xenon_nodes.size(),
    };
    s_callbacks.tree_changed(s_callbacks.context, view.id(), &tree);
}

static void request_tree(View& view)
{
    if (s_enabled)
        view.request_accessibility_tree();
}

static void request_tree_soon(XenonViewId id)
{
    if (!s_enabled)
        return;
    if (!s_refresh_timers.contains(id)) {
        s_refresh_timers.remove_all_matching([](XenonViewId view_id, auto const&) {
            return !Engine::the().view(view_id);
        });
    }
    auto& timer = s_refresh_timers.ensure(id, [id] {
        return Core::Timer::create_single_shot(refresh_delay, [id] {
            if (auto* view = Engine::the().view(id))
                request_tree(*view);
        });
    });
    if (!timer->is_active())
        timer->start();
}

void install_accessibility(View& view)
{
    auto id = view.id();
    s_views.set(id, view.make_weak_ptr<View>());

    view.on_accessibility_tree_received = [&view](Vector<WebView::AccessibilityNodeData> nodes) {
        report_tree(view, nodes);
    };

    view.on_accessibility_focus_changed = [id](i64 node) {
        if (s_enabled && s_callbacks.focus_changed)
            s_callbacks.focus_changed(s_callbacks.context, id, node);
    };

    WebView::ViewImplementation::NavigationListener listener;
    listener.on_load_finish = [weak_view = view.make_weak_ptr<View>()](URL::URL const&) {
        if (weak_view)
            request_tree(*weak_view);
    };
    view.add_navigation_listener(move(listener));

    view.reset_zoom_action().add_observer(make<ZoomObserver>([id] {
        request_tree_soon(id);
    }));
}

}

using Xenon::Engine;
using Xenon::View;

static StringView string_view_from(XenonStr string)
{
    if (!string.data || string.length == 0)
        return {};
    return { string.data, string.length };
}

static void post_to_view(XenonViewId id, Function<void(View&)> command)
{
    Engine::the().post([id, command = move(command)] {
        if (auto* view = Engine::the().view(id))
            command(*view);
    });
}

static StringView action_name(u32 action)
{
    switch (action) {
    case XENON_ACCESSIBILITY_ACTION_PRESS:
        return "press"sv;
    case XENON_ACCESSIBILITY_ACTION_FOCUS:
        return "focus"sv;
    case XENON_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW:
        return "scroll_into_view"sv;
    default:
        return {};
    }
}

void xenon_engine_set_accessibility_callbacks(XenonAccessibilityCallbacks const* callbacks)
{
    Xenon::s_callbacks = callbacks ? *callbacks : XenonAccessibilityCallbacks {};
}

void xenon_engine_set_accessibility_enabled(bool enabled)
{
    Engine::the().post([enabled] {
        if (Xenon::s_enabled == enabled)
            return;
        Xenon::s_enabled = enabled;
        if (!enabled) {
            Xenon::s_refresh_timers.clear();
            return;
        }
        Vector<XenonViewId> gone;
        for (auto& [id, view] : Xenon::s_views) {
            if (view)
                Xenon::request_tree(*view);
            else
                gone.append(id);
        }
        for (auto id : gone)
            Xenon::s_views.remove(id);
    });
}

void xenon_engine_request_accessibility_tree(XenonViewId id)
{
    Engine::the().post([id] {
        Xenon::request_tree_soon(id);
    });
}

void xenon_engine_accessibility_action(XenonViewId id, int64_t node, uint32_t action)
{
    auto name = action_name(action);
    if (name.is_empty())
        return;
    post_to_view(id, [node, name](View& view) {
        view.perform_accessibility_action(node, MUST(String::from_utf8(name)));
    });
}

void xenon_engine_accessibility_set_value(XenonViewId id, int64_t node, XenonStr value)
{
    post_to_view(id, [node, value = ByteString { string_view_from(value) }](View& view) {
        auto text = MUST(String::from_utf8(value.view()));
        view.perform_accessibility_text_action(node, "insert_text"_string, 0, NumericLimits<i32>::max(), move(text));
    });
}
