/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::time::Duration;

use rune::tok::TextStyle;
use rune::{
    Align, El, LoopSpec, Phase, PointerButton, ScrollSpec, Side, SurfaceFit, col, el, icon, row,
    text,
};

use crate::chrome_menu;
use crate::engine::ViewId;
use crate::state::{Browser, TAB_STRIP_HEIGHT};
use crate::tab_list::Tab;
use crate::view::icons;
use crate::view::palette::Palette;

const TAB_WIDTH: f32 = 232.0;
const TAB_MIN_WIDTH: f32 = 44.0;
const TAB_TOP_INSET: f32 = 6.0;
const TAB_HEIGHT: f32 = TAB_STRIP_HEIGHT - TAB_TOP_INSET;
const TAB_RADIUS: f32 = 9.0;
const STRIP_PADDING: f32 = 8.0;
const ICON_SIZE: f32 = 16.0;
const TITLE_FONT: TextStyle = TextStyle::new(12.5, 450.0);
const ACTIVE_TITLE_FONT: TextStyle = TextStyle::new(12.5, 550.0);

pub const VERTICAL_TABS_EXPANDED_WIDTH: f32 = 232.0;
pub const VERTICAL_TABS_COLLAPSED_WIDTH: f32 = 52.0;
const VERTICAL_TAB_HEIGHT: f32 = 37.0;
const VERTICAL_TABS_EXPANDED_MARGIN: f32 = 5.0;
const VERTICAL_TABS_COLLAPSED_MARGIN: f32 = 6.0;
const VERTICAL_TABS_TOP_MARGIN: f32 = 8.0;

const HOVER_CARD_DELAY: Duration = Duration::from_millis(350);
const HOVER_CARD_WIDTH: f32 = 320.0;
const HOVER_CARD_THUMBNAIL_HEIGHT: f32 = 180.0;
const CARD_TITLE_FONT: TextStyle = TextStyle::new(13.0, 600.0);
const CARD_DETAIL_FONT: TextStyle = TextStyle::new(12.0, 400.0);

pub fn tab_strip(s: &Browser, p: &Palette, titlebar_inset: f32) -> El {
    row()
        .key("tab-strip")
        .w_full()
        .h(TAB_STRIP_HEIGHT)
        .pl(titlebar_inset + STRIP_PADDING)
        .pr(STRIP_PADDING)
        .pt(TAB_TOP_INSET)
        .items_end()
        .gap(4.0)
        .window_drag()
        .on_secondary_press(|s: &mut Browser, at, tx| chrome_menu::show_window_menu(s, at, tx))
        .child(horizontal_tabs(s, p))
        .child(new_tab_button(p))
}

pub fn horizontal_tabs(s: &Browser, p: &Palette) -> El {
    let mut tabs = row()
        .key("tabs")
        .role("tablist")
        .label("Tabs")
        .h_full()
        .items_end()
        .gap(2.0)
        .shrink();
    for tab in s.tabs.iter() {
        tabs = tabs.child(tab_button(tab, s.tabs.is_active(tab.view), p));
    }
    tabs
}

fn tab_key(view: ViewId) -> String {
    format!("tab-{view}")
}

fn with_tab_events(tab_el: El, view: ViewId) -> El {
    tab_el
        .focusable(false)
        .pointer_buttons([PointerButton::Middle])
        .on_press_tx(move |s: &mut Browser, tx| {
            if tx.button() == PointerButton::Middle {
                s.close_tab(view, tx);
            } else {
                s.activate_tab(view, tx);
            }
        })
        .on_secondary_press(move |s: &mut Browser, at, tx| {
            s.tab_hover_changed(view, false);
            chrome_menu::show_tab_menu(s, view, at, tx)
        })
        .hover_delays(HOVER_CARD_DELAY, Duration::ZERO)
        .on_hover_change(move |s: &mut Browser, hovered, _| s.tab_hover_changed(view, hovered))
}

