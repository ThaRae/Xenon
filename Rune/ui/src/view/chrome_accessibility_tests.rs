/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Arc;

use rune::{Env, Pixels, Runtime};
use serde_json::Value;

use crate::bookmarks::{BarVisibility, BookmarkNode};
use crate::engine::ChromeLayout;
use crate::state::Browser;
use crate::tab_list::Tab;
use crate::view::window::browser_view;

const ACTIVE_VIEW: u64 = 7001;
const BACKGROUND_VIEW: u64 = 7002;

fn browser(layout: ChromeLayout) -> Browser {
    let mut browser = Browser::new(Arc::default());
    browser.chrome.layout = layout;
    browser.vertical_tabs_expanded = true;
    let mut active = Tab::new(ACTIVE_VIEW, "https://example.com/", Pixels::new());
    active.title = "Example".to_owned();
    active.loading = false;
    let mut background = Tab::new(BACKGROUND_VIEW, "https://example.org/", Pixels::new());
    background.title = "Background".to_owned();
    background.loading = false;
    browser.tabs.insert(active, 0);
    browser.tabs.insert(background, 1);
    browser.tabs.activate(ACTIVE_VIEW);
    browser.bookmarks.visibility = BarVisibility::Always;
    browser.bookmarks.items = vec![
        BookmarkNode::Bookmark {
            id: "b1".to_owned(),
            title: String::new(),
            url: "https://ladybird.org/".to_owned(),
            favicon: None,
        },
        BookmarkNode::Folder {
            id: "f1".to_owned(),
            title: "Work".to_owned(),
            children: Vec::new(),
        },
    ];
    browser
}

fn tree(layout: ChromeLayout) -> Value {
    let mut rt = Runtime::new(
        browser(layout),
        browser_view,
        (1300.0, 600.0),
        Env::new(false, 1.0),
    );
    rt.frame(0.0);
    rt.frame(0.0);
    rt.accessibility_json()
}

fn walk<'a>(node: &'a Value, out: &mut Vec<&'a Value>) {
    out.push(node);
    for child in node["children"].as_array().into_iter().flatten() {
        walk(child, out);
    }
}

fn nodes(tree: &Value) -> Vec<&Value> {
    let mut out = Vec::new();
    walk(tree, &mut out);
    out
}

fn has(node: &Value, field: &str, value: &str) -> bool {
    node[field]
        .as_array()
        .is_some_and(|values| values.iter().any(|v| v == value))
}

fn named(node: &Value) -> bool {
    node["name"]
        .as_str()
        .is_some_and(|name| !name.trim().is_empty())
}

const LAYOUTS: [ChromeLayout; 3] = [
    ChromeLayout::Classic,
    ChromeLayout::Compact,
    ChromeLayout::Vertical,
];

#[test]
fn every_chrome_control_has_a_name_in_every_layout() {
    for layout in LAYOUTS {
        let tree = tree(layout);
        let unnamed: Vec<String> = nodes(&tree)
            .into_iter()
            .filter(|node| has(node, "actions", "click") && !named(node))
            .map(|node| format!("{} {}", node["role"], node["key"]))
            .collect();
        assert!(
            unnamed.is_empty(),
            "{layout:?}: unnamed controls {unnamed:?}"
        );
    }
}

#[test]
fn tabs_are_a_tab_list_with_the_active_tab_selected() {
    for layout in LAYOUTS {
        let tree = tree(layout);
        let all = nodes(&tree);
        assert!(
            all.iter()
                .any(|node| node["role"] == "tab-list" && node["name"] == "Tabs"),
            "{layout:?}: no tab list"
        );
        let tabs: Vec<&&Value> = all.iter().filter(|node| node["role"] == "tab").collect();
        assert_eq!(tabs.len(), 2, "{layout:?}");
        let active = tabs.iter().find(|tab| tab["name"] == "Example").unwrap();
        let background = tabs.iter().find(|tab| tab["name"] == "Background").unwrap();
        assert!(has(active, "states", "selected"), "{layout:?}: {active}");
        assert!(
            !has(background, "states", "selected"),
            "{layout:?}: {background}"
        );
        assert!(!has(active, "states", "checked"), "{layout:?}: {active}");
    }
}

#[test]
fn the_page_is_a_document_named_after_its_tab() {
    let tree = tree(ChromeLayout::Compact);
    let page = nodes(&tree)
        .into_iter()
        .find(|node| node["key"] == "page")
        .expect("the page");
    assert_eq!(page["role"], "document");
    assert_eq!(page["name"], "Example");
}

#[test]
fn popover_buttons_say_whether_their_popover_is_open() {
    let tree = tree(ChromeLayout::Classic);
    let all = nodes(&tree);
    let shield = all.iter().find(|node| node["key"] == "shield-button");
    if let Some(shield) = shield {
        assert!(has(shield, "states", "collapsed"), "{shield}");
    }
    let toolbar = all
        .iter()
        .find(|node| node["role"] == "toolbar")
        .expect("a toolbar");
    assert!(named(toolbar), "{toolbar}");
}

