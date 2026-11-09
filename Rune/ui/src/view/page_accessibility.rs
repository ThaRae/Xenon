/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Color, El, LiveRegion, edit_access, el, memo, text_input};

use crate::accessibility::{AxNode, PageTree};
use crate::engine::ViewId;
use crate::ffi_accessibility::{
    XENON_ACCESSIBILITY_DISABLED, XENON_ACCESSIBILITY_EXPANDED_COLLAPSED,
    XENON_ACCESSIBILITY_EXPANDED_EXPANDED, XENON_ACCESSIBILITY_INVALID,
    XENON_ACCESSIBILITY_MULTI_LINE, XENON_ACCESSIBILITY_PASSWORD, XENON_ACCESSIBILITY_READ_ONLY,
    XENON_ACCESSIBILITY_SELECTED, XENON_ACCESSIBILITY_TOGGLE_MIXED,
    XENON_ACCESSIBILITY_TOGGLE_NONE, XENON_ACCESSIBILITY_TOGGLE_ON,
};
use crate::state::Browser;

const FIELD_FONT: TextStyle = TextStyle::new(12.0, 400.0);
const MAXIMUM_DEPTH: usize = 256;
const MINIMUM_FIELD_SIZE: f32 = 1.0;

pub fn page_accessibility_layer(view: ViewId, tree: &PageTree) -> El {
    let version = (
        tree.generation,
        tree.zoom.to_bits(),
        tree.root,
        tree.fields_digest(),
    );
    memo(format!("page-ax-{view}"), version, || {
        el().role("none")
            .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
            .clip_rounded(0.0)
            .children(children_of(view, tree, tree.root, [0.0, 0.0], 0))
    })
}

fn children_of(view: ViewId, tree: &PageTree, id: i64, origin: [f32; 2], depth: usize) -> Vec<El> {
    let Some(node) = tree.node(id) else {
        return Vec::new();
    };
    if depth >= MAXIMUM_DEPTH {
        return Vec::new();
    }
    let mut elements = Vec::with_capacity(node.children.len());
    for child in &node.children {
        let Some(child) = tree.node(*child) else {
            continue;
        };
        if child.is_ignored() {
            elements.extend(children_of(view, tree, child.id, origin, depth + 1));
        } else {
            elements.push(element_for(view, tree, child, origin, depth + 1));
        }
    }
    elements
}

fn bounds_in_points(node: &AxNode, zoom: f32) -> [f32; 4] {
    let [x, y, width, height] = node.bounds;
    [
        x * zoom,
        y * zoom,
        (width * zoom).max(0.0),
        (height * zoom).max(0.0),
    ]
}

fn element_for(view: ViewId, tree: &PageTree, node: &AxNode, origin: [f32; 2], depth: usize) -> El {
    let [x, y, width, height] = bounds_in_points(node, tree.zoom);
    let id = node.id;
    let element = if tree.fields.contains_key(&id) {
        field_element(view, tree, node).size(
            width.max(MINIMUM_FIELD_SIZE),
            height.max(MINIMUM_FIELD_SIZE),
        )
    } else {
        described(el().role(role_for(node)), node).size(width, height)
    };
    let element =
        element
            .key(tree.element_key(id))
            .pin(Some(x - origin[0]), Some(y - origin[1]), None, None);
    let element = if is_pressable(node) && !tree.fields.contains_key(&id) {
        element.on_press(move |s: &mut Browser| s.accessibility.press(view, id))
    } else {
        element
    };
    element.children(children_of(view, tree, id, [x, y], depth))
}

fn field_element(view: ViewId, tree: &PageTree, node: &AxNode) -> El {
    let id = node.id;
    let edit = &tree.fields[&id].edit;
    let mut input = text_input(edit, FIELD_FONT)
        .color(Color::TRANSPARENT)
        .caret(Color::TRANSPARENT, Color::TRANSPARENT)
        .focus_return("page")
        .on_edit(edit_access(move |s: &mut Browser| {
            s.accessibility.field_edit(view, id)
        }));
    if !node.has(XENON_ACCESSIBILITY_MULTI_LINE) {
        input = input.single_line();
    }
    if node.has(XENON_ACCESSIBILITY_PASSWORD) {
        input = input.secure();
    }
    if node.role == "searchbox" {
        input = input.role("searchbox");
    }
    described(input, node)
}