fn tab_title(tab: &Tab, active: bool, p: &Palette) -> El {
    text(tab.display_title())
        .key("title")
        .font(if active {
            ACTIVE_TITLE_FONT
        } else {
            TITLE_FONT
        })
        .color(if active { p.text } else { p.text_muted })
        .truncate()
        .grow(1.0)
        .shrink()
        .a11y_hidden()
}

fn tab_button(tab: &Tab, active: bool, p: &Palette) -> El {
    let view = tab.view;
    let tab_el = row()
        .key(tab_key(view))
        .role("tab")
        .label(tab.display_title())
        .checked(active)
        .w(if tab.pinned { TAB_MIN_WIDTH } else { TAB_WIDTH })
        .shrink()
        .min_w(TAB_MIN_WIDTH)
        .h(TAB_HEIGHT)
        .pl(10.0)
        .pr(6.0)
        .gap(8.0)
        .center_y()
        .corners([TAB_RADIUS, TAB_RADIUS, 0.0, 0.0])
        .child(tab_icon(tab, p));
    let tab_el = with_tab_events(tab_el, view);
    let tab_el = with_tab_drag(tab_el, view, Axis::Horizontal);
    let tab_el = if tab.pinned {
        tab_el.children(audio_button(tab, p))
    } else {
        tab_el
            .child(tab_title(tab, active, p))
            .children(audio_button(tab, p))
            .child(close_button(view, p))
    };
    if active {
        tab_el.fill(p.surface).child(
            el().key("indicator")
                .pin(Some(TAB_RADIUS), Some(0.0), Some(TAB_RADIUS), None)
                .h(2.0)
                .corners([0.0, 0.0, 2.0, 2.0])
                .fill(p.accent)
                .a11y_hidden(),
        )
    } else {
        tab_el.hover(p.tab_hover)
    }
}

pub fn vertical_tab_sidebar(s: &Browser, p: &Palette) -> El {
    let expanded = s.vertical_tabs_expanded;
    let margin = if expanded {
        VERTICAL_TABS_EXPANDED_MARGIN
    } else {
        VERTICAL_TABS_COLLAPSED_MARGIN
    };
    let mut tabs = col()
        .key("tabs")
        .role("tablist")
        .label("Tabs")
        .w_full()
        .gap(2.0);
    let mut previous_pinned = None;
    for tab in s.tabs.iter() {
        if previous_pinned == Some(true) && !tab.pinned {
            tabs = tabs.child(
                el().key("pinned-separator")
                    .w_full()
                    .h(p.hairline)
                    .my(4.0)
                    .fill(p.separator)
                    .a11y_hidden(),
            );
        }
        previous_pinned = Some(tab.pinned);
        tabs = tabs.child(vertical_tab(tab, s.tabs.is_active(tab.view), expanded, p));
    }
    let new_tab = row()
        .key("new-tab")
        .role("button")
        .label("New tab")
        .w_full()
        .h(VERTICAL_TAB_HEIGHT)
        .px(10.0)
        .gap(8.0)
        .center_y()
        .radius(8.0)
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press_tx(|s: &mut Browser, tx| s.new_tab(tx))
        .child(icon(&icons::PLUS, ICON_SIZE).color(p.icon).a11y_hidden())
        .children(expanded.then(|| {
            text("New Tab")
                .font(TITLE_FONT)
                .color(p.text_muted)
                .truncate()
                .shrink()
                .a11y_hidden()
        }));
    col()
        .key("tab-sidebar")
        .w(if expanded {
            VERTICAL_TABS_EXPANDED_WIDTH
        } else {
            VERTICAL_TABS_COLLAPSED_WIDTH
        })
        .h_full()
        .px(margin)
        .pt(VERTICAL_TABS_TOP_MARGIN)
        .fill(p.chrome)
        .on_secondary_press(|s: &mut Browser, at, tx| chrome_menu::show_window_menu(s, at, tx))
        .child(
            col()
                .key("tab-scroll")
                .w_full()
                .grow(1.0)
                .shrink()
                .min_h(0.0)
                .scroll(ScrollSpec {
                    stick_bottom: false,
                })
                .child(tabs.child(new_tab)),
        )
}

