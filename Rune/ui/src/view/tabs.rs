/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{El, LoopSpec, PointerButton, el, icon, row, text};

use crate::engine::ViewId;
use crate::state::{Browser, TAB_STRIP_HEIGHT};
use crate::tab_list::Tab;
use crate::view::icons;
use crate::view::palette::Palette;

const TAB_WIDTH: f32 = 232.0;
const TAB_MIN_WIDTH: f32 = 44.0;
const TAB_TOP_INSET: f32 = 6.0;
const TAB_HEIGHT: f32 = TAB_STRIP_HEIGHT - TAB_TOP_INSET;
const TAB_RADIUS: f32 = 9.0;
const STRIP_PADDING: f32 = 8.0;
const ICON_SIZE: f32 = 16.0;
const TITLE_FONT: TextStyle = TextStyle::new(12.5, 450.0);
const ACTIVE_TITLE_FONT: TextStyle = TextStyle::new(12.5, 550.0);

pub fn tab_strip(s: &Browser, p: &Palette, titlebar_inset: f32) -> El {
    let mut tabs = row()
        .key("tabs")
        .role("tablist")
        .label("Tabs")
        .h_full()
        .items_end()
        .gap(2.0)
        .shrink();
    for tab in s.tabs.iter() {
        tabs = tabs.child(tab_button(tab, s.tabs.is_active(tab.view), p));
    }
    row()
        .key("tab-strip")
        .w_full()
        .h(TAB_STRIP_HEIGHT)
        .pl(titlebar_inset + STRIP_PADDING)
        .pr(STRIP_PADDING)
        .pt(TAB_TOP_INSET)
        .items_end()
        .gap(4.0)
        .child(tabs)
        .child(new_tab_button(p))
}

fn tab_button(tab: &Tab, active: bool, p: &Palette) -> El {
    let view = tab.view;
    let title = text(tab.display_title())
        .key("title")
        .font(if active {
            ACTIVE_TITLE_FONT
        } else {
            TITLE_FONT
        })
        .color(if active { p.text } else { p.text_muted })
        .truncate()
        .grow(1.0)
        .shrink()
        .a11y_hidden();
    let tab_el = row()
        .key(format!("tab-{view}"))
        .role("tab")
        .label(tab.display_title())
        .checked(active)
        .w(if tab.pinned { TAB_MIN_WIDTH } else { TAB_WIDTH })
        .shrink()
        .min_w(TAB_MIN_WIDTH)
        .h(TAB_HEIGHT)
        .pl(10.0)
        .pr(6.0)
        .gap(8.0)
        .center_y()
        .corners([TAB_RADIUS, TAB_RADIUS, 0.0, 0.0])
        .focusable(false)
        .pointer_buttons([PointerButton::Middle])
        .on_press_tx(move |s: &mut Browser, tx| {
            if tx.button() == PointerButton::Middle {
                s.close_tab(view, tx);
            } else {
                s.activate_tab(view, tx);
            }
        })
        .on_secondary_press(move |s: &mut Browser, _, _| s.toggle_tab_pinned(view))
        .child(tab_icon(tab, p));
    let tab_el = if tab.pinned {
        tab_el
    } else {
        tab_el.child(title).child(close_button(view, p))
    };
    if active {
        tab_el.fill(p.surface).child(
            el().key("indicator")
                .pin(Some(TAB_RADIUS), Some(0.0), Some(TAB_RADIUS), None)
                .h(2.0)
                .corners([0.0, 0.0, 2.0, 2.0])
                .fill(p.accent)
                .a11y_hidden(),
        )
    } else {
        tab_el.hover(p.tab_hover)
    }
}

fn tab_icon(tab: &Tab, p: &Palette) -> El {
    let slot = el()
        .key("icon")
        .size(ICON_SIZE, ICON_SIZE)
        .center()
        .a11y_hidden();
    if tab.crashed {
        return slot.child(icon(&icons::CRASHED, ICON_SIZE).color(p.text_muted));
    }
    if tab.loading {
        return slot
            .child(
                icon(&icons::SPINNER_TRACK, ICON_SIZE)
                    .color(p.separator)
                    .absolute(0.0, 0.0),
            )
            .child(
                icon(&icons::SPINNER, ICON_SIZE)
                    .color(p.accent)
                    .absolute(0.0, 0.0)
                    .looping(LoopSpec::rotation(0.9)),
            );
    }
    match &tab.favicon {
        Some(favicon) => slot.child(el().size(ICON_SIZE, ICON_SIZE).radius(3.0).pixels(favicon)),
        None => slot.child(icon(&icons::GLOBE, ICON_SIZE - 1.0).color(p.text_muted)),
    }
}

fn close_button(view: ViewId, p: &Palette) -> El {
    el().key("close")
        .role("button")
        .label("Close tab")
        .size(20.0, 20.0)
        .radius(6.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press_tx(move |s: &mut Browser, tx| s.close_tab(view, tx))
        .child(icon(&icons::CLOSE, 13.0).color(p.icon).a11y_hidden())
}

fn new_tab_button(p: &Palette) -> El {
    row().key("new-tab-slot").h(TAB_HEIGHT).center_y().child(
        el().key("new-tab")
            .role("button")
            .label("New tab")
            .size(28.0, 28.0)
            .radius(8.0)
            .center()
            .focusable(false)
            .hover(p.hover)
            .press(p.press)
            .on_press_tx(|s: &mut Browser, tx| s.new_tab(tx))
            .child(icon(&icons::PLUS, 16.0).color(p.icon).a11y_hidden()),
    )
}
