/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ops::Range;

use rune::tok::TextStyle;
use rune::{Align, Color, Cursor, Cx, El, Side, Span, col, el, icon, rich, row};

use crate::location::{Suggestion, SuggestionKind};
use crate::state::Browser;
use crate::view::icons;
use crate::view::location::style::{
    LocationColors, ROW_PRIMARY_FONT, ROW_SECONDARY_FONT, SEARCH, STAR,
};
use crate::view::palette::Palette;

const POPUP_GAP: f32 = 4.0;
const POPUP_PADDING: f32 = 6.0;
const ROW_INSET: f32 = 6.0;
const ROW_HEIGHT: f32 = 36.0;
const ROW_HEIGHT_WITH_TITLE: f32 = 50.0;
const ROW_ICON_SIZE: f32 = 18.0;
const MINIMUM_WIDTH: f32 = 240.0;

const REGULAR: f32 = 400.0;
const MEDIUM: f32 = 500.0;
const DEMIBOLD: f32 = 600.0;

pub fn highlight_spans(text: &str, matches: &[Range<usize>], emphasize_origin: bool) -> Vec<Span> {
    if !matches.is_empty() {
        let mut spans = vec![Span::new(0, text.len()).weight(DEMIBOLD)];
        spans.extend(
            matches
                .iter()
                .map(|range| Span::new(range.start, range.end).weight(REGULAR)),
        );
        return spans;
    }
    if emphasize_origin {
        let origin_end = text.find('/').unwrap_or(text.len());
        if origin_end > 0 {
            return vec![Span::new(0, origin_end).weight(MEDIUM)];
        }
    }
    Vec::new()
}

fn suggestion_icon(suggestion: &Suggestion, p: &Palette) -> El {
    let slot = el()
        .size(ROW_ICON_SIZE, ROW_ICON_SIZE)
        .center()
        .a11y_hidden();
    let shows_favicon = matches!(
        suggestion.kind,
        SuggestionKind::History | SuggestionKind::Bookmark | SuggestionKind::Adaptive
    );
    let slot = match (&suggestion.favicon, suggestion.kind) {
        (_, SuggestionKind::Search) => slot.child(icon(&SEARCH, 16.0).color(p.icon)),
        (Some(favicon), _) if shows_favicon => {
            slot.child(el().size(16.0, 16.0).radius(3.0).pixels(favicon))
        }
        _ => slot.child(icon(&icons::GLOBE, 16.0).color(p.icon)),
    };
    if suggestion.kind == SuggestionKind::Bookmark {
        slot.child(
            el().absolute(ROW_ICON_SIZE - 9.0, ROW_ICON_SIZE - 9.0)
                .size(10.0, 10.0)
                .center()
                .child(icon(&STAR, 9.0).color(p.accent)),
        )
    } else {
        slot
    }
}

fn line(
    text: &str,
    matches: &[Range<usize>],
    emphasize: bool,
    font: TextStyle,
    color: Color,
) -> El {
    rich(text.to_owned(), highlight_spans(text, matches, emphasize))
        .font(font)
        .color(color)
        .truncate()
        .w_full()
}

fn suggestion_row(
    index: usize,
    count: usize,
    suggestion: &Suggestion,
    selected: bool,
    p: &Palette,
    colors: &LocationColors,
) -> El {
    let has_secondary = !suggestion.secondary.is_empty();
    let mut lines = col().grow(1.0).shrink().min_w(0.0).gap(2.0).child(line(
        &suggestion.primary,
        &suggestion.primary_matches,
        suggestion.emphasize_origin && !has_secondary,
        ROW_PRIMARY_FONT,
        p.text,
    ));
    if has_secondary {
        lines = lines.child(line(
            &suggestion.secondary,
            &suggestion.secondary_matches,
            suggestion.emphasize_origin,
            ROW_SECONDARY_FONT,
            p.text_muted,
        ));
    }
    let row = row()
        .key(format!("location-suggestion-{index}"))
        .role("option")
        .label(if has_secondary {
            format!("{}, {}", suggestion.primary, suggestion.secondary)
        } else {
            suggestion.primary.clone()
        })
        .position_in_set(index + 1, count)
        .description(suggestion.text.clone())
        .h(if has_secondary {
            ROW_HEIGHT_WITH_TITLE
        } else {
            ROW_HEIGHT
        })
        .mx(ROW_INSET)
        .px(10.0)
        .gap(10.0)
        .center_y()
        .radius(8.0)
        .cursor(Cursor::Default)
        .focusable(false)
        .hover(colors.row_hover)
        .on_press(move |s: &mut Browser| s.location_suggestion_pressed(index))
        .child(suggestion_icon(suggestion, p))
        .child(lines);
    if selected {
        row.fill(colors.row_selected)
    } else {
        row
    }
}

pub fn suggestion_popup(
    s: &Browser,
    p: &Palette,
    colors: &LocationColors,
    cx: &mut Cx<Browser>,
) -> Option<El> {
    let bar = &s.location;
    if !bar.popup_visible || !bar.edit.focused || bar.suggestions.is_empty() {
        return None;
    }
    let width = cx
        .layout_size("location")
        .map_or(MINIMUM_WIDTH, |[width, _]| width.max(MINIMUM_WIDTH));
    let count = bar.suggestions.len();
    let rows = bar
        .suggestions
        .iter()
        .enumerate()
        .map(|(index, suggestion)| {
            suggestion_row(
                index,
                count,
                suggestion,
                bar.selected == Some(index),
                p,
                colors,
            )
        });
    let popup = col()
        .key("location-popup")
        .role("listbox")
        .label("Suggestions")
        .w(width)
        .py(POPUP_PADDING)
        .fill(colors.popup)
        .radius(14.0)
        .border(p.hairline, colors.popup_border)
        .shadow(8.0, 24.0, colors.shadow)
        .preserve_focus_on_press(true)
        .instant_changes()
        .anchored("location", Side::Bottom, Align::Start, POPUP_GAP)
        .children(rows);
    Some(match bar.selected {
        Some(index) => popup.active_descendant(format!("location-suggestion-{index}")),
        None => popup,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn typed_text_stays_regular_and_the_rest_of_a_suggestion_stands_out() {
        let spans = highlight_spans("ladybird.org", &[0..4], false);
        assert_eq!(spans.len(), 2);
        assert_eq!(
            (spans[0].start, spans[0].end, spans[0].weight),
            (0, 12, Some(DEMIBOLD))
        );
        assert_eq!(
            (spans[1].start, spans[1].end, spans[1].weight),
            (0, 4, Some(REGULAR))
        );
    }

    #[test]
    fn without_a_match_an_address_emphasizes_its_origin() {
        let spans = highlight_spans("example.com/path", &[], true);
        assert_eq!(spans.len(), 1);
        assert_eq!((spans[0].start, spans[0].end), (0, 11));
        assert!(highlight_spans("plain words", &[], false).is_empty());
    }
}
