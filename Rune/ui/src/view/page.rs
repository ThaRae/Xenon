/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Align, Color, Cursor, El, PointerButton, SurfaceFit, col, el, icon, row, text};

use crate::ffi;
use crate::state::Browser;
use crate::tab_list::Tab;
use crate::view::icons;
use crate::view::page_overlays::page_overlays;
use crate::view::palette::Palette;

const STATUS_FONT: TextStyle = TextStyle::new(12.0, 400.0);
const NOTICE_TITLE_FONT: TextStyle = TextStyle::new(15.0, 600.0);
const NOTICE_BODY_FONT: TextStyle = TextStyle::new(13.0, 400.0);
const BUTTON_FONT: TextStyle = TextStyle::new(13.0, 550.0);
const STATUS_MAX_WIDTH: f32 = 560.0;

pub fn page_area(s: &Browser, p: &Palette) -> El {
    let area = col()
        .key("page-area")
        .grow(1.0)
        .w_full()
        .min_h(0.0)
        .fill(p.page)
        .on_layout(|s: &mut Browser, rect| s.page_laid_out(rect));
    let Some(tab) = s.tabs.active() else {
        return area;
    };
    let mut area = area.child(page_surface(s, tab));
    if !tab.hovered_link.is_empty() {
        area = area.child(status_bubble(&tab.hovered_link, p));
    }
    if tab.crashed {
        area = area.child(crashed_notice(p));
    } else if tab.unresponsive {
        area = area.child(unresponsive_prompt(tab, p));
    }
    area.children(page_overlays(s, tab.view, p))
}

fn page_surface(s: &Browser, tab: &Tab) -> El {
    let surface = el()
        .key("page")
        .role("document")
        .label(tab.display_title())
        .w_full()
        .grow(1.0)
        .min_h(0.0)
        .pixels_fit(&tab.page, SurfaceFit::Fill)
        .surface_anchor(Align::Start, Align::Start)
        .surface_unscaled(true)
        .cursor(cursor_for(tab.cursor))
        .focusable(true)
        .no_focus_ring()
        .pointer_buttons([
            PointerButton::Middle,
            PointerButton::Back,
            PointerButton::Forward,
        ])
        .on_drag(|s: &mut Browser, drag, tx| {
            s.page_drag(drag, tx);
            if drag.phase == rune::Phase::Move {
                tx.live_only();
            }
        })
        .on_pointer_move_local(|s: &mut Browser, pointer, tx| {
            s.page_pointer_moved(pointer, tx);
            tx.live_only();
        })
        .on_hover_change(|s: &mut Browser, hovered, tx| s.page_hover_changed(hovered, tx))
        .on_secondary_press_local(|s: &mut Browser, pointer, tx| {
            s.page_secondary_click(pointer, tx)
        })
        .on_wheel(|s: &mut Browser, wheel, tx| {
            s.page_wheel(wheel);
            tx.live_only();
            true
        })
        .on_key(|s: &mut Browser, event, tx| s.page_key(event, tx))
        .on_key_up(|s: &mut Browser, event, tx| s.page_key_up(event, tx));
    if s.page_has_keyboard() {
        surface.ime_cursor()
    } else {
        surface
    }
}

fn status_bubble(url: &str, p: &Palette) -> El {
    row()
        .key("status")
        .role("status")
        .pin(Some(0.0), None, None, Some(0.0))
        .max_w(STATUS_MAX_WIDTH)
        .h(24.0)
        .px(9.0)
        .center_y()
        .fill(p.bubble)
        .border(p.hairline, p.bubble_border)
        .corners([0.0, 7.0, 0.0, 0.0])
        .inert()
        .child(
            text(url)
                .key("status-text")
                .font(STATUS_FONT)
                .color(p.text_muted)
                .truncate()
                .shrink(),
        )
}

