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
use crate::split_view;
use crate::state::{Browser, TAB_STRIP_HEIGHT};
use crate::tab_groups::{self, Group, StripEntry, StripKey};
use crate::tab_list::Tab;
use crate::view::icons;
use crate::view::palette::Palette;
use crate::view::split_view::pair_split_tabs;
use crate::view::tab_groups as group_views;

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
        .fill(p.chrome)
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
    let tabs = row()
        .key("tabs")
        .role("tablist")
        .label("Tabs")
        .h_full()
        .items_end()
        .gap(2.0)
        .shrink();
    let groups = s.tab_groups.groups(s.shown_window());
    let mut entries = Vec::new();
    for entry in tab_groups::strip_entries(s.tabs.iter().map(|tab| tab.view), groups) {
        match entry {
            StripEntry::Header(id) => {
                if let Some(group) = groups.iter().find(|group| group.id == id) {
                    entries.push((None, group_views::horizontal_header(group, TAB_HEIGHT, p)));
                }
            }
            StripEntry::Tab {
                view,
                group,
                last_in_group,
            } => {
                if let Some(tab) = s.tabs.get(view) {
                    let group = group.and_then(|id| groups.iter().find(|g| g.id == id));
                    entries.push((
                        Some(view),
                        tab_button(
                            tab,
                            s.tabs.is_active(view),
                            s.window_focused,
                            group,
                            last_in_group,
                            p,
                        ),
                    ));
                }
            }
        }
    }
    tabs.children(pair_split_tabs(s, entries, false, TAB_RADIUS, p))
}

fn tab_key(view: ViewId) -> String {
    format!("tab-{view}")
}

fn with_tab_events(tab_el: El, view: ViewId) -> El {
    tab_el
        .focusable(false)
        .preserve_focus_on_press(true)
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

fn tab_button(
    tab: &Tab,
    active: bool,
    window_focused: bool,
    group: Option<&Group>,
    last_in_group: bool,
    p: &Palette,
) -> El {
    let view = tab.view;
    let tab_el = row()
        .key(tab_key(view))
        .role("tab")
        .label(tab.display_title())
        .focus_selected(active)
        .w(if tab.pinned { TAB_MIN_WIDTH } else { TAB_WIDTH })
        .shrink()
        .min_w(TAB_MIN_WIDTH)
        .h(TAB_HEIGHT)
        .pl(10.0)
        .pr(6.0)
        .gap(8.0)
        .center_y()
        .corners([TAB_RADIUS, TAB_RADIUS, 0.0, 0.0])
        .child(tab_icon(tab, active && window_focused, p));
    let tab_el = with_tab_events(tab_el, view);
    let tab_el = with_tab_drag(tab_el, view, tab_groups::Axis::Horizontal).children(
        group_views::horizontal_line(group, last_in_group, TAB_RADIUS, p),
    );
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
    let groups = s.tab_groups.groups(s.shown_window());
    let mut entries = Vec::new();
    for entry in tab_groups::strip_entries(s.tabs.iter().map(|tab| tab.view), groups) {
        let pinned = match entry {
            StripEntry::Tab { view, .. } => s.tabs.get(view).is_some_and(|tab| tab.pinned),
            StripEntry::Header(_) => false,
        };
        if previous_pinned == Some(true) && !pinned {
            tabs = tabs.children(pair_split_tabs(
                s,
                std::mem::take(&mut entries),
                true,
                8.0,
                p,
            ));
            tabs = tabs.child(
                el().key("pinned-separator")
                    .w_full()
                    .h(p.hairline)
                    .my(4.0)
                    .fill(p.separator)
                    .a11y_hidden(),
            );
        }
        previous_pinned = Some(pinned);
        match entry {
            StripEntry::Header(id) => {
                if let Some(group) = groups.iter().find(|group| group.id == id) {
                    entries.push((None, group_views::vertical_header(group, expanded, p)));
                }
            }
            StripEntry::Tab {
                view,
                group,
                last_in_group,
            } => {
                if let Some(tab) = s.tabs.get(view) {
                    let group = group.and_then(|id| groups.iter().find(|g| g.id == id));
                    entries.push((
                        Some(view),
                        group_views::vertical_member(
                            view,
                            vertical_tab(
                                tab,
                                s.tabs.is_active(view),
                                s.window_focused,
                                expanded,
                                p,
                            ),
                            group,
                            last_in_group,
                            expanded,
                            p,
                        ),
                    ));
                }
            }
        }
    }
    tabs = tabs.children(pair_split_tabs(s, entries, true, 8.0, p));
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

fn vertical_tab(tab: &Tab, active: bool, window_focused: bool, expanded: bool, p: &Palette) -> El {
    let view = tab.view;
    let tab_el = row()
        .key(tab_key(view))
        .role("tab")
        .label(tab.display_title())
        .focus_selected(active)
        .w_full()
        .h(VERTICAL_TAB_HEIGHT)
        .px(if expanded { 10.0 } else { 0.0 })
        .gap(8.0)
        .center_y()
        .radius(8.0)
        .child(tab_icon(tab, active && window_focused, p));
    let tab_el = if expanded { tab_el } else { tab_el.center() };
    let tab_el = with_tab_events(tab_el, view);
    let tab_el = with_tab_drag(tab_el, view, tab_groups::Axis::Vertical);
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
        .expanded(s.vertical_tabs_expanded)
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
    let loaded = s.hibernation.is_loaded(card.view);
    let thumbnail = (!active && loaded && frame_width > 0 && frame_height > 0).then(|| {
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

fn with_tab_drag(tab_el: El, view: ViewId, axis: tab_groups::Axis) -> El {
    let origin =
        rune::memo::window_layout_with(&tab_key(view), |[x, y, _, _]| [x.round(), y.round()]);
    group_views::track_layout(tab_el, StripKey::Tab(view)).on_drag(
        move |s: &mut Browser, drag, tx| {
            let over_page = split_view::tab_drag(s, view, origin, drag);
            if over_page && drag.phase == Phase::Move {
                split_view::wake_for_drop(s, tx);
                return;
            }
            tab_groups::drag_tab(s, view, axis, drag, tx)
        },
    )
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

fn tab_icon(tab: &Tab, animate: bool, p: &Palette) -> El {
    let slot = el()
        .key("icon")
        .size(ICON_SIZE, ICON_SIZE)
        .center()
        .a11y_hidden();
    if tab.crashed {
        return slot.child(icon(&icons::CRASHED, ICON_SIZE).color(p.text_muted));
    }
    if tab.loading {
        let spinner = icon(&icons::SPINNER, ICON_SIZE)
            .color(p.accent)
            .absolute(0.0, 0.0);
        return slot
            .child(
                icon(&icons::SPINNER_TRACK, ICON_SIZE)
                    .color(p.separator)
                    .absolute(0.0, 0.0),
            )
            .child(if animate {
                spinner.looping(LoopSpec::rotation(0.9))
            } else {
                spinner
            });
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
