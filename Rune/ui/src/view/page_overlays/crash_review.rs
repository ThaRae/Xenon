/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Color, El, ScrollSpec, col, edit_access, el, row, text, text_input};

use crate::crash_reports::{self, CrashReview, Stage};
use crate::engine::ViewId;
use crate::state::Browser;
use crate::view::page_overlays::controls::{
    BODY_FONT, ButtonStyle, CHECK, CHEVRON_DOWN, CHEVRON_RIGHT, SMALL_FONT, TITLE_FONT, button,
    panel_shadow,
};
use crate::view::palette::Palette;

const CARD_WIDTH: f32 = 520.0;
const CODE_FONT: TextStyle = TextStyle::new(12.0, 400.0);

fn field_box(input: El, focused: bool, p: &Palette) -> El {
    let field = col()
        .px(10.0)
        .py(7.0)
        .radius(8.0)
        .items_stretch()
        .child(input);
    if focused {
        field
            .fill(p.field_focused)
            .border(1.5, p.focus_ring)
            .outline(3.0, 0.0, p.focus_glow)
    } else {
        field.fill(p.field).border(p.hairline, p.separator)
    }
}

fn buttons(children: Vec<El>) -> El {
    let mut row = row()
        .key("crash-review-buttons")
        .gap(8.0)
        .center_y()
        .child(el().grow(1.0).a11y_hidden());
    for child in children {
        row = row.child(child);
    }
    row
}

fn review_page(view: ViewId, review: &CrashReview, p: &Palette) -> Vec<El> {
    let mut children = Vec::new();
    children.push(
        text("What were you doing when it crashed? (optional)")
            .font(BODY_FONT)
            .color(p.text),
    );
    let description = text_input(&review.description, BODY_FONT)
        .key("crash-review-description")
        .label("What were you doing when it crashed? (optional)")
        .h(72.0)
        .color(p.text)
        .placeholder(
            "A few words about what you were doing can help us reproduce it.",
            p.text_muted,
        )
        .caret(p.accent, p.selection)
        .on_edit(edit_access(move |s: &mut Browser| {
            crash_reports::description_edit(s, view)
        }));
    children.push(field_box(description, review.description.focused, p));
    if review.website.is_some() {
        let checkbox = row()
            .key("crash-review-include-website")
            .role("checkbox")
            .label("Include this website URL in the report")
            .checked(review.include_website)
            .gap(8.0)
            .center_y()
            .focusable(false)
            .on_press(move |s: &mut Browser| crash_reports::toggle_include_website(s, view))
            .child(
                el().size(16.0, 16.0)
                    .radius(4.0)
                    .center()
                    .fill(if review.include_website {
                        p.accent
                    } else {
                        p.field
                    })
                    .border(p.hairline, p.separator)
                    .children(
                        review
                            .include_website
                            .then(|| rune::icon(&CHECK, 12.0).color(Color::WHITE).a11y_hidden()),
                    ),
            )
            .child(
                text("Include this website URL in the report")
                    .font(BODY_FONT)
                    .color(p.text)
                    .a11y_hidden(),
            );
        children.push(checkbox);
        if review.include_website {
            let website = text_input(&review.website_edit, BODY_FONT)
                .key("crash-review-website")
                .label("Website")
                .single_line()
                .color(p.text)
                .caret(p.accent, p.selection)
                .on_edit(edit_access(move |s: &mut Browser| {
                    crash_reports::website_edit(s, view)
                }));
            children.push(field_box(website, review.website_edit.focused, p));
        }
    }
    children.push(
        text("Includes technical crash details, your Ladybird version and platform.")
            .font(SMALL_FONT)
            .color(p.text_muted)
            .wrap(),
    );
    children.push(
        row()
            .key("crash-review-details-toggle")
            .role("button")
            .label("Report details")
            .checked(review.details_open)
            .gap(4.0)
            .center_y()
            .focusable(false)
            .on_press(move |s: &mut Browser| crash_reports::toggle_details(s, view))
            .child(
                rune::icon(
                    if review.details_open {
                        &CHEVRON_DOWN
                    } else {
                        &CHEVRON_RIGHT
                    },
                    14.0,
                )
                .color(p.icon)
                .a11y_hidden(),
            )
            .child(
                text("Report details")
                    .font(BODY_FONT)
                    .color(p.text)
                    .a11y_hidden(),
            ),
    );
    if review.details_open {
        let mut details = col().key("crash-review-details").gap(4.0).items_stretch();
        for field in &review.fields {
            details = details.child(
                row()
                    .gap(10.0)
                    .child(
                        text(field.label.clone())
                            .font(SMALL_FONT)
                            .color(p.text_muted)
                            .w(140.0),
                    )
                    .child(
                        text(field.value.clone())
                            .font(if field.is_code { CODE_FONT } else { SMALL_FONT })
                            .color(p.text)
                            .wrap_anywhere()
                            .selectable(true)
                            .grow(1.0)
                            .shrink(),
                    ),
            );
        }
        details = details.child(row().child(button(
            "crash-review-view-report",
            "View full report",
            ButtonStyle::Secondary,
            p,
            move |s, _| crash_reports::show_report_text(s, view),
        )));
        children.push(details);
    }
    if let Some(validation) = &review.validation {
        children.push(
            text(validation.clone())
                .key("crash-review-validation")
                .role("alert")
                .font(SMALL_FONT)
                .color(p.accent)
                .wrap(),
        );
    }
    children.push(buttons(vec![
        button(
            "crash-review-decline",
            "Don’t send",
            ButtonStyle::Secondary,
            p,
            move |s, _| crash_reports::decline(s, view),
        ),
        button(
            "crash-review-send",
            "Send report",
            ButtonStyle::Primary,
            p,
            move |s, tx| crash_reports::send(s, view, tx),
        )
        .default_button(),
    ]));
    children
}

