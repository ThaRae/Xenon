/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{PopupAnchor, Tx, popup_menu_supported};

use crate::page_prompts::{MenuEntry, popup_menu_named};
use crate::state::Browser;

pub const CHROME_MENU_ANCHOR_KEY: &str = "chrome-menu-anchor";

type OnPick = Box<dyn FnOnce(&mut Browser, &str, &mut Tx)>;

pub struct PopupItem {
    id: String,
    title: String,
    enabled: bool,
    checked: Option<bool>,
}

impl PopupItem {
    pub fn new(id: impl Into<String>, title: impl Into<String>) -> PopupItem {
        PopupItem {
            id: id.into(),
            title: title.into(),
            enabled: true,
            checked: None,
        }
    }

    pub fn enabled(mut self, enabled: bool) -> PopupItem {
        self.enabled = enabled;
        self
    }

    pub fn checked(mut self, checked: bool) -> PopupItem {
        self.checked = Some(checked);
        self
    }
}

#[derive(Default)]
pub struct PopupMenu {
    entries: Vec<MenuEntry>,
    names: Vec<String>,
}

impl PopupMenu {
    pub fn new() -> PopupMenu {
        PopupMenu::default()
    }

    pub fn from_entries(entries: Vec<MenuEntry>) -> PopupMenu {
        let mut names = Vec::new();
        let entries = entries
            .into_iter()
            .map(|entry| renumbered(entry, &mut names))
            .collect();
        PopupMenu { entries, names }
    }

    pub fn item(mut self, item: PopupItem) -> PopupMenu {
        let id = self.names.len() as u32;
        self.names.push(item.id);
        self.entries.push(MenuEntry::Action {
            id,
            title: item.title,
            enabled: item.enabled,
            checkable: item.checked.is_some(),
            checked: item.checked.unwrap_or(false),
            indented: false,
        });
        self
    }

    pub fn separator(mut self) -> PopupMenu {
        self.entries.push(MenuEntry::Separator);
        self
    }

    pub fn submenu(mut self, title: impl Into<String>, submenu: PopupMenu) -> PopupMenu {
        let offset = self.names.len() as u32;
        self.names.extend(submenu.names);
        self.entries.push(MenuEntry::Submenu {
            title: title.into(),
            entries: submenu
                .entries
                .into_iter()
                .map(|entry| shifted(entry, offset))
                .collect(),
        });
        self
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    pub fn entries(&self) -> &[MenuEntry] {
        &self.entries
    }

    pub fn name(&self, item: u32) -> Option<&str> {
        self.names.get(item as usize).map(String::as_str)
    }

    fn native(&self) -> rune::PopupMenu {
        popup_menu_named(&self.entries, &|item| {
            self.name(item).unwrap_or_default().to_owned()
        })
    }
}

fn renumbered(entry: MenuEntry, names: &mut Vec<String>) -> MenuEntry {
    match entry {
        MenuEntry::Action {
            id,
            title,
            enabled,
            checkable,
            checked,
            indented,
        } => {
            names.push(id.to_string());
            MenuEntry::Action {
                id: names.len() as u32 - 1,
                title,
                enabled,
                checkable,
                checked,
                indented,
            }
        }
        MenuEntry::Submenu { title, entries } => MenuEntry::Submenu {
            title,
            entries: entries
                .into_iter()
                .map(|entry| renumbered(entry, names))
                .collect(),
        },
        other => other,
    }
}

fn shifted(entry: MenuEntry, offset: u32) -> MenuEntry {
    match entry {
        MenuEntry::Action {
            id,
            title,
            enabled,
            checkable,
            checked,
            indented,
        } => MenuEntry::Action {
            id: id + offset,
            title,
            enabled,
            checkable,
            checked,
            indented,
        },
        MenuEntry::Submenu { title, entries } => MenuEntry::Submenu {
            title,
            entries: entries
                .into_iter()
                .map(|entry| shifted(entry, offset))
                .collect(),
        },
        other => other,
    }
}

pub struct DrawnMenu {
    pub anchor: PopupAnchor,
    pub menu: PopupMenu,
    pub open_submenus: Vec<usize>,
    on_pick: OnPick,
}

pub fn drawn() -> bool {
    !popup_menu_supported() || std::env::var_os("XENON_RUNE_DRAWN_MENUS").is_some()
}

pub fn open(
    s: &mut Browser,
    tx: &mut Tx,
    menu: PopupMenu,
    anchor: impl Into<PopupAnchor>,
    on_pick: impl FnOnce(&mut Browser, &str, &mut Tx) + 'static,
) {
    if menu.is_empty() {
        return;
    }
    s.hover_card = None;
    if drawn() {
        present(s, menu, anchor.into(), on_pick);
        return;
    }
    tx.popup_menu(
        menu.native(),
        anchor,
        move |s: &mut Browser, picked: Option<&str>, tx: &mut Tx| {
            if let Some(picked) = picked {
                on_pick(s, picked, tx);
            }
        },
    );
}

pub fn present(
    s: &mut Browser,
    menu: PopupMenu,
    anchor: PopupAnchor,
    on_pick: impl FnOnce(&mut Browser, &str, &mut Tx) + 'static,
) {
    s.chrome_menu = Some(DrawnMenu {
        anchor,
        menu,
        open_submenus: Vec::new(),
        on_pick: Box::new(on_pick),
    });
}

pub fn pick(s: &mut Browser, item: u32, tx: &mut Tx) {
    let Some(menu) = s.chrome_menu.take() else {
        return;
    };
    if let Some(name) = menu.menu.name(item) {
        (menu.on_pick)(s, name, tx);
    }
}

pub fn dismiss(s: &mut Browser) {
    s.chrome_menu = None;
}

pub fn open_submenu(s: &mut Browser, depth: usize, index: usize) {
    if let Some(menu) = s.chrome_menu.as_mut() {
        menu.open_submenus.truncate(depth);
        menu.open_submenus.push(index);
    }
}

pub fn close_submenus_below(s: &mut Browser, depth: usize) {
    if let Some(menu) = s.chrome_menu.as_mut() {
        menu.open_submenus.truncate(depth);
    }
}

#[cfg(test)]
mod tests {
    use std::cell::RefCell;
    use std::rc::Rc;

