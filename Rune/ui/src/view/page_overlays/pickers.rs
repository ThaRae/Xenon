/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::f32::consts::FRAC_PI_2;

use rune::{Color, Cursor, El, Key, col, edit_access, el, row, text, text_input};

use crate::engine::ViewId;
use crate::pickers::{
    self, BASIC_COLORS, ColorPicker, DateKind, DatePicker, Day, Hsv, MONTH_NAMES, WebAuthn,
    WebAuthnState, days_in_month,
};
use crate::state::Browser;
use crate::view::page_overlays::controls::{
    BODY_FONT, ButtonStyle, CHEVRON_DOWN, CHEVRON_UP, SMALL_FONT, TITLE_FONT, button, icon_button,
    panel_shadow,
};
use crate::view::palette::{Palette, srgb};

const COLOR_PANEL_WIDTH: f32 = 300.0;
const SATURATION_HEIGHT: f32 = 160.0;
const HUE_HEIGHT: f32 = 14.0;
const MARKER: f32 = 12.0;
const SWATCH: f32 = 22.0;
const DATE_PANEL_WIDTH: f32 = 300.0;
const DAY_CELL: f32 = 34.0;
const WEB_AUTHN_WIDTH: f32 = 360.0;
const WEEKDAYS: [&str; 7] = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"];

fn scrim(key: &'static str, panel: El) -> El {
    col()
        .key(key)
        .role("none")
        .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
        .center()
        .padding(24.0)
        .fill(Color::BLACK.alpha(0.3))
        .cursor(Cursor::Default)
        .on_press(|_: &mut Browser| {})
        .on_wheel(|_: &mut Browser, _, _| true)
        .child(panel_shadow(panel))
}

