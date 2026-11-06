/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Cx, El, col, el, row};

use crate::chrome_menu;
use crate::engine::ChromeLayout;
use crate::page_prompts::chrome_hidden;
use crate::state::Browser;
use crate::view::location::location_overlays;
use crate::view::page::page_area;
use crate::view::palette::Palette;
use crate::view::tabs::{
    horizontal_tabs, hover_card, new_tab_button, sidebar_toggle, tab_strip, vertical_tab_sidebar,
};
use crate::view::toolbar::{toolbar, toolbar_band, toolbar_controls};

const COMPACT_TOOLBAR_MIN_WIDTH: f32 = 380.0;
const COMPACT_TOOLBAR_MAX_WIDTH: f32 = 620.0;
const CHROME_PADDING: f32 = 8.0;

pub fn browser_view(s: &Browser, cx: &mut Cx<Browser>) -> El {
    let scale = cx.env().scale;
    s.observe_scale(scale);
    s.observe_system_dark(cx.system_dark());
    let palette = Palette::new(cx.system_dark(), scale);
    let titlebar_inset = cx.titlebar_inset();
    let layout = s.chrome.layout;
    let window = col()
        .key("window")
        .role("window")
        .w_full()
        .h_full()
        .fill(palette.chrome)
        .on_window_focus(|s: &mut Browser, focused, _| s.window_focus_changed(focused));
    let window = if chrome_hidden(s) {
        window.child(page_area(s, &palette))
    } else {
        match layout {
            ChromeLayout::Classic => window
                .child(tab_strip(s, &palette, titlebar_inset))
                .child(toolbar(s, &palette, cx))
                .child(page_area(s, &palette)),
            ChromeLayout::Compact => window
                .child(compact_bar(s, &palette, titlebar_inset, cx))
                .child(page_area(s, &palette)),
            ChromeLayout::Vertical => window
                .child(vertical_toolbar(s, &palette, titlebar_inset, cx))
                .child(
                    row()
                        .key("sidebar-and-page")
                        .w_full()
                        .grow(1.0)
                        .min_h(0.0)
                        .child(vertical_tab_sidebar(s, &palette))
                        .child(
                            el().key("sidebar-edge")
                                .w(palette.hairline)
                                .h_full()
                                .fill(palette.separator)
                                .a11y_hidden(),
                        )
                        .child(
                            col()
                                .key("page-column")
                                .h_full()
                                .grow(1.0)
                                .min_w(0.0)
                                .child(page_area(s, &palette)),
                        ),
                ),
        }
    };
    let window = window.children(location_overlays(s, &palette, cx));
    if chrome_hidden(s) {
        return window;
    }
    window.children(hover_card(s, &palette, layout == ChromeLayout::Vertical))
}

fn compact_bar(s: &Browser, p: &Palette, titlebar_inset: f32, cx: &mut Cx<Browser>) -> El {
    let window_width = s.page_width();
    let toolbar_width =
        (window_width * 2.0 / 5.0).clamp(COMPACT_TOOLBAR_MIN_WIDTH, COMPACT_TOOLBAR_MAX_WIDTH);
    toolbar_band(
        row()
            .key("compact-row")
            .w_full()
            .pl(titlebar_inset + CHROME_PADDING)
            .pr(CHROME_PADDING)
            .gap(6.0)
            .center_y()
            .window_drag()
            .on_secondary_press(|s: &mut Browser, at, tx| chrome_menu::show_window_menu(s, at, tx))
            .child(toolbar_controls(s, p, cx).w(toolbar_width).shrink())
            .child(horizontal_tabs(s, p))
            .child(new_tab_button(p)),
        p.chrome,
        p,
    )
}

fn vertical_toolbar(s: &Browser, p: &Palette, titlebar_inset: f32, cx: &mut Cx<Browser>) -> El {
    toolbar_band(
        row()
            .key("toolbar-row")
            .w_full()
            .pl(titlebar_inset + CHROME_PADDING)
            .pr(CHROME_PADDING)
            .gap(4.0)
            .center_y()
            .child(sidebar_toggle(s, p))
            .child(toolbar_controls(s, p, cx).grow(1.0).shrink()),
        p.surface,
        p,
    )
}
