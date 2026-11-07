/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cell::Cell;

use rune::Pixels;

use crate::engine::{ViewId, Viewport, WindowId};

pub struct Tab {
    pub view: ViewId,
    pub title: String,
    pub url: String,
    pub loading: bool,
    pub can_go_back: bool,
    pub can_go_forward: bool,
    pub page: Pixels,
    pub favicon: Option<Pixels>,
    pub cursor: u32,
    pub hovered_link: String,
    pub crashed: bool,
    pub sent_viewport: Cell<Option<Viewport>>,
    pub pinned: bool,
    pub focus_location_when_shown: bool,
    pub zoom_percent: u32,
    pub bookmarked: bool,
    pub playing_audio: bool,
    pub muted: bool,
    pub unresponsive: bool,
    pub blocking_state: u32,
    pub blocking_site: String,
    pub blocked_requests: u64,
}

impl Tab {
    pub fn new(view: ViewId, url: &str, page: Pixels) -> Tab {
        Tab {
            view,
            title: String::new(),
            url: url.to_owned(),
            loading: true,
            can_go_back: false,
            can_go_forward: false,
            page,
            favicon: None,
            cursor: crate::ffi::XENON_CURSOR_DEFAULT,
            hovered_link: String::new(),
            crashed: false,
            sent_viewport: Cell::new(None),
            pinned: false,
            focus_location_when_shown: false,
            zoom_percent: 100,
            bookmarked: false,
            playing_audio: false,
            muted: false,
            unresponsive: false,
            blocking_state: 0,
            blocking_site: String::new(),
            blocked_requests: 0,
        }
    }

    pub fn from_insertion(insertion: &TabInsertion, page: Pixels) -> Tab {
        Tab {
            title: insertion.title.clone(),
            loading: false,
            pinned: insertion.pinned,
            focus_location_when_shown: insertion.is_new_tab_page,
            ..Tab::new(insertion.view, &insertion.url, page)
        }
    }

