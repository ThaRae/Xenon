/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::HashMap;
use std::ffi::c_void;
use std::sync::Arc;
use std::time::Duration;

use rune::{TextEdit, Tx};

use crate::engine::ViewId;
use crate::ffi::XenonStr;
use crate::ffi_accessibility::{
    self, XENON_ACCESSIBILITY_ACTION_FOCUS, XENON_ACCESSIBILITY_ACTION_PRESS,
    XENON_ACCESSIBILITY_EDITABLE, XENON_ACCESSIBILITY_FOCUSED,
    XENON_ACCESSIBILITY_HAS_NUMERIC_VALUE, XenonAccessibilityCallbacks, XenonAccessibilityNode,
    XenonAccessibilityTree,
};
use crate::state::Browser;
use crate::window_slots::{Slots, ViewRoute};

const FORCE_VARIABLE: &str = "XENON_ACCESSIBILITY";
const ASSISTIVE_TECHNOLOGY_CHECK_INTERVAL: Duration = Duration::from_secs(2);

#[derive(Clone, Copy, Debug, Default)]
pub struct AccessibilityEngine;

impl AccessibilityEngine {
    pub fn set_enabled(self, enabled: bool) {
        unsafe { ffi_accessibility::xenon_engine_set_accessibility_enabled(enabled) }
    }

    pub fn request_tree(self, view: ViewId) {
        unsafe { ffi_accessibility::xenon_engine_request_accessibility_tree(view) }
    }

    pub fn press(self, view: ViewId, node: i64) {
        unsafe {
            ffi_accessibility::xenon_engine_accessibility_action(
                view,
                node,
                XENON_ACCESSIBILITY_ACTION_PRESS,
            )
        }
    }

    pub fn focus(self, view: ViewId, node: i64) {
        unsafe {
            ffi_accessibility::xenon_engine_accessibility_action(
                view,
                node,
                XENON_ACCESSIBILITY_ACTION_FOCUS,
            )
        }
    }

    pub fn set_value(self, view: ViewId, node: i64, value: &str) {
        unsafe {
            ffi_accessibility::xenon_engine_accessibility_set_value(
                view,
                node,
                XenonStr::borrowed(value),
            )
        }
    }

