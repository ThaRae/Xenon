/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Align, Color, El, Icon, Side, col, el, icon, row, text};

use crate::state::Browser;
use crate::updates;
use crate::view::palette::{Palette, srgb};

const PILL_HEIGHT: f32 = 26.0;
const MIN_TOOLBAR_WIDTH_FOR_LABEL: f32 = 640.0;
const POPOVER_WIDTH: f32 = 320.0;
const ROSE_100: u32 = 0xff0050;
const ROSE_300: u32 = 0xe8004a;
const ROSE_500: u32 = 0xcc0042;
const LABEL_FONT: TextStyle = TextStyle::new(12.5, 600.0);
const TITLE_FONT: TextStyle = TextStyle::new(14.0, 600.0);
const BODY_FONT: TextStyle = TextStyle::new(13.0, 400.0);
const BUTTON_FONT: TextStyle = TextStyle::new(13.0, 550.0);

static UPDATE_ARROW: Icon = Icon {
    name: "xenon-update-arrow",
    view: 14.0,
    body: r#"<path d="M7 12.5V2.5M2.5 7L7 2.5L11.5 7" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>"#,
};

pub fn update_button(s: &Browser) -> Option<El> {
    let version = s.updates.offered_version()?;
    let shows_label = s.page_width() >= MIN_TOOLBAR_WIDTH_FOR_LABEL;
    let mut pill = row()
        .key("update-button")
        .role("button")
        .label("Update Xenon")
        .description(format!("Xenon {version} is ready to install"))
        .checked(s.updates.popover_open)
        .h(PILL_HEIGHT)
        .radius(PILL_HEIGHT / 2.0)
        .gap(5.0)
        .center()
        .focusable(false)
        .fill(srgb(ROSE_100))
        .hover(srgb(ROSE_300))
        .press(srgb(ROSE_500))
        .on_press(updates::toggle_popover)
        .child(icon(&UPDATE_ARROW, 14.0).color(Color::WHITE).a11y_hidden());
    pill = if shows_label {
        pill.pl(9.0).pr(11.0).child(
            text("Update")
                .font(LABEL_FONT)
                .color(Color::WHITE)
                .a11y_hidden(),
        )
    } else {
        pill.w(PILL_HEIGHT)
    };
    Some(pill.mx(2.0))
}

fn popover_button(key: &'static str, label: &'static str, primary: bool, p: &Palette) -> El {
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

pub fn update_popover(s: &Browser, p: &Palette) -> Option<El> {
    if !s.updates.popover_open {
        return None;
    }
    let version = s.updates.offered_version()?;
    Some(
        col()
            .key("update-popover")
            .role("dialog")
            .label("Update Xenon")
            .w(POPOVER_WIDTH)
            .px(16.0)
            .py(14.0)
            .gap(10.0)
            .fill(p.surface)
            .radius(10.0)
            .border(p.hairline, p.bubble_border)
            .shadow(8.0, 24.0, Color::BLACK.alpha(0.25))
            .anchored("update-button", Side::Bottom, Align::End, 6.0)
            .modal(true, updates::close_popover)
            .items_stretch()
            .child(
                text(format!("Xenon {version} is ready"))
                    .key("update-title")
                    .font(TITLE_FONT)
                    .color(p.text)
                    .wrap(),
            )
            .child(
                text(updates::popover_body(s.windows.private_window_count()))
                    .key("update-body")
                    .font(BODY_FONT)
                    .color(p.text_muted)
                    .wrap(),
            )
            .child(
                row()
                    .key("update-buttons")
                    .gap(8.0)
                    .center_y()
                    .child(el().grow(1.0).a11y_hidden())
                    .child(
                        popover_button("update-later", "Later", false, p)
                            .description("Hide the Update button until Xenon starts again")
                            .on_press(updates::later),
                    )
                    .child(
                        popover_button("update-relaunch", "Relaunch to Update", true, p)
                            .on_press(updates::relaunch),
                    ),
            ),
    )
}
