/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::HashMap;
use std::sync::{Mutex, MutexGuard};

use crate::ffi::{XenonStr, XenonViewId};
use crate::ffi_accessibility::{
    XENON_ACCESSIBILITY_EDITABLE, XENON_ACCESSIBILITY_FOCUSED, XENON_ACCESSIBILITY_TOGGLE_NONE,
    XENON_ACCESSIBILITY_TOGGLE_ON, XenonAccessibilityCallbacks, XenonAccessibilityNode,
    XenonAccessibilityTree,
};

const DOCUMENT: i64 = 1;
const HEADING: i64 = 2;
const LINK: i64 = 3;
const WRAPPER: i64 = 4;
const TEXT: i64 = 5;
const FIELD: i64 = 6;
const CHECKBOX: i64 = 7;
const BUTTON: i64 = 8;

struct SendableCallbacks(XenonAccessibilityCallbacks);

unsafe impl Send for SendableCallbacks {}

static CALLBACKS: Mutex<Option<SendableCallbacks>> = Mutex::new(None);
static STATE: Mutex<Option<MockAccessibility>> = Mutex::new(None);

#[derive(Default)]
struct MockAccessibility {
    enabled: bool,
    pages: HashMap<XenonViewId, MockPage>,
}

struct MockPage {
    title: String,
    link: [f32; 4],
    field_value: String,
    focused: Option<i64>,
}

fn state() -> MutexGuard<'static, Option<MockAccessibility>> {
    STATE
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn callbacks() -> Option<XenonAccessibilityCallbacks> {
    CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .as_ref()
        .map(|callbacks| callbacks.0)
}

static RECORDED: Mutex<Vec<String>> = Mutex::new(Vec::new());

fn record(answer: String) {
    eprintln!("mock accessibility: {answer}");
    RECORDED
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .push(answer);
}

#[cfg(test)]
pub fn recorded() -> Vec<String> {
    RECORDED
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .clone()
}

struct Node {
    id: i64,
    parent: i64,
    children: Vec<i64>,
    role: &'static str,
    name: String,
    value: String,
    bounds: [f32; 4],
    flags: u32,
    checked: u8,
}

fn node(id: i64, parent: i64, role: &'static str, name: &str, bounds: [f32; 4]) -> Node {
    Node {
        id,
        parent,
        children: Vec::new(),
        role,
        name: name.to_owned(),
        value: String::new(),
        bounds,
        flags: 0,
        checked: XENON_ACCESSIBILITY_TOGGLE_NONE,
    }
}

fn nodes_for(page: &MockPage) -> Vec<Node> {
    let mut document = node(
        DOCUMENT,
        -1,
        "document",
        &page.title,
        [0.0, 0.0, 800.0, 600.0],
    );
    document.children = vec![HEADING, LINK, WRAPPER, FIELD, CHECKBOX, BUTTON];
    let mut wrapper = node(WRAPPER, DOCUMENT, "generic", "", [24.0, 120.0, 240.0, 20.0]);
    wrapper.children = vec![TEXT];
    let mut field = node(
        FIELD,
        DOCUMENT,
        "textbox",
        "Search",
        [24.0, 160.0, 240.0, 28.0],
    );
    field.value = page.field_value.clone();
    field.flags = XENON_ACCESSIBILITY_EDITABLE;
    let mut checkbox = node(
        CHECKBOX,
        DOCUMENT,
        "checkbox",
        "Remember me",
        [24.0, 200.0, 140.0, 20.0],
    );
    checkbox.checked = XENON_ACCESSIBILITY_TOGGLE_ON;
    let mut nodes = vec![
        document,
        node(
            HEADING,
            DOCUMENT,
            "heading",
            "Mock page",
            [24.0, 24.0, 300.0, 28.0],
        ),
        node(LINK, DOCUMENT, "link", &page.title, page.link),
        wrapper,
        node(
            TEXT,
            WRAPPER,
            "text leaf",
            "Drawn by the mock engine",
            [24.0, 120.0, 200.0, 16.0],
        ),
        field,
        checkbox,
        node(
            BUTTON,
            DOCUMENT,
            "button",
            "Send",
            [24.0, 240.0, 80.0, 30.0],
        ),
    ];
    for node in &mut nodes {
        if Some(node.id) == page.focused {
            node.flags |= XENON_ACCESSIBILITY_FOCUSED;
        }
    }
    nodes
}

