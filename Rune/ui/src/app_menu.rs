/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{MenuItem, Shortcut, Tx};

use crate::engine::{Action, ChromeLayout};
use crate::keys::Shortcut as BrowserShortcut;
use crate::state::Browser;

type Run = fn(&mut Browser, &mut Tx);

fn item(menu: &str, title: &str, chord: Option<&str>, run: Run) -> MenuItem<Browser> {
    let item = MenuItem::new(title, run).id(format!("{menu}/{title}"));
    let item = if menu.is_empty() {
        item
    } else {
        item.menu(menu)
    };
    match chord.and_then(|chord| chord.parse::<Shortcut>().ok()) {
        Some(shortcut) => item.key_equivalent(shortcut).keys_first(),
        None => item,
    }
}

fn separator(menu: &str) -> MenuItem<Browser> {
    MenuItem::separator().menu(menu)
}

/// The macOS menu bar. Each item with a key equivalent lets the window's own shortcuts (keys.rs) take the key
/// first, so a chord runs once whether it's typed or picked.
pub fn menu_bar() -> Vec<MenuItem<Browser>> {
    vec![
        item("", "Settings…", Some("cmd+,"), |s, tx| {
            s.run_shortcut(BrowserShortcut::OpenSettings, tx)
        }),
        item("File", "New Tab", Some("cmd+t"), |s, tx| {
            s.run_shortcut(BrowserShortcut::NewTab, tx)
        }),
        item("File", "New Window", Some("cmd+n"), |s, tx| {
            s.run_shortcut(BrowserShortcut::NewWindow, tx)
        }),
        item(
            "File",
            "New Private Window",
            Some("cmd+shift+n"),
            |s, tx| s.run_shortcut(BrowserShortcut::NewPrivateWindow, tx),
        ),
        item("File", "Reopen Closed Tab", Some("cmd+shift+t"), |s, tx| {
            s.run_shortcut(BrowserShortcut::ReopenClosedTab, tx)
        }),
        separator("File"),
        item("File", "Open File…", Some("cmd+o"), |s, tx| {
            s.run_shortcut(BrowserShortcut::OpenFile, tx)
        }),
        item("File", "Open Location", Some("cmd+l"), |s, tx| {
            s.run_shortcut(BrowserShortcut::FocusLocation, tx)
        }),
        separator("File"),
        item("File", "Close Tab", Some("cmd+w"), |s, tx| {
            s.run_shortcut(BrowserShortcut::CloseTab, tx)
        }),
        item("File", "Close Window", Some("cmd+shift+w"), |s, tx| {
            s.run_shortcut(BrowserShortcut::CloseWindow, tx)
        }),
        separator("Edit"),
        item("Edit", "Find…", Some("cmd+f"), |s, tx| {
            crate::page_prompts::open_find(s, tx)
        }),
        item("Edit", "Find Next", Some("cmd+g"), |s, tx| {
            s.run_shortcut(BrowserShortcut::FindNext, tx)
        }),
        item("Edit", "Find Previous", Some("cmd+shift+g"), |s, tx| {
            s.run_shortcut(BrowserShortcut::FindPrevious, tx)
        }),
        item("View", "Reload Page", Some("cmd+r"), |s, tx| {
            s.run_shortcut(BrowserShortcut::Reload, tx)
        }),
        item(
            "View",
            "Reload Ignoring Cache",
            Some("cmd+shift+r"),
            |s, tx| s.run_shortcut(BrowserShortcut::HardReload, tx),
        ),
        separator("View"),
        item("View", "Zoom In", Some("cmd+="), |s, tx| {
            s.run_shortcut(BrowserShortcut::ZoomIn, tx)
        }),
        item("View", "Zoom Out", Some("cmd+-"), |s, tx| {
            s.run_shortcut(BrowserShortcut::ZoomOut, tx)
        }),
        item("View", "Actual Size", Some("cmd+0"), |s, tx| {
            s.run_shortcut(BrowserShortcut::ResetZoom, tx)
        }),
        separator("View"),
        item("View", "Classic Layout", None, |s, _| {
            s.set_chrome_layout(ChromeLayout::Classic)
        }),
        item("View", "Compact Layout", None, |s, _| {
            s.set_chrome_layout(ChromeLayout::Compact)
        }),
        item("View", "Vertical Tabs", None, |s, _| {
            s.set_chrome_layout(ChromeLayout::Vertical)
        }),
        item(
            "View",
            "Show Bookmarks Bar",
            Some("cmd+shift+b"),
            |s, tx| s.run_shortcut(BrowserShortcut::ToggleBookmarksBar, tx),
        ),
        item("View", "Dark Pages", None, |s, _| {
            s.activate_action(Action::ForceDark)
        }),
        separator("View"),
        item("View", "Toggle Full Screen", Some("ctrl+cmd+f"), |s, tx| {
            s.run_shortcut(BrowserShortcut::ToggleFullscreen, tx)
        }),
        item("History", "Back", Some("cmd+["), |s, tx| {
            s.run_shortcut(BrowserShortcut::Back, tx)
        }),
        item("History", "Forward", Some("cmd+]"), |s, tx| {
            s.run_shortcut(BrowserShortcut::Forward, tx)
        }),
        separator("History"),
        item("History", "Show All History", Some("cmd+y"), |s, tx| {
            s.run_shortcut(BrowserShortcut::ShowHistory, tx)
        }),
        item("History", "Clear Browsing Data…", None, |s, tx| {
            s.run_shortcut(BrowserShortcut::ClearBrowsingData, tx)
        }),
        item("Bookmarks", "Bookmark This Page", Some("cmd+d"), |s, tx| {
            s.run_shortcut(BrowserShortcut::ToggleBookmark, tx)
        }),
        item(
            "Bookmarks",
            "Bookmark All Tabs…",
            Some("cmd+shift+d"),
            |s, tx| s.run_shortcut(BrowserShortcut::BookmarkAllTabs, tx),
        ),
        item("Bookmarks", "Manage Bookmarks", None, |s, _| {
            s.activate_action(Action::ManageBookmarks)
        }),
        item("Window", "Select Next Tab", Some("cmd+shift+]"), |s, tx| {
            s.run_shortcut(BrowserShortcut::NextTab, tx)
        }),
        item(
            "Window",
            "Select Previous Tab",
            Some("cmd+shift+["),
            |s, tx| s.run_shortcut(BrowserShortcut::PreviousTab, tx),
        ),
        item("Window", "Move Tab Left", None, |s, tx| {
            s.run_shortcut(BrowserShortcut::MoveTabLeft, tx)
        }),
        item("Window", "Move Tab Right", None, |s, tx| {
            s.run_shortcut(BrowserShortcut::MoveTabRight, tx)
        }),
        separator("Window"),
        item("Window", "Downloads", Some("cmd+shift+j"), |s, tx| {
            s.run_shortcut(BrowserShortcut::ShowDownloads, tx)
        }),
        item("Window", "Always on Top", None, |s, _| {
            s.toggle_always_on_top()
        }),
        item("Window", "Task Manager", Some("cmd+shift+m"), |s, tx| {
            s.run_shortcut(BrowserShortcut::OpenTaskManager, tx)
        }),
        item("Develop", "View Page Source", Some("alt+cmd+u"), |s, tx| {
            s.run_shortcut(BrowserShortcut::ViewSource, tx)
        }),
        item("Develop", "Developer Tools", Some("alt+cmd+i"), |s, tx| {
            s.run_shortcut(BrowserShortcut::ToggleDevTools, tx)
        }),
        item("Develop", "Report Website Issue…", None, |s, _| {
            s.activate_action(Action::ReportWebsiteIssue)
        }),
        item("Help", "About Xenon", None, |s, _| {
            s.activate_action(Action::OpenAbout)
        }),
    ]
}