#[test]
fn the_bookmarks_bar_is_a_toolbar_of_named_buttons() {
    for layout in LAYOUTS {
        let tree = tree(layout);
        let all = nodes(&tree);
        let bar = all
            .iter()
            .find(|node| node["key"] == "bookmarks-bar")
            .unwrap_or_else(|| panic!("{layout:?}: no bookmarks bar"));
        assert_eq!(bar["role"], "toolbar", "{layout:?}: {bar}");
        assert_eq!(bar["name"], "Bookmarks", "{layout:?}: {bar}");
        let names: Vec<&str> = all
            .iter()
            .filter(|node| {
                node["key"]
                    .as_str()
                    .is_some_and(|key| key.starts_with("bookmark-"))
            })
            .filter_map(|node| node["name"].as_str())
            .collect();
        assert_eq!(names, ["https://ladybird.org/", "Work"], "{layout:?}");
    }
}

fn tree_for(browser: Browser) -> Value {
    let mut rt = Runtime::new(browser, browser_view, (1300.0, 600.0), Env::new(false, 1.0));
    rt.frame(0.0);
    rt.frame(0.0);
    rt.accessibility_json()
}

fn unnamed_controls(tree: &Value) -> Vec<String> {
    nodes(tree)
        .into_iter()
        .filter(|node| has(node, "actions", "click") && !named(node))
        .map(|node| format!("{} {}", node["role"], node["key"]))
        .collect()
}

fn named_role(tree: &Value, key: &str, role: &str) {
    let node = nodes(tree)
        .into_iter()
        .find(|node| node["key"] == key)
        .unwrap_or_else(|| panic!("no {key}"));
    assert_eq!(node["role"], role, "{node}");
    assert!(named(node), "{node}");
}

fn check_overlay(set_up: impl FnOnce(&mut Browser), key: &str, role: &str) {
    let mut state = browser(ChromeLayout::Classic);
    set_up(&mut state);
    let tree = tree_for(state);
    let unnamed = unnamed_controls(&tree);
    assert!(unnamed.is_empty(), "{key}: unnamed controls {unnamed:?}");
    named_role(&tree, key, role);
}

#[test]
fn prompts_media_and_report_overlays_name_their_controls() {
    check_overlay(
        |s| crate::pickers::color_picker_requested(s, ACTIVE_VIEW, 1, 0x3366cc),
        "color-picker-ok",
        "default-button",
    );
    check_overlay(
        |s| {
            crate::pickers::date_picker_requested(
                s,
                ACTIVE_VIEW,
                2,
                "date".to_owned(),
                "2026-11-06".to_owned(),
                String::new(),
                String::new(),
            )
        },
        "date-day-2026-11-20",
        "button",
    );
    check_overlay(
        |s| {
            crate::pickers::web_authn_prompt(
                s,
                ACTIVE_VIEW,
                crate::pickers::WebAuthn {
                    state: crate::pickers::WebAuthnState::EnterPin,
                    registering: false,
                    relying_party: "example.com".to_owned(),
                    user_name: String::new(),
                    caller: String::new(),
                    pin_was_wrong: false,
                    pin_retries: Some(3),
                    accounts: Vec::new(),
                    chosen_account: 0,
                    pin: rune::TextEdit::new("").secure(),
                },
            )
        },
        "web-authn",
        "dialog",
    );
    check_overlay(
        |s| {
            crate::crash_reports::review_opened(
                s,
                ACTIVE_VIEW,
                "This page crashed.".to_owned(),
                "Reload page".to_owned(),
                Some("https://example.com/".to_owned()),
                Vec::new(),
            )
        },
        "crash-review-description",
        "multiline-text-input",
    );
    check_overlay(
        |s| crate::screenshots::selection_requested(s, ACTIVE_VIEW, 9002),
        "screenshot-instructions",
        "dialog",
    );
    check_overlay(
        |s| {
            s.updates.status_changed(Some("9.9.9".to_owned()));
        },
        "update-button",
        "button",
    );
    check_overlay(
        |s| {
            s.updates.status_changed(Some("9.9.9".to_owned()));
            s.updates.popover_open = true;
        },
        "update-relaunch",
        "button",
    );
    check_overlay(
        |s| crate::crash_reports::offer(s, "report".to_owned()),
        "crash-report-popover",
        "dialog",
    );
    check_overlay(
        |s| {
            crate::picture_in_picture::shown(
                s,
                crate::picture_in_picture::Shown {
                    id: 9001,
                    owner: ACTIVE_VIEW,
                    size: (320.0, 180.0),
                    minimum: (160.0, 90.0),
                    maximum: (640.0, 360.0),
                },
            )
        },
        "picture-in-picture-9001",
        "dialog",
    );
}
