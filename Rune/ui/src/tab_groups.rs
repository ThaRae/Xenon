/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::{HashMap, HashSet};

use rune::{Drag, Phase, PopupItem, PopupMenu, TextEdit, Tx};

use crate::engine::{ViewId, WindowId};
use crate::ffi::XenonStr;
use crate::ffi_tab_groups as ffi;
use crate::state::Browser;

pub type GroupId = u64;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum GroupColor {
    Rose,
    Sky,
    Amber,
    Jade,
    Orchid,
    Ember,
    Iris,
    Moss,
    Slate,
}

impl GroupColor {
    pub const ALL: [GroupColor; 9] = [
        GroupColor::Rose,
        GroupColor::Sky,
        GroupColor::Amber,
        GroupColor::Jade,
        GroupColor::Orchid,
        GroupColor::Ember,
        GroupColor::Iris,
        GroupColor::Moss,
        GroupColor::Slate,
    ];

    pub fn from_raw(raw: u32) -> GroupColor {
        GroupColor::ALL
            .get(raw as usize)
            .copied()
            .unwrap_or(GroupColor::Rose)
    }

    pub fn raw(self) -> u32 {
        self as u32
    }

    pub fn name(self) -> &'static str {
        match self {
            GroupColor::Rose => "Rose",
            GroupColor::Sky => "Sky",
            GroupColor::Amber => "Amber",
            GroupColor::Jade => "Jade",
            GroupColor::Orchid => "Orchid",
            GroupColor::Ember => "Ember",
            GroupColor::Iris => "Iris",
            GroupColor::Moss => "Moss",
            GroupColor::Slate => "Slate",
        }
    }

    pub fn rgb(self, dark: bool) -> u32 {
        let (light, dark_tone) = match self {
            GroupColor::Rose => (0xe8004a, 0xff85aa),
            GroupColor::Sky => (0x0787b5, 0x6bceff),
            GroupColor::Amber => (0xa37100, 0xebb761),
            GroupColor::Jade => (0x089174, 0x61dab8),
            GroupColor::Orchid => (0xab4ac7, 0xe1a8f3),
            GroupColor::Ember => (0xcd4d01, 0xffa887),
            GroupColor::Iris => (0x566bf0, 0xabbefe),
            GroupColor::Moss => (0x4a9002, 0x9fd283),
            GroupColor::Slate => (0x707d8e, 0xb4c2d5),
        };
        if dark { dark_tone } else { light }
    }

    pub fn title_rgb(dark: bool) -> u32 {
        if dark { 0x1c1b1f } else { 0xffffff }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Group {
    pub id: GroupId,
    pub title: String,
    pub color: GroupColor,
    pub collapsed: bool,
    pub tabs: Vec<ViewId>,
}

impl Group {
    pub fn tooltip(&self) -> String {
        let tabs = if self.tabs.len() == 1 {
            "1 tab".to_owned()
        } else {
            format!("{} tabs", self.tabs.len())
        };
        if self.title.is_empty() {
            tabs
        } else {
            format!("{} ({tabs})", self.title)
        }
    }

    pub fn menu_text(&self, first_tab_title: &str) -> String {
        if !self.title.is_empty() {
            return self.title.clone();
        }
        match self.tabs.len().saturating_sub(1) {
            0 => format!("“{first_tab_title}”"),
            1 => format!("“{first_tab_title}” and 1 other tab"),
            others => format!("“{first_tab_title}” and {others} other tabs"),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum StripKey {
    Tab(ViewId),
    Group(GroupId),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StripEntry {
    Header(GroupId),
    Tab {
        view: ViewId,
        group: Option<GroupId>,
        last_in_group: bool,
    },
}

pub struct GroupEditor {
    pub group: GroupId,
    pub title: TextEdit,
    sent_title: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Axis {
    Horizontal,
    Vertical,
}

type DropTarget = (usize, Option<GroupId>);

struct TabDrag {
    view: ViewId,
    origin: f32,
    extent: f32,
    layout: DragLayout,
    start: DropTarget,
    sent: DropTarget,
}

#[derive(Default)]
pub struct TabGroups {
    windows: HashMap<WindowId, Vec<Group>>,
    pub editor: Option<GroupEditor>,
    rects: HashMap<StripKey, [f32; 4]>,
    drag: Option<TabDrag>,
    detached_title: TextEdit,
}

impl TabGroups {
    pub fn groups(&self, window: Option<WindowId>) -> &[Group] {
        window
            .and_then(|window| self.windows.get(&window))
            .map_or(&[], Vec::as_slice)
    }

    pub fn group(&self, window: Option<WindowId>, id: GroupId) -> Option<&Group> {
        self.groups(window).iter().find(|group| group.id == id)
    }

    pub fn group_of(&self, window: Option<WindowId>, view: ViewId) -> Option<&Group> {
        self.groups(window)
            .iter()
            .find(|group| group.tabs.contains(&view))
    }

    pub fn groups_changed(&mut self, window: WindowId, groups: Vec<Group>) {
        if groups.is_empty() {
            self.windows.remove(&window);
        } else {
            self.windows.insert(window, groups);
        }
    }

    pub fn window_closed(&mut self, window: WindowId) {
        self.windows.remove(&window);
    }

    pub fn record_rect(&mut self, key: StripKey, rect: [f32; 4]) {
        self.rects.insert(key, rect);
    }

    pub fn open_editor(&mut self, group: &Group, focus_title: bool) {
        let mut title = TextEdit::new(group.title.as_str());
        if focus_title {
            title.set_selection(0, group.title.len());
            title.focused = true;
        }
        self.editor = Some(GroupEditor {
            group: group.id,
            title,
            sent_title: group.title.clone(),
        });
    }

    pub fn close_editor(&mut self) {
        self.editor = None;
    }

    pub fn editor_title(&mut self) -> &mut TextEdit {
        match self.editor.as_mut() {
            Some(editor) => &mut editor.title,
            None => &mut self.detached_title,
        }
    }
}

pub fn strip_entries<'a>(
    views: impl IntoIterator<Item = ViewId>,
    groups: &'a [Group],
) -> Vec<StripEntry> {
    let group_of = |view: ViewId| groups.iter().find(|group| group.tabs.contains(&view));
    let views: Vec<ViewId> = views.into_iter().collect();
    let mut entries = Vec::with_capacity(views.len());
    let mut headed = HashSet::new();
    for (index, &view) in views.iter().enumerate() {
        let group = group_of(view);
        if let Some(group) = group
            && headed.insert(group.id)
        {
            entries.push(StripEntry::Header(group.id));
        }
        if group.is_some_and(|group| group.collapsed) {
            continue;
        }
        let group_id = group.map(|group| group.id);
        let next_group = views
            .get(index + 1)
            .and_then(|next| group_of(*next))
            .map(|group| group.id);
        entries.push(StripEntry::Tab {
            view,
            group: group_id,
            last_in_group: group_id.is_some() && next_group != group_id,
        });
    }
    entries
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Span {
    pub start: f32,
    pub extent: f32,
}

impl Span {
    fn center(self) -> f32 {
        self.start + self.extent / 2.0
    }
}

#[derive(Clone, Debug)]
pub struct SlotTab {
    pub view: ViewId,
    pub pinned: bool,
    pub group: Option<GroupId>,
    pub span: Option<Span>,
}

pub struct DragLayout {
    pub tabs: Vec<SlotTab>,
    pub groups: Vec<Group>,
    pub headers: HashMap<GroupId, Span>,
}

impl DragLayout {
    fn collapsed(&self, group: Option<GroupId>) -> bool {
        group.is_some_and(|id| {
            self.groups
                .iter()
                .any(|group| group.id == id && group.collapsed)
        })
    }

    fn hidden(&self, index: usize) -> bool {
        self.tabs
            .get(index)
            .is_some_and(|tab| self.collapsed(tab.group))
    }

    fn open_group_at(&self, index: Option<usize>) -> Option<GroupId> {
        let group = self.tabs.get(index?)?.group;
        (!self.collapsed(group)).then_some(group).flatten()
    }

    fn group_bounds(&self, group: GroupId) -> Option<(usize, usize)> {
        let first = self.tabs.iter().position(|tab| tab.group == Some(group))?;
        let last = self.tabs.iter().rposition(|tab| tab.group == Some(group))?;
        Some((first, last))
    }

    fn slot_span(&self, slot: usize, dragged: usize) -> Option<Span> {
        if !self.hidden(slot) {
            return self.tabs.get(slot)?.span;
        }
        let group = self.tabs.get(slot)?.group?;
        let (first, last) = self.group_bounds(group)?;
        if (slot == last && slot > dragged) || (slot == first && slot < dragged) {
            return self.headers.get(&group).copied();
        }
        None
    }

    pub fn drop_target(&self, dragged_view: ViewId, center: f32) -> Option<DropTarget> {
        let dragged = self.tabs.iter().position(|tab| tab.view == dragged_view)?;
        let tab = &self.tabs[dragged];
        let pinned_count = self.tabs.iter().take_while(|tab| tab.pinned).count();
        let (first_slot, last_slot) = if tab.pinned {
            (0, pinned_count.checked_sub(1)?)
        } else {
            (pinned_count, self.tabs.len().checked_sub(1)?)
        };

        let mut slot = dragged;
        let mut best_distance = f32::MAX;
        for candidate in first_slot..=last_slot {
            let Some(span) = self.slot_span(candidate, dragged) else {
                continue;
            };
            let distance = (span.center() - center).abs();
            if distance < best_distance {
                best_distance = distance;
                slot = candidate;
            }
        }

        if tab.pinned {
            return Some((slot, None));
        }

        let (left, right) = if slot > dragged {
            (Some(slot), slot + 1)
        } else if slot == dragged {
            (slot.checked_sub(1), slot + 1)
        } else {
            (slot.checked_sub(1), slot)
        };
        let left_group = self.open_group_at(left);
        let right_group = self.open_group_at(Some(right));
        let own_group = tab.group;
        let dragging_whole_group = own_group.is_some_and(|own| {
            self.tabs
                .iter()
                .filter(|other| other.group == Some(own))
                .count()
                == 1
        });

        let group = if left_group.is_some() && left_group == right_group {
            left_group
        } else if dragging_whole_group {
            own_group
        } else if (left_group.is_some() || right_group.is_some()) && !self.hidden(slot) {
            self.tabs[slot].span.and_then(|gap| {
                let offset = center - gap.center();
                let buffer = gap.extent / 4.0;
                let threshold = |group: Option<GroupId>| {
                    if group == own_group { 0.0 } else { buffer }
                };
                if left_group.is_some() && offset <= -threshold(left_group) {
                    left_group
                } else if right_group.is_some() && offset >= threshold(right_group) {
                    right_group
                } else {
                    None
                }
            })
        } else {
            None
        };
        Some((slot, group))
    }
}

fn main_axis(rect: [f32; 4], axis: Axis) -> Span {
    match axis {
        Axis::Horizontal => Span {
            start: rect[0],
            extent: rect[2],
        },
        Axis::Vertical => Span {
            start: rect[1],
            extent: rect[3],
        },
    }
}

fn drag_layout(s: &Browser, axis: Axis) -> DragLayout {
    let window = s.shown_window();
    let groups = s.tab_groups.groups(window);
    let rects = &s.tab_groups.rects;
    DragLayout {
        tabs: s
            .tabs
            .iter()
            .map(|tab| SlotTab {
                view: tab.view,
                pinned: tab.pinned,
                group: s
                    .tab_groups
                    .group_of(window, tab.view)
                    .map(|group| group.id),
                span: rects
                    .get(&StripKey::Tab(tab.view))
                    .map(|rect| main_axis(*rect, axis)),
            })
            .collect(),
        groups: groups.to_vec(),
        headers: groups
            .iter()
            .filter_map(|group| {
                rects
                    .get(&StripKey::Group(group.id))
                    .map(|rect| (group.id, main_axis(*rect, axis)))
            })
            .collect(),
    }
}

pub fn drag_tab(s: &mut Browser, view: ViewId, axis: Axis, drag: &Drag, tx: &mut Tx) {
    match drag.phase {
        Phase::Down => {
            let window = s.shown_window();
            let start = s.tabs.position(view).map(|index| {
                let group = s.tab_groups.group_of(window, view).map(|group| group.id);
                (index, group)
            });
            let span = s
                .tab_groups
                .rects
                .get(&StripKey::Tab(view))
                .map(|rect| main_axis(*rect, axis));
            s.tab_groups.drag = start.zip(span).map(|(start, span)| TabDrag {
                view,
                origin: span.start,
                extent: span.extent,
                layout: drag_layout(s, axis),
                start,
                sent: start,
            });
        }
        Phase::Move => {
            tx.live_only();
            let Some(tab_drag) = s.tab_groups.drag.as_mut().filter(|d| d.view == view) else {
                return;
            };
            let travel = match axis {
                Axis::Horizontal => drag.dx,
                Axis::Vertical => drag.dy,
            };
            let center = tab_drag.origin + travel + tab_drag.extent / 2.0;
            let Some(target) = tab_drag.layout.drop_target(view, center) else {
                return;
            };
            if target == tab_drag.sent {
                return;
            }
            tab_drag.sent = target;
            GroupEngine.drop_tab(view, target.0, target.1);
        }
        Phase::Up => {
            if let Some(tab_drag) = s.tab_groups.drag.take().filter(|d| d.view == view)
                && drag.cancelled()
                && tab_drag.sent != tab_drag.start
            {
                GroupEngine.drop_tab(view, tab_drag.start.0, tab_drag.start.1);
            }
        }
    }
}

pub fn relative_shown_view(s: &Browser, offset: isize) -> Option<ViewId> {
    let window = s.shown_window();
    let count = s.tabs.iter().count() as isize;
    (1..count)
        .filter_map(|step| s.tabs.relative_view(offset.signum() * step))
        .find(|view| {
            !s.tab_groups
                .group_of(window, *view)
                .is_some_and(|group| group.collapsed)
        })
}

pub fn edit_requested(s: &mut Browser, window: WindowId, group: GroupId) {
    if s.shown_window() != Some(window) {
        return;
    }
    if let Some(group) = s.tab_groups.group(Some(window), group).cloned() {
        s.hover_card = None;
        s.tab_groups.open_editor(&group, true);
    }
}

pub fn show_editor(s: &mut Browser, group: GroupId) {
    if let Some(group) = s.tab_groups.group(s.shown_window(), group).cloned() {
        s.hover_card = None;
        s.tab_groups.open_editor(&group, false);
    }
}

pub fn toggle_collapsed(s: &mut Browser, group: GroupId) {
    if let Some(group) = s.tab_groups.group(s.shown_window(), group) {
        GroupEngine.set_collapsed(group.id, !group.collapsed);
    }
}

pub fn poll(s: &mut Browser) -> bool {
    let window = s.shown_window();
    let Some(editor) = s.tab_groups.editor.as_ref() else {
        return false;
    };
    if s.tab_groups.group(window, editor.group).is_none() {
        s.tab_groups.editor = None;
        return true;
    }
    if let Some(editor) = s.tab_groups.editor.as_mut()
        && editor.title.text != editor.sent_title
    {
        editor.sent_title = editor.title.text.clone();
        GroupEngine.set_title(editor.group, &editor.sent_title);
    }
    false
}

pub fn run_editor_action(s: &mut Browser, action: EditorAction) {
    let Some(editor) = s.tab_groups.editor.take() else {
        return;
    };
    let group = editor.group;
    match action {
        EditorAction::NewTab => GroupEngine.new_tab_in_group(group),
        EditorAction::MoveToNewWindow => GroupEngine.move_to_new_window(group),
        EditorAction::Close => GroupEngine.close_group(group),
        EditorAction::Ungroup => GroupEngine.ungroup(group),
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EditorAction {
    NewTab,
    MoveToNewWindow,
    Close,
    Ungroup,
}

const MENU_NEW_GROUP: &str = "group-new";
const MENU_NEW_GROUP_IN_SUBMENU: &str = "group-new-in-submenu";
const MENU_REMOVE_FROM_GROUP: &str = "group-remove";
const MENU_ADD_TO_GROUP_PREFIX: &str = "group-add-";

pub fn with_tab_menu_items(s: &Browser, view: ViewId, menu: PopupMenu) -> PopupMenu {
    let window = s.shown_window();
    let own_group = s.tab_groups.group_of(window, view).map(|group| group.id);
    let other_groups: Vec<&Group> = s
        .tab_groups
        .groups(window)
        .iter()
        .filter(|group| Some(group.id) != own_group)
        .collect();
    let menu = menu.separator();
    let menu = if other_groups.is_empty() {
        menu.item(PopupItem::new(MENU_NEW_GROUP, "Add Tab to New Group"))
    } else {
        let mut groups = PopupMenu::new()
            .item(PopupItem::new(MENU_NEW_GROUP_IN_SUBMENU, "New Group"))
            .separator();
        for group in other_groups {
            let first_title = group
                .tabs
                .first()
                .and_then(|first| s.tabs.get(*first))
                .map_or("", |tab| tab.display_title());
            groups = groups.item(
                PopupItem::new(
                    format!("{MENU_ADD_TO_GROUP_PREFIX}{}", group.id),
                    group.menu_text(first_title),
                )
                .symbol("circle.fill"),
            );
        }
        menu.submenu("Add Tab to Group", groups)
    };
    if own_group.is_some() {
        menu.item(PopupItem::new(MENU_REMOVE_FROM_GROUP, "Remove from Group"))
    } else {
        menu
    }
}

pub fn run_tab_menu_item(view: ViewId, picked: &str) -> bool {
    match picked {
        MENU_NEW_GROUP | MENU_NEW_GROUP_IN_SUBMENU => GroupEngine.add_to_new_group(view),
        MENU_REMOVE_FROM_GROUP => GroupEngine.remove_from_group(view),
        _ => {
            let Some(group) = picked
                .strip_prefix(MENU_ADD_TO_GROUP_PREFIX)
                .and_then(|id| id.parse::<GroupId>().ok())
            else {
                return false;
            };
            GroupEngine.add_to_group(view, group);
        }
    }
    true
}

#[derive(Clone, Copy, Debug, Default)]
pub struct GroupEngine;

impl GroupEngine {
    pub fn add_to_new_group(self, view: ViewId) {
        unsafe { ffi::xenon_engine_add_tab_to_new_group(view) }
    }

    pub fn add_to_group(self, view: ViewId, group: GroupId) {
        unsafe { ffi::xenon_engine_add_tab_to_group(view, group) }
    }

    pub fn remove_from_group(self, view: ViewId) {
        unsafe { ffi::xenon_engine_remove_tab_from_group(view) }
    }

    pub fn set_title(self, group: GroupId, title: &str) {
        unsafe { ffi::xenon_engine_set_tab_group_title(group, XenonStr::borrowed(title)) }
    }

    pub fn set_color(self, group: GroupId, color: GroupColor) {
        unsafe { ffi::xenon_engine_set_tab_group_color(group, color.raw()) }
    }

    pub fn set_collapsed(self, group: GroupId, collapsed: bool) {
        unsafe { ffi::xenon_engine_set_tab_group_collapsed(group, collapsed) }
    }

    pub fn ungroup(self, group: GroupId) {
        unsafe { ffi::xenon_engine_ungroup_tabs(group) }
    }

    pub fn close_group(self, group: GroupId) {
        unsafe { ffi::xenon_engine_close_tab_group(group) }
    }

    pub fn new_tab_in_group(self, group: GroupId) {
        unsafe { ffi::xenon_engine_new_tab_in_group(group) }
    }

    pub fn move_to_new_window(self, group: GroupId) {
        unsafe { ffi::xenon_engine_move_tab_group_to_new_window(group) }
    }

    pub fn drop_tab(self, view: ViewId, index: usize, group: Option<GroupId>) {
        unsafe {
            ffi::xenon_engine_drop_tab(
                view,
                u32::try_from(index).unwrap_or(u32::MAX),
                group.unwrap_or(ffi::XENON_TAB_GROUP_NONE),
            )
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn group(id: GroupId, tabs: &[ViewId], collapsed: bool) -> Group {
        Group {
            id,
            title: String::new(),
            color: GroupColor::Rose,
            collapsed,
            tabs: tabs.to_vec(),
        }
    }

    fn slot(view: ViewId, group: Option<GroupId>, start: f32) -> SlotTab {
        SlotTab {
            view,
            pinned: false,
            group,
            span: Some(Span {
                start,
                extent: 100.0,
            }),
        }
    }

    #[test]
    fn colours_come_from_the_engines_numbers() {
        assert_eq!(GroupColor::from_raw(4), GroupColor::Orchid);
        assert_eq!(GroupColor::from_raw(42), GroupColor::Rose);
        assert_eq!(GroupColor::Moss.raw(), 7);
        assert_eq!(GroupColor::Sky.rgb(false), 0x0787b5);
        assert_eq!(GroupColor::Sky.rgb(true), 0x6bceff);
    }

    #[test]
    fn tooltips_and_menu_text_read_as_qts() {
        let mut work = group(1, &[1, 2], false);
        assert_eq!(work.tooltip(), "2 tabs");
        assert_eq!(work.menu_text("Example"), "“Example” and 1 other tab");
        work.tabs.push(3);
        assert_eq!(work.menu_text("Example"), "“Example” and 2 other tabs");
        work.title = "Work".into();
        assert_eq!(work.tooltip(), "Work (3 tabs)");
        assert_eq!(work.menu_text("Example"), "Work");
        assert_eq!(group(2, &[5], false).menu_text("Five"), "“Five”");
    }

    #[test]
    fn the_strip_puts_a_header_before_each_group_and_hides_collapsed_tabs() {
        let groups = [group(7, &[2, 3], false), group(8, &[5], true)];
        let entries = strip_entries([1, 2, 3, 4, 5], &groups);
        assert_eq!(
            entries,
            [
                StripEntry::Tab {
                    view: 1,
                    group: None,
                    last_in_group: false
                },
                StripEntry::Header(7),
                StripEntry::Tab {
                    view: 2,
                    group: Some(7),
                    last_in_group: false
                },
                StripEntry::Tab {
                    view: 3,
                    group: Some(7),
                    last_in_group: true
                },
                StripEntry::Tab {
                    view: 4,
                    group: None,
                    last_in_group: false
                },
                StripEntry::Header(8),
            ]
        );
    }

    #[test]
    fn the_mirror_replaces_a_windows_groups_and_forgets_empty_ones() {
        let mut mirror = TabGroups::default();
        mirror.groups_changed(1, vec![group(7, &[10, 11], false)]);
        assert_eq!(mirror.group_of(Some(1), 11).map(|group| group.id), Some(7));
        assert!(mirror.group_of(Some(2), 11).is_none());
        mirror.groups_changed(1, Vec::new());
        assert!(mirror.groups(Some(1)).is_empty());
    }

    #[test]
    fn opening_the_editor_for_a_new_group_selects_its_title() {
        let mut mirror = TabGroups::default();
        let mut named = group(3, &[1], false);
        named.title = "Docs".into();
        mirror.open_editor(&named, true);
        let editor = mirror.editor.as_ref().unwrap();
        assert!(editor.title.focused);
        assert_eq!(editor.title.text, "Docs");
        mirror.open_editor(&named, false);
        assert!(!mirror.editor.as_ref().unwrap().title.focused);
    }

    fn layout(groups: &[Group], tabs: Vec<SlotTab>) -> DragLayout {
        DragLayout {
            tabs,
            groups: groups.to_vec(),
            headers: HashMap::new(),
        }
    }

    #[test]
    fn a_dragged_tab_takes_the_nearest_slot() {
        let groups = [];
        let layout = layout(
            &groups,
            vec![
                slot(1, None, 0.0),
                slot(2, None, 100.0),
                slot(3, None, 200.0),
            ],
        );
        assert_eq!(layout.drop_target(1, 50.0), Some((0, None)));
        assert_eq!(layout.drop_target(1, 140.0), Some((1, None)));
        assert_eq!(layout.drop_target(1, 260.0), Some((2, None)));
    }

    #[test]
    fn a_tab_dropped_between_two_members_joins_their_group() {
        let groups = [group(9, &[2, 3], false)];
        let layout = layout(
            &groups,
            vec![
                slot(1, None, 0.0),
                slot(2, Some(9), 100.0),
                slot(3, Some(9), 200.0),
            ],
        );
        assert_eq!(layout.drop_target(1, 150.0), Some((1, Some(9))));
    }

    #[test]
    fn at_a_groups_edge_the_offset_decides_whether_a_tab_joins() {
        let groups = [group(9, &[1, 2], false)];
        let layout = layout(
            &groups,
            vec![
                slot(1, Some(9), 0.0),
                slot(2, Some(9), 100.0),
                slot(3, None, 200.0),
            ],
        );
        assert_eq!(layout.drop_target(3, 250.0), Some((2, None)));
        assert_eq!(layout.drop_target(3, 220.0), Some((2, Some(9))));
        assert_eq!(layout.drop_target(2, 150.0), Some((1, Some(9))));
        assert_eq!(layout.drop_target(2, 160.0), Some((1, None)));
    }

    #[test]
    fn a_group_of_one_keeps_its_group_wherever_it_goes() {
        let groups = [group(4, &[1], false)];
        let layout = layout(
            &groups,
            vec![
                slot(1, Some(4), 0.0),
                slot(2, None, 100.0),
                slot(3, None, 200.0),
            ],
        );
        assert_eq!(layout.drop_target(1, 260.0), Some((2, Some(4))));
    }

    #[test]
    fn a_collapsed_group_is_one_slot_and_cant_be_joined() {
        let groups = [group(5, &[2, 3], true)];
        let mut layout = layout(
            &groups,
            vec![
                slot(1, None, 0.0),
                SlotTab {
                    span: None,
                    ..slot(2, Some(5), 0.0)
                },
                SlotTab {
                    span: None,
                    ..slot(3, Some(5), 0.0)
                },
                slot(4, None, 140.0),
            ],
        );
        layout.headers.insert(
            5,
            Span {
                start: 100.0,
                extent: 40.0,
            },
        );
        assert_eq!(layout.drop_target(1, 125.0), Some((2, None)));
        assert_eq!(layout.drop_target(4, 115.0), Some((1, None)));
    }

    #[test]
    fn pinned_tabs_stay_among_pinned_tabs_and_out_of_groups() {
        let groups = [group(9, &[3], false)];
        let layout = layout(
            &groups,
            vec![
                SlotTab {
                    pinned: true,
                    ..slot(1, None, 0.0)
                },
                SlotTab {
                    pinned: true,
                    ..slot(2, None, 100.0)
                },
                slot(3, Some(9), 200.0),
            ],
        );
        assert_eq!(layout.drop_target(1, 290.0), Some((1, None)));
    }
}
