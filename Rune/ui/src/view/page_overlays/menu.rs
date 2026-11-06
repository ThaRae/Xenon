/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Align, El, PopupAnchor, ScrollSpec, Side, Tx, col, el, icon, row, text};

use crate::chrome_popup::{self, CHROME_MENU_ANCHOR_KEY, DrawnMenu};
use crate::engine::ViewId;
use crate::page_prompts::{self, MENU_ANCHOR_KEY, MenuEntry, MenuPurpose, PromptId, ShownMenu};
use crate::state::Browser;
use crate::view::page_overlays::controls::{
    BODY_FONT, CHECK, CHEVRON_RIGHT, SMALL_FONT, panel_shadow,
};
use crate::view::palette::Palette;

const ROW_HEIGHT: f32 = 28.0;
const MENU_MIN_WIDTH: f32 = 200.0;
const MENU_MAX_WIDTH: f32 = 420.0;

#[derive(Clone, Copy)]
enum MenuOwner {
    Page {
        view: ViewId,
        id: PromptId,
        purpose: MenuPurpose,
    },
    Chrome,
}

impl MenuOwner {
    fn is_select(self) -> bool {
        matches!(
            self,
            MenuOwner::Page {
                purpose: MenuPurpose::Select,
                ..
            }
        )
    }

    fn pick(self, s: &mut Browser, item: u32, tx: &mut Tx) {
        match self {
            MenuOwner::Page { view, id, .. } => page_prompts::pick_menu_item(s, view, id, item),
            MenuOwner::Chrome => chrome_popup::pick(s, item, tx),
        }
    }

    fn open_submenu(self, s: &mut Browser, depth: usize, index: usize) {
        match self {
            MenuOwner::Page { .. } => page_prompts::open_submenu(s, depth, index),
            MenuOwner::Chrome => chrome_popup::open_submenu(s, depth, index),
        }
    }

    fn close_submenus_below(self, s: &mut Browser, depth: usize) {
        match self {
            MenuOwner::Page { .. } => page_prompts::close_submenus_below(s, depth),
            MenuOwner::Chrome => chrome_popup::close_submenus_below(s, depth),
        }
    }
}

struct MenuLayout<'a> {
    owner: MenuOwner,
    entries: &'a [MenuEntry],
    open_submenus: &'a [usize],
    minimum_width: f32,
    names: Option<&'a chrome_popup::PopupMenu>,
}

impl MenuLayout<'_> {
    fn surface_key(&self, depth: usize) -> String {
        match self.owner {
            MenuOwner::Page { .. } => format!("page-menu-{depth}"),
            MenuOwner::Chrome => format!("chrome-menu-{depth}"),
        }
    }

    fn row_key(&self, depth: usize, index: usize) -> String {
        match self.owner {
            MenuOwner::Page { .. } => format!("page-menu-row-{depth}-{index}"),
            MenuOwner::Chrome => format!("chrome-menu-row-{depth}-{index}"),
        }
    }

    fn item_key(&self, depth: usize, index: usize, item: u32) -> String {
        match self.names.and_then(|names| names.name(item)) {
            Some(name) => format!("chrome-menu-item-{name}"),
            None => self.row_key(depth, index),
        }
    }
}

pub fn menu_layers(menu: &ShownMenu, p: &Palette) -> Vec<El> {
    let mut layers = vec![
        el().key(MENU_ANCHOR_KEY)
            .pin(Some(menu.x), Some(menu.y), None, None)
            .size(0.0, 0.0)
            .a11y_hidden(),
    ];
    if menu.native {
        return layers;
    }
    let layout = MenuLayout {
        owner: MenuOwner::Page {
            view: menu.view,
            id: menu.id,
            purpose: menu.purpose,
        },
        entries: &menu.entries,
        open_submenus: &menu.open_submenus,
        minimum_width: menu.minimum_width,
        names: None,
    };
    layers.extend(surfaces(
        &layout,
        MENU_ANCHOR_KEY.to_owned(),
        page_prompts::dismiss_menu,
        p,
    ));
    layers
}

pub fn chrome_menu_layers(menu: &DrawnMenu, p: &Palette) -> Vec<El> {
    let mut layers = Vec::new();
    let anchor = match &menu.anchor {
        PopupAnchor::Point(x, y) => {
            layers.push(
                el().key(CHROME_MENU_ANCHOR_KEY)
                    .pin(Some(*x), Some(*y), None, None)
                    .size(0.0, 0.0)
                    .a11y_hidden(),
            );
            CHROME_MENU_ANCHOR_KEY.to_owned()
        }
        PopupAnchor::Key(key) => key.clone(),
    };
    let layout = MenuLayout {
        owner: MenuOwner::Chrome,
        entries: menu.menu.entries(),
        open_submenus: &menu.open_submenus,
        minimum_width: 0.0,
        names: Some(&menu.menu),
    };
    layers.extend(surfaces(&layout, anchor, chrome_popup::dismiss, p));
    layers
}

