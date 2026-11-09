/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::{HashMap, HashSet};

use crate::ffi::{XenonViewId, XenonWindowId};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ModelEvent {
    WindowOpened(XenonWindowId, bool),
    WindowClosed(XenonWindowId),
    WindowActivated(XenonWindowId),
    TabInserted {
        window: XenonWindowId,
        view: XenonViewId,
        index: usize,
        opener: Option<XenonViewId>,
        pinned: bool,
        url: String,
    },
    TabRemoved(XenonWindowId, XenonViewId),
    TabMoved {
        view: XenonViewId,
        from: XenonWindowId,
        to: XenonWindowId,
        index: usize,
    },
    TabActivated(XenonWindowId, XenonViewId),
    PinnedChanged(XenonViewId, bool),
    ClosedTabsChanged(bool),
}

struct MockWindow {
    id: XenonWindowId,
    private: bool,
    tabs: Vec<XenonViewId>,
    active: Option<XenonViewId>,
    recent: Vec<XenonViewId>,
    cycle: Option<usize>,
}

struct ClosedTab {
    url: String,
    window: XenonWindowId,
    index: usize,
    pinned: bool,
}

#[derive(Default)]
pub struct MockModel {
    windows: Vec<MockWindow>,
    pinned: HashSet<XenonViewId>,
    openers: HashMap<XenonViewId, XenonViewId>,
    closed: Vec<ClosedTab>,
    activation: Vec<XenonWindowId>,
    events: Vec<ModelEvent>,
}

impl MockModel {
    pub fn take_events(&mut self) -> Vec<ModelEvent> {
        std::mem::take(&mut self.events)
    }

    pub fn active_window(&self) -> Option<XenonWindowId> {
        self.activation.last().copied()
    }

    pub fn has_window(&self, window: XenonWindowId) -> bool {
        self.windows.iter().any(|w| w.id == window)
    }

    pub fn window_of(&self, view: XenonViewId) -> Option<XenonWindowId> {
        self.windows
            .iter()
            .find(|w| w.tabs.contains(&view))
            .map(|w| w.id)
    }

    #[cfg(test)]
    pub fn tabs(&self, window: XenonWindowId) -> Vec<XenonViewId> {
        self.window(window)
            .map(|w| w.tabs.clone())
            .unwrap_or_default()
    }

    #[cfg(test)]
    pub fn active_tab(&self, window: XenonWindowId) -> Option<XenonViewId> {
        self.window(window).and_then(|w| w.active)
    }

    fn window(&self, window: XenonWindowId) -> Option<&MockWindow> {
        self.windows.iter().find(|w| w.id == window)
    }

    fn window_mut(&mut self, window: XenonWindowId) -> Option<&mut MockWindow> {
        self.windows.iter_mut().find(|w| w.id == window)
    }

    fn pinned_count(&self, window: XenonWindowId) -> usize {
        self.window(window).map_or(0, |w| {
            w.tabs
                .iter()
                .take_while(|v| self.pinned.contains(v))
                .count()
        })
    }

    pub fn open_window(&mut self, window: XenonWindowId, private: bool) {
        self.windows.push(MockWindow {
            id: window,
            private,
            tabs: Vec::new(),
            active: None,
            recent: Vec::new(),
            cycle: None,
        });
        self.events.push(ModelEvent::WindowOpened(window, private));
        self.activate_window(window);
    }

    pub fn activate_window(&mut self, window: XenonWindowId) {
        if !self.has_window(window) {
            return;
        }
        self.activation.retain(|id| *id != window);
        self.activation.push(window);
        self.events.push(ModelEvent::WindowActivated(window));
    }

    pub fn open_tab(
        &mut self,
        window: XenonWindowId,
        view: XenonViewId,
        url: &str,
        index: Option<usize>,
        opener: Option<XenonViewId>,
        activate: bool,
    ) {
        let pinned_count = self.pinned_count(window);
        let Some(target) = self.window(window) else {
            return;
        };
        let after_opener = opener
            .and_then(|opener| target.tabs.iter().position(|v| *v == opener))
            .map(|position| {
                let mut index = position + 1;
                while index < target.tabs.len()
                    && self.openers.get(&target.tabs[index]) == opener.as_ref()
                {
                    index += 1;
                }
                index
            });
        let index = index
            .or(after_opener)
            .unwrap_or(target.tabs.len())
            .clamp(pinned_count, target.tabs.len());
        if let Some(opener) = opener {
            self.openers.insert(view, opener);
        }
        let target = self.window_mut(window).expect("the window exists");
        target.tabs.insert(index, view);
        target.recent.push(view);
        let activates = activate || target.active.is_none();
        self.events.push(ModelEvent::TabInserted {
            window,
            view,
            index,
            opener,
            pinned: false,
            url: url.to_owned(),
        });
        if activates {
            self.activate_tab(view);
        }
    }

