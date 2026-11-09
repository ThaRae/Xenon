/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{El, Key, edit_access, row, text, text_input};

use crate::element_picker;
use crate::state::Browser;
use crate::view::bookmarks::{dialog_buttons, dialog_panel, field_frame, scrim};
use crate::view::page_overlays::controls::{BODY_FONT, panel_shadow};
use crate::view::palette::Palette;

const MESSAGE_DIALOG_WIDTH: f32 = 440.0;

pub fn element_filter_dialog(s: &Browser, p: &Palette) -> Option<El> {
    let dialog = s.shown_element_filter()?;
    let input = text_input(&dialog.filter, BODY_FONT)
        .key("element-filter-input")
        .label("Filter")
        .single_line()
        .grow(1.0)
        .shrink()
        .color(p.text)
        .caret(p.accent, p.selection)
        .on_edit(edit_access(|s: &mut Browser| s.element_filter_input()))
        .on_input_key(|s: &mut Browser, event, tx| {
            if event.key == Key::Escape {
                s.answer_element_filter(false, tx);
                return true;
            }
            false
        })
        .on_submit(|s: &mut Browser, tx| s.answer_element_filter(true, tx));
    let panel = dialog_panel(
        "element-filter-dialog",
        element_picker::TITLE,
        MESSAGE_DIALOG_WIDTH,
        p,
    )
    .modal(false, |s: &mut Browser| s.cancel_element_filter())
    .child(
        text(element_picker::MESSAGE)
            .key("element-filter-message")
            .font(BODY_FONT)
            .color(p.text)
            .wrap(),
    )
    .child(row().key("element-filter-row").child(field_frame(
        "element-filter-field",
        dialog.filter.focused,
        p,
        input,
    )))
    .child(dialog_buttons(
        "element-filter",
        p,
        Browser::answer_element_filter,
    ));
    Some(scrim("element-filter-scrim", panel_shadow(panel)))
}
