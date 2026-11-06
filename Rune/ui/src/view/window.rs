/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Cx, El, col};

use crate::page_prompts::chrome_hidden;
use crate::state::Browser;
use crate::view::location::location_overlays;
use crate::view::page::page_area;
use crate::view::palette::Palette;
use crate::view::tabs::tab_strip;
use crate::view::toolbar::toolbar;

pub fn browser_view(s: &Browser, cx: &mut Cx<Browser>) -> El {
    let scale = cx.env().scale;
    s.observe_scale(scale);
    s.observe_system_dark(cx.system_dark());
    let palette = Palette::new(cx.system_dark(), scale);
    let titlebar_inset = cx.titlebar_inset();
    col()
        .key("window")
        .role("window")
        .w_full()
        .h_full()
        .fill(palette.chrome)
        .on_window_focus(|s: &mut Browser, focused, _| s.window_focus_changed(focused))
        .children((!chrome_hidden(s)).then(|| tab_strip(s, &palette, titlebar_inset)))
        .children((!chrome_hidden(s)).then(|| toolbar(s, &palette, cx)))
        .child(page_area(s, &palette))
        .children(location_overlays(s, &palette, cx))
}