fn status_page(view: ViewId, review: &CrashReview, p: &Palette) -> Vec<El> {
    let Some(status) = &review.status else {
        return Vec::new();
    };
    let mut children = vec![
        text(status.title.clone())
            .key("crash-review-status-title")
            .font(TITLE_FONT)
            .color(p.text),
        text(status.message.clone())
            .key("crash-review-status-message")
            .role("status")
            .font(BODY_FONT)
            .color(p.text_muted)
            .wrap(),
    ];
    if let Some(progress) = status.progress {
        children.push(
            el().key("crash-review-progress")
                .role("progressbar")
                .h(4.0)
                .radius(2.0)
                .fill(p.field)
                .w_full()
                .child(
                    el().h(4.0)
                        .radius(2.0)
                        .fill(p.accent)
                        .w_frac(progress.clamp(0.0, 1.0)),
                ),
        );
    }
    let exit_label = review.exit_label.clone();
    let actions = match status.stage {
        Stage::Sending | Stage::Retrying => vec![button(
            "crash-review-cancel",
            "Cancel",
            ButtonStyle::Secondary,
            p,
            move |s, _| crash_reports::cancel(s, view),
        )],
        Stage::FailedRetryable => vec![
            button(
                "crash-review-cancel",
                "Cancel",
                ButtonStyle::Secondary,
                p,
                move |s, _| crash_reports::cancel(s, view),
            ),
            button(
                "crash-review-retry",
                "Try sending again",
                ButtonStyle::Primary,
                p,
                move |s, tx| crash_reports::send(s, view, tx),
            ),
        ],
        Stage::Answered => {
            let mut actions = Vec::new();
            if status.has_next {
                actions.push(button(
                    "crash-review-next",
                    "Review the next crash report",
                    ButtonStyle::Primary,
                    p,
                    move |s, _| crash_reports::next(s, view),
                ));
            }
            actions.push(button(
                "crash-review-exit",
                &exit_label,
                if status.has_next {
                    ButtonStyle::Secondary
                } else {
                    ButtonStyle::Primary
                },
                p,
                move |s, _| crash_reports::exit(s, view),
            ));
            actions
        }
    };
    children.push(buttons(actions));
    children
}

fn report_text_page(view: ViewId, report: &str, p: &Palette) -> Vec<El> {
    vec![
        col()
            .key("crash-review-report-text")
            .max_h(360.0)
            .padding(10.0)
            .radius(8.0)
            .fill(p.field)
            .items_stretch()
            .scroll(ScrollSpec {
                stick_bottom: false,
            })
            .child(
                text(report.to_owned())
                    .font(CODE_FONT)
                    .color(p.text)
                    .wrap_anywhere()
                    .selectable(true),
            ),
        buttons(vec![button(
            "crash-review-back",
            "Back",
            ButtonStyle::Secondary,
            p,
            move |s, _| crash_reports::hide_report_text(s, view),
        )]),
    ]
}

pub fn crash_review(view: ViewId, review: &CrashReview, p: &Palette) -> El {
    let mut card = col()
        .key("crash-review")
        .role("dialog")
        .label("Crash report")
        .w(CARD_WIDTH)
        .shrink()
        .padding(20.0)
        .gap(12.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .items_stretch()
        .child(
            text(review.message.clone())
                .key("crash-review-message")
                .font(TITLE_FONT)
                .color(p.text)
                .wrap(),
        );
    let children = if let Some(report) = &review.report_text {
        report_text_page(view, report, p)
    } else if review.status.is_some() {
        status_page(view, review, p)
    } else {
        review_page(view, review, p)
    };
    for child in children {
        card = card.child(child);
    }
    col()
        .key("crash-review-backdrop")
        .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
        .center()
        .padding(24.0)
        .fill(p.page)
        .on_press(|_: &mut Browser| {})
        .child(panel_shadow(card))
}