    pub fn display_title(&self) -> &str {
        if !self.title.is_empty() {
            &self.title
        } else if !self.url.is_empty() {
            &self.url
        } else {
            "New Tab"
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TabInsertion {
    pub window: WindowId,
    pub view: ViewId,
    pub index: usize,
    pub pinned: bool,
    pub private: bool,
    pub is_new_tab_page: bool,
    pub url: String,
    pub title: String,
}

#[derive(Debug, PartialEq, Eq)]
pub struct Activation {
    pub previous: Option<ViewId>,
    pub current: ViewId,
}

#[derive(Default)]
pub struct TabList {
    tabs: Vec<Tab>,
    active: Option<usize>,
}

impl TabList {
    pub fn iter(&self) -> impl Iterator<Item = &Tab> {
        self.tabs.iter()
    }

    pub fn active(&self) -> Option<&Tab> {
        self.active.map(|i| &self.tabs[i])
    }

    pub fn active_view(&self) -> Option<ViewId> {
        self.active().map(|tab| tab.view)
    }

    pub fn is_active(&self, view: ViewId) -> bool {
        self.active_view() == Some(view)
    }

    pub fn get(&self, view: ViewId) -> Option<&Tab> {
        self.tabs.iter().find(|tab| tab.view == view)
    }

    pub fn get_mut(&mut self, view: ViewId) -> Option<&mut Tab> {
        self.tabs.iter_mut().find(|tab| tab.view == view)
    }

    pub fn position(&self, view: ViewId) -> Option<usize> {
        self.tabs.iter().position(|tab| tab.view == view)
    }

    pub fn insert(&mut self, tab: Tab, index: usize) {
        if self.get(tab.view).is_some() {
            return;
        }
        let index = index.min(self.tabs.len());
        self.tabs.insert(index, tab);
        if let Some(active) = self.active
            && active >= index
        {
            self.active = Some(active + 1);
        }
    }

    pub fn remove(&mut self, view: ViewId) -> Option<Tab> {
        let index = self.position(view)?;
        let tab = self.tabs.remove(index);
        self.active = match self.active {
            Some(active) if active == index => None,
            Some(active) if active > index => Some(active - 1),
            active => active,
        };
        Some(tab)
    }

    pub fn move_to(&mut self, view: ViewId, index: usize) -> bool {
        let Some(from) = self.position(view) else {
            return false;
        };
        let active = self.active_view();
        let tab = self.tabs.remove(from);
        self.tabs.insert(index.min(self.tabs.len()), tab);
        self.active = active.and_then(|view| self.position(view));
        true
    }

    pub fn activate(&mut self, view: ViewId) -> Option<Activation> {
        let index = self.position(view)?;
        let previous = self.active_view();
        if previous == Some(view) {
            return None;
        }
        self.active = Some(index);
        Some(Activation {
            previous,
            current: view,
        })
    }

    pub fn view_at(&self, index: usize) -> Option<ViewId> {
        self.tabs.get(index).map(|tab| tab.view)
    }

    pub fn last_view(&self) -> Option<ViewId> {
        self.tabs.last().map(|tab| tab.view)
    }

    pub fn relative_view(&self, offset: isize) -> Option<ViewId> {
        let count = self.tabs.len() as isize;
        let active = self.active? as isize;
        self.view_at((active + offset).rem_euclid(count) as usize)
    }

    fn drain(&mut self) -> Vec<Tab> {
        self.active = None;
        std::mem::take(&mut self.tabs)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WindowInfo {
    pub id: WindowId,
    pub private: bool,
}

struct HiddenWindow {
    info: WindowInfo,
    tabs: TabList,
}

#[derive(Default)]
pub struct Windows {
    shown: Option<WindowInfo>,
    hidden: Vec<HiddenWindow>,
    activation_order: Vec<WindowId>,
    pub can_reopen_closed_tab: bool,
    slotted: bool,
    bound: Option<WindowId>,
}

impl Windows {
    pub fn slotted() -> Windows {
        Windows {
            slotted: true,
            ..Windows::default()
        }
    }

    pub fn bind(&mut self, window: WindowId, shown_tabs: &mut TabList) -> bool {
        self.slotted = true;
        self.bound = Some(window);
        self.show(window, shown_tabs)
    }

    pub fn shown_is_active(&self) -> bool {
        self.shown_id().is_some() && self.activation_order.last().copied() == self.shown_id()
    }

    #[cfg(test)]
    pub fn ids(&self) -> impl Iterator<Item = WindowId> + '_ {
        self.shown
            .iter()
            .map(|info| info.id)
            .chain(self.hidden.iter().map(|hidden| hidden.info.id))
    }

    pub fn shown_is_private(&self) -> bool {
        self.shown.as_ref().is_some_and(|window| window.private)
    }

    pub fn shown_id(&self) -> Option<WindowId> {
        self.shown.map(|info| info.id)
    }

    pub fn is_empty(&self) -> bool {
        self.shown.is_none() && self.hidden.is_empty()
    }

    pub fn least_recently_active_hidden(&self) -> Option<WindowId> {
        self.hidden
            .iter()
            .map(|hidden| hidden.info.id)
            .min_by_key(|id| self.activation_order.iter().position(|other| other == id))
    }

    pub fn contains(&self, window: WindowId) -> bool {
        self.shown_id() == Some(window) || self.hidden.iter().any(|w| w.info.id == window)
    }

    pub fn opened(&mut self, info: WindowInfo) {
        if self.contains(info.id) {
            return;
        }
        let adopt = if self.slotted {
            self.bound == Some(info.id)
        } else {
            self.shown.is_none()
        };
        if adopt && self.shown.is_none() {
            self.shown = Some(info);
        } else {
            self.hidden.push(HiddenWindow {
                info,
                tabs: TabList::default(),
            });
        }
    }

    pub fn tabs_mut<'a>(
        &'a mut self,
        window: WindowId,
        shown_tabs: &'a mut TabList,
    ) -> Option<&'a mut TabList> {
        if self.shown_id() == Some(window) {
            return Some(shown_tabs);
        }
        self.hidden
            .iter_mut()
            .find(|hidden| hidden.info.id == window)
            .map(|hidden| &mut hidden.tabs)
    }

    pub fn window_of(&self, view: ViewId, shown_tabs: &TabList) -> Option<WindowId> {
        if shown_tabs.get(view).is_some() {
            return self.shown_id();
        }
        self.hidden
            .iter()
            .find(|hidden| hidden.tabs.get(view).is_some())
            .map(|hidden| hidden.info.id)
    }

    pub fn activated(&mut self, window: WindowId, shown_tabs: &mut TabList) -> bool {
        self.activation_order.retain(|id| *id != window);
        self.activation_order.push(window);
        if self.slotted {
            return false;
        }
        self.show(window, shown_tabs)
    }

    fn show(&mut self, window: WindowId, shown_tabs: &mut TabList) -> bool {
        if self.shown_id() == Some(window) {
            return false;
        }
        let Some(index) = self.hidden.iter().position(|w| w.info.id == window) else {
            return false;
        };
        let mut incoming = self.hidden.remove(index);
        std::mem::swap(&mut incoming.tabs, shown_tabs);
        if let Some(outgoing) = self.shown.replace(incoming.info) {
            self.hidden.push(HiddenWindow {
                info: outgoing,
                tabs: incoming.tabs,
            });
        }
        true
    }

    pub fn closed(&mut self, window: WindowId, shown_tabs: &mut TabList) -> Option<Vec<Tab>> {
        self.activation_order.retain(|id| *id != window);
        if self.shown_id() != Some(window) {
            let index = self.hidden.iter().position(|w| w.info.id == window)?;
            return Some(self.hidden.remove(index).tabs.drain());
        }
        let closed = shown_tabs.drain();
        self.shown = None;
        if self.slotted {
            self.bound = None;
            return Some(closed);
        }
        let next = self
            .activation_order
            .iter()
            .rev()
            .find_map(|id| self.hidden.iter().position(|w| w.info.id == *id))
            .or_else(|| self.hidden.len().checked_sub(1));
        if let Some(index) = next {
            let incoming = self.hidden.remove(index);
            *shown_tabs = incoming.tabs;
            self.shown = Some(incoming.info);
        }
        Some(closed)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tab(view: ViewId) -> Tab {
        Tab::new(view, "about:blank", Pixels::new())
    }

    fn list(views: &[ViewId]) -> TabList {
        let mut tabs = TabList::default();
        for &view in views {
            let end = tabs.iter().count();
            tabs.insert(tab(view), end);
        }
        tabs
    }

    fn order(tabs: &TabList) -> Vec<ViewId> {
        tabs.iter().map(|tab| tab.view).collect()
    }

    fn info(id: WindowId) -> WindowInfo {
        WindowInfo { id, private: false }
    }

    #[test]
    fn tabs_go_where_the_engine_puts_them() {
        let mut tabs = list(&[1, 2, 3]);
        tabs.insert(tab(9), 1);
        tabs.insert(tab(8), 42);
        assert_eq!(order(&tabs), [1, 9, 2, 3, 8]);
        tabs.insert(tab(9), 0);
        assert_eq!(order(&tabs), [1, 9, 2, 3, 8]);
    }

    #[test]
    fn inserting_before_the_active_tab_keeps_it_active() {
        let mut tabs = list(&[1, 2]);
        tabs.activate(2);
        tabs.insert(tab(5), 0);
        assert_eq!(tabs.active_view(), Some(2));
    }

    #[test]
    fn removing_the_active_tab_leaves_the_choice_to_the_engine() {
        let mut tabs = list(&[1, 2, 3]);
        tabs.activate(3);
        assert_eq!(tabs.remove(1).map(|tab| tab.view), Some(1));
        assert_eq!(tabs.active_view(), Some(3));
        tabs.remove(3);
        assert_eq!(tabs.active_view(), None);
        assert!(tabs.remove(3).is_none());
    }

    #[test]
    fn moving_a_tab_keeps_the_active_one() {
        let mut tabs = list(&[1, 2, 3]);
        tabs.activate(2);
        assert!(tabs.move_to(3, 0));
        assert_eq!(order(&tabs), [3, 1, 2]);
        assert_eq!(tabs.active_view(), Some(2));
        assert!(!tabs.move_to(7, 0));
    }

    #[test]
    fn relative_views_wrap() {
        let mut tabs = list(&[1, 2, 3]);
        tabs.activate(1);
        assert_eq!(tabs.relative_view(-1), Some(3));
        assert_eq!(tabs.relative_view(1), Some(2));
        assert_eq!(tabs.last_view(), Some(3));
        assert_eq!(tabs.view_at(7), None);
    }

    #[test]
    fn activating_the_active_tab_is_no_change() {
        let mut tabs = list(&[1, 2]);
        tabs.activate(1);
        assert_eq!(tabs.activate(1), None);
        assert_eq!(
            tabs.activate(2),
            Some(Activation {
                previous: Some(1),
                current: 2
            })
        );
    }

    #[test]
    fn insertions_keep_the_engines_title_and_flags() {
        let insertion = TabInsertion {
            window: 1,
            view: 4,
            index: 0,
            pinned: true,
            private: false,
            is_new_tab_page: false,
            url: "https://example.com/".into(),
            title: "Example".into(),
        };
        let tab = Tab::from_insertion(&insertion, Pixels::new());
        assert_eq!(tab.display_title(), "Example");
        assert!(tab.pinned && !tab.loading);
        assert_eq!(tab.url, "https://example.com/");
    }

    #[test]
    fn titles_fall_back_to_the_url() {
        let mut tab = tab(1);
        assert_eq!(tab.display_title(), "about:blank");
        tab.title = "Example".into();
        assert_eq!(tab.display_title(), "Example");
    }

    #[test]
    fn the_first_window_is_shown_and_activation_swaps_the_shown_tabs() {
        let mut windows = Windows::default();
        let mut shown = TabList::default();
        windows.opened(info(1));
        shown.insert(tab(10), 0);
        windows.opened(info(2));
        windows.tabs_mut(2, &mut shown).unwrap().insert(tab(20), 0);
        assert_eq!(windows.shown_id(), Some(1));
        assert_eq!(windows.window_of(20, &shown), Some(2));

        assert!(windows.activated(2, &mut shown));
        assert_eq!(windows.shown_id(), Some(2));
        assert_eq!(order(&shown), [20]);
        assert_eq!(order(windows.tabs_mut(1, &mut shown).unwrap()), [10]);
        assert!(!windows.activated(2, &mut shown));
        assert_eq!(windows.least_recently_active_hidden(), Some(1));
    }

    #[test]
    fn closing_the_shown_window_shows_the_last_active_one() {
        let mut windows = Windows::default();
        let mut shown = TabList::default();
        for id in [1, 2, 3] {
            windows.opened(info(id));
            windows
                .tabs_mut(id, &mut shown)
                .unwrap()
                .insert(tab(id * 10), 0);
        }
        windows.activated(2, &mut shown);
        windows.activated(3, &mut shown);
        let closed = windows.closed(3, &mut shown).unwrap();
        assert_eq!(closed.iter().map(|t| t.view).collect::<Vec<_>>(), [30]);
        assert_eq!(windows.shown_id(), Some(2));
        assert_eq!(order(&shown), [20]);

        assert!(windows.closed(1, &mut shown).is_some());
        assert!(windows.closed(2, &mut shown).is_some());
        assert!(windows.is_empty());
        assert_eq!(shown.iter().count(), 0);
        assert!(windows.closed(2, &mut shown).is_none());
    }

    #[test]
    fn a_bound_window_list_keeps_showing_its_own_window() {
        let mut windows = Windows::slotted();
        let mut shown = TabList::default();
        windows.opened(info(1));
        windows.tabs_mut(1, &mut shown).unwrap().insert(tab(10), 0);
        assert_eq!(windows.shown_id(), None);
        windows.bind(2, &mut shown);
        windows.opened(info(2));
        windows.tabs_mut(2, &mut shown).unwrap().insert(tab(20), 0);
        assert_eq!(windows.shown_id(), Some(2));
        assert_eq!(order(&shown), [20]);

        assert!(!windows.activated(1, &mut shown));
        assert_eq!(windows.shown_id(), Some(2));
        assert!(!windows.shown_is_active());
        windows.activated(2, &mut shown);
        assert!(windows.shown_is_active());

        windows.closed(2, &mut shown);
        assert_eq!(windows.shown_id(), None);
        assert!(!windows.is_empty());
        assert!(windows.bind(1, &mut shown));
        assert_eq!(order(&shown), [10]);
        assert_eq!(windows.ids().collect::<Vec<_>>(), [1]);
    }
}