fn described(element: El, node: &AxNode) -> El {
    let mut element = element;
    let name = node.name.trim();
    if node.role == "text leaf" {
        element = element.a11y_value(name);
    } else if !name.is_empty() {
        element = element.label(name);
    }
    if !node.description.trim().is_empty() {
        element = element.description(node.description.trim());
    }
    if !node.value.is_empty() && !node.is_field() && node.role != "text leaf" {
        element = element.a11y_value(node.value.clone());
    }
    if let Some(range) = &node.range {
        element = element.range_value(range.now, range.min, range.max);
        if let Some(step) = range.step {
            element = element.range_step(step);
        }
    }
    if node.level > 0 {
        element = element.level(node.level as u32);
    }
    let toggle = if node.checked != XENON_ACCESSIBILITY_TOGGLE_NONE {
        node.checked
    } else {
        node.pressed
    };
    element = match toggle {
        XENON_ACCESSIBILITY_TOGGLE_ON => element.checked(true),
        XENON_ACCESSIBILITY_TOGGLE_MIXED => element.checked_mixed(),
        XENON_ACCESSIBILITY_TOGGLE_NONE => element,
        _ => element.checked(false),
    };
    element = match node.expanded {
        XENON_ACCESSIBILITY_EXPANDED_EXPANDED => element.expanded(true),
        XENON_ACCESSIBILITY_EXPANDED_COLLAPSED => element.expanded(false),
        _ => element,
    };
    if node.has(XENON_ACCESSIBILITY_SELECTED) {
        element = element.focus_selected(true);
    }
    if node.has(XENON_ACCESSIBILITY_DISABLED) {
        element = element.disabled(true);
    }
    if node.has(XENON_ACCESSIBILITY_READ_ONLY) {
        element = element.read_only(true);
    }
    if node.has(XENON_ACCESSIBILITY_INVALID) {
        element = element.invalid(true);
    }
    if !node.keybinding.is_empty() {
        element = element.shortcut_hint(node.keybinding.clone());
    }
    match node.live.as_str() {
        "polite" => element.live_region(LiveRegion::Polite),
        "assertive" => element.live_region(LiveRegion::Assertive),
        _ => element,
    }
}

fn is_pressable(node: &AxNode) -> bool {
    if node.has(XENON_ACCESSIBILITY_DISABLED) {
        return false;
    }
    matches!(
        node.role.as_str(),
        "button"
            | "link"
            | "checkbox"
            | "radio"
            | "switch"
            | "menuitem"
            | "menuitemcheckbox"
            | "menuitemradio"
            | "tab"
            | "option"
            | "treeitem"
            | "combobox"
    ) || node.expanded != 0
}

pub fn role_for(node: &AxNode) -> &'static str {
    match node.role.as_str() {
        "text leaf" => "text",
        "image" | "img" => "img",
        "generic" | "iframe" | "section" | "none" | "presentation" => "group",
        role => known_role(role).unwrap_or(if is_pressable(node) {
            "button"
        } else {
            "group"
        }),
    }
}

