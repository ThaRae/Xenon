/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{El, col, icon, row, text};

use crate::engine::ViewId;
use crate::page_prompts::{self, PermissionAnswer, PermissionPrompt, Tooltip};
use crate::view::page_overlays::controls::{
    BODY_FONT, ButtonStyle, SHIELD, SMALL_FONT, button, panel_shadow,
};
use crate::view::palette::Palette;

const TOOLTIP_MAX_WIDTH: f32 = 360.0;
const TOOLTIP_OFFSET: (f32, f32) = (10.0, 20.0);
const PERMISSION_WIDTH: f32 = 340.0;

pub fn tooltip(tooltip: &Tooltip, p: &Palette) -> El {
    panel_shadow(
        col()
            .key("page-tooltip")
            .role("tooltip")
            .pin(
                Some(tooltip.x + TOOLTIP_OFFSET.0),
                Some(tooltip.y + TOOLTIP_OFFSET.1),
                None,
                None,
            )
            .max_w(TOOLTIP_MAX_WIDTH)
            .px(8.0)
            .py(5.0)
            .radius(6.0)
            .fill(p.bubble)
            .border(p.hairline, p.bubble_border)
            .inert()
            .child(
                text(tooltip.text.clone())
                    .font(SMALL_FONT)
                    .color(p.text)
                    .wrap()
                    .max_lines(12),
            ),
    )
}

pub fn permission_prompts(view: ViewId, prompts: &[PermissionPrompt], p: &Palette) -> El {
    let Some(prompt) = prompts.first() else {
        return col();
    };
    let id = prompt.id;
    let answer = move |answer: PermissionAnswer| {
        move |s: &mut crate::state::Browser, _: &mut rune::Tx| {
            page_prompts::answer_permission(s, view, id, answer)
        }
    };
    let mut buttons = row().key("permission-buttons").gap(8.0).center_y();
    buttons = buttons.child(rune::el().grow(1.0).a11y_hidden());
    if prompt.notice {
        buttons = buttons.child(button(
            "permission-ok",
            "OK",
            ButtonStyle::Primary,
            p,
            answer(PermissionAnswer::Dismiss),
        ));
    } else {
        buttons = buttons
            .child(button(
                "permission-block",
                "Block",
                ButtonStyle::Secondary,
                p,
                answer(PermissionAnswer::Block),
            ))
            .child(button(
                "permission-allow-once",
                "Allow this time",
                ButtonStyle::Secondary,
                p,
                answer(PermissionAnswer::AllowOnce),
            ))
            .child(button(
                "permission-allow",
                "Allow",
                ButtonStyle::Primary,
                p,
                answer(PermissionAnswer::AllowAlways),
            ));
    }
    let heading = row()
        .key("permission-heading")
        .gap(10.0)
        .child(icon(&SHIELD, 18.0).color(p.accent).a11y_hidden())
        .child(
            text(prompt.question())
                .key("permission-question")
                .font(BODY_FONT)
                .color(p.text)
                .wrap()
                .grow(1.0)
                .shrink(),
        );
    let mut bubble = col()
        .key("permission-prompt")
        .role("dialog")
        .label(prompt.question())
        .pin(Some(12.0), Some(8.0), None, None)
        .w(PERMISSION_WIDTH)
        .padding(16.0)
        .gap(14.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .items_stretch()
        .child(heading)
        .child(buttons);
    if prompts.len() > 1 {
        bubble = bubble.child(
            text(format!("{} more requests waiting", prompts.len() - 1))
                .font(SMALL_FONT)
                .color(p.text_muted),
        );
    }
    panel_shadow(bubble)
}
