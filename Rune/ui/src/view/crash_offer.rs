/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Align, Color, El, Side, col, el, row, text};

use crate::crash_reports;
use crate::state::Browser;
use crate::view::palette::Palette;

const POPOVER_WIDTH: f32 = 340.0;
const TITLE_FONT: TextStyle = TextStyle::new(14.0, 600.0);
const BODY_FONT: TextStyle = TextStyle::new(13.0, 400.0);
const BUTTON_FONT: TextStyle = TextStyle::new(13.0, 550.0);

fn popover_button(
    key: &'static str,
    label: &'static str,
    primary: bool,
    p: &Palette,
    action: fn(&mut Browser),
) -> El {
    let button = row()
        .key(key)
        .role("button")
        .label(label)
        .h(30.0)
        .px(14.0)
        .radius(8.0)
        .center()
        .focusable(false)
        .on_press(action)
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

pub fn crash_report_popover(s: &Browser, p: &Palette) -> Option<El> {
    s.crash_report_offer.as_ref()?;
    Some(
        col()
            .key("crash-report-popover")
            .role("dialog")
            .label("Xenon hit a snag!")
            .w(POPOVER_WIDTH)
            .px(16.0)
            .py(14.0)
            .gap(10.0)
            .fill(p.surface)
            .radius(10.0)
            .border(p.hairline, p.bubble_border)
            .shadow(8.0, 24.0, Color::BLACK.alpha(0.25))
            .anchored("menu-button", Side::Bottom, Align::End, 6.0)
            .modal(true, crash_reports::dismiss_offer)
            .items_stretch()
            .child(text("Xenon hit a snag!").font(TITLE_FONT).color(p.text))
            .child(
                text("Part of Ladybird stopped unexpectedly. You can send us a crash report to help fix it.")
                    .font(BODY_FONT)
                    .color(p.text_muted)
                    .wrap(),
            )
            .child(
                row()
                    .gap(8.0)
                    .center_y()
                    .child(el().grow(1.0).a11y_hidden())
                    .child(popover_button(
                        "crash-report-dismiss",
                        "Dismiss",
                        false,
                        p,
                        crash_reports::dismiss_offer,
                    ))
                    .child(popover_button(
                        "crash-report-review",
                        "Review report",
                        true,
                        p,
                        crash_reports::review_offer,
                    )),
            ),
    )
}