fn vertical_tab(tab: &Tab, active: bool, expanded: bool, p: &Palette) -> El {
    let view = tab.view;
    let tab_el = row()
        .key(tab_key(view))
        .role("tab")
        .label(tab.display_title())
        .checked(active)
        .w_full()
        .h(VERTICAL_TAB_HEIGHT)
        .px(if expanded { 10.0 } else { 0.0 })
        .gap(8.0)
        .center_y()
        .radius(8.0)
        .child(tab_icon(tab, p));
    let tab_el = if expanded { tab_el } else { tab_el.center() };
    let tab_el = with_tab_events(tab_el, view);
    let tab_el = with_tab_drag(tab_el, view, Axis::Vertical);
    let tab_el = if expanded {
        let tab_el = tab_el
            .child(tab_title(tab, active, p))
            .children(audio_button(tab, p));
        if tab.pinned {
            tab_el
        } else {
            tab_el.child(close_button(view, p))
        }
    } else {
        tab_el
    };
    if active {
        tab_el.fill(p.surface).child(
            el().key("indicator")
                .pin(Some(0.0), Some(8.0), None, Some(8.0))
                .w(2.0)
                .corners([0.0, 2.0, 2.0, 0.0])
                .fill(p.accent)
                .a11y_hidden(),
        )
    } else {
        tab_el.hover(p.tab_hover)
    }
}