fn surfaces(
    layout: &MenuLayout,
    anchor: String,
    dismiss: fn(&mut Browser),
    p: &Palette,
) -> Vec<El> {
    let mut layers = vec![
        surface(layout, layout.entries, 0, p)
            .modal(true, dismiss)
            .anchored_fit(anchor, Side::Bottom, Align::Start, 2.0),
    ];
    let mut entries = layout.entries;
    for (depth, index) in layout.open_submenus.iter().enumerate() {
        let Some(MenuEntry::Submenu {
            entries: submenu, ..
        }) = entries.get(*index)
        else {
            break;
        };
        layers.push(surface(layout, submenu, depth + 1, p).anchored_fit(
            layout.row_key(depth, *index),
            Side::Right,
            Align::Start,
            -4.0,
        ));
        entries = submenu;
    }
    layers
}

fn surface(layout: &MenuLayout, entries: &[MenuEntry], depth: usize, p: &Palette) -> El {
    let has_checks = entries.iter().any(|entry| {
        matches!(
            entry,
            MenuEntry::Action {
                checkable: true,
                ..
            }
        )
    });
    let minimum_width = if depth == 0 {
        layout.minimum_width.max(MENU_MIN_WIDTH)
    } else {
        MENU_MIN_WIDTH
    };
    let rows = entries
        .iter()
        .enumerate()
        .map(|(index, entry)| menu_row(layout, entry, depth, index, has_checks, p));
    let list = col()
        .key("rows")
        .items_stretch()
        .shrink()
        .min_h(0.0)
        .scroll(ScrollSpec {
            stick_bottom: false,
        })
        .children(rows);
    panel_shadow(
        col()
            .key(layout.surface_key(depth))
            .role(if layout.owner.is_select() {
                "listbox"
            } else {
                "menu"
            })
            .focus_group()
            .min_w(minimum_width)
            .max_w(MENU_MAX_WIDTH.max(minimum_width))
            .padding(4.0)
            .radius(9.0)
            .fill(p.surface)
            .border(p.hairline, p.bubble_border)
            .items_stretch()
            .child(list),
    )
}

fn menu_row(
    layout: &MenuLayout,
    entry: &MenuEntry,
    depth: usize,
    index: usize,
    has_checks: bool,
    p: &Palette,
) -> El {
    let key = layout.row_key(depth, index);
    let owner = layout.owner;
    match entry {
        MenuEntry::Separator => el()
            .key(key)
            .h(1.0)
            .mt(4.0)
            .mb(4.0)
            .mx(6.0)
            .fill(p.separator)
            .a11y_hidden(),
        MenuEntry::Heading(title) => row().key(key).h(24.0).px(10.0).center_y().child(
            text(title.clone())
                .font(SMALL_FONT)
                .color(p.text_muted)
                .truncate(),
        ),
        MenuEntry::Action {
            id,
            title,
            enabled,
            checked,
            indented,
            ..
        } => {
            let item = *id;
            let key = layout.item_key(depth, index, item);
            let mut row = item_row(key, title, *indented, has_checks, *checked, p)
                .role(if owner.is_select() {
                    "option"
                } else {
                    "menuitem"
                })
                .checked(*checked);
            if *enabled {
                row = row
                    .focusable(true)
                    .focus_highlight(p.hover)
                    .on_hover_change(move |s: &mut Browser, hovered, _| {
                        if hovered {
                            owner.close_submenus_below(s, depth);
                        }
                    })
                    .on_press_tx(move |s: &mut Browser, tx| owner.pick(s, item, tx));
            } else {
                row = row.disabled(true).dim(0.45);
            }
            row
        }
        MenuEntry::Submenu { title, .. } => {
            let open = layout.open_submenus.get(depth) == Some(&index);
            let row = item_row(key, title, false, has_checks, false, p)
                .role("menuitem")
                .expanded(open)
                .focusable(true)
                .focus_highlight(p.hover)
                .on_hover_change(move |s: &mut Browser, hovered, _| {
                    if hovered {
                        owner.open_submenu(s, depth, index);
                    }
                })
                .on_press(move |s: &mut Browser| owner.open_submenu(s, depth, index))
                .child(icon(&CHEVRON_RIGHT, 14.0).color(p.text_muted).a11y_hidden());
            if open { row.fill(p.hover) } else { row }
        }
    }
}

fn item_row(
    key: String,
    title: &str,
    indented: bool,
    has_checks: bool,
    checked: bool,
    p: &Palette,
) -> El {
    let mut row = row()
        .key(key)
        .label(title)
        .h(ROW_HEIGHT)
        .pl(if indented { 22.0 } else { 8.0 })
        .pr(10.0)
        .gap(6.0)
        .radius(6.0)
        .center_y();
    if has_checks {
        row = row.child(
            el().size(16.0, 16.0).center().a11y_hidden().child(
                icon(&CHECK, 14.0)
                    .color(p.accent)
                    .opacity(if checked { 1.0 } else { 0.0 }),
            ),
        );
    }
    row.child(
        text(title.to_owned())
            .font(BODY_FONT)
            .color(p.text)
            .grow(1.0)
            .shrink()
            .truncate()
            .a11y_hidden(),
    )
}
