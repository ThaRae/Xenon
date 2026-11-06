/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cell::Cell;

use rune::Pixels;

use crate::engine::{ViewId, Viewport};

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

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Placement {
    End,
    After(ViewId),
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
    pub fn is_empty(&self) -> bool {
        self.tabs.is_empty()
    }

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

    fn position(&self, view: ViewId) -> Option<usize> {
        self.tabs.iter().position(|tab| tab.view == view)
    }

    pub fn insert(&mut self, tab: Tab, placement: Placement, activate: bool) -> Option<Activation> {
        let view = tab.view;
        let index = match placement {
            Placement::After(opener) => self.position(opener).map_or(self.tabs.len(), |i| i + 1),
            Placement::End => self.tabs.len(),
        };
        self.tabs.insert(index, tab);
        match self.active {
            Some(active) if active >= index => self.active = Some(active + 1),
            None => return self.activate(view),
            _ => {}
        }
        if activate { self.activate(view) } else { None }
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

    pub fn activate_index(&mut self, index: usize) -> Option<Activation> {
        let view = self.tabs.get(index)?.view;
        self.activate(view)
    }

    pub fn activate_last(&mut self) -> Option<Activation> {
        self.activate_index(self.tabs.len().checked_sub(1)?)
    }

    pub fn activate_relative(&mut self, offset: isize) -> Option<Activation> {
        let count = self.tabs.len() as isize;
        let active = self.active? as isize;
        self.activate_index((active + offset).rem_euclid(count) as usize)
    }

    pub fn remove(&mut self, view: ViewId) -> Option<(Tab, Option<Activation>)> {
        let index = self.position(view)?;
        let tab = self.tabs.remove(index);
        let Some(active) = self.active else {
            return Some((tab, None));
        };
        if self.tabs.is_empty() {
            self.active = None;
            return Some((tab, None));
        }
        if active > index {
            self.active = Some(active - 1);
            return Some((tab, None));
        }
        if active < index {
            return Some((tab, None));
        }
        let successor = index.min(self.tabs.len() - 1);
        self.active = Some(successor);
        Some((
            tab,
            Some(Activation {
                previous: None,
                current: self.tabs[successor].view,
            }),
        ))
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
            tabs.insert(tab(view), Placement::End, false);
        }
        tabs
    }

    fn order(tabs: &TabList) -> Vec<ViewId> {
        tabs.iter().map(|tab| tab.view).collect()
    }

    #[test]
    fn the_first_tab_becomes_active() {
        let tabs = list(&[1, 2, 3]);
        assert_eq!(tabs.active_view(), Some(1));
        assert_eq!(order(&tabs), [1, 2, 3]);
    }

    #[test]
    fn opened_tabs_go_right_of_their_opener() {
        let mut tabs = list(&[1, 2, 3]);
        tabs.activate(2);
        assert_eq!(tabs.insert(tab(9), Placement::After(1), false), None);
        assert_eq!(order(&tabs), [1, 9, 2, 3]);
        assert_eq!(tabs.active_view(), Some(2));
        assert_eq!(
            tabs.insert(tab(8), Placement::After(3), true),
            Some(Activation {
                previous: Some(2),
                current: 8
            })
        );
        assert_eq!(order(&tabs), [1, 9, 2, 3, 8]);
    }

    #[test]
    fn an_unknown_opener_appends() {
        let mut tabs = list(&[1, 2]);
        tabs.insert(tab(5), Placement::After(42), false);
        assert_eq!(order(&tabs), [1, 2, 5]);
    }

    #[test]
    fn closing_the_active_tab_activates_its_right_neighbour() {
        let mut tabs = list(&[1, 2, 3]);
        tabs.activate(2);
        let (closed, activation) = tabs.remove(2).unwrap();
        assert_eq!(closed.view, 2);
        assert_eq!(activation.map(|a| a.current), Some(3));
        assert_eq!(tabs.active_view(), Some(3));
    }

    #[test]
    fn closing_the_last_active_tab_activates_its_left_neighbour() {
        let mut tabs = list(&[1, 2, 3]);
        tabs.activate(3);
        let (_, activation) = tabs.remove(3).unwrap();
        assert_eq!(activation.map(|a| a.current), Some(2));
    }

    #[test]
    fn closing_a_tab_left_of_the_active_one_keeps_it_active() {
        let mut tabs = list(&[1, 2, 3]);
        tabs.activate(3);
        let (_, activation) = tabs.remove(1).unwrap();
        assert_eq!(activation, None);
        assert_eq!(tabs.active_view(), Some(3));
        let (_, activation) = tabs.remove(2).unwrap();
        assert_eq!(activation, None);
        assert_eq!(tabs.active_view(), Some(3));
    }

    #[test]
    fn closing_every_tab_leaves_none_active() {
        let mut tabs = list(&[1]);
        let (_, activation) = tabs.remove(1).unwrap();
        assert_eq!(activation, None);
        assert!(tabs.is_empty());
        assert_eq!(tabs.active_view(), None);
        assert!(tabs.remove(1).is_none());
    }

    #[test]
    fn relative_activation_wraps() {
        let mut tabs = list(&[1, 2, 3]);
        assert_eq!(tabs.activate_relative(-1).map(|a| a.current), Some(3));
        assert_eq!(tabs.activate_relative(1).map(|a| a.current), Some(1));
        assert_eq!(tabs.activate_last().map(|a| a.current), Some(3));
        assert_eq!(tabs.activate_index(7), None);
    }

    #[test]
    fn activating_the_active_tab_is_no_change() {
        let mut tabs = list(&[1, 2]);
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
    fn titles_fall_back_to_the_url() {
        let mut tab = tab(1);
        assert_eq!(tab.display_title(), "about:blank");
        tab.title = "Example".into();
        assert_eq!(tab.display_title(), "Example");
    }
}
