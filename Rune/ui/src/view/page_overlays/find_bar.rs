/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{El, edit_access, row, text, text_input};

use crate::engine::ViewId;
use crate::page_prompts::{self, FindBar, FindResult};
use crate::state::Browser;
use crate::view::icons;
use crate::view::page_overlays::controls::{
    BODY_FONT, CHEVRON_DOWN, CHEVRON_UP, SMALL_FONT, icon_button, panel_shadow,
};
use crate::view::palette::Palette;

const FIELD_WIDTH: f32 = 220.0;

pub fn find_bar(view: ViewId, find: &FindBar, p: &Palette) -> El {
    let input = text_input(&find.query, BODY_FONT)
        .key("find-input")
        .label("Find in page")
        .single_line()
        .w(FIELD_WIDTH)
        .color(p.text)
        .placeholder("Find in page", p.text_muted)
        .caret(p.accent, p.selection)
        .on_edit(edit_access(move |s: &mut Browser| {
            s.page_prompts.find_query(view)
        }))
        .on_input_key(move |s: &mut Browser, event, tx| page_prompts::find_key(s, view, event, tx));
    let not_found = find.result == Some(FindResult::NotFound);
    let has_matches = matches!(find.result, Some(FindResult::Match { .. }));
    let mut bar = row()
        .key("find-bar")
        .role("search")
        .pin(None, Some(8.0), Some(16.0), None)
        .h(40.0)
        .pl(12.0)
        .pr(6.0)
        .gap(4.0)
        .center_y()
        .radius(10.0)
        .fill(p.surface)
        .border(
            if not_found { 1.5 } else { p.hairline },
            if not_found { p.accent } else { p.bubble_border },
        )
        .child(input);
    if let Some(label) = find.result.and_then(FindResult::label) {
        bar = bar.child(
            text(label)
                .key("find-result")
                .role("status")
                .font(SMALL_FONT)
                .color(if not_found { p.accent } else { p.text_muted })
                .ml(4.0)
                .mr(4.0),
        );
    }
    panel_shadow(
        bar.child(icon_button(
            "find-previous",
            "Previous match",
            &CHEVRON_UP,
            has_matches,
            p,
            move |s, _| page_prompts::find_step(s, view, false),
        ))
        .child(icon_button(
            "find-next",
            "Next match",
            &CHEVRON_DOWN,
            has_matches,
            p,
            move |s, _| page_prompts::find_step(s, view, true),
        ))
        .child(icon_button(
            "find-close",
            "Close find bar",
            &icons::CLOSE,
            true,
            p,
            move |s, tx| page_prompts::close_find(s, view, tx),
        )),
    )
}