fn send(view: XenonViewId, page: &MockPage) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let Some(tree_changed) = callbacks.tree_changed else {
        return;
    };
    let nodes = nodes_for(page);
    let xenon_nodes: Vec<XenonAccessibilityNode> = nodes
        .iter()
        .map(|node| XenonAccessibilityNode {
            id: node.id,
            parent_id: node.parent,
            child_ids: node.children.as_ptr(),
            child_count: node.children.len(),
            role: XenonStr::borrowed(node.role),
            name: XenonStr::borrowed(&node.name),
            description: XenonStr::borrowed(""),
            value: XenonStr::borrowed(&node.value),
            url: XenonStr::borrowed(""),
            live: XenonStr::borrowed(""),
            keybinding: XenonStr::borrowed(""),
            x: node.bounds[0],
            y: node.bounds[1],
            width: node.bounds[2],
            height: node.bounds[3],
            flags: node.flags,
            checked: node.checked,
            pressed: XENON_ACCESSIBILITY_TOGGLE_NONE,
            expanded: 0,
            level: if node.id == HEADING { 1 } else { 0 },
            value_now: f64::NAN,
            value_min: f64::NAN,
            value_max: f64::NAN,
            value_step: f64::NAN,
            caret_offset: -1,
            selection_start: -1,
            selection_end: -1,
        })
        .collect();
    let tree = XenonAccessibilityTree {
        root_id: DOCUMENT,
        focused_id: page.focused.unwrap_or(-1),
        zoom: 1.0,
        nodes: xenon_nodes.as_ptr(),
        node_count: xenon_nodes.len(),
    };
    unsafe { tree_changed(callbacks.context, view, &tree) };
}

fn send_focus(view: XenonViewId, node: i64) {
    if let Some(callbacks) = callbacks()
        && let Some(focus_changed) = callbacks.focus_changed
    {
        unsafe { focus_changed(callbacks.context, view, node) };
    }
}

pub fn navigated(view: XenonViewId, title: &str, link: [f32; 4]) {
    let mut state = state();
    let state = state.get_or_insert_with(MockAccessibility::default);
    let page = MockPage {
        title: title.to_owned(),
        link,
        field_value: String::new(),
        focused: None,
    };
    if state.enabled {
        send(view, &page);
    }
    state.pages.insert(view, page);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_accessibility_callbacks(
    callbacks: *const XenonAccessibilityCallbacks,
) {
    let callbacks = unsafe { callbacks.as_ref() }.map(|callbacks| SendableCallbacks(*callbacks));
    *CALLBACKS
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner()) = callbacks;
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_accessibility_enabled(enabled: bool) {
    record(format!("enabled {enabled}"));
    let mut state = state();
    let state = state.get_or_insert_with(MockAccessibility::default);
    state.enabled = enabled;
    if enabled {
        for (view, page) in &state.pages {
            send(*view, page);
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_request_accessibility_tree(view: XenonViewId) {
    let state = state();
    if let Some(page) = state
        .as_ref()
        .filter(|state| state.enabled)
        .and_then(|state| state.pages.get(&view))
    {
        send(view, page);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_accessibility_action(view: XenonViewId, node: i64, action: u32) {
    record(format!("action {action} on {node} in view {view}"));
    if action != crate::ffi_accessibility::XENON_ACCESSIBILITY_ACTION_FOCUS {
        return;
    }
    {
        let mut state = state();
        let Some(page) = state.as_mut().and_then(|state| state.pages.get_mut(&view)) else {
            return;
        };
        page.focused = Some(node);
        send(view, page);
    }
    send_focus(view, node);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_accessibility_set_value(
    view: XenonViewId,
    node: i64,
    value: XenonStr,
) {
    let value = unsafe { value.to_string_lossy() };
    record(format!("set value of {node} in view {view} to {value:?}"));
    let mut state = state();
    let Some(page) = state.as_mut().and_then(|state| state.pages.get_mut(&view)) else {
        return;
    };
    if node == FIELD {
        page.field_value = value;
        send(view, page);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_accessibility_assistive_technology_running() -> bool {
    false
}
