/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Color, Cursor, Cx, El, Span, el, icon, row, text, text_input};

use crate::engine::Action;
use crate::location::{BangChip, LeadingIndicator, LocationBar};
use crate::state::Browser;
use crate::view::icons;
use crate::view::location::style::{LOCATION_FONT, LOCK, LocationColors, PILL_FONT, SEARCH};
use crate::view::palette::Palette;

const FIELD_HEIGHT: f32 = 32.0;
const FIELD_PADDING: f32 = 14.0;
const INDICATOR_PADDING: f32 = 6.0;
const INDICATOR_SIZE: f32 = 24.0;
const PILL_HEIGHT: f32 = 22.0;
const PILL_PADDING: f32 = 8.0;
const CHIP_LEFT: f32 = 8.0;
const CHIP_GAP: f32 = 6.0;
const CHIP_MAX_TEXT_WIDTH: f32 = 160.0;

struct ChipLayout {
    chip: BangChip,
    width: f32,
    text_margin: f32,
}

fn chip_layout(bar: &LocationBar, field_width: Option<f32>) -> Option<ChipLayout> {
    let chip = bar.bang_chip()?;
    let text = &bar.edit.text;
    let prefix_width = rune::text::advance(&text[..chip.prefix_length], LOCATION_FONT);
    let name_width = rune::text::advance(&chip.name, PILL_FONT).min(CHIP_MAX_TEXT_WIDTH);
    let width = (name_width + PILL_PADDING * 2.0).max(prefix_width).ceil();
    let text_margin = (CHIP_LEFT + width + CHIP_GAP - prefix_width - FIELD_PADDING).max(0.0);
    let text_width = rune::text::advance(text, LOCATION_FONT);
    let fits = field_width
        .is_none_or(|field| FIELD_PADDING + text_margin + text_width <= field - FIELD_PADDING);
    fits.then_some(ChipLayout {
        chip,
        width,
        text_margin,
    })
}

fn text_spans(bar: &LocationBar, colors: &LocationColors, chip: Option<&ChipLayout>) -> Vec<Span> {
    let mut spans = Vec::new();
    let length = bar.edit.text.len();
    if let Some(emphasis) = bar.emphasis() {
        if emphasis.start > 0 {
            spans.push(Span::new(0, emphasis.start).color(colors.dimmed_text));
        }
        if emphasis.end < length {
            spans.push(Span::new(emphasis.end, length).color(colors.dimmed_text));
        }
    }
    if let Some(chip) = chip {
        spans.push(Span::new(0, chip.chip.prefix_length).color(Color::TRANSPARENT));
    }
    spans
}

fn indicator(
    indicator: LeadingIndicator,
    site_info_open: bool,
    p: &Palette,
    colors: &LocationColors,
) -> El {
    let glyph = match indicator {
        LeadingIndicator::NotSecure => {
            return row()
                .key("location-security")
                .role("button")
                .label("Not secure")
                .help("Not secure")
                .expanded(site_info_open)
                .h(PILL_HEIGHT)
                .px(PILL_PADDING)
                .mr(INDICATOR_PADDING)
                .center_y()
                .radius(PILL_HEIGHT / 2.0)
                .fill(colors.not_secure_fill)
                .border(1.0, colors.not_secure_border)
                .hover(colors.not_secure_hover)
                .cursor(Cursor::Default)
                .focusable(false)
                .on_press(|s: &mut Browser| s.location.toggle_site_info())
                .child(
                    text("Not secure")
                        .font(PILL_FONT)
                        .color(colors.not_secure_text)
                        .a11y_hidden(),
                );
        }
        LeadingIndicator::Lock => &LOCK,
        LeadingIndicator::Globe => &icons::GLOBE,
        LeadingIndicator::Search => &SEARCH,
    };
    let button = el()
        .size(INDICATOR_SIZE, INDICATOR_SIZE)
        .mr(INDICATOR_PADDING - 2.0)
        .radius(INDICATOR_SIZE / 2.0)
        .center()
        .focusable(false)
        .child(icon(glyph, 16.0).color(p.icon).a11y_hidden());
    if indicator.opens_site_information() {
        button
            .key("location-security")
            .role("button")
            .label("View site information")
            .help("View site information")
            .expanded(site_info_open)
            .hover(p.hover)
            .press(p.press)
            .cursor(Cursor::Default)
            .on_press(|s: &mut Browser| s.location.toggle_site_info())
    } else {
        button.key("location-kind").a11y_hidden()
    }
}