fn prompt_button(key: &'static str, label: &'static str, primary: bool, p: &Palette) -> El {
    let button = row()
        .key(key)
        .role("button")
        .label(label)
        .h(30.0)
        .px(14.0)
        .radius(8.0)
        .center()
        .focusable(false)
        .child(
            text(label)
                .font(BUTTON_FONT)
                .color(if primary { Color::WHITE } else { p.text })
                .a11y_hidden(),
        );
    if primary {
        button
            .fill(p.accent)
            .hover(Color::WHITE.alpha(0.12))
            .press(Color::BLACK.alpha(0.12))
    } else {
        button.fill(p.field).hover(p.hover).press(p.press)
    }
}

fn unresponsive_prompt(tab: &Tab, p: &Palette) -> El {
    let view = tab.view;
    let name = if tab.title.is_empty() {
        "This page".to_owned()
    } else {
        format!("“{}”", tab.title)
    };
    let card = col()
        .key("unresponsive")
        .role("alertdialog")
        .label("Page unresponsive")
        .w(380.0)
        .padding(16.0)
        .gap(10.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .shadow(8.0, 24.0, Color::BLACK.alpha(0.25))
        .child(
            text("Page unresponsive")
                .font(NOTICE_TITLE_FONT)
                .color(p.text),
        )
        .child(
            text(format!(
                "{name} isn't responding. You can wait for it, or exit the page."
            ))
            .font(NOTICE_BODY_FONT)
            .color(p.text_muted)
            .wrap(),
        )
        .child(
            row()
                .key("unresponsive-buttons")
                .w_full()
                .gap(8.0)
                .justify_end()
                .child(
                    prompt_button("unresponsive-exit", "Exit page", false, p)
                        .on_press(move |s: &mut Browser| s.answer_unresponsive_page(view, false)),
                )
                .child(
                    prompt_button("unresponsive-wait", "Wait", true, p)
                        .on_press(move |s: &mut Browser| s.answer_unresponsive_page(view, true)),
                ),
        );
    row()
        .key("unresponsive-row")
        .pin(Some(0.0), Some(16.0), Some(0.0), None)
        .justify_center()
        .child(card)
}

fn crashed_notice(p: &Palette) -> El {
    let reload = row()
        .key("crash-reload")
        .role("button")
        .label("Reload")
        .h(32.0)
        .px(18.0)
        .mt(6.0)
        .radius(8.0)
        .center()
        .fill(p.accent)
        .hover(Color::WHITE.alpha(0.12))
        .press(Color::BLACK.alpha(0.12))
        .on_press(|s: &mut Browser| s.reload())
        .child(
            text("Reload")
                .font(BUTTON_FONT)
                .color(Color::WHITE)
                .a11y_hidden(),
        );
    col()
        .key("crashed")
        .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
        .center()
        .gap(8.0)
        .fill(p.page)
        .child(
            icon(&icons::CRASHED, 44.0)
                .color(p.text_muted)
                .mb(6.0)
                .a11y_hidden(),
        )
        .child(
            text("This tab crashed")
                .font(NOTICE_TITLE_FONT)
                .color(p.text),
        )
        .child(
            text("Something went wrong while showing this page.")
                .font(NOTICE_BODY_FONT)
                .color(p.text_muted),
        )
        .child(reload)
}

fn cursor_for(cursor: u32) -> Cursor {
    match cursor {
        ffi::XENON_CURSOR_POINTER => Cursor::Pointer,
        ffi::XENON_CURSOR_TEXT | ffi::XENON_CURSOR_VERTICAL_TEXT => Cursor::Text,
        ffi::XENON_CURSOR_CROSSHAIR => Cursor::Crosshair,
        ffi::XENON_CURSOR_MOVE => Cursor::Move,
        ffi::XENON_CURSOR_NOT_ALLOWED => Cursor::NotAllowed,
        ffi::XENON_CURSOR_GRAB => Cursor::Grab,
        ffi::XENON_CURSOR_GRABBING => Cursor::Grabbing,
        ffi::XENON_CURSOR_RESIZE_EW | ffi::XENON_CURSOR_RESIZE_COLUMN => Cursor::ResizeColumn,
        ffi::XENON_CURSOR_RESIZE_NS | ffi::XENON_CURSOR_RESIZE_ROW => Cursor::ResizeRow,
        ffi::XENON_CURSOR_HIDDEN => Cursor::Hidden,
        _ => Cursor::Default,
    }
}
