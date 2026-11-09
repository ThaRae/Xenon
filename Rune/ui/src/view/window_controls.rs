/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Color, Cursor, El, Icon, Phase, el, icon, row};

use crate::state::Browser;
use crate::view::icons;
use crate::view::palette::Palette;

const BUTTON_SIZE: f32 = 40.0;
const ICON_SIZE: f32 = 18.0;
const TOP_MARGIN: f32 = 2.0;
const RIGHT_MARGIN: f32 = 4.0;
const RESIZE_EDGE: f32 = 6.0;
pub const OUTLINE_WIDTH: f32 = 1.0;
const RESIZE_CORNER: f32 = 12.0;

pub const fn draws_window_frame() -> bool {
    cfg!(not(target_os = "macos"))
}

pub fn window_controls(s: &Browser, p: &Palette) -> Option<El> {
    controls(s, p).map(|controls| controls.mt(TOP_MARGIN).mr(RIGHT_MARGIN))
}

pub fn toolbar_window_controls(s: &Browser, p: &Palette, takes_spare_width: bool) -> Option<El> {
    controls(s, p).map(|controls| {
        if takes_spare_width {
            controls.grow(1.0).justify_end()
        } else {
            controls
        }
    })
}

fn controls(s: &Browser, p: &Palette) -> Option<El> {
    if !draws_window_frame() {
        return None;
    }
    let maximized = s.window_is_maximized();
    let (maximize_label, maximize_icon) = if maximized {
        ("Restore", &icons::WINDOW_RESTORE)
    } else {
        ("Maximize", &icons::WINDOW_MAXIMIZE)
    };
    Some(
        row()
            .key("window-controls")
            .role("group")
            .label("Window")
            .child(
                control(
                    "window-minimize",
                    "Minimize",
                    &icons::WINDOW_MINIMIZE,
                    p.window_control_hover,
                    p,
                )
                .on_press(|s: &mut Browser| s.minimize_window()),
            )
            .child(
                control(
                    "window-maximize",
                    maximize_label,
                    maximize_icon,
                    p.window_control_hover,
                    p,
                )
                .on_press(|s: &mut Browser| s.toggle_window_maximized()),
            )
            .child(
                control(
                    "window-close",
                    "Close",
                    &icons::WINDOW_CLOSE,
                    p.window_close_hover,
                    p,
                )
                .press(p.window_close_hover)
                .on_press(|s: &mut Browser| s.request_close_shown_window()),
            ),
    )
}

fn control(
    key: &'static str,
    label: &'static str,
    glyph: &'static Icon,
    hover: Color,
    p: &Palette,
) -> El {
    el().key(key)
        .role("button")
        .label(label)
        .size(BUTTON_SIZE, BUTTON_SIZE)
        .center()
        .focusable(false)
        .hover(hover)
        .press(p.window_control_press)
        .child(icon(glyph, ICON_SIZE).color(p.icon).a11y_hidden())
}

pub fn window_outline(s: &Browser, p: &Palette) -> Option<El> {
    (draws_window_frame() && !s.window_is_maximized()).then(|| {
        el().key("window-outline")
            .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
            .border(OUTLINE_WIDTH, p.window_outline)
            .inert()
            .a11y_hidden()
    })
}

pub fn resize_edges(s: &Browser) -> Vec<El> {
    if !draws_window_frame() || s.window_is_maximized() {
        return Vec::new();
    }
    let edge = |key: &'static str, direction: (i8, i8)| {
        let (horizontal, vertical) = direction;
        let corner = horizontal != 0 && vertical != 0;
        let along = if corner { 0.0 } else { RESIZE_CORNER };
        let across = if corner { RESIZE_CORNER } else { RESIZE_EDGE };
        let handle = el()
            .key(key)
            .pin(
                (horizontal <= 0).then_some(if horizontal == 0 { along } else { 0.0 }),
                (vertical <= 0).then_some(if vertical == 0 { along } else { 0.0 }),
                (horizontal >= 0).then_some(if horizontal == 0 { along } else { 0.0 }),
                (vertical >= 0).then_some(if vertical == 0 { along } else { 0.0 }),
            )
            .cursor(if vertical == 0 {
                Cursor::ResizeColumn
            } else {
                Cursor::ResizeRow
            })
            .focusable(false)
            .a11y_hidden()
            .on_drag(move |s: &mut Browser, drag, _| {
                if drag.phase == Phase::Down {
                    s.resize_window_from(direction);
                }
            });
        let handle = if horizontal != 0 {
            handle.w(across)
        } else {
            handle
        };
        if vertical != 0 {
            handle.h(across)
        } else {
            handle
        }
    };
    vec![
        edge("resize-top", (0, -1)),
        edge("resize-bottom", (0, 1)),
        edge("resize-left", (-1, 0)),
        edge("resize-right", (1, 0)),
        edge("resize-top-left", (-1, -1)),
        edge("resize-top-right", (1, -1)),
        edge("resize-bottom-left", (-1, 1)),
        edge("resize-bottom-right", (1, 1)),
    ]
}
