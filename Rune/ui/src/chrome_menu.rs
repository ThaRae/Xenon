/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{PointerPress, PopupItem, PopupMenu, Tx};

use crate::engine::{ChromeLayout, ViewId};
use crate::state::Browser;

const LAYOUTS: [(ChromeLayout, &str, &str); 3] = [
    (ChromeLayout::Classic, "layout-classic", "Classic"),
    (ChromeLayout::Compact, "layout-compact", "Compact"),
    (ChromeLayout::Vertical, "layout-vertical", "Vertical Tabs"),
];

pub fn show_window_menu(s: &Browser, at: PointerPress, tx: &mut Tx) {
    let mut layouts = PopupMenu::new();
    for (layout, id, title) in LAYOUTS {
        layouts = layouts.item(PopupItem::new(id, title).checked(s.chrome.layout == layout));
    }
    let menu = PopupMenu::new()
        .item(PopupItem::new("new-tab", "New Tab"))
        .item(
            PopupItem::new("reopen-closed-tab", "Reopen Closed Tab")
                .enabled(s.windows.can_reopen_closed_tab),
        )
        .separator()
        .item(
            PopupItem::new("always-on-top", "Always on Top")
                .checked(crate::platform::is_always_on_top()),
        )
        .separator()
        .submenu("Layout", layouts)
        .item(
            PopupItem::new("cycle-recently-used", "Cycle Tabs in Recently Used Order")
                .checked(s.chrome.cycle_tabs_in_recently_used_order),
        );
    tx.popup_menu(
        menu,
        at,
        |s: &mut Browser, picked: Option<&str>, tx: &mut Tx| {
            if let Some(picked) = picked {
                run_window_menu_item(s, picked, tx);
            }
        },
    );
}

fn run_window_menu_item(s: &mut Browser, picked: &str, tx: &mut Tx) {
    match picked {
        "new-tab" => s.new_tab(tx),
        "reopen-closed-tab" => s.reopen_closed_tab(),
        "cycle-recently-used" => s.toggle_cycle_tabs_in_recently_used_order(),
        "always-on-top" => crate::platform::set_always_on_top(!crate::platform::is_always_on_top()),
        _ => {
            if let Some((layout, _, _)) = LAYOUTS.iter().find(|(_, id, _)| *id == picked) {
                s.set_chrome_layout(*layout);
            }
        }
    }
}

struct TabNeighbours {
    left: Vec<ViewId>,
    right: Vec<ViewId>,
}

fn unpinned_neighbours(s: &Browser, view: ViewId) -> TabNeighbours {
    let mut neighbours = TabNeighbours {
        left: Vec::new(),
        right: Vec::new(),
    };
    let mut seen = false;
    for tab in s.tabs.iter() {
        if tab.view == view {
            seen = true;
        } else if !tab.pinned {
            if seen {
                neighbours.right.push(tab.view);
            } else {
                neighbours.left.push(tab.view);
            }
        }
    }
    neighbours
}

pub fn show_tab_menu(s: &Browser, view: ViewId, at: PointerPress, tx: &mut Tx) {
    let Some(tab) = s.tabs.get(view) else {
        return;
    };
    let pinned = tab.pinned;
    let neighbours = unpinned_neighbours(s, view);
    let position = s.tabs.position(view);
    let section_end = s.tabs.iter().filter(|tab| tab.pinned == pinned).count();
    let section_start = if pinned {
        0
    } else {
        s.tabs.iter().filter(|tab| tab.pinned).count()
    };
    let move_menu = PopupMenu::new()
        .item(
            PopupItem::new("move-start", "Move to Start").enabled(position != Some(section_start)),
        )
        .item(
            PopupItem::new("move-end", "Move to End")
                .enabled(position.is_some_and(|index| index + 1 != section_start + section_end)),
        );
    let close_menu = PopupMenu::new()
        .item(
            PopupItem::new("close-left", "Close Tabs to Left").enabled(!neighbours.left.is_empty()),
        )
        .item(
            PopupItem::new("close-right", "Close Tabs to Right")
                .enabled(!neighbours.right.is_empty()),
        )
        .item(
            PopupItem::new("close-others", "Close Other Tabs")
                .enabled(!neighbours.left.is_empty() || !neighbours.right.is_empty()),
        );
    let menu = PopupMenu::new()
        .item(PopupItem::new("reload", "Reload"))
        .item(PopupItem::new("duplicate", "Duplicate Tab"))
        .item(PopupItem::new(
            "pin",
            if pinned { "Unpin Tab" } else { "Pin Tab" },
        ));
    let menu = crate::tab_groups::with_tab_menu_items(s, view, menu)
        .separator()
        .submenu("Move Tab", move_menu)
        .separator()
        .item(PopupItem::new("close", "Close Tab"))
        .submenu("Close Multiple Tabs", close_menu);
    tx.popup_menu(
        menu,
        at,
        move |s: &mut Browser, picked: Option<&str>, tx: &mut Tx| {
            if let Some(picked) = picked {
                run_tab_menu_item(s, view, picked, tx);
            }
        },
    );
}

fn run_tab_menu_item(s: &mut Browser, view: ViewId, picked: &str, tx: &mut Tx) {
    if s.tabs.get(view).is_none() {
        return;
    }
    match picked {
        "reload" => s.reload_tab(view),
        "duplicate" => s.duplicate_tab(view),
        "pin" => s.toggle_tab_pinned(view),
        "move-start" => s.move_tab(view, 0),
        "move-end" => s.move_tab_to_end(view),
        "close" => s.close_tab(view, tx),
        "close-left" => {
            let views = unpinned_neighbours(s, view).left;
            s.close_tabs(view, views);
        }
        "close-right" => {
            let views = unpinned_neighbours(s, view).right;
            s.close_tabs(view, views);
        }
        "close-others" => {
            let TabNeighbours { mut left, right } = unpinned_neighbours(s, view);
            left.extend(right);
            s.close_tabs(view, left);
        }
        _ => {
            crate::tab_groups::run_tab_menu_item(view, picked);
        }
    }
}
