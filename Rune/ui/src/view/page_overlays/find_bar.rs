/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{El, Icon, edit_access, icon, row, text, text_input};

use crate::engine::ViewId;
use crate::page_prompts::{self, FindBar, FindResult};
use crate::state::Browser;
use crate::view::icons;
use crate::view::page_overlays::controls::{
    BODY_FONT, CHEVRON_DOWN, CHEVRON_UP, SMALL_FONT, icon_button, panel_shadow,
};
use crate::view::palette::Palette;

const FIELD_WIDTH: f32 = 220.0;

static HIGHLIGHT: Icon = Icon {
    name: "xenon-highlight-all",
    view: 24.0,
    body: r#"<path d="M9 11l-4 4v4h4l4-4M9 11l6-6 4 4-6 6M9 11l4 4M4 21h16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
};

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
    let match_case = row()
        .key("find-match-case")
        .role("button")
        .label("Match case")
        .checked(find.case_sensitive)
        .h(28.0)
        .px(7.0)
        .radius(6.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(move |s: &mut Browser| page_prompts::toggle_find_case_sensitive(s, view))
        .child(
            text("Aa")
                .font(SMALL_FONT)
                .color(if find.case_sensitive {
                    p.accent
                } else {
                    p.text_muted
                })
                .a11y_hidden(),
        );
    let highlight_all = row()
        .key("find-highlight-all")
        .role("button")
        .label("Highlight all")
        .checked(find.highlight_all)
        .h(28.0)
        .px(6.0)
        .radius(6.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(move |s: &mut Browser| page_prompts::toggle_find_highlight_all(s, view))
        .child(
            icon(&HIGHLIGHT, 16.0)
                .color(if find.highlight_all {
                    p.accent
                } else {
                    p.text_muted
                })
                .a11y_hidden(),
        );
    let bar = bar
        .child(if find.highlight_all {
            highlight_all.fill(p.hover)
        } else {
            highlight_all
        })
        .child(if find.case_sensitive {
            match_case.fill(p.hover)
        } else {
            match_case
        });
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