fn known_role(role: &str) -> Option<&'static str> {
    const ROLES: &[&str] = &[
        "alert",
        "alertdialog",
        "article",
        "banner",
        "blockquote",
        "button",
        "cell",
        "checkbox",
        "code",
        "columnheader",
        "combobox",
        "complementary",
        "contentinfo",
        "dialog",
        "document",
        "figure",
        "form",
        "grid",
        "gridcell",
        "group",
        "heading",
        "link",
        "list",
        "listbox",
        "listitem",
        "log",
        "main",
        "menu",
        "menubar",
        "menuitem",
        "menuitemcheckbox",
        "menuitemradio",
        "meter",
        "navigation",
        "note",
        "option",
        "paragraph",
        "progressbar",
        "radio",
        "radiogroup",
        "region",
        "row",
        "rowgroup",
        "rowheader",
        "search",
        "separator",
        "slider",
        "spinbutton",
        "status",
        "switch",
        "tab",
        "table",
        "tablist",
        "tabpanel",
        "timer",
        "toolbar",
        "tooltip",
        "tree",
        "treegrid",
        "treeitem",
    ];
    ROLES.iter().copied().find(|known| *known == role)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::accessibility::AxNode;
    use crate::ffi_accessibility::XENON_ACCESSIBILITY_EDITABLE;

    fn node(id: i64, parent: i64, role: &str, name: &str, children: &[i64]) -> AxNode {
        AxNode {
            id,
            parent,
            children: children.to_vec(),
            role: role.to_owned(),
            name: name.to_owned(),
            ..AxNode::default()
        }
    }

    #[test]
    fn engine_roles_become_rune_roles() {
        assert_eq!(role_for(&node(1, 0, "text leaf", "a", &[])), "text");
        assert_eq!(role_for(&node(1, 0, "heading", "a", &[])), "heading");
        assert_eq!(role_for(&node(1, 0, "generic", "a", &[])), "group");
        assert_eq!(role_for(&node(1, 0, "made-up", "a", &[])), "group");
        let disclosure = AxNode {
            expanded: XENON_ACCESSIBILITY_EXPANDED_COLLAPSED,
            ..node(1, 0, "made-up", "a", &[])
        };
        assert_eq!(role_for(&disclosure), "button");
    }

    #[test]
    fn fields_and_disabled_controls_are_not_pressed_through_the_engine() {
        let field = AxNode {
            flags: XENON_ACCESSIBILITY_EDITABLE,
            ..node(1, 0, "textbox", "a", &[])
        };
        assert!(field.is_field());
        let disabled = AxNode {
            flags: XENON_ACCESSIBILITY_DISABLED,
            ..node(1, 0, "button", "a", &[])
        };
        assert!(!is_pressable(&disabled));
        assert!(is_pressable(&node(1, 0, "link", "a", &[])));
    }

    #[test]
    fn bounds_scale_with_the_page_zoom() {
        let mut button = node(1, 0, "button", "a", &[]);
        button.bounds = [10.0, 20.0, 30.0, 40.0];
        assert_eq!(bounds_in_points(&button, 1.5), [15.0, 30.0, 45.0, 60.0]);
    }
}

#[cfg(all(test, feature = "mock-engine"))]
mod runtime_tests {
    use std::sync::Arc;

    use rune::accesskit::{Action, ActionData};
    use rune::{Cx, Env, Runtime, col};
    use serde_json::Value;

    use super::*;
    use crate::accessibility::{self, AxNode, ReceivedTree};
    use crate::ffi_accessibility::XENON_ACCESSIBILITY_EDITABLE;
    use crate::mock::accessibility::recorded;

    const VIEW: ViewId = 4242;

    fn node(id: i64, parent: i64, role: &str, name: &str, bounds: [f32; 4]) -> AxNode {
        AxNode {
            id,
            parent,
            role: role.to_owned(),
            name: name.to_owned(),
            bounds,
            ..AxNode::default()
        }
    }

    fn page_tree() -> ReceivedTree {
        let mut document = node(1, -1, "document", "Page", [0.0, 0.0, 800.0, 600.0]);
        document.children = vec![2, 3, 5];
        let mut wrapper = node(3, 1, "generic", "", [10.0, 50.0, 300.0, 40.0]);
        wrapper.children = vec![4];
        let mut field = node(5, 1, "textbox", "Name", [10.0, 100.0, 200.0, 24.0]);
        field.flags = XENON_ACCESSIBILITY_EDITABLE;
        field.value = "old".to_owned();
        let mut list = node(6, 1, "list", "", [10.0, 140.0, 300.0, 40.0]);
        list.children = vec![7];
        document.children.push(6);
        ReceivedTree {
            root: 1,
            focused: None,
            zoom: 2.0,
            nodes: vec![
                document,
                node(2, 1, "heading", "Title", [10.0, 10.0, 300.0, 30.0]),
                wrapper,
                node(4, 3, "button", "Save", [20.0, 55.0, 80.0, 30.0]),
                field,
                list,
                node(7, 6, "listitem", "First", [20.0, 145.0, 100.0, 20.0]),
            ],
        }
    }