fn bang_chip(layout: &ChipLayout, colors: &LocationColors) -> El {
    el().key("location-bang")
        .role("status")
        .label(format!("Search {}", layout.chip.name))
        .absolute(CHIP_LEFT, (FIELD_HEIGHT - PILL_HEIGHT) / 2.0)
        .w(layout.width)
        .h(PILL_HEIGHT)
        .px(PILL_PADDING)
        .radius(PILL_HEIGHT / 2.0)
        .fill(colors.bang_fill)
        .border(1.0, colors.bang_border)
        .center()
        .child(
            text(layout.chip.name.clone())
                .font(PILL_FONT)
                .color(colors.bang_text)
                .truncate(),
        )
}

pub fn location_field(s: &Browser, p: &Palette, dark: bool, cx: &mut Cx<Browser>) -> El {
    let bar = &s.location;
    let colors = LocationColors::new(dark, p);
    let focused = bar.edit.focused;
    let field_width = cx.layout_size("location").map(|[width, _]| width);
    let chip = chip_layout(bar, field_width);
    let leading = if chip.is_some() {
        None
    } else {
        bar.leading_indicator()
    };

    let mut input = text_input(&bar.edit, LOCATION_FONT)
        .key("location-input")
        .label("Address")
        .single_line()
        .grow(1.0)
        .shrink()
        .color(p.text)
        .placeholder(bar.placeholder(), p.text_muted)
        .caret(p.accent, p.selection)
        .spans(text_spans(bar, &colors, chip.as_ref()))
        .on_edit(|s: &mut Browser| &mut s.location.edit)
        .on_input_key(|s: &mut Browser, event, tx| s.location_key(event, tx))
        .on_submit(|s: &mut Browser, tx| s.submit_location(tx));
    if let Some(chip) = &chip {
        input = input.ml(chip.text_margin);
    }

    let mut field = row()
        .key("location")
        .grow(1.0)
        .shrink()
        .h(FIELD_HEIGHT)
        .ml(6.0)
        .mr(4.0)
        .pl(if leading.is_some() {
            INDICATOR_PADDING
        } else {
            FIELD_PADDING
        })
        .pr(FIELD_PADDING)
        .center_y()
        .radius(FIELD_HEIGHT / 2.0)
        .on_secondary_press(|s: &mut Browser, at, tx| s.show_location_menu(at, tx));
    if let Some(leading) = leading {
        field = field.child(indicator(leading, bar.site_info.is_some(), p, &colors));
    }
    field = field.child(input);
    if let Some(chip) = &chip {
        field = field.child(bang_chip(chip, &colors));
    }
    if let Some(tab) = s.tabs.active() {
        if tab.zoom_percent != 100 {
            field = field.child(zoom_indicator(tab.zoom_percent, p));
        }
        if !focused {
            field = field.child(bookmark_star(tab.bookmarked, p));
        }
    }
    if focused {
        field
            .fill(p.field_focused)
            .border(1.5, p.focus_ring)
            .outline(3.0, 0.0, p.focus_glow)
    } else {
        field.fill(p.field).border(p.hairline, Color::TRANSPARENT)
    }
}

fn zoom_indicator(percent: u32, p: &Palette) -> El {
    let label = format!("{percent}%");
    row()
        .key("location-zoom")
        .role("button")
        .label(format!("Zoom {label}, reset"))
        .h(24.0)
        .px(8.0)
        .ml(4.0)
        .radius(12.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(|s: &mut Browser| s.activate_action(Action::ResetZoom))
        .child(
            text(label)
                .font(rune::tok::TextStyle::new(12.0, 500.0))
                .color(p.text_muted)
                .a11y_hidden(),
        )
}

fn bookmark_star(bookmarked: bool, p: &Palette) -> El {
    el().key("location-bookmark")
        .role("button")
        .label(if bookmarked {
            "Edit bookmark"
        } else {
            "Bookmark this page"
        })
        .checked(bookmarked)
        .size(28.0, 28.0)
        .ml(2.0)
        .radius(14.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(|s: &mut Browser| s.activate_action(Action::ToggleBookmark))
        .child(
            icon(
                if bookmarked {
                    &icons::STAR_FILLED
                } else {
                    &icons::STAR
                },
                16.0,
            )
            .color(if bookmarked { p.accent } else { p.icon })
            .a11y_hidden(),
        )
}
