/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Align, Color, El, Side, col, el, icon, row, text};

use crate::ffi;
use crate::state::Browser;
use crate::tab_list::Tab;
use crate::view::icons;
use crate::view::palette::Palette;

const POPOVER_WIDTH: f32 = 300.0;
const TITLE_FONT: TextStyle = TextStyle::new(14.0, 600.0);
const BODY_FONT: TextStyle = TextStyle::new(13.0, 400.0);
const COUNT_FONT: TextStyle = TextStyle::new(10.0, 700.0);

fn blocking_on(tab: &Tab) -> bool {
    tab.blocking_state == ffi::XENON_BLOCKING_ENABLED
        || tab.blocking_state == ffi::XENON_BLOCKING_NO_RULES
}

fn has_site(tab: &Tab) -> bool {
    !tab.blocking_site.is_empty()
}

/// The content blocking shield, as the Qt frontend's ContentBlockingButton: the page's blocked request count over it,
/// struck through where blocking is off for the site.
pub fn shield_button(s: &Browser, p: &Palette) -> Option<El> {
    let tab = s.tabs.active()?;
    if !has_site(tab) {
        return None;
    }
    let on = blocking_on(tab);
    let mut button = el()
        .key("shield-button")
        .role("button")
        .label("Content blocking")
        .checked(s.shield_popover_open)
        .size(32.0, 32.0)
        .radius(8.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(|s: &mut Browser| s.toggle_shield_popover())
        .child(
            icon(
                if on {
                    &icons::SHIELD
                } else {
                    &icons::SHIELD_OFF
                },
                18.0,
            )
            .color(if on { p.accent } else { p.icon_disabled })
            .a11y_hidden(),
        );
    if on && tab.blocked_requests > 0 {
        let count = if tab.blocked_requests > 99 {
            "99+".to_owned()
        } else {
            tab.blocked_requests.to_string()
        };
        button = button.child(
            row()
                .key("shield-count")
                .pin(None, Some(2.0), Some(0.0), None)
                .h(13.0)
                .px(3.0)
                .radius(6.5)
                .center()
                .fill(p.accent)
                .inert()
                .child(
                    text(count)
                        .font(COUNT_FONT)
                        .color(Color::WHITE)
                        .a11y_hidden(),
                ),
        );
    }
    Some(button)
}

fn action_row(key: &'static str, label: &'static str, action: u32, p: &Palette) -> El {
    row()
        .key(key)
        .role("button")
        .label(label)
        .w_full()
        .h(32.0)
        .px(10.0)
        .radius(8.0)
        .center_y()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(move |s: &mut Browser| s.blocking_action(action))
        .child(text(label).font(BODY_FONT).color(p.text).a11y_hidden())
}

pub fn shield_popover(s: &Browser, p: &Palette) -> Option<El> {
    if !s.shield_popover_open {
        return None;
    }
    let tab = s.tabs.active()?;
    let on = blocking_on(tab);
    let summary = if tab.blocking_state == ffi::XENON_BLOCKING_SITE_DISABLED {
        "Ads and trackers aren't blocked on this site.".to_owned()
    } else if !on {
        "Content blocking is off.".to_owned()
    } else if tab.blocked_requests == 1 {
        "1 request blocked on this page.".to_owned()
    } else {
        format!("{} requests blocked on this page.", tab.blocked_requests)
    };
    let can_toggle = on || tab.blocking_state == ffi::XENON_BLOCKING_SITE_DISABLED;
    let mut card = col()
        .key("shield-popover")
        .role("dialog")
        .label("Content blocking")
        .w(POPOVER_WIDTH)
        .padding(10.0)
        .gap(4.0)
        .fill(p.surface)
        .radius(10.0)
        .border(p.hairline, p.bubble_border)
        .shadow(8.0, 24.0, Color::BLACK.alpha(0.25))
        .anchored("shield-button", Side::Bottom, Align::End, 6.0)
        .modal(true, |s: &mut Browser| s.close_shield_popover())
        .child(
            text(tab.blocking_site.as_str())
                .font(TITLE_FONT)
                .color(p.text)
                .truncate()
                .px(10.0)
                .pt(4.0),
        )
        .child(
            text(summary)
                .font(BODY_FONT)
                .color(p.text_muted)
                .wrap()
                .px(10.0)
                .pb(6.0),
        );
    if can_toggle {
        card = card.child(action_row(
            "shield-toggle",
            if on {
                "Turn Off Blocking for This Site"
            } else {
                "Turn On Blocking for This Site"
            },
            ffi::XENON_BLOCKING_ACTION_TOGGLE_SITE,
            p,
        ));
    }
    Some(
        card.child(action_row(
            "shield-pick",
            "Block an Element…",
            ffi::XENON_BLOCKING_ACTION_PICK_ELEMENT,
            p,
        ))
        .child(action_row(
            "shield-dashboard",
            "Open Blocking Dashboard",
            ffi::XENON_BLOCKING_ACTION_OPEN_DASHBOARD,
            p,
        )),
    )
}
