/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Align, Color, Cursor, El, SurfaceFit, col, el, row, text};

use crate::screenshots::{self, Answer, ScreenshotSelection};
use crate::state::Browser;
use crate::view::icons;
use crate::view::page_overlays::controls::{
    BODY_FONT, ButtonStyle, button, icon_button, panel_shadow,
};
use crate::view::palette::Palette;

const DIMMING: f32 = 110.0 / 255.0;
const INSTRUCTIONS_MARGIN: f32 = 16.0;

fn dim(left: Option<f32>, top: Option<f32>, right: Option<f32>, bottom: Option<f32>) -> El {
    el().pin(left, top, right, bottom)
        .fill(Color::BLACK.alpha(DIMMING))
        .inert()
        .a11y_hidden()
}

fn instructions(p: &Palette) -> El {
    let card = col()
        .key("screenshot-instructions")
        .role("dialog")
        .label("Take a screenshot")
        .padding(16.0)
        .gap(12.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .cursor(Cursor::Default)
        .on_drag(|_: &mut Browser, _, _| {})
        .child(
            row()
                .gap(8.0)
                .center_y()
                .child(
                    text("Drag to select the part of the page to save.")
                        .key("screenshot-instructions-text")
                        .font(BODY_FONT)
                        .color(p.text)
                        .wrap()
                        .grow(1.0)
                        .shrink(),
                )
                .child(icon_button(
                    "screenshot-cancel",
                    "Close",
                    &icons::CLOSE,
                    true,
                    p,
                    |s, _| screenshots::finish(s, Answer::Cancel),
                )),
        )
        .child(
            row()
                .gap(8.0)
                .center_y()
                .child(el().grow(1.0).a11y_hidden())
                .child(button(
                    "screenshot-visible-page",
                    "Capture Visible Page",
                    ButtonStyle::Secondary,
                    p,
                    |s, _| screenshots::finish(s, Answer::VisiblePage),
                ))
                .child(button(
                    "screenshot-full-page",
                    "Capture Full Page",
                    ButtonStyle::Secondary,
                    p,
                    |s, _| screenshots::finish(s, Answer::FullPage),
                )),
        );
    row()
        .key("screenshot-instructions-row")
        .pin(
            Some(INSTRUCTIONS_MARGIN),
            Some(INSTRUCTIONS_MARGIN),
            Some(INSTRUCTIONS_MARGIN),
            None,
        )
        .justify_center()
        .child(panel_shadow(card))
}

pub fn selection_overlay(selection: &ScreenshotSelection, p: &Palette) -> El {
    let mut overlay = el()
        .key("screenshot-selection")
        .role("application")
        .label("Select the part of the page to save")
        .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
        .cursor(Cursor::Crosshair)
        .on_drag(|s: &mut Browser, drag, _| screenshots::drag(s, drag))
        .on_wheel(|s: &mut Browser, wheel, tx| {
            screenshots::wheel(s, wheel, tx);
            true
        });
    if selection.reveals_page {
        return overlay;
    }
    overlay = overlay.child(
        el().key("screenshot-frame")
            .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
            .pixels_fit(&selection.frame, SurfaceFit::Fill)
            .surface_anchor(Align::Start, Align::Start)
            .surface_unscaled(true)
            .inert()
            .a11y_hidden(),
    );
    overlay = match selection.selection() {
        Some([x, y, width, height]) if width > 0.0 && height > 0.0 => overlay
            .child(dim(Some(0.0), Some(0.0), Some(0.0), None).h(y))
            .child(dim(Some(0.0), Some(y + height), Some(0.0), Some(0.0)))
            .child(dim(Some(0.0), Some(y), None, None).size(x, height))
            .child(dim(Some(x + width), Some(y), Some(0.0), None).h(height))
            .child(
                el().key("screenshot-outline")
                    .pin(Some(x), Some(y), None, None)
                    .size(width, height)
                    .border(2.0, p.accent)
                    .inert()
                    .a11y_hidden(),
            ),
        _ => overlay.child(dim(Some(0.0), Some(0.0), Some(0.0), Some(0.0))),
    };
    if selection.drag.is_none() {
        overlay = overlay.child(instructions(p));
    }
    overlay
}