    pub fn activate_tab(&mut self, view: XenonViewId) {
        let Some(window) = self.window_of(view) else {
            return;
        };
        let target = self.window_mut(window).expect("the window exists");
        if target.active == Some(view) {
            return;
        }
        target.active = Some(view);
        if target.cycle.is_none() {
            target.recent.retain(|v| *v != view);
            target.recent.insert(0, view);
        }
        self.events.push(ModelEvent::TabActivated(window, view));
        if self.active_window() != Some(window) {
            self.activate_window(window);
        }
    }

    pub fn close_tab(&mut self, view: XenonViewId, url: &str) {
        let Some(window) = self.window_of(view) else {
            return;
        };
        let target = self.window(window).expect("the window exists");
        let index = target.tabs.iter().position(|v| *v == view).unwrap_or(0);
        if target.active == Some(view) {
            let successor = target
                .tabs
                .get(index + 1)
                .or_else(|| index.checked_sub(1).and_then(|i| target.tabs.get(i)))
                .copied();
            if let Some(successor) = successor {
                self.activate_tab(successor);
            }
        }
        if !url.is_empty() && url != "about:newtab" && url != "about:blank" {
            self.closed.push(ClosedTab {
                url: url.to_owned(),
                window,
                index,
                pinned: self.pinned.contains(&view),
            });
        }
        let target = self.window_mut(window).expect("the window exists");
        target.tabs.retain(|v| *v != view);
        target.recent.retain(|v| *v != view);
        if target.active == Some(view) {
            target.active = None;
        }
        let emptied = target.tabs.is_empty();
        self.pinned.remove(&view);
        self.openers.remove(&view);
        self.events.push(ModelEvent::TabRemoved(window, view));
        if emptied {
            self.close_window(window);
        }
        self.events
            .push(ModelEvent::ClosedTabsChanged(!self.closed.is_empty()));
    }

    pub fn close_window(&mut self, window: XenonWindowId) -> Vec<XenonViewId> {
        let Some(position) = self.windows.iter().position(|w| w.id == window) else {
            return Vec::new();
        };
        let closed = self.windows.remove(position);
        let was_active = self.active_window() == Some(window);
        self.activation.retain(|id| *id != window);
        self.events.push(ModelEvent::WindowClosed(window));
        if was_active && let Some(next) = self.active_window() {
            self.activate_window(next);
        }
        closed.tabs
    }

    pub fn move_tab(&mut self, view: XenonViewId, to: XenonWindowId, index: usize) {
        let Some(from) = self.window_of(view) else {
            return;
        };
        let pinned = self.pinned.contains(&view);
        if from == to {
            let pinned_count = self.pinned_count(to);
            let target = self.window_mut(to).expect("the window exists");
            let last = target.tabs.len() - 1;
            let index = if pinned {
                index.min(pinned_count.saturating_sub(1))
            } else {
                index.clamp(pinned_count, last)
            };
            target.tabs.retain(|v| *v != view);
            target.tabs.insert(index, view);
            self.events.push(ModelEvent::TabMoved {
                view,
                from,
                to,
                index,
            });
            return;
        }
        if self.window(from).map(|w| w.private) != self.window(to).map(|w| w.private) {
            return;
        }
        let source = self.window(from).expect("the window exists");
        if source.active == Some(view)
            && let Some(successor) = source.tabs.iter().find(|v| **v != view).copied()
        {
            self.activate_tab(successor);
        }
        let source = self.window_mut(from).expect("the window exists");
        source.tabs.retain(|v| *v != view);
        source.recent.retain(|v| *v != view);
        if source.active == Some(view) {
            source.active = None;
        }
        let emptied = source.tabs.is_empty();
        let pinned_count = self.pinned_count(to);
        let target = self.window_mut(to).expect("the window exists");
        let index = if pinned {
            index.min(pinned_count)
        } else {
            index.clamp(pinned_count, target.tabs.len())
        };
        target.tabs.insert(index, view);
        target.recent.push(view);
        self.events.push(ModelEvent::TabMoved {
            view,
            from,
            to,
            index,
        });
        self.activate_tab(view);
        if emptied {
            self.close_window(from);
        }
    }

