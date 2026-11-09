/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Color, Cursor, El, col, el, row, text};

use crate::state::Browser;
use crate::view::page_overlays::controls::{
    BODY_FONT, ButtonStyle, SMALL_FONT, TITLE_FONT, button, panel_shadow,
};
use crate::view::palette::Palette;

const DIALOG_WIDTH: f32 = 420.0;
const BANNER_HEIGHT: f32 = 34.0;

pub fn devtools_banner(s: &Browser, p: &Palette) -> Option<El> {
    let port = s.window.devtools_port?;
    let label = format!("DevTools is enabled on port {port}");
    Some(
        col()
            .key("devtools-banner")
            .w_full()
            .child(
                el().key("devtools-banner-edge")
                    .w_full()
                    .h(p.hairline)
                    .fill(p.separator)
                    .a11y_hidden(),
            )
            .child(
                row()
                    .key("devtools-banner-row")
                    .role("status")
                    .label(label.clone())
                    .w_full()
                    .h(BANNER_HEIGHT)
                    .px(12.0)
                    .gap(6.0)
                    .center_y()
                    .fill(p.surface)
                    .child(
                        text(label)
                            .font(SMALL_FONT)
                            .color(p.text)
                            .grow(1.0)
                            .shrink()
                            .truncate()
                            .a11y_hidden(),
                    )
                    .child(
                        button(
                            "devtools-open-client",
                            "Open Client",
                            ButtonStyle::Secondary,
                            p,
                            |s, _| s.launch_devtools_client(),
                        )
                        .h(24.0)
                        .px(10.0),
                    )
                    .child(
                        button(
                            "devtools-disable",
                            "Disable",
                            ButtonStyle::Secondary,
                            p,
                            |s, _| s.disable_devtools(),
                        )
                        .h(24.0)
                        .px(10.0),
                    ),
            ),
    )
}

pub fn stop_downloads_dialog(s: &Browser, p: &Palette) -> Option<El> {
    s.window.stop_downloads_prompt?;
    let panel = col()
        .key("stop-downloads-dialog")
        .role("alertdialog")
        .label("Downloads are still in progress.")
        .w(DIALOG_WIDTH)
        .shrink()
        .padding(20.0)
        .gap(12.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .items_stretch()
        .child(
            text("Downloads are still in progress.")
                .font(TITLE_FONT)
                .color(p.text)
                .wrap(),
        )
        .child(
            text("Quitting will cancel downloads that cannot be resumed.")
                .font(BODY_FONT)
                .color(p.text)
                .wrap(),
        )
        .child(
            row()
                .key("stop-downloads-buttons")
                .gap(8.0)
                .center_y()
                .child(el().grow(1.0).a11y_hidden())
                .child(
                    button(
                        "stop-downloads-cancel",
                        "Cancel",
                        ButtonStyle::Secondary,
                        p,
                        |s, _| s.answer_stop_downloads(false),
                    )
                    .default_button(),
                )
                .child(button(
                    "stop-downloads-quit",
                    "Quit and Cancel Downloads",
                    ButtonStyle::Primary,
                    p,
                    |s, _| s.answer_stop_downloads(true),
                )),
        );
    Some(
        col()
            .key("stop-downloads-scrim")
            .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
            .center()
            .padding(24.0)
            .fill(Color::BLACK.alpha(0.3))
            .cursor(Cursor::Default)
            .on_press(|_: &mut Browser| {})
            .on_wheel(|_: &mut Browser, _, _| true)
            .child(panel_shadow(panel)),
    )
}