    pub fn assistive_technology_running(self) -> bool {
        #[cfg(all(target_os = "linux", not(feature = "mock-engine")))]
        return crate::at_spi::assistive_technology_running();
        #[allow(unreachable_code)]
        unsafe {
            ffi_accessibility::xenon_accessibility_assistive_technology_running()
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Range {
    pub now: f64,
    pub min: f64,
    pub max: f64,
    pub step: Option<f64>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct AxNode {
    pub id: i64,
    pub parent: i64,
    pub children: Vec<i64>,
    pub role: String,
    pub name: String,
    pub description: String,
    pub value: String,
    pub live: String,
    pub keybinding: String,
    pub bounds: [f32; 4],
    pub flags: u32,
    pub checked: u8,
    pub pressed: u8,
    pub expanded: u8,
    pub level: i32,
    pub range: Option<Range>,
    pub caret: i32,
    pub selection: (i32, i32),
}

impl AxNode {
    pub fn has(&self, flag: u32) -> bool {
        self.flags & flag != 0
    }

    pub fn is_field(&self) -> bool {
        self.has(XENON_ACCESSIBILITY_EDITABLE) && self.role != "document"
    }

    pub fn is_live_region(&self) -> bool {
        matches!(self.live.as_str(), "polite" | "assertive")
    }

    pub fn is_ignored(&self) -> bool {
        match self.role.as_str() {
            "generic" => self.name.trim().is_empty() && !self.is_live_region(),
            "text leaf" => self.name.trim().is_empty(),
            "none" | "presentation" => true,
            _ => false,
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct ReceivedTree {
    pub root: i64,
    pub focused: Option<i64>,
    pub zoom: f32,
    pub nodes: Vec<AxNode>,
}

pub struct Field {
    pub edit: TextEdit,
    synced: String,
    focus_reported: bool,
    pub epoch: u64,
}

impl Field {
    fn new(value: &str) -> Field {
        Field {
            edit: TextEdit::new(value),
            synced: value.to_owned(),
            focus_reported: false,
            epoch: 0,
        }
    }
}

#[derive(Default)]
pub struct PageTree {
    pub generation: u64,
    pub zoom: f32,
    pub root: i64,
    pub focused: Option<i64>,
    pub nodes: HashMap<i64, AxNode>,
    pub fields: HashMap<i64, Field>,
}

impl PageTree {
    pub fn node(&self, id: i64) -> Option<&AxNode> {
        self.nodes.get(&id)
    }

    pub fn element_key(&self, id: i64) -> String {
        match self.fields.get(&id) {
            Some(field) => format!("ax{id}.{}", field.epoch),
            None => format!("ax{id}"),
        }
    }

    pub fn shown_ancestor_key(&self, id: i64) -> Option<String> {
        let mut current = self.nodes.get(&id)?;
        loop {
            if current.id == self.root {
                return None;
            }
            if !current.is_ignored() {
                return Some(self.element_key(current.id));
            }
            current = self.nodes.get(&current.parent)?;
        }
    }

    pub fn focused_key(&self) -> Option<String> {
        self.shown_ancestor_key(self.focused?)
    }

    pub fn fields_digest(&self) -> u64 {
        use std::hash::{Hash, Hasher};
        let mut ids: Vec<&i64> = self.fields.keys().collect();
        ids.sort();
        let mut hasher = std::collections::hash_map::DefaultHasher::new();
        for id in ids {
            let field = &self.fields[id];
            (id, field.epoch, &field.edit).hash(&mut hasher);
        }
        hasher.finish()
    }

    fn apply(&mut self, received: ReceivedTree) {
        let mut fields = std::mem::take(&mut self.fields);
        self.root = received.root;
        self.focused = received.focused;
        self.zoom = if received.zoom > 0.0 {
            received.zoom
        } else {
            1.0
        };
        self.nodes = received
            .nodes
            .into_iter()
            .map(|node| (node.id, node))
            .collect();
        hand_disclosure_state_to_summaries(&mut self.nodes);
        for node in self.nodes.values().filter(|node| node.is_field()) {
            let field = fields
                .remove(&node.id)
                .unwrap_or_else(|| Field::new(&node.value));
            let field = self.fields.entry(node.id).or_insert(field);
            if field.synced != node.value {
                field.edit.replace_text(node.value.clone());
                field.synced = node.value.clone();
            }
            if node.has(XENON_ACCESSIBILITY_FOCUSED) {
                select_from_engine(&mut field.edit, node);
            }
        }
        self.generation = self.generation.wrapping_add(1);
    }

    fn return_focus_from_fields(&mut self, keep: Option<i64>) -> bool {
        let mut returned = false;
        for (id, field) in &mut self.fields {
            if field.edit.focused && Some(*id) != keep {
                field.edit.focused = false;
                field.focus_reported = false;
                field.epoch = field.epoch.wrapping_add(1);
                returned = true;
            }
        }
        if returned {
            self.generation = self.generation.wrapping_add(1);
        }
        returned
    }
}

fn hand_disclosure_state_to_summaries(nodes: &mut HashMap<i64, AxNode>) {
    let disclosures: Vec<(i64, i64, u8)> = nodes
        .values()
        .filter(|node| node.role == "group" && node.expanded != 0)
        .filter_map(|node| {
            let summary = *node.children.first()?;
            (nodes.get(&summary)?.role == "button").then_some((node.id, summary, node.expanded))
        })
        .collect();
    for (details, summary, expanded) in disclosures {
        if let Some(details) = nodes.get_mut(&details) {
            details.expanded = 0;
        }
        if let Some(summary) = nodes.get_mut(&summary) {
            summary.expanded = expanded;
        }
    }
}

fn byte_offset(text: &str, characters: i32) -> Option<usize> {
    let characters = usize::try_from(characters).ok()?;
    Some(
        text.char_indices()
            .nth(characters)
            .map_or(text.len(), |(byte, _)| byte),
    )
}

fn select_from_engine(edit: &mut TextEdit, node: &AxNode) {
    let (start, end) = node.selection;
    let text = edit.text.clone();
    if let (Some(start), Some(end)) = (byte_offset(&text, start), byte_offset(&text, end)) {
        edit.set_selection(start, end);
    } else if let Some(caret) = byte_offset(&text, node.caret) {
        edit.set_caret(caret);
    }
}

#[derive(Default)]
pub struct PageAccessibility {
    pub enabled: bool,
    forced: Option<bool>,
    next_check: Option<f64>,
    trees: HashMap<ViewId, PageTree>,
    scratch: TextEdit,
}

impl PageAccessibility {
    pub fn tree(&self, view: ViewId) -> Option<&PageTree> {
        self.enabled.then(|| self.trees.get(&view)).flatten()
    }

    pub fn field_edit(&mut self, view: ViewId, node: i64) -> &mut TextEdit {
        match self
            .trees
            .get_mut(&view)
            .and_then(|tree| tree.fields.get_mut(&node))
        {
            Some(field) => &mut field.edit,
            None => &mut self.scratch,
        }
    }

    fn forced(&mut self) -> bool {
        *self.forced.get_or_insert_with(|| {
            std::env::var(FORCE_VARIABLE).is_ok_and(|value| !value.is_empty() && value != "0")
        })
    }

    pub fn page_input(&self, view: Option<ViewId>) {
        if let Some(view) = view.filter(|_| self.enabled) {
            AccessibilityEngine.request_tree(view);
        }
    }

    pub fn press(&self, view: ViewId, node: i64) {
        let Some(tree) = self.trees.get(&view) else {
            return;
        };
        match tree.node(node) {
            Some(node) if node.is_field() => AccessibilityEngine.focus(view, node.id),
            Some(node) => AccessibilityEngine.press(view, node.id),
            None => {}
        }
    }
}

pub fn tree_received(s: &mut Browser, view: ViewId, received: ReceivedTree) {
    if !s.accessibility.enabled {
        return;
    }
    s.accessibility
        .trees
        .entry(view)
        .or_default()
        .apply(received);
}

pub fn focus_changed(s: &mut Browser, view: ViewId, node: i64) {
    let Some(tree) = s.accessibility.trees.get_mut(&view) else {
        return;
    };
    let node = Some(node).filter(|node| *node != tree.root && tree.nodes.contains_key(node));
    if tree.focused == node {
        return;
    }
    tree.focused = node;
    tree.generation = tree.generation.wrapping_add(1);
    tree.return_focus_from_fields(node);
}

pub fn poll(s: &mut Browser, now: f64, tx: &mut Tx) -> bool {
    let mut changed = false;
    if s.accessibility.next_check.is_none_or(|next| {
        now >= next || next - now > ASSISTIVE_TECHNOLOGY_CHECK_INTERVAL.as_secs_f64()
    }) {
        s.accessibility.next_check = Some(now + ASSISTIVE_TECHNOLOGY_CHECK_INTERVAL.as_secs_f64());
        tx.wake_after(ASSISTIVE_TECHNOLOGY_CHECK_INTERVAL);
        let wanted = s.accessibility.forced() || AccessibilityEngine.assistive_technology_running();
        if wanted != s.accessibility.enabled {
            s.accessibility.enabled = wanted;
            if !wanted {
                s.accessibility.trees.clear();
            }
            AccessibilityEngine.set_enabled(wanted);
            changed = true;
        }
    }
    if !s.accessibility.enabled {
        return changed;
    }
    let open: Vec<ViewId> = s.accessibility.trees.keys().copied().collect();
    for view in open {
        if s.tabs.get(view).is_none() {
            s.accessibility.trees.remove(&view);
        }
    }
    sync_fields(s);
    changed
}

pub fn sync_fields(s: &mut Browser) {
    let Some(view) = s.tabs.active_view() else {
        return;
    };
    if s.accessibility.sync_fields_of(view) {
        s.focus_page();
    }
}

impl PageAccessibility {
    pub(crate) fn sync_fields_of(&mut self, view: ViewId) -> bool {
        let Some(tree) = self.trees.get_mut(&view) else {
            return false;
        };
        let mut focused_field = false;
        for (id, field) in &mut tree.fields {
            if field.edit.focused && !field.focus_reported {
                field.focus_reported = true;
                focused_field = true;
                AccessibilityEngine.focus(view, *id);
            } else if !field.edit.focused {
                field.focus_reported = false;
            }
            if field.edit.text != field.synced {
                field.synced = field.edit.text.clone();
                AccessibilityEngine.set_value(view, *id, &field.synced);
            }
        }
        focused_field
    }
}

pub struct AccessibilityContext {
    handle: Arc<Slots>,
}

pub fn install(handle: Arc<Slots>) -> Box<AccessibilityContext> {
    let context = Box::new(AccessibilityContext { handle });
    let callbacks = XenonAccessibilityCallbacks {
        context: std::ptr::from_ref::<AccessibilityContext>(&context)
            .cast_mut()
            .cast(),
        tree_changed: Some(tree_changed),
        focus_changed: Some(focus_changed_callback),
    };
    unsafe { ffi_accessibility::xenon_engine_set_accessibility_callbacks(&callbacks) };
    context
}

unsafe fn handle<'a>(context: *mut c_void) -> ViewRoute<'a> {
    ViewRoute(unsafe { &(*context.cast::<AccessibilityContext>()).handle })
}

fn text(string: XenonStr) -> String {
    unsafe { string.to_string_lossy() }
}

fn number(value: f64) -> Option<f64> {
    value.is_finite().then_some(value)
}

pub fn node_from(node: &XenonAccessibilityNode) -> AxNode {
    let children = if node.child_ids.is_null() || node.child_count == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(node.child_ids, node.child_count) }.to_vec()
    };
    let range = (node.flags & XENON_ACCESSIBILITY_HAS_NUMERIC_VALUE != 0)
        .then(|| {
            let now = number(node.value_now)?;
            Some(Range {
                now,
                min: number(node.value_min).unwrap_or(now.min(0.0)),
                max: number(node.value_max).unwrap_or(now.max(100.0)),
                step: number(node.value_step),
            })
        })
        .flatten();
    AxNode {
        id: node.id,
        parent: node.parent_id,
        children,
        role: text(node.role),
        name: text(node.name),
        description: text(node.description),
        value: text(node.value),
        live: text(node.live),
        keybinding: text(node.keybinding),
        bounds: [node.x, node.y, node.width, node.height],
        flags: node.flags,
        checked: node.checked,
        pressed: node.pressed,
        expanded: node.expanded,
        level: node.level,
        range,
        caret: node.caret_offset,
        selection: (node.selection_start, node.selection_end),
    }
}

pub fn tree_from(tree: &XenonAccessibilityTree) -> ReceivedTree {
    let nodes = if tree.nodes.is_null() || tree.node_count == 0 {
        &[][..]
    } else {
        unsafe { std::slice::from_raw_parts(tree.nodes, tree.node_count) }
    };
    ReceivedTree {
        root: tree.root_id,
        focused: (tree.focused_id >= 0).then_some(tree.focused_id),
        zoom: tree.zoom as f32,
        nodes: nodes.iter().map(node_from).collect(),
    }
}

unsafe extern "C" fn tree_changed(
    context: *mut c_void,
    view: ViewId,
    tree: *const XenonAccessibilityTree,
) {
    let Some(tree) = (unsafe { tree.as_ref() }) else {
        return;
    };
    let received = tree_from(tree);
    unsafe { handle(context) }.update(view, move |browser, _| {
        tree_received(browser, view, received)
    });
}

unsafe extern "C" fn focus_changed_callback(context: *mut c_void, view: ViewId, node: i64) {
    unsafe { handle(context) }.update(view, move |browser, _| focus_changed(browser, view, node));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn node(id: i64, parent: i64, role: &str, name: &str) -> AxNode {
        AxNode {
            id,
            parent,
            role: role.to_owned(),
            name: name.to_owned(),
            ..AxNode::default()
        }
    }

    fn field(id: i64, parent: i64, value: &str) -> AxNode {
        AxNode {
            value: value.to_owned(),
            flags: XENON_ACCESSIBILITY_EDITABLE,
            ..node(id, parent, "textbox", "Name")
        }
    }

    fn received(nodes: Vec<AxNode>, focused: Option<i64>) -> ReceivedTree {
        ReceivedTree {
            root: 1,
            focused,
            zoom: 1.0,
            nodes,
        }
    }

    #[test]
    fn unnamed_generic_boxes_and_blank_text_are_left_out() {
        assert!(node(2, 1, "generic", "").is_ignored());
        assert!(node(2, 1, "text leaf", "  \n").is_ignored());
        assert!(!node(2, 1, "generic", "Card").is_ignored());
        assert!(!node(2, 1, "button", "").is_ignored());
        assert!(!node(2, 1, "paragraph", "").is_ignored());
        let log = AxNode {
            live: "polite".to_owned(),
            ..node(2, 1, "generic", "")
        };
        assert!(!log.is_ignored());
    }

    #[test]
    fn focus_on_a_left_out_node_lands_on_its_nearest_shown_ancestor() {
        let mut tree = PageTree::default();
        tree.apply(received(
            vec![
                node(1, -1, "document", ""),
                node(2, 1, "link", "Home"),
                node(3, 2, "generic", ""),
            ],
            Some(3),
        ));
        assert_eq!(tree.focused_key().as_deref(), Some("ax2"));
        tree.focused = Some(1);
        assert_eq!(tree.focused_key(), None);
    }

    #[test]
    fn fields_follow_the_engine_value_without_echoing_it_back() {
        let mut tree = PageTree::default();
        tree.apply(received(
            vec![node(1, -1, "document", ""), field(2, 1, "old")],
            None,
        ));
        assert_eq!(tree.fields[&2].edit.text, "old");
        tree.apply(received(
            vec![node(1, -1, "document", ""), field(2, 1, "new")],
            None,
        ));
        let field = &tree.fields[&2];
        assert_eq!(field.edit.text, "new");
        assert_eq!(field.synced, "new");
        tree.apply(received(vec![node(1, -1, "document", "")], None));
        assert!(tree.fields.is_empty());
    }

    #[test]
    fn the_engine_moving_focus_off_a_field_gives_it_a_new_key() {
        let mut tree = PageTree::default();
        tree.apply(received(
            vec![
                node(1, -1, "document", ""),
                field(2, 1, ""),
                node(3, 1, "button", "Go"),
            ],
            None,
        ));
        let field = tree.fields.get_mut(&2).unwrap();
        field.edit.focused = true;
        assert_eq!(tree.element_key(2), "ax2.0");
        assert!(!tree.return_focus_from_fields(Some(2)));
        assert!(tree.return_focus_from_fields(Some(3)));
        assert!(!tree.fields[&2].edit.focused);
        assert_eq!(tree.element_key(2), "ax2.1");
    }

    #[test]
    fn a_details_elements_open_state_is_on_its_summary() {
        let mut tree = PageTree::default();
        let mut details = node(2, 1, "group", "");
        details.children = vec![3, 4];
        details.expanded = 1;
        let mut document = node(1, -1, "document", "");
        document.children = vec![2];
        tree.apply(received(
            vec![
                document,
                details,
                node(3, 2, "button", "More"),
                node(4, 2, "paragraph", ""),
            ],
            None,
        ));
        assert_eq!(tree.nodes[&2].expanded, 0);
        assert_eq!(tree.nodes[&3].expanded, 1);
    }

    #[test]
    fn engine_carets_count_characters_not_bytes() {
        assert_eq!(byte_offset("héllo", 2), Some(3));
        assert_eq!(byte_offset("héllo", 9), Some(6));
        assert_eq!(byte_offset("héllo", -1), None);
    }
}