    use super::*;

    fn titles(entries: &[MenuEntry]) -> Vec<String> {
        entries
            .iter()
            .map(|entry| match entry {
                MenuEntry::Action { id, title, .. } => format!("{id}:{title}"),
                MenuEntry::Separator => "-".into(),
                MenuEntry::Heading(title) => format!("#{title}"),
                MenuEntry::Submenu { title, entries } => {
                    format!("{title}>[{}]", titles(entries).join(","))
                }
            })
            .collect()
    }

    #[test]
    fn a_built_menu_numbers_its_items_through_its_submenus() {
        let menu = PopupMenu::new()
            .item(PopupItem::new("new-tab", "New Tab"))
            .separator()
            .submenu(
                "Layout",
                PopupMenu::new()
                    .item(PopupItem::new("classic", "Classic").checked(true))
                    .item(PopupItem::new("compact", "Compact").checked(false)),
            )
            .item(PopupItem::new("always-on-top", "Always on Top").enabled(false));
        assert_eq!(
            titles(menu.entries()),
            [
                "0:New Tab",
                "-",
                "Layout>[1:Classic,2:Compact]",
                "3:Always on Top"
            ]
        );
        assert_eq!(menu.name(1), Some("classic"));
        assert_eq!(menu.name(3), Some("always-on-top"));
        assert_eq!(menu.name(4), None);
        assert!(matches!(
            menu.entries()[3],
            MenuEntry::Action {
                enabled: false,
                checkable: false,
                ..
            }
        ));
        assert!(matches!(
            &menu.entries()[2],
            MenuEntry::Submenu { entries, .. } if matches!(entries[1], MenuEntry::Action { checkable: true, checked: false, .. })
        ));
    }

    #[test]
    fn an_engine_menu_keeps_its_item_ids_as_names() {
        let action = |id, title: &str| MenuEntry::Action {
            id,
            title: title.into(),
            enabled: true,
            checkable: false,
            checked: false,
            indented: false,
        };
        let menu = PopupMenu::from_entries(vec![
            action(7, "Back"),
            MenuEntry::Separator,
            MenuEntry::Submenu {
                title: "More".into(),
                entries: vec![action(12, "Forward")],
            },
        ]);
        assert_eq!(titles(menu.entries()), ["0:Back", "-", "More>[1:Forward]"]);
        assert_eq!(menu.name(0), Some("7"));
        assert_eq!(menu.name(1), Some("12"));
    }

