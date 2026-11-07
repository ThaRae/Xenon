/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Color, Cursor, El, Key, col, edit_access, row, text, text_input};

use crate::engine::ViewId;
use crate::page_prompts::{self, Dialog, DialogKind};
use crate::state::Browser;
use crate::view::page_overlays::controls::{
    BODY_FONT, ButtonStyle, TITLE_FONT, button, panel_shadow,
};
use crate::view::palette::Palette;

const DIALOG_WIDTH: f32 = 440.0;
const MESSAGE_LINES: usize = 14;

pub fn dialog_sheet(view: ViewId, dialog: &Dialog, p: &Palette) -> El {
    let mut panel = col()
        .key("page-dialog")
        .role("alertdialog")
        .label(dialog.title.clone())
        .w(DIALOG_WIDTH)
        .shrink()
        .padding(20.0)
        .gap(12.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .items_stretch()
        .child(
            text(dialog.title.clone())
                .key("page-dialog-title")
                .font(TITLE_FONT)
                .color(p.text)
                .truncate(),
        );
    if !dialog.message.is_empty() {
        panel = panel.child(
            text(dialog.message.clone())
                .key("page-dialog-message")
                .font(BODY_FONT)
                .color(p.text)
                .wrap()
                .max_lines(MESSAGE_LINES)
                .selectable(true),
        );
    }
    if dialog.kind == DialogKind::Prompt {
        panel = panel.child(prompt_field(view, dialog, p));
    }
    let mut buttons = row().key("page-dialog-buttons").gap(8.0).center_y();
    buttons = buttons.child(rune::el().grow(1.0).a11y_hidden());
    if let Some(cancel_label) = &dialog.cancel_label {
        buttons = buttons.child(button(
            "page-dialog-cancel",
            cancel_label,
            ButtonStyle::Secondary,
            p,
            move |s, tx| page_prompts::answer_dialog(s, view, false, tx),
        ));
    }
    buttons = buttons.child(
        button(
            "page-dialog-accept",
            &dialog.accept_label,
            ButtonStyle::Primary,
            p,
            move |s, tx| page_prompts::answer_dialog(s, view, true, tx),
        )
        .default_button(),
    );
    panel = panel_shadow(panel.child(buttons));

    col()
        .key("page-dialog-scrim")
        .role("none")
        .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
        .center()
        .padding(24.0)
        .fill(Color::BLACK.alpha(0.3))
        .cursor(Cursor::Default)
        .on_press(|_: &mut Browser| {})
        .on_wheel(|_: &mut Browser, _, _| true)
        .child(panel)
}

fn prompt_field(view: ViewId, dialog: &Dialog, p: &Palette) -> El {
    let input = text_input(&dialog.input, BODY_FONT)
        .key("page-dialog-input")
        .label(dialog.message.clone())
        .single_line()
        .grow(1.0)
        .shrink()
        .color(p.text)
        .caret(p.accent, p.selection)
        .on_edit(edit_access(move |s: &mut Browser| {
            s.page_prompts.dialog_input(view)
        }))
        .on_input_key(move |s: &mut Browser, event, tx| {
            if event.key == Key::Escape {
                page_prompts::answer_dialog(s, view, false, tx);
                return true;
            }
            false
        })
        .on_submit(move |s: &mut Browser, tx| page_prompts::answer_dialog(s, view, true, tx));
    let field = row()
        .key("page-dialog-field")
        .h(34.0)
        .px(12.0)
        .center_y()
        .radius(8.0)
        .child(input);
    if dialog.input.focused {
        field
            .fill(p.field_focused)
            .border(1.5, p.focus_ring)
            .outline(3.0, 0.0, p.focus_glow)
    } else {
        field.fill(p.field).border(p.hairline, p.separator)
    }
}
