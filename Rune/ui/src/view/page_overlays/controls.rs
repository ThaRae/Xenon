/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Color, El, Icon, el, icon, row, text};

use crate::state::Browser;
use crate::view::palette::Palette;

pub const BODY_FONT: TextStyle = TextStyle::new(13.5, 400.0);
pub const BUTTON_FONT: TextStyle = TextStyle::new(13.0, 550.0);
pub const SMALL_FONT: TextStyle = TextStyle::new(12.0, 400.0);
pub const TITLE_FONT: TextStyle = TextStyle::new(15.0, 600.0);

pub static CHEVRON_UP: Icon = stroked(
    "xenon-chevron-up",
    r#"<path d="M6 15l6-6 6 6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
pub static CHEVRON_DOWN: Icon = stroked(
    "xenon-chevron-down",
    r#"<path d="M6 9l6 6 6-6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
pub static CHEVRON_RIGHT: Icon = stroked(
    "xenon-chevron-right",
    r#"<path d="M9 6l6 6-6 6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
pub static CHECK: Icon = stroked(
    "xenon-check",
    r#"<path d="M5 12.5l4.5 4.5L19 7.5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
pub static SHIELD: Icon = stroked(
    "xenon-permission",
    r#"<path d="M12 3l7 3v5c0 4.5-3 8.3-7 10c-4-1.7-7-5.5-7-10V6z" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);

const fn stroked(name: &'static str, body: &'static str) -> Icon {
    Icon {
        name,
        view: 24.0,
        body,
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum ButtonStyle {
    Primary,
    Secondary,
}

pub fn button(
    key: impl Into<String>,
    label: &str,
    style: ButtonStyle,
    p: &Palette,
    action: impl Fn(&mut Browser, &mut rune::Tx) + 'static,
) -> El {
    let (fill, ink, hover, press) = match style {
        ButtonStyle::Primary => (
            p.accent,
            Color::WHITE,
            Color::WHITE.alpha(0.12),
            Color::BLACK.alpha(0.12),
        ),
        ButtonStyle::Secondary => (p.field, p.text, p.hover, p.press),
    };
    row()
        .key(key)
        .role("button")
        .label(label)
        .h(32.0)
        .px(16.0)
        .radius(8.0)
        .center()
        .fill(fill)
        .hover(hover)
        .press(press)
        .on_press_tx(action)
        .child(text(label).font(BUTTON_FONT).color(ink).a11y_hidden())
}

pub fn icon_button(
    key: impl Into<String>,
    label: &str,
    glyph: &'static Icon,
    enabled: bool,
    p: &Palette,
    action: impl Fn(&mut Browser, &mut rune::Tx) + 'static,
) -> El {
    let button = el()
        .key(key)
        .role("button")
        .label(label)
        .size(26.0, 26.0)
        .radius(6.0)
        .center()
        .focusable(false)
        .child(
            icon(glyph, 16.0)
                .color(if enabled { p.icon } else { p.icon_disabled })
                .a11y_hidden(),
        );
    if enabled {
        button.hover(p.hover).press(p.press).on_press_tx(action)
    } else {
        button.disabled(true)
    }
}

pub fn panel_shadow(panel: El) -> El {
    panel
        .shadow(10.0, 28.0, Color::BLACK.alpha(0.22))
        .shadow(1.0, 3.0, Color::BLACK.alpha(0.12))
}