    #[test]
    fn the_drawn_menu_answers_once_with_the_picked_name() {
        let mut browser = Browser::new(Default::default());
        let picked = Rc::new(RefCell::new(Vec::<String>::new()));
        let seen = picked.clone();
        present(
            &mut browser,
            PopupMenu::new()
                .item(PopupItem::new("reload", "Reload"))
                .submenu(
                    "Move Tab",
                    PopupMenu::new().item(PopupItem::new("move-end", "Move to End")),
                ),
            PopupAnchor::Point(10.0, 20.0),
            move |_, name, _| seen.borrow_mut().push(name.to_owned()),
        );
        open_submenu(&mut browser, 0, 1);
        assert_eq!(browser.chrome_menu.as_ref().unwrap().open_submenus, [1]);
        close_submenus_below(&mut browser, 0);
        assert!(
            browser
                .chrome_menu
                .as_ref()
                .unwrap()
                .open_submenus
                .is_empty()
        );
        let mut tx = Tx::default();
        pick(&mut browser, 1, &mut tx);
        assert!(browser.chrome_menu.is_none());
        pick(&mut browser, 0, &mut tx);
        assert_eq!(*picked.borrow(), ["move-end"]);
    }

    fn window(size: (f32, f32)) -> rune::Runtime<Browser> {
        let spec = rune::AppSpec::new(
            crate::state::APP_NAME,
            size,
            || Browser::new(Default::default()),
            crate::view::browser_view,
        );
        let mut runtime = rune::App::new(spec).headless_runtime(size, rune::Env::new(false, 1.0));
        runtime.frame(0.0);
        runtime
    }

    fn click(runtime: &mut rune::Runtime<Browser>, key: &str) {
        let (x, y) = runtime
            .find(key)
            .unwrap_or_else(|| panic!("nothing is keyed {key}"));
        runtime.pointer_move(x, y);
        runtime.frame(0.1);
        runtime.pointer_down(x, y);
        runtime.pointer_up(x, y);
        runtime.frame(0.1);
    }

    #[test]
    fn the_window_draws_the_window_menu_and_runs_a_submenu_item() {
        if !drawn() {
            return;
        }
        let mut runtime = window((1000.0, 700.0));
        runtime.update(|s, tx| {
            crate::chrome_menu::show_window_menu(s, rune::PointerPress { x: 300.0, y: 20.0 }, tx)
        });
        runtime.frame(0.1);
        let (x, y) = runtime.find("chrome-menu-item-new-tab").unwrap();
        assert!(
            x > 300.0 && y > 20.0,
            "the menu opens below and right of the press"
        );
        assert!(runtime.find("chrome-menu-item-layout-vertical").is_none());

        let (x, y) = runtime.find("chrome-menu-row-0-5").unwrap();
        runtime.pointer_move(x, y);
        runtime.frame(0.1);
        click(&mut runtime, "chrome-menu-item-layout-vertical");

        let seen = Rc::new(RefCell::new(None));
        let layout = seen.clone();
        runtime.update(move |s, _| {
            *layout.borrow_mut() = Some((s.chrome.layout, s.chrome_menu.is_some()))
        });
        assert_eq!(
            *seen.borrow(),
            Some((crate::engine::ChromeLayout::Vertical, false))
        );
    }

    #[test]
    fn escape_closes_a_drawn_menu() {
        if !drawn() {
            return;
        }
        let mut runtime = window((1000.0, 700.0));
        runtime.update(|s, tx| {
            open(
                s,
                tx,
                PopupMenu::new().item(PopupItem::new("a", "A")),
                rune::PointerPress { x: 40.0, y: 40.0 },
                |_, _, _| panic!("nothing was picked"),
            )
        });
        runtime.frame(0.1);
        assert!(runtime.find("chrome-menu-item-a").is_some());
        runtime.key(rune::Key::Escape.into());
        runtime.frame(0.1);
        assert!(runtime.find("chrome-menu-item-a").is_none());
    }

    #[test]
    fn a_dismissed_drawn_menu_picks_nothing() {
        let mut browser = Browser::new(Default::default());
        let picked = Rc::new(RefCell::new(None::<String>));
        let seen = picked.clone();
        present(
            &mut browser,
            PopupMenu::new().item(PopupItem::new("a", "A")),
            PopupAnchor::Key("menu-button".into()),
            move |_, name, _| *seen.borrow_mut() = Some(name.to_owned()),
        );
        dismiss(&mut browser);
        pick(&mut browser, 0, &mut Tx::default());
        assert!(browser.chrome_menu.is_none());
        assert_eq!(*picked.borrow(), None);
    }
}