    pub fn set_pinned(&mut self, view: XenonViewId, pinned: bool) {
        let Some(window) = self.window_of(view) else {
            return;
        };
        if self.pinned.contains(&view) == pinned {
            return;
        }
        let pinned_count = self.pinned_count(window);
        let destination = if pinned {
            pinned_count
        } else {
            pinned_count - 1
        };
        if pinned {
            self.pinned.insert(view);
        } else {
            self.pinned.remove(&view);
        }
        let target = self.window_mut(window).expect("the window exists");
        let index = target.tabs.iter().position(|v| *v == view).unwrap_or(0);
        if index != destination {
            target.tabs.retain(|v| *v != view);
            target.tabs.insert(destination, view);
            self.events.push(ModelEvent::TabMoved {
                view,
                from: window,
                to: window,
                index: destination,
            });
        }
        self.events.push(ModelEvent::PinnedChanged(view, pinned));
    }

    pub fn reopen_closed_tab(
        &mut self,
        window: Option<XenonWindowId>,
        allocate_view: impl FnOnce() -> XenonViewId,
        allocate_window: impl FnOnce() -> XenonWindowId,
    ) -> Option<(XenonViewId, String)> {
        let closed = self.closed.pop()?;
        let view = allocate_view();
        let (target, index) = if self.has_window(closed.window) {
            (closed.window, Some(closed.index))
        } else if let Some(window) = window.filter(|w| self.has_window(*w)) {
            (window, None)
        } else {
            let window = allocate_window();
            self.open_window(window, false);
            (window, None)
        };
        self.open_tab(target, view, &closed.url, index, None, true);
        if closed.pinned {
            self.set_pinned(view, true);
        }
        self.events
            .push(ModelEvent::ClosedTabsChanged(!self.closed.is_empty()));
        Some((view, closed.url))
    }

    pub fn cycle_recent(&mut self, window: XenonWindowId, toward_less_recent: bool) {
        let Some(target) = self.window_mut(window) else {
            return;
        };
        let count = target.recent.len();
        if count < 2 {
            return;
        }
        let position = target.cycle.unwrap_or(0);
        let next = if toward_less_recent {
            (position + 1) % count
        } else {
            (position + count - 1) % count
        };
        target.cycle = Some(next);
        let view = target.recent[next];
        self.activate_tab(view);
    }

    pub fn end_recent_cycle(&mut self, window: XenonWindowId) {
        let Some(target) = self.window_mut(window) else {
            return;
        };
        if target.cycle.take().is_some()
            && let Some(active) = target.active
        {
            target.recent.retain(|v| *v != active);
            target.recent.insert(0, active);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn model_with_tabs(views: &[XenonViewId]) -> MockModel {
        let mut model = MockModel::default();
        model.open_window(1, false);
        for &view in views {
            model.open_tab(
                1,
                view,
                &format!("https://{view}.example/"),
                None,
                None,
                false,
            );
        }
        model.take_events();
        model
    }

    #[test]
    fn closing_the_active_tab_activates_the_right_neighbour_first() {
        let mut model = model_with_tabs(&[10, 11, 12]);
        model.activate_tab(11);
        model.take_events();
        model.close_tab(11, "https://11.example/");
        assert_eq!(
            model.take_events()[..2],
            [
                ModelEvent::TabActivated(1, 12),
                ModelEvent::TabRemoved(1, 11)
            ]
        );
    }

    #[test]
    fn a_closed_tab_reopens_at_its_place() {
        let mut model = model_with_tabs(&[10, 11, 12]);
        model.close_tab(11, "https://11.example/");
        let reopened = model.reopen_closed_tab(Some(1), || 20, || 2);
        assert_eq!(reopened, Some((20, "https://11.example/".to_owned())));
        assert_eq!(model.tabs(1), [10, 20, 12]);
        assert_eq!(model.active_tab(1), Some(20));
        assert_eq!(model.reopen_closed_tab(Some(1), || 21, || 2), None);
    }

    #[test]
    fn pinning_moves_the_tab_into_the_pinned_group() {
        let mut model = model_with_tabs(&[10, 11, 12]);
        model.set_pinned(12, true);
        assert_eq!(model.tabs(1), [12, 10, 11]);
        model.open_tab(1, 13, "https://13.example/", Some(0), None, false);
        assert_eq!(model.tabs(1), [12, 13, 10, 11]);
    }

    #[test]
    fn recent_cycling_flips_between_the_last_two_tabs() {
        let mut model = model_with_tabs(&[10, 11, 12]);
        model.activate_tab(12);
        model.cycle_recent(1, true);
        assert_eq!(model.active_tab(1), Some(10));
        model.end_recent_cycle(1);
        model.cycle_recent(1, true);
        assert_eq!(model.active_tab(1), Some(12));
    }

    #[test]
    fn closing_the_last_tab_closes_the_window() {
        let mut model = model_with_tabs(&[10]);
        model.close_tab(10, "about:newtab");
        assert!(!model.has_window(1));
        assert!(model.take_events().contains(&ModelEvent::WindowClosed(1)));
    }
}