fn panel(key: &'static str, label: &str, width: f32, p: &Palette) -> El {
    col()
        .key(key)
        .role("dialog")
        .label(label)
        .w(width)
        .shrink()
        .padding(18.0)
        .gap(12.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .items_stretch()
}

fn marker(x: f32, y: f32, p: &Palette) -> El {
    el().pin(Some(x - MARKER / 2.0), Some(y - MARKER / 2.0), None, None)
        .size(MARKER, MARKER)
        .radius(MARKER / 2.0)
        .border(2.0, Color::WHITE)
        .outline(1.0, 0.0, p.separator)
        .inert()
        .a11y_hidden()
}

fn field(input: El, focused: bool, p: &Palette) -> El {
    let field = row().h(32.0).px(10.0).center_y().radius(8.0).child(input);
    if focused {
        field
            .fill(p.field_focused)
            .border(1.5, p.focus_ring)
            .outline(3.0, 0.0, p.focus_glow)
    } else {
        field.fill(p.field).border(p.hairline, p.separator)
    }
}

fn button_row(key: &'static str) -> El {
    row()
        .key(key)
        .gap(8.0)
        .center_y()
        .child(el().grow(1.0).a11y_hidden())
}

pub fn color_picker(view: ViewId, picker: &ColorPicker, p: &Palette) -> El {
    let hsv = picker.hsv;
    let hue_color = srgb(
        Hsv {
            hue: hsv.hue,
            saturation: 1.0,
            value: 1.0,
        }
        .to_rgb(),
    );
    let width = COLOR_PANEL_WIDTH - 36.0;
    let saturation_value = el()
        .key("color-saturation-value")
        .role("slider")
        .label("Saturation and brightness")
        .size(width, SATURATION_HEIGHT)
        .radius(8.0)
        .fill(hue_color)
        .cursor(Cursor::Crosshair)
        .on_drag(move |s: &mut Browser, drag, _| {
            let saturation = drag.x / drag.w.max(1.0);
            let value = 1.0 - drag.y / drag.h.max(1.0);
            if let Some(hue) = s
                .page_prompts
                .of(view)
                .and_then(|prompts| prompts.color_picker.as_ref())
                .map(|picker| picker.hsv.hue)
            {
                pickers::set_color_hsv(
                    s,
                    view,
                    Hsv {
                        hue,
                        saturation,
                        value,
                    },
                );
            }
        })
        .child(
            el().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
                .radius(8.0)
                .linear_gradient(
                    FRAC_PI_2,
                    [(0.0, Color::WHITE), (1.0, Color::WHITE.alpha(0.0))],
                )
                .inert()
                .a11y_hidden(),
        )
        .child(
            el().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
                .radius(8.0)
                .gradient(Color::BLACK.alpha(0.0), Color::BLACK)
                .inert()
                .a11y_hidden(),
        )
        .child(marker(
            hsv.saturation * width,
            (1.0 - hsv.value) * SATURATION_HEIGHT,
            p,
        ));
    let hue_stops: Vec<(f32, Color)> = (0..=6)
        .map(|step| {
            (
                step as f32 / 6.0,
                srgb(
                    Hsv {
                        hue: (step as f32 * 60.0).min(359.999),
                        saturation: 1.0,
                        value: 1.0,
                    }
                    .to_rgb(),
                ),
            )
        })
        .collect();
    let hue = el()
        .key("color-hue")
        .role("slider")
        .label("Hue")
        .size(width, HUE_HEIGHT)
        .radius(HUE_HEIGHT / 2.0)
        .linear_gradient(FRAC_PI_2, hue_stops)
        .cursor(Cursor::Pointer)
        .on_drag(move |s: &mut Browser, drag, _| {
            if let Some(current) = s
                .page_prompts
                .of(view)
                .and_then(|prompts| prompts.color_picker.as_ref())
                .map(|picker| picker.hsv)
            {
                pickers::set_color_hsv(
                    s,
                    view,
                    Hsv {
                        hue: drag.x / drag.w.max(1.0) * 360.0,
                        ..current
                    },
                );
            }
        })
        .child(marker(hsv.hue / 360.0 * width, HUE_HEIGHT / 2.0, p));
    let mut swatches = row().key("color-swatches").gap(4.0).flex_wrap();
    for rgb in BASIC_COLORS {
        swatches = swatches.child(
            el().key(format!("color-swatch-{rgb:06x}"))
                .role("button")
                .label(pickers::hex_text(rgb))
                .size(SWATCH, SWATCH)
                .radius(5.0)
                .fill(srgb(rgb))
                .border(p.hairline, p.separator)
                .focusable(false)
                .press_scale(0.92)
                .on_press(move |s: &mut Browser| pickers::set_color_rgb(s, view, rgb)),
        );
    }
    let hex_input = text_input(&picker.hex, BODY_FONT)
        .key("color-hex")
        .label("Hex colour")
        .single_line()
        .grow(1.0)
        .shrink()
        .color(p.text)
        .caret(p.accent, p.selection)
        .on_edit(edit_access(move |s: &mut Browser| {
            pickers::color_hex_edit(s, view)
        }))
        .on_input_key(move |s: &mut Browser, event, tx| {
            if event.key == Key::Escape {
                pickers::close_color_picker(s, view, false, tx);
                return true;
            }
            false
        })
        .on_submit(move |s: &mut Browser, tx| pickers::close_color_picker(s, view, true, tx));
    let preview = row()
        .key("color-preview-row")
        .gap(10.0)
        .center_y()
        .child(
            el().key("color-preview")
                .size(32.0, 32.0)
                .radius(8.0)
                .fill(srgb(picker.rgb()))
                .border(p.hairline, p.separator)
                .a11y_hidden(),
        )
        .child(field(hex_input, picker.hex.focused, p).grow(1.0));
    let panel = panel("color-picker", "Choose a colour", COLOR_PANEL_WIDTH, p)
        .child(text("Choose a colour").font(TITLE_FONT).color(p.text))
        .child(saturation_value)
        .child(hue)
        .child(preview)
        .child(swatches)
        .child(
            button_row("color-picker-buttons")
                .child(button(
                    "color-picker-cancel",
                    "Cancel",
                    ButtonStyle::Secondary,
                    p,
                    move |s, tx| pickers::close_color_picker(s, view, false, tx),
                ))
                .child(
                    button(
                        "color-picker-ok",
                        "OK",
                        ButtonStyle::Primary,
                        p,
                        move |s, tx| pickers::close_color_picker(s, view, true, tx),
                    )
                    .default_button(),
                ),
        );
    scrim("color-picker-scrim", panel)
}

fn calendar_cell(
    key: String,
    label: String,
    selected: bool,
    in_month: bool,
    enabled: bool,
    today: bool,
    p: &Palette,
) -> El {
    let ink = if selected {
        Color::WHITE
    } else if !enabled {
        p.icon_disabled
    } else if in_month {
        p.text
    } else {
        p.text_muted
    };
    let mut cell = row()
        .key(key)
        .role("button")
        .label(label.clone())
        .checked(selected)
        .h(DAY_CELL)
        .grow(1.0)
        .radius(8.0)
        .center()
        .focusable(false)
        .child(text(label).font(BODY_FONT).color(ink).a11y_hidden());
    if selected {
        cell = cell.fill(p.accent);
    } else if today {
        cell = cell.border(1.0, p.accent);
    }
    if enabled {
        cell.hover(p.hover).press(p.press)
    } else {
        cell.disabled(true)
    }
}

fn month_grid(view: ViewId, picker: &DatePicker, p: &Palette) -> El {
    let today = Day::today();
    let mut grid = col().key("date-months").gap(4.0).items_stretch();
    for quarter in 0..4u32 {
        let mut line = row().gap(4.0);
        for column in 0..3u32 {
            let month = quarter * 3 + column + 1;
            let day = Day {
                year: picker.shown_year,
                month,
                day: 1,
            };
            let selected = picker
                .selected
                .is_some_and(|chosen| chosen.year == day.year && chosen.month == month);
            let enabled = picker.day_allowed(day);
            let cell = calendar_cell(
                format!("date-month-{month}"),
                MONTH_NAMES[month as usize - 1][..3].to_owned(),
                selected,
                true,
                enabled,
                today.year == day.year && today.month == month,
                p,
            );
            line = line.child(if enabled {
                cell.on_press_tx(move |s: &mut Browser, tx| pickers::pick_day(s, view, day, tx))
            } else {
                cell
            });
        }
        grid = grid.child(line);
    }
    grid
}

fn day_grid(view: ViewId, picker: &DatePicker, p: &Palette) -> El {
    let today = Day::today();
    let first = Day {
        year: picker.shown_year,
        month: picker.shown_month,
        day: 1,
    };
    let start = first.plus_days(-(first.weekday_from_monday() as i64));
    let rows = (first.weekday_from_monday() + days_in_month(first.year, first.month)).div_ceil(7);
    let mut header = row().key("date-weekdays").gap(2.0);
    for weekday in WEEKDAYS {
        header = header.child(
            row()
                .grow(1.0)
                .center()
                .child(text(weekday).font(SMALL_FONT).color(p.text_muted)),
        );
    }
    let mut grid = col()
        .key("date-days")
        .gap(2.0)
        .items_stretch()
        .child(header);
    for week in 0..rows {
        let mut line = row().gap(2.0);
        let week_start = start.plus_days(week as i64 * 7);
        let week_selected = picker.kind == DateKind::Week
            && picker
                .selected
                .is_some_and(|chosen| chosen.iso_week() == week_start.iso_week());
        if week_selected {
            line = line.fill(p.accent.alpha(0.16)).radius(8.0);
        }
        for offset in 0..7 {
            let day = week_start.plus_days(offset);
            let selected = picker.kind != DateKind::Week && picker.selected == Some(day);
            let enabled = picker.day_allowed(day);
            let cell = calendar_cell(
                format!("date-day-{}", pickers::format_day(day)),
                day.day.to_string(),
                selected,
                day.month == first.month,
                enabled,
                day == today,
                p,
            );
            line = line.child(if enabled {
                cell.on_press_tx(move |s: &mut Browser, tx| pickers::pick_day(s, view, day, tx))
            } else {
                cell
            });
        }
        grid = grid.child(line);
    }
    grid
}

pub fn date_picker(view: ViewId, picker: &DatePicker, p: &Palette) -> El {
    let mut panel = panel("date-picker", picker.kind.title(), DATE_PANEL_WIDTH, p)
        .child(text(picker.kind.title()).font(TITLE_FONT).color(p.text));
    if picker.kind.has_calendar() {
        let heading = if picker.kind == DateKind::Month {
            picker.shown_year.to_string()
        } else {
            format!(
                "{} {}",
                MONTH_NAMES[picker.shown_month as usize - 1],
                picker.shown_year
            )
        };
        let (previous_label, next_label) = if picker.kind == DateKind::Month {
            ("Previous year", "Next year")
        } else {
            ("Previous month", "Next month")
        };
        panel = panel.child(
            row()
                .key("date-heading")
                .gap(4.0)
                .center_y()
                .child(
                    text(heading)
                        .key("date-heading-label")
                        .font(BODY_FONT)
                        .color(p.text)
                        .grow(1.0),
                )
                .child(icon_button(
                    "date-previous",
                    previous_label,
                    &CHEVRON_UP,
                    true,
                    p,
                    move |s, _| pickers::show_other_month(s, view, -1),
                ))
                .child(icon_button(
                    "date-next",
                    next_label,
                    &CHEVRON_DOWN,
                    true,
                    p,
                    move |s, _| pickers::show_other_month(s, view, 1),
                )),
        );
        panel = panel.child(if picker.kind == DateKind::Month {
            month_grid(view, picker, p)
        } else {
            day_grid(view, picker, p)
        });
    }
    if picker.kind.has_time() {
        let input = text_input(&picker.time, BODY_FONT)
            .key("date-time")
            .label("Time")
            .single_line()
            .grow(1.0)
            .shrink()
            .color(p.text)
            .placeholder("HH:MM", p.text_muted)
            .caret(p.accent, p.selection)
            .on_edit(edit_access(move |s: &mut Browser| {
                pickers::date_time_edit(s, view)
            }))
            .on_input_key(move |s: &mut Browser, event, tx| {
                if event.key == Key::Escape {
                    pickers::close_date_picker(s, view, false, tx);
                    return true;
                }
                false
            })
            .on_submit(move |s: &mut Browser, tx| pickers::close_date_picker(s, view, true, tx));
        panel = panel.child(
            row()
                .key("date-time-row")
                .gap(10.0)
                .center_y()
                .child(text("Time").font(BODY_FONT).color(p.text_muted))
                .child(field(input, picker.time.focused, p).grow(1.0)),
        );
    }
    let mut buttons = button_row("date-picker-buttons")
        .child(button(
            "date-picker-clear",
            "Clear",
            ButtonStyle::Secondary,
            p,
            move |s, tx| pickers::clear_date_picker(s, view, tx),
        ))
        .child(button(
            "date-picker-cancel",
            "Cancel",
            ButtonStyle::Secondary,
            p,
            move |s, tx| pickers::close_date_picker(s, view, false, tx),
        ));
    if picker.kind.has_time() {
        buttons = buttons.child(
            button(
                "date-picker-ok",
                "OK",
                ButtonStyle::Primary,
                p,
                move |s, tx| pickers::close_date_picker(s, view, true, tx),
            )
            .default_button(),
        );
    }
    scrim("date-picker-scrim", panel.child(buttons))
}

pub fn web_authn(view: ViewId, prompt: &WebAuthn, p: &Palette) -> El {
    let title = prompt.title();
    let mut card = col()
        .key("web-authn")
        .role("dialog")
        .label(title.clone())
        .w(WEB_AUTHN_WIDTH)
        .padding(16.0)
        .gap(10.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .items_stretch()
        .child(
            text(title)
                .key("web-authn-title")
                .font(TITLE_FONT)
                .color(p.text)
                .wrap(),
        );
    if !prompt.caller.is_empty() {
        card = card.child(
            text(prompt.caller.clone())
                .key("web-authn-caller")
                .font(SMALL_FONT)
                .color(p.text_muted)
                .wrap(),
        );
    }
    card = card.child(
        text(prompt.body())
            .key("web-authn-body")
            .font(BODY_FONT)
            .color(p.text)
            .wrap(),
    );
    if prompt.state == WebAuthnState::EnterPin {
        let input = text_input(&prompt.pin, BODY_FONT)
            .key("web-authn-pin")
            .label("PIN")
            .single_line()
            .secure()
            .grow(1.0)
            .shrink()
            .color(p.text)
            .placeholder("PIN", p.text_muted)
            .caret(p.accent, p.selection)
            .on_edit(edit_access(move |s: &mut Browser| {
                pickers::web_authn_pin(s, view)
            }))
            .on_input_key(move |s: &mut Browser, event, tx| {
                if event.key == Key::Escape {
                    pickers::dismiss_web_authn(s, view, tx);
                    return true;
                }
                false
            })
            .on_submit(move |s: &mut Browser, _| pickers::submit_web_authn(s, view));
        card = card.child(field(input, prompt.pin.focused, p));
    }
    if prompt.state == WebAuthnState::ChooseAccount {
        let mut accounts = col()
            .key("web-authn-accounts")
            .role("listbox")
            .max_h(160.0)
            .gap(2.0)
            .items_stretch();
        for (index, account) in prompt.accounts.iter().enumerate() {
            let chosen = index == prompt.chosen_account;
            let mut item = row()
                .key(format!("web-authn-account-{index}"))
                .role("option")
                .label(account.clone())
                .checked(chosen)
                .h(30.0)
                .px(10.0)
                .radius(6.0)
                .center_y()
                .focusable(false)
                .hover(p.hover)
                .press(p.press)
                .on_press(move |s: &mut Browser| pickers::choose_web_authn_account(s, view, index))
                .on_double_press(move |s: &mut Browser| {
                    pickers::choose_web_authn_account(s, view, index);
                    pickers::submit_web_authn(s, view);
                })
                .child(
                    text(account.clone())
                        .font(BODY_FONT)
                        .color(p.text)
                        .truncate()
                        .a11y_hidden(),
                );
            if chosen {
                item = item.fill(p.accent.alpha(0.16));
            }
            accounts = accounts.child(item);
        }
        card = card.child(accounts);
    }
    let mut buttons = button_row("web-authn-buttons").child(button(
        "web-authn-dismiss",
        prompt.dismiss_label(),
        ButtonStyle::Secondary,
        p,
        move |s, tx| pickers::dismiss_web_authn(s, view, tx),
    ));
    if let Some(accept) = prompt.accept_label() {
        buttons = buttons.child(
            button(
                "web-authn-accept",
                accept,
                ButtonStyle::Primary,
                p,
                move |s, _| pickers::submit_web_authn(s, view),
            )
            .default_button(),
        );
    }
    row()
        .key("web-authn-row")
        .pin(Some(0.0), Some(12.0), Some(0.0), None)
        .justify_center()
        .child(panel_shadow(card.child(buttons)))
}
