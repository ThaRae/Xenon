/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::time::Duration;

use rune::{Drag, Phase, Tx};

use crate::chrome_popup::{self, PopupItem, PopupMenu};

use crate::engine::{ChromeLayout, ViewId, Viewport};
use crate::ffi_split_view::{
    XENON_SPLIT_SIDE_LEFT, XENON_SPLIT_SIDE_RIGHT, xenon_engine_split_view_create,
    xenon_engine_split_view_drop_tab, xenon_engine_split_view_open_new_tab,
    xenon_engine_split_view_reverse, xenon_engine_split_view_separate,
};
use crate::state::Browser;
use crate::tab_list::TabList;

pub const INSET: f32 = 5.0;
pub const SIDEBAR_INSET: f32 = 4.0;
pub const GAP: f32 = 12.0;
pub const RING_WIDTH: f32 = 2.0;
pub const RING_OFFSET: f32 = 2.0;
pub const PAGE_RADIUS: f32 = 10.0;
const MIN_PAGE_WIDTH: f32 = 240.0;
const SNAP_DISTANCE: f32 = 15.0;
const DROP_DELAY: f64 = 0.5;
const DROP_EDGE_FRACTION: f32 = 0.2;
const DROP_EDGE_MIN_WIDTH: f32 = 64.0;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Side {
    Left,
    Right,
}