    fn view(s: &Browser, _: &mut Cx<Browser>) -> El {
        let tree = s.accessibility.tree(VIEW).expect("a tree");
        let page = el()
            .key("page")
            .role("document")
            .label("Page")
            .w_full()
            .grow(1.0)
            .focusable(true)
            .on_key(|_: &mut Browser, _, _| true);
        let page = match tree.focused_key() {
            Some(key) => page.active_descendant(key),
            None => page,
        };
        col()
            .w_full()
            .h_full()
            .child(el().key("chrome").h(40.0).w_full())
            .child(page.child(page_accessibility_layer(VIEW, tree)))
    }

    fn find<'a>(v: &'a Value, key: &str) -> Option<&'a Value> {
        if v["key"] == key {
            return Some(v);
        }
        v["children"].as_array()?.iter().find_map(|c| find(c, key))
    }

    fn runtime() -> Runtime<Browser> {
        let mut browser = Browser::new(Arc::default());
        browser.accessibility.enabled = true;
        accessibility::tree_received(&mut browser, VIEW, page_tree());
        let mut rt = Runtime::new(browser, view, (800.0, 600.0), Env::new(false, 1.0));
        rt.frame(0.0);
        rt
    }

    #[test]
    fn the_page_tree_is_mirrored_under_the_page_in_window_points() {
        let rt = runtime();
        let tree = rt.accessibility_json();
        let page = find(&tree, "page").expect("the page");
        let children: Vec<&str> = page["children"]
            .as_array()
            .unwrap()
            .iter()
            .map(|c| c["key"].as_str().unwrap())
            .collect();
        assert_eq!(children, ["ax2", "ax4", "ax5.0", "ax6"]);
        let heading = find(&tree, "ax2").unwrap();
        assert_eq!(heading["role"], "heading");
        assert_eq!(heading["name"], "Title");
        assert_eq!(
            heading["bounds"],
            serde_json::json!([20.0, 60.0, 600.0, 60.0])
        );
        let button = find(&tree, "ax4").unwrap();
        assert_eq!(button["role"], "button");
        assert_eq!(button["name"], "Save");
        assert_eq!(
            button["bounds"],
            serde_json::json!([40.0, 150.0, 160.0, 60.0])
        );
        let field = find(&tree, "ax5.0").unwrap();
        assert_eq!(field["role"], "text-input");
        assert_eq!(field["value"], "old");
        let item = find(&tree, "ax7").unwrap();
        assert_eq!(item["role"], "list-item");
        assert_eq!(
            item["bounds"],
            serde_json::json!([40.0, 330.0, 200.0, 40.0])
        );
    }

    #[test]
    fn actions_reach_the_engine_and_focus_follows_it() {
        let mut rt = runtime();
        assert!(rt.accessibility_action("ax4", Action::Click, None));
        rt.frame(0.0);
        assert!(recorded().contains(&format!("action 0 on 4 in view {VIEW}")));

        assert!(rt.accessibility_action(
            "ax5.0",
            Action::SetValue,
            Some(ActionData::Value("new".into()))
        ));
        rt.frame(0.0);
        let mut focused_field = false;
        rt.update(|s, _| focused_field = s.accessibility.sync_fields_of(VIEW));
        assert!(focused_field);
        let recorded = recorded();
        assert!(recorded.contains(&format!("action 1 on 5 in view {VIEW}")));
        assert!(recorded.contains(&format!("set value of 5 in view {VIEW} to \"new\"")));

        rt.update(|s, _| accessibility::focus_changed(s, VIEW, 4));
        rt.frame(0.0);
        rt.frame(0.0);
        let tree = rt.accessibility_json();
        let page = find(&tree, "page").unwrap();
        assert_eq!(page["activeDescendant"], "ax4");
        assert!(
            page["states"]
                .as_array()
                .is_some_and(|s| s.iter().any(|s| s == "focused")),
            "focus went back to the page: {page}"
        );
        assert!(find(&tree, "ax5.1").is_some());
    }
}
