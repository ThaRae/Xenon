/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Arc;

use rune::{Env, Pixels, Runtime};
use serde_json::Value;

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