impl Side {
    fn raw(self) -> u32 {
        match self {
            Side::Left => XENON_SPLIT_SIDE_LEFT,
            Side::Right => XENON_SPLIT_SIDE_RIGHT,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rect {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

impl Rect {
    pub fn new(x: f32, y: f32, w: f32, h: f32) -> Rect {
        Rect { x, y, w, h }
    }

    pub fn right(&self) -> f32 {
        self.x + self.w
    }

    pub fn contains(&self, (x, y): (f32, f32)) -> bool {
        x >= self.x && x < self.right() && y >= self.y && y < self.y + self.h
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Split {
    pub left: ViewId,
    pub right: ViewId,
    pub ratio: f32,
}

impl Split {
    pub fn contains(&self, view: ViewId) -> bool {
        self.left == view || self.right == view
    }

    pub fn partner(&self, view: ViewId) -> ViewId {
        if self.left == view {
            self.right
        } else {
            self.left
        }
    }

    pub fn side(&self, view: ViewId) -> Side {
        if self.left == view {
            Side::Left
        } else {
            Side::Right
        }
    }

    pub fn at(&self, side: Side) -> ViewId {
        match side {
            Side::Left => self.left,
            Side::Right => self.right,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Layout {
    pub left: Rect,
    pub divider: Rect,
    pub right: Rect,
}

impl Layout {
    pub fn pane(&self, side: Side) -> Rect {
        match side {
            Side::Left => self.left,
            Side::Right => self.right,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DropTarget {
    pub side: Side,
    pub armed: bool,
    since: Option<f64>,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TabDrag {
    pub view: ViewId,
    press: (f32, f32),
    pub pointer: (f32, f32),
    pub target: Option<DropTarget>,
}

pub struct SplitViews {
    splits: Vec<Split>,
    pub drag_and_drop: bool,
    pub page_origin: (f32, f32),
    pub divider_active: bool,
    pub divider_hovered: bool,
    pub tab_drag: Option<TabDrag>,
}

impl Default for SplitViews {
    fn default() -> SplitViews {
        SplitViews {
            splits: Vec::new(),
            drag_and_drop: true,
            page_origin: (0.0, 0.0),
            divider_active: false,
            divider_hovered: false,
            tab_drag: None,
        }
    }
}

impl SplitViews {
    pub fn split_of(&self, view: ViewId) -> Option<&Split> {
        self.splits.iter().find(|split| split.contains(view))
    }

    pub fn partner(&self, view: ViewId) -> Option<ViewId> {
        self.split_of(view).map(|split| split.partner(view))
    }

    pub fn side(&self, view: ViewId) -> Option<Side> {
        self.split_of(view).map(|split| split.side(view))
    }

    pub fn shown(&self, tabs: &TabList) -> Option<&Split> {
        let active = tabs.active_view()?;
        self.split_of(active)
            .filter(|split| tabs.get(split.left).is_some() && tabs.get(split.right).is_some())
    }

    pub fn is_shown(&self, tabs: &TabList, view: ViewId) -> bool {
        tabs.is_active(view) || self.shown(tabs).is_some_and(|split| split.contains(view))
    }

    pub fn replace(&mut self, pairs: &[(ViewId, ViewId)]) {
        let previous = std::mem::take(&mut self.splits);
        self.splits = pairs
            .iter()
            .map(|&(left, right)| {
                let ratio = previous
                    .iter()
                    .find_map(|split| {
                        if split.left == left && split.right == right {
                            Some(split.ratio)
                        } else if split.left == right && split.right == left {
                            Some(1.0 - split.ratio)
                        } else {
                            None
                        }
                    })
                    .unwrap_or(0.5);
                Split { left, right, ratio }
            })
            .collect();
    }

    fn set_ratio(&mut self, view: ViewId, ratio: f32) -> bool {
        match self.splits.iter_mut().find(|split| split.contains(view)) {
            Some(split) if split.ratio != ratio => {
                split.ratio = ratio;
                true
            }
            _ => false,
        }
    }
}

fn minimum_page_width(available_width: f32) -> f32 {
    MIN_PAGE_WIDTH.min((available_width / 4.0).floor())
}

pub fn split_area(width: f32, height: f32, sidebar_on_left: bool) -> Rect {
    let left = if sidebar_on_left {
        SIDEBAR_INSET
    } else {
        INSET
    };
    Rect::new(
        left,
        INSET,
        (width - left - INSET).max(0.0),
        (height - 2.0 * INSET).max(0.0),
    )
}

pub fn divider_center(area: Rect, ratio: f32) -> f32 {
    let available_width = (area.w - GAP).max(0.0);
    let minimum_width = minimum_page_width(available_width);
    let left_width = (available_width * ratio).round().clamp(
        minimum_width,
        minimum_width.max(available_width - minimum_width),
    );
    area.x + left_width + GAP / 2.0
}

pub fn layout(area: Rect, ratio: f32) -> Layout {
    let center = divider_center(area, ratio);
    let left_right = center - GAP / 2.0;
    let right_left = center + GAP / 2.0;
    Layout {
        left: Rect::new(area.x, area.y, left_right - area.x, area.h),
        divider: Rect::new(left_right, area.y, GAP, area.h),
        right: Rect::new(right_left, area.y, area.right() - right_left, area.h),
    }
}

pub fn ratio_for_divider(area: Rect, center: f32) -> f32 {
    let available_width = (area.w - GAP).max(1.0);
    let mut left_width = center - GAP / 2.0 - area.x;
    if (left_width - available_width / 2.0).abs() <= SNAP_DISTANCE {
        left_width = available_width / 2.0;
    }
    let minimum_width = minimum_page_width(available_width);
    left_width = left_width.clamp(
        minimum_width,
        minimum_width.max(available_width - minimum_width),
    );
    left_width / available_width
}

pub fn drop_side_at(x: f32, width: f32, armed: bool) -> Option<Side> {
    let edge_width = if armed {
        width / 2.0
    } else {
        DROP_EDGE_MIN_WIDTH.max((width * DROP_EDGE_FRACTION).round())
    };
    if x < edge_width {
        Some(Side::Left)
    } else if x >= width - edge_width {
        Some(Side::Right)
    } else {
        None
    }
}

pub fn drop_indicator_rect(area: Rect, split_shown: bool, side: Side) -> Rect {
    let area = if split_shown {
        area
    } else {
        Rect::new(area.x, 0.0, area.w, area.y + area.h)
    };
    let half_width = ((area.w - GAP) / 2.0).floor();
    match side {
        Side::Left => Rect::new(area.x, area.y, half_width, area.h),
        Side::Right => Rect::new(area.right() - half_width, area.y, half_width, area.h),
    }
}

pub fn host_of(url: &str) -> Option<&str> {
    let (_, rest) = url.split_once("://")?;
    let authority = rest.split(['/', '?', '#']).next()?;
    let host = authority.rsplit('@').next()?;
    let host = if host.starts_with('[') {
        host.split_inclusive(']').next()?
    } else {
        host.split(':').next()?
    };
    (!host.is_empty()).then_some(host)
}

pub fn shown_area(s: &Browser) -> Rect {
    let (width, height) = s.page_size();
    split_area(width, height, s.chrome.layout == ChromeLayout::Vertical)
}

pub fn shown_layout(s: &Browser) -> Option<(Split, Layout)> {
    let split = *s.split.shown(&s.tabs)?;
    Some((split, layout(shown_area(s), split.ratio)))
}

pub fn pane_viewport(s: &Browser, view: ViewId) -> Option<Viewport> {
    if crate::page_prompts::chrome_hidden(s) {
        return None;
    }
    let (split, layout) = shown_layout(s)?;
    if !split.contains(view) {
        return None;
    }
    let pane = layout.pane(split.side(view));
    Some(Viewport::from_points(pane.w, pane.h, s.page.scale()))
}

pub fn splits_changed(s: &mut Browser, pairs: &[(ViewId, ViewId)]) {
    s.split.replace(pairs);
    s.sync_active_viewport();
}

pub fn new_split_view(s: &mut Browser) {
    let Some(active) = s.tabs.active_view() else {
        return;
    };
    if s.split.partner(active).is_none() {
        open_new_tab_beside(active, Side::Right);
    }
}

fn open_new_tab_beside(view: ViewId, new_tab_side: Side) {
    unsafe { xenon_engine_split_view_open_new_tab(view, new_tab_side.raw()) }
}

pub fn reverse(view: ViewId) {
    unsafe { xenon_engine_split_view_reverse(view) }
}

pub fn separate(view: ViewId) {
    unsafe { xenon_engine_split_view_separate(view) }
}

pub fn close_side(s: &mut Browser, view: ViewId, side: Side, tx: &mut Tx) {
    if let Some(split) = s.split.split_of(view).copied() {
        s.close_tab(split.at(side), tx);
    }
}

pub fn resize_shown(s: &mut Browser, center: f32) {
    let Some((split, _)) = shown_layout(s) else {
        return;
    };
    let ratio = ratio_for_divider(shown_area(s), center);
    if s.split.set_ratio(split.left, ratio) {
        s.sync_active_viewport();
    }
}

pub fn divider_drag(s: &mut Browser, drag: &mut Drag, tx: &mut Tx) {
    match drag.phase {
        Phase::Down => {
            let Some((split, layout)) = shown_layout(s) else {
                return;
            };
            if drag.clicks == 2 {
                reverse(split.left);
                return;
            }
            drag.data = layout.divider.x + GAP / 2.0;
            s.split.divider_active = true;
        }
        Phase::Move => {
            if s.split.divider_active {
                resize_shown(s, drag.data + drag.dx);
            }
            tx.live_only();
        }
        Phase::Up => s.split.divider_active = false,
    }
}

const MENU_REVERSE: &str = "split-reverse";
const MENU_CLOSE_LEFT: &str = "split-close-left";
const MENU_CLOSE_RIGHT: &str = "split-close-right";
const MENU_SEPARATE: &str = "split-separate";
const MENU_CLOSE_THIS: &str = "split-close-this";
const MENU_ADD_TO_NEW: &str = "split-add-to-new";
const MENU_WITH_CURRENT: &str = "split-with-current";

fn with_split_actions(menu: PopupMenu) -> PopupMenu {
    menu.item(PopupItem::new(MENU_REVERSE, "Reverse Views"))
        .item(PopupItem::new(MENU_CLOSE_LEFT, "Close Left View"))
        .item(PopupItem::new(MENU_CLOSE_RIGHT, "Close Right View"))
        .separator()
        .item(PopupItem::new(MENU_SEPARATE, "Separate Views"))
}

pub fn show_split_menu(s: &mut Browser, view: ViewId, anchor: &'static str, tx: &mut Tx) {
    if s.split.partner(view).is_none() {
        return;
    }
    chrome_popup::open(
        s,
        tx,
        with_split_actions(PopupMenu::new()),
        anchor,
        move |s, picked, tx| {
            run_menu_item(s, view, picked, tx);
        },
    );
}

pub fn show_inactive_view_menu(s: &mut Browser, view: ViewId, anchor: &'static str, tx: &mut Tx) {
    if s.split.partner(view).is_none() {
        return;
    }
    let menu = PopupMenu::new()
        .item(PopupItem::new(MENU_CLOSE_THIS, "Close This View"))
        .separator();
    chrome_popup::open(
        s,
        tx,
        with_split_actions(menu),
        anchor,
        move |s, picked, tx| {
            run_menu_item(s, view, picked, tx);
        },
    );
}

pub fn tab_menu_items(s: &Browser, view: ViewId, menu: PopupMenu) -> PopupMenu {
    let in_split = s.split.partner(view).is_some();
    let current = s.tabs.active_view();
    let is_current = current == Some(view);
    let current_in_split = current.is_some_and(|current| s.split.partner(current).is_some());
    let mut menu = menu;
    if !in_split && is_current {
        menu = menu.item(PopupItem::new(MENU_ADD_TO_NEW, "Add Tab to New Split View"));
    }
    if !in_split && !is_current && current.is_some() && !current_in_split {
        menu = menu.item(PopupItem::new(
            MENU_WITH_CURRENT,
            "New Split View with Current Tab",
        ));
    }
    if in_split {
        menu = menu
            .item(PopupItem::new(MENU_REVERSE, "Reverse Views"))
            .item(PopupItem::new(MENU_SEPARATE, "Separate Views"));
    }
    menu
}

pub fn run_menu_item(s: &mut Browser, view: ViewId, picked: &str, tx: &mut Tx) -> bool {
    if s.tabs.get(view).is_none() {
        return false;
    }
    match picked {
        MENU_ADD_TO_NEW => open_new_tab_beside(view, Side::Right),
        MENU_WITH_CURRENT => {
            if let Some(current) = s.tabs.active_view().filter(|current| *current != view) {
                unsafe { xenon_engine_split_view_create(current, view, Side::Right.raw(), current) }
            }
        }
        MENU_REVERSE => reverse(view),
        MENU_SEPARATE => separate(view),
        MENU_CLOSE_LEFT => close_side(s, view, Side::Left, tx),
        MENU_CLOSE_RIGHT => close_side(s, view, Side::Right, tx),
        MENU_CLOSE_THIS => s.close_tab(view, tx),
        _ => return false,
    }
    true
}

fn can_drop_beside_active_tab(s: &Browser, dragged: ViewId) -> bool {
    let Some(active) = s.tabs.active_view() else {
        return false;
    };
    if s.split.partner(active).is_some() {
        return s.split.is_shown(&s.tabs, dragged);
    }
    true
}

fn page_rect(s: &Browser) -> Rect {
    let (x, y) = s.split.page_origin;
    let (w, h) = s.page_size();
    Rect::new(x, y, w, h)
}

pub fn tab_drag(s: &mut Browser, view: ViewId, origin: Option<[f32; 2]>, drag: &Drag) -> bool {
    match drag.phase {
        Phase::Down => {
            s.split.tab_drag = origin.map(|[x, y]| {
                let press = (x + drag.start_x, y + drag.start_y);
                TabDrag {
                    view,
                    press,
                    pointer: press,
                    target: None,
                }
            });
            false
        }
        Phase::Move => {
            let Some(mut tracked) = s.split.tab_drag.filter(|tracked| tracked.view == view) else {
                return false;
            };
            tracked.pointer = (tracked.press.0 + drag.dx, tracked.press.1 + drag.dy);
            let page = page_rect(s);
            let over_page = page.contains(tracked.pointer);
            let side = (s.split.drag_and_drop
                && over_page
                && !crate::page_prompts::chrome_hidden(s)
                && can_drop_beside_active_tab(s, view))
            .then(|| {
                let armed = tracked.target.is_some_and(|target| target.armed);
                drop_side_at(tracked.pointer.0 - page.x, page.w, armed)
            })
            .flatten();
            if tracked.target.map(|target| target.side) != side {
                tracked.target = side.map(|side| DropTarget {
                    side,
                    armed: false,
                    since: None,
                });
            }
            s.split.tab_drag = Some(tracked);
            over_page
        }
        Phase::Up => {
            let tracked = s.split.tab_drag.take();
            let armed = tracked
                .filter(|tracked| tracked.view == view && !drag.cancelled())
                .and_then(|tracked| tracked.target)
                .filter(|target| target.armed);
            if let Some(target) = armed {
                unsafe { xenon_engine_split_view_drop_tab(view, target.side.raw()) }
                return true;
            }
            false
        }
    }
}

pub fn armed_drop_side(s: &Browser) -> Option<Side> {
    s.split
        .tab_drag
        .and_then(|tracked| tracked.target)
        .filter(|target| target.armed)
        .map(|target| target.side)
}

pub fn poll(s: &mut Browser, now: f64, tx: &mut Tx) -> bool {
    let Some(target) = s
        .split
        .tab_drag
        .as_mut()
        .and_then(|tracked| tracked.target.as_mut())
        .filter(|target| !target.armed)
    else {
        return false;
    };
    let since = *target.since.get_or_insert(now);
    let remaining = DROP_DELAY - (now - since);
    if remaining > 0.0 {
        tx.wake_after(Duration::from_secs_f64(remaining));
        return false;
    }
    target.armed = true;
    true
}

pub fn wake_for_drop(s: &Browser, tx: &mut Tx) {
    if s.split
        .tab_drag
        .and_then(|tracked| tracked.target)
        .is_some_and(|target| !target.armed && target.since.is_none())
    {
        tx.wake_after(Duration::ZERO);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const AREA: Rect = Rect {
        x: 5.0,
        y: 5.0,
        w: 1012.0,
        h: 600.0,
    };

    #[test]
    fn the_split_area_is_inset_and_narrower_beside_the_sidebar() {
        assert_eq!(split_area(1022.0, 610.0, false), AREA);
        assert_eq!(
            split_area(1022.0, 610.0, true),
            Rect::new(4.0, 5.0, 1013.0, 600.0)
        );
        assert_eq!(split_area(4.0, 4.0, false).w, 0.0);
    }

    #[test]
    fn an_even_split_divides_the_area_around_the_gap() {
        let layout = layout(AREA, 0.5);
        assert_eq!(layout.left, Rect::new(5.0, 5.0, 500.0, 600.0));
        assert_eq!(layout.divider, Rect::new(505.0, 5.0, GAP, 600.0));
        assert_eq!(layout.right, Rect::new(517.0, 5.0, 500.0, 600.0));
        assert_eq!(layout.left.w + GAP + layout.right.w, AREA.w);
    }

    #[test]
    fn pages_keep_a_minimum_width() {
        let narrow = layout(AREA, 0.0);
        assert_eq!(narrow.left.w, 240.0);
        let wide = layout(AREA, 1.0);
        assert_eq!(wide.right.w, 240.0);
        let small = layout(Rect::new(0.0, 0.0, 412.0, 100.0), 0.0);
        assert_eq!(small.left.w, 100.0);
    }

    #[test]
    fn the_divider_snaps_to_the_middle() {
        let middle = AREA.x + 500.0 + GAP / 2.0;
        assert_eq!(ratio_for_divider(AREA, middle + 14.0), 0.5);
        assert_eq!(ratio_for_divider(AREA, middle - 15.0), 0.5);
        assert!(ratio_for_divider(AREA, middle + 16.0) > 0.5);
        assert_eq!(ratio_for_divider(AREA, 0.0), 240.0 / 1000.0);
        assert_eq!(ratio_for_divider(AREA, 5000.0), 760.0 / 1000.0);
    }

    #[test]
    fn a_resized_split_keeps_its_divider_where_it_was_dropped() {
        let center = AREA.x + 700.0 + GAP / 2.0;
        let ratio = ratio_for_divider(AREA, center);
        assert_eq!(divider_center(AREA, ratio), center);
    }

    #[test]
    fn drops_land_on_the_edges_and_widen_once_armed() {
        assert_eq!(drop_side_at(10.0, 1000.0, false), Some(Side::Left));
        assert_eq!(drop_side_at(199.0, 1000.0, false), Some(Side::Left));
        assert_eq!(drop_side_at(500.0, 1000.0, false), None);
        assert_eq!(drop_side_at(800.0, 1000.0, false), Some(Side::Right));
        assert_eq!(drop_side_at(450.0, 1000.0, true), Some(Side::Left));
        assert_eq!(drop_side_at(500.0, 1000.0, true), Some(Side::Right));
        assert_eq!(drop_side_at(63.0, 200.0, false), Some(Side::Left));
    }

    #[test]
    fn the_drop_indicator_covers_half_the_page() {
        assert_eq!(
            drop_indicator_rect(AREA, true, Side::Left),
            Rect::new(5.0, 5.0, 500.0, 600.0)
        );
        assert_eq!(
            drop_indicator_rect(AREA, false, Side::Right),
            Rect::new(517.0, 0.0, 500.0, 605.0)
        );
    }

    #[test]
    fn ratios_survive_the_engine_reordering_a_split() {
        let mut splits = SplitViews::default();
        splits.replace(&[(1, 2)]);
        assert!(splits.set_ratio(2, 0.3));
        splits.replace(&[(2, 1), (3, 4)]);
        assert!((splits.split_of(1).unwrap().ratio - 0.7).abs() < 1e-6);
        assert_eq!(splits.split_of(4).unwrap().ratio, 0.5);
        assert_eq!(splits.partner(1), Some(2));
        assert_eq!(splits.side(1), Some(Side::Right));
        splits.replace(&[]);
        assert!(splits.split_of(1).is_none());
    }

    #[test]
    fn only_the_active_tabs_split_is_shown() {
        let mut tabs = TabList::default();
        for view in [1, 2, 3] {
            tabs.insert(
                crate::tab_list::Tab::new(view, "about:blank", rune::Pixels::new()),
                view as usize,
            );
        }
        let mut splits = SplitViews::default();
        splits.replace(&[(1, 2)]);
        tabs.activate(3);
        assert!(splits.shown(&tabs).is_none());
        assert!(!splits.is_shown(&tabs, 1));
        tabs.activate(2);
        assert_eq!(splits.shown(&tabs).map(|split| split.left), Some(1));
        assert!(splits.is_shown(&tabs, 1) && !splits.is_shown(&tabs, 3));
    }

    fn menu_titles(s: &Browser, view: ViewId) -> Vec<String> {
        tab_menu_items(s, view, PopupMenu::new())
            .entries()
            .iter()
            .filter_map(|entry| match entry {
                crate::page_prompts::MenuEntry::Action { title, .. } => Some(title.clone()),
                _ => None,
            })
            .collect()
    }

    #[test]
    fn the_tab_menu_offers_the_split_view_items_the_qt_menu_shows() {
        let mut s = Browser::new(Default::default());
        for view in [1, 2, 3] {
            s.tabs.insert(
                crate::tab_list::Tab::new(view, "about:blank", rune::Pixels::new()),
                view as usize,
            );
        }
        s.tabs.activate(1);
        assert_eq!(menu_titles(&s, 1), ["Add Tab to New Split View"]);
        assert_eq!(menu_titles(&s, 2), ["New Split View with Current Tab"]);
        s.split.replace(&[(1, 2)]);
        assert_eq!(menu_titles(&s, 1), ["Reverse Views", "Separate Views"]);
        assert_eq!(menu_titles(&s, 2), ["Reverse Views", "Separate Views"]);
        assert!(menu_titles(&s, 3).is_empty());
    }

    #[test]
    fn hosts_drop_the_scheme_port_and_path() {
        assert_eq!(host_of("https://example.com/a?b"), Some("example.com"));
        assert_eq!(host_of("http://user@host:8080/"), Some("host"));
        assert_eq!(host_of("http://[::1]:80/"), Some("[::1]"));
        assert_eq!(host_of("about:newtab"), None);
        assert_eq!(host_of("file:///tmp/x"), None);
    }
}
