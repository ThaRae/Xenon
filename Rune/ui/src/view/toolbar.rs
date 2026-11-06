/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Color, El, Icon, col, el, icon, row, text_input};

use crate::state::{Browser, TOOLBAR_HEIGHT};
use crate::view::icons;
use crate::view::palette::Palette;

const BUTTON_SIZE: f32 = 32.0;
const FIELD_HEIGHT: f32 = 32.0;
const LOCATION_FONT: TextStyle = TextStyle::new(13.5, 400.0);

pub fn toolbar(s: &Browser, p: &Palette) -> El {
    let active = s.tabs.active();
    let can_go_back = active.is_some_and(|tab| tab.can_go_back);
    let can_go_forward = active.is_some_and(|tab| tab.can_go_forward);
    let loading = active.is_some_and(|tab| tab.loading);
    let reload = if loading {
        nav_button("stop", "Stop", &icons::STOP, true, p, |s| {
            s.reload_or_stop()
        })
    } else {
        nav_button(
            "reload",
            "Reload",
            &icons::RELOAD,
            active.is_some(),
            p,
            |s| s.reload_or_stop(),
        )
    };
    let bar = row()
        .key("toolbar")
        .role("toolbar")
        .w_full()
        .h(TOOLBAR_HEIGHT - p.hairline)
        .px(8.0)
        .gap(2.0)
        .center_y()
        .child(nav_button(
            "back",
            "Back",
            &icons::BACK,
            can_go_back,
            p,
            |s| s.go_back(),
        ))
        .child(nav_button(
            "forward",
            "Forward",
            &icons::FORWARD,
            can_go_forward,
            p,
            |s| s.go_forward(),
        ))
        .child(reload)
        .child(location_field(s, p));
    col()
        .key("toolbar-band")
        .w_full()
        .fill(p.surface)
        .child(bar)
        .child(
            el().key("toolbar-edge")
                .w_full()
                .h(p.hairline)
                .fill(p.separator)
                .a11y_hidden(),
        )
}

fn nav_button(
    key: &'static str,
    label: &'static str,
    glyph: &'static Icon,
    enabled: bool,
    p: &Palette,
    action: fn(&mut Browser),
) -> El {
    let button = el()
        .key(key)
        .role("button")
        .label(label)
        .size(BUTTON_SIZE, BUTTON_SIZE)
        .radius(8.0)
        .center()
        .focusable(false)
        .child(
            icon(glyph, 18.0)
                .color(if enabled { p.icon } else { p.icon_disabled })
                .a11y_hidden(),
        );
    if enabled {
        button.hover(p.hover).press(p.press).on_press(action)
    } else {
        button.disabled(true)
    }
}

fn location_field(s: &Browser, p: &Palette) -> El {
    let focused = s.location.focused;
    let input = text_input(&s.location, LOCATION_FONT)
        .key("location-input")
        .label("Address")
        .single_line()
        .grow(1.0)
        .shrink()
        .color(p.text)
        .placeholder("Search or enter address", p.text_muted)
        .caret(p.accent, p.selection)
        .on_edit(|s: &mut Browser| &mut s.location)
        .on_input_key(|s: &mut Browser, event, tx| s.location_key(event, tx))
        .on_submit(|s: &mut Browser, tx| s.submit_location(tx));
    let field = row()
        .key("location")
        .grow(1.0)
        .shrink()
        .h(FIELD_HEIGHT)
        .ml(6.0)
        .mr(4.0)
        .px(14.0)
        .center_y()
        .radius(FIELD_HEIGHT / 2.0)
        .child(input);
    if focused {
        field
            .fill(p.field_focused)
            .border(1.5, p.focus_ring)
            .outline(3.0, 0.0, p.focus_glow)
    } else {
        field.fill(p.field).border(p.hairline, Color::TRANSPARENT)
    }
}
