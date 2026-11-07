/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

mod field;
mod popup;
mod site_info;
mod style;

use rune::{Align, Beat, Cx, Easing, El, Sequence, Side, el, text};

use crate::state::Browser;
use crate::view::location::style::{LocationColors, PILL_FONT};
use crate::view::palette::Palette;

pub use field::location_field;

static COPIED_LINK_FADE: [Beat; 4] = [
    Beat::new(0.0).opacity(0.0),
    Beat::new(0.15).opacity(1.0).easing(Easing::EASE_OUT),
    Beat::new(1.5).opacity(1.0),
    Beat::new(1.75).opacity(0.0).easing(Easing::EASE_IN),
];

fn copied_link_toast(s: &Browser, p: &Palette) -> Option<El> {
    let copies = s.location.copied_links;
    (copies > 0).then(|| {
        el().key(format!("location-copied-{copies}"))
            .role("status")
            .label("Copied link")
            .px(12.0)
            .py(6.0)
            .radius(8.0)
            .fill(p.bubble)
            .border(p.hairline, p.bubble_border)
            .anchored("toolbar", Side::Bottom, Align::Center, 6.0)
            .sequence(Sequence::new(&COPIED_LINK_FADE))
            .inert()
            .child(text("Copied link").font(PILL_FONT).color(p.text))
    })
}

pub fn location_overlays(s: &Browser, p: &Palette, cx: &mut Cx<Browser>) -> Vec<El> {
    let colors = LocationColors::new(cx.system_dark(), p);
    let mut overlays = Vec::new();
    overlays.extend(popup::suggestion_popup(s, p, &colors, cx));
    if let Some(site_info) = &s.location.site_info {
        overlays.push(site_info::site_info_popover(s, site_info, p, &colors));
    }
    overlays.extend(copied_link_toast(s, p));
    overlays
}