pub fn sidebar_toggle(s: &Browser, p: &Palette) -> El {
    let label = if s.vertical_tabs_expanded {
        "Collapse tabs"
    } else {
        "Expand tabs"
    };
    el().key("sidebar-toggle")
        .role("button")
        .label(label)
        .size(32.0, 32.0)
        .radius(8.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(|s: &mut Browser| s.toggle_vertical_tabs_expanded())
        .child(icon(&icons::SIDEBAR, 18.0).color(p.icon).a11y_hidden())
}

pub fn hover_card(s: &Browser, p: &Palette, vertical: bool) -> Option<El> {
    let card = s.hover_card.as_ref()?;
    let tab = s.tabs.get(card.view)?;
    let active = s.tabs.is_active(card.view);
    let (frame_width, frame_height) = tab.page.size();
    let thumbnail = (!active && frame_width > 0 && frame_height > 0).then(|| {
        el().key("thumbnail")
            .w_full()
            .h(HOVER_CARD_THUMBNAIL_HEIGHT)
            .radius(7.0)
            .fill(p.page)
            .pixels_fit(&tab.page, SurfaceFit::Fill)
            .surface_anchor(Align::Start, Align::Start)
            .a11y_hidden()
    });
    let title = if tab.title.is_empty() {
        "Untitled"
    } else {
        tab.title.as_str()
    };
    let (side, align) = if vertical {
        (Side::Right, Align::Start)
    } else {
        (Side::Bottom, Align::Center)
    };
    Some(
        col()
            .key("tab-hover-card")
            .role("tooltip")
            .w(HOVER_CARD_WIDTH)
            .padding(8.0)
            .gap(4.0)
            .radius(10.0)
            .fill(p.bubble)
            .border(p.hairline, p.bubble_border)
            .shadow(8.0, 24.0, rune::Color::BLACK.alpha(0.25))
            .inert()
            .children(thumbnail.map(|thumbnail| thumbnail.mb(4.0)))
            .child(
                text(title)
                    .key("card-title")
                    .font(CARD_TITLE_FONT)
                    .color(p.text)
                    .truncate()
                    .px(4.0),
            )
            .child(
                text(tab.url.as_str())
                    .key("card-url")
                    .font(CARD_DETAIL_FONT)
                    .color(p.text_muted)
                    .truncate()
                    .px(4.0),
            )
            .children(card.memory.as_ref().map(|memory| {
                text(memory.as_str())
                    .key("card-memory")
                    .font(CARD_DETAIL_FONT)
                    .color(p.text_muted)
                    .truncate()
                    .px(4.0)
            }))
            .anchored(tab_key(card.view), side, align, 6.0),
    )
}

#[derive(Clone, Copy)]
enum Axis {
    Horizontal,
    Vertical,
}

const TAB_GAP: f32 = 2.0;

fn with_tab_drag(tab_el: El, view: ViewId, axis: Axis) -> El {
    tab_el.on_drag(move |s: &mut Browser, drag, tx| match drag.phase {
        Phase::Down => {
            drag.data = s.tabs.position(view).map_or(-1.0, |index| index as f32);
        }
        Phase::Move => {
            if drag.data < 0.0 {
                return;
            }
            let (travel, extent) = match axis {
                Axis::Horizontal => (drag.dx, drag.w),
                Axis::Vertical => (drag.dy, drag.h),
            };
            let steps = (travel / (extent + TAB_GAP)).round() as isize;
            let target = (drag.data as isize + steps).max(0) as usize;
            if s.tabs.position(view) != Some(target) {
                s.move_tab(view, target);
            }
            tx.live_only();
        }
        _ => {}
    })
}

fn audio_button(tab: &Tab, p: &Palette) -> Option<El> {
    if !tab.playing_audio && !tab.muted {
        return None;
    }
    let view = tab.view;
    Some(
        el().key("audio")
            .role("button")
            .label(if tab.muted { "Unmute tab" } else { "Mute tab" })
            .size(20.0, 20.0)
            .radius(6.0)
            .center()
            .focusable(false)
            .hover(p.hover)
            .press(p.press)
            .on_press(move |s: &mut Browser| s.toggle_mute(view))
            .child(
                icon(
                    if tab.muted {
                        &icons::SPEAKER_MUTED
                    } else {
                        &icons::SPEAKER
                    },
                    13.0,
                )
                .color(if tab.muted { p.text_muted } else { p.icon })
                .a11y_hidden(),
            ),
    )
}

fn tab_icon(tab: &Tab, p: &Palette) -> El {
    let slot = el()
        .key("icon")
        .size(ICON_SIZE, ICON_SIZE)
        .center()
        .a11y_hidden();
    if tab.crashed {
        return slot.child(icon(&icons::CRASHED, ICON_SIZE).color(p.text_muted));
    }
    if tab.loading {
        return slot
            .child(
                icon(&icons::SPINNER_TRACK, ICON_SIZE)
                    .color(p.separator)
                    .absolute(0.0, 0.0),
            )
            .child(
                icon(&icons::SPINNER, ICON_SIZE)
                    .color(p.accent)
                    .absolute(0.0, 0.0)
                    .looping(LoopSpec::rotation(0.9)),
            );
    }
    match &tab.favicon {
        Some(favicon) => slot.child(el().size(ICON_SIZE, ICON_SIZE).radius(3.0).pixels(favicon)),
        None => slot.child(icon(&icons::GLOBE, ICON_SIZE - 1.0).color(p.text_muted)),
    }
}

fn close_button(view: ViewId, p: &Palette) -> El {
    el().key("close")
        .role("button")
        .label("Close tab")
        .size(20.0, 20.0)
        .radius(6.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press_tx(move |s: &mut Browser, tx| s.close_tab(view, tx))
        .child(icon(&icons::CLOSE, 13.0).color(p.icon).a11y_hidden())
}

pub fn new_tab_button(p: &Palette) -> El {
    row().key("new-tab-slot").h(TAB_HEIGHT).center_y().child(
        el().key("new-tab")
            .role("button")
            .label("New tab")
            .size(28.0, 28.0)
            .radius(8.0)
            .center()
            .focusable(false)
            .hover(p.hover)
            .press(p.press)
            .on_press_tx(|s: &mut Browser, tx| s.new_tab(tx))
            .child(icon(&icons::PLUS, 16.0).color(p.icon).a11y_hidden()),
    )
}
