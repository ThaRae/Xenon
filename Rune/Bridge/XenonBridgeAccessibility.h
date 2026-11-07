/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <Rune/Bridge/XenonBridge.h>

#ifdef __cplusplus
extern "C" {
#endif

enum XenonAccessibilityFlag {
    XENON_ACCESSIBILITY_FOCUSED = 1u << 0,
    XENON_ACCESSIBILITY_DISABLED = 1u << 1,
    XENON_ACCESSIBILITY_SELECTED = 1u << 2,
    XENON_ACCESSIBILITY_EDITABLE = 1u << 3,
    XENON_ACCESSIBILITY_MULTI_LINE = 1u << 4,
    XENON_ACCESSIBILITY_PASSWORD = 1u << 5,
    XENON_ACCESSIBILITY_READ_ONLY = 1u << 6,
    XENON_ACCESSIBILITY_REQUIRED = 1u << 7,
    XENON_ACCESSIBILITY_INVALID = 1u << 8,
    XENON_ACCESSIBILITY_VISITED = 1u << 9,
    XENON_ACCESSIBILITY_MULTI_SELECTABLE = 1u << 10,
    XENON_ACCESSIBILITY_HAS_NUMERIC_VALUE = 1u << 11,
};

enum XenonAccessibilityToggle {
    XENON_ACCESSIBILITY_TOGGLE_NONE = 0,
    XENON_ACCESSIBILITY_TOGGLE_OFF = 1,
    XENON_ACCESSIBILITY_TOGGLE_ON = 2,
    XENON_ACCESSIBILITY_TOGGLE_MIXED = 3,
};

enum XenonAccessibilityExpanded {
    XENON_ACCESSIBILITY_EXPANDED_NONE = 0,
    XENON_ACCESSIBILITY_EXPANDED_COLLAPSED = 1,
    XENON_ACCESSIBILITY_EXPANDED_EXPANDED = 2,
};

enum XenonAccessibilityAction {
    XENON_ACCESSIBILITY_ACTION_PRESS = 0,
    XENON_ACCESSIBILITY_ACTION_FOCUS = 1,
    XENON_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW = 2,
};

typedef struct XenonAccessibilityNode {
    int64_t id;
    int64_t parent_id;
    int64_t const* child_ids;
    size_t child_count;
    XenonStr role;
    XenonStr name;
    XenonStr description;
    XenonStr value;
    XenonStr url;
    XenonStr live;
    XenonStr keybinding;
    float x;
    float y;
    float width;
    float height;
    uint32_t flags;
    uint8_t checked;
    uint8_t pressed;
    uint8_t expanded;
    int32_t level;
    double value_now;
    double value_min;
    double value_max;
    double value_step;
    int32_t caret_offset;
    int32_t selection_start;
    int32_t selection_end;
} XenonAccessibilityNode;

typedef struct XenonAccessibilityTree {
    int64_t root_id;
    int64_t focused_id;
    double zoom;
    XenonAccessibilityNode const* nodes;
    size_t node_count;
} XenonAccessibilityTree;

typedef struct XenonAccessibilityCallbacks {
    void* context;
    void (*tree_changed)(void* context, XenonViewId, XenonAccessibilityTree const*);
    void (*focus_changed)(void* context, XenonViewId, int64_t node);
} XenonAccessibilityCallbacks;

void xenon_engine_set_accessibility_callbacks(XenonAccessibilityCallbacks const*);

void xenon_engine_set_accessibility_enabled(bool enabled);
void xenon_engine_request_accessibility_tree(XenonViewId);
void xenon_engine_accessibility_action(XenonViewId, int64_t node, uint32_t action);
void xenon_engine_accessibility_set_value(XenonViewId, int64_t node, XenonStr value);

bool xenon_accessibility_assistive_technology_running(void);

#ifdef __cplusplus
}
#endif
