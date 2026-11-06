/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Cx, El, Icon, col, el, icon, row};

use crate::state::{Browser, TOOLBAR_HEIGHT};
use crate::view::icons;
use crate::view::location::location_field;
use crate::view::palette::Palette;

const BUTTON_SIZE: f32 = 32.0;

pub fn toolbar(s: &Browser, p: &Palette, cx: &mut Cx<Browser>) -> El {
    let dark = cx.system_dark();
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
        .child(location_field(s, p, dark, cx));
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
