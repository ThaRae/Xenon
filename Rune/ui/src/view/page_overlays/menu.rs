/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Align, El, ScrollSpec, Side, col, el, icon, row, text};

use crate::page_prompts::{self, MENU_ANCHOR_KEY, MenuEntry, MenuPurpose, ShownMenu};
use crate::state::Browser;
use crate::view::page_overlays::controls::{
    BODY_FONT, CHECK, CHEVRON_RIGHT, SMALL_FONT, panel_shadow,
};
use crate::view::palette::Palette;

const ROW_HEIGHT: f32 = 28.0;
const MENU_MIN_WIDTH: f32 = 200.0;
const MENU_MAX_WIDTH: f32 = 420.0;

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
    layers.push(
        surface(menu, &menu.entries, 0, p)
            .modal(true, page_prompts::dismiss_menu)
            .anchored_fit(MENU_ANCHOR_KEY, Side::Bottom, Align::Start, 2.0),
    );
    let mut entries = menu.entries.as_slice();
    for (depth, index) in menu.open_submenus.iter().enumerate() {
        let Some(MenuEntry::Submenu {
            entries: submenu, ..
        }) = entries.get(*index)
        else {
            break;
        };
        layers.push(surface(menu, submenu, depth + 1, p).anchored_fit(
            row_key(depth, *index),
            Side::Right,
            Align::Start,
            -4.0,
        ));
        entries = submenu;
    }
    layers
}

fn row_key(depth: usize, index: usize) -> String {
    format!("page-menu-row-{depth}-{index}")
}

fn surface(menu: &ShownMenu, entries: &[MenuEntry], depth: usize, p: &Palette) -> El {
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
        menu.minimum_width.max(MENU_MIN_WIDTH)
    } else {
        MENU_MIN_WIDTH
    };
    let rows = entries
        .iter()
        .enumerate()
        .map(|(index, entry)| menu_row(menu, entry, depth, index, has_checks, p));
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
            .key(format!("page-menu-{depth}"))
            .role(if menu.purpose == MenuPurpose::Select {
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
    menu: &ShownMenu,
    entry: &MenuEntry,
    depth: usize,
    index: usize,
    has_checks: bool,
    p: &Palette,
) -> El {
    let key = row_key(depth, index);
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
            let (view, menu_id) = (menu.view, menu.id);
            let mut row = item_row(key, title, *indented, has_checks, *checked, p)
                .role(if menu.purpose == MenuPurpose::Select {
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
                            page_prompts::close_submenus_below(s, depth);
                        }
                    })
                    .on_press(move |s: &mut Browser| {
                        page_prompts::pick_menu_item(s, view, menu_id, item)
                    });
            } else {
                row = row.disabled(true).dim(0.45);
            }
            row
        }
        MenuEntry::Submenu { title, .. } => {
            let open = menu.open_submenus.get(depth) == Some(&index);
            let row = item_row(key, title, false, has_checks, false, p)
                .role("menuitem")
                .expanded(open)
                .focusable(true)
                .focus_highlight(p.hover)
                .on_hover_change(move |s: &mut Browser, hovered, _| {
                    if hovered {
                        page_prompts::open_submenu(s, depth, index);
                    }
                })
                .on_press(move |s: &mut Browser| page_prompts::open_submenu(s, depth, index))
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
