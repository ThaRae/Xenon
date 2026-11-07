/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Cursor, El, Icon, col, el, icon, row, text};

use crate::engine::ViewId;
use crate::split_view::{
    self, Layout, PAGE_RADIUS, RING_OFFSET, RING_WIDTH, Rect, Side, Split, armed_drop_side,
    drop_indicator_rect, host_of, shown_area,
};
use crate::state::Browser;
use crate::tab_list::Tab;
use crate::view::icons;
use crate::view::page::page_content;
use crate::view::palette::Palette;

const MINI_TOOLBAR_HEIGHT: f32 = 26.0;
const MINI_TOOLBAR_MAX_LABEL_WIDTH: f32 = 220.0;
const MINI_TOOLBAR_MENU_KEY: &str = "split-mini-more";
const TOOLBAR_BUTTON_KEY: &str = "split-button";
const GRIP_WIDTH: f32 = 4.0;
const GRIP_HEIGHT: f32 = 36.0;
const INACTIVE_WINDOW_RING_ALPHA: f32 = 150.0 / 255.0;
const DIVIDER_IDLE_ALPHA: f32 = 110.0 / 255.0;
const LABEL_FONT: TextStyle = TextStyle::new(12.0, 450.0);
const INDICATOR_FONT: TextStyle = TextStyle::new(13.0, 600.0);

static MORE: Icon = Icon {
    name: "xenon-split-more",
    view: 24.0,
    body: r#"<circle cx="6.5" cy="12" r="1.7" fill="currentColor"/><circle cx="12" cy="12" r="1.7" fill="currentColor"/><circle cx="17.5" cy="12" r="1.7" fill="currentColor"/>"#,
};

pub fn split_page_area(s: &Browser, area: El, split: Split, layout: Layout, p: &Palette) -> El {
    let active = s.tabs.active_view();
    let mut area = area;
    for side in [Side::Left, Side::Right] {
        let view = split.at(side);
        let Some(tab) = s.tabs.get(view) else {
            continue;
        };
        let rect = layout.pane(side);
        let is_active = active == Some(view);
        let pane = col()
            .key(format!("pane-{view}"))
            .absolute(rect.x, rect.y)
            .w(rect.w)
            .h(rect.h)
            .radius(PAGE_RADIUS)
            .clip_rounded(PAGE_RADIUS)
            .fill(p.page)
            .children(page_content(s, tab, p))
            .children((!is_active).then(|| mini_toolbar(tab, p)));
        area = area
            .child(ring(view, rect, is_active, s.window_focused, p))
            .child(pane);
    }
    area.child(divider(s, layout.divider, p))
        .children(drop_indicator(s, true, p))
}

fn ring(view: ViewId, page: Rect, active: bool, window_focused: bool, p: &Palette) -> El {
    let (width, color) = if active {
        let alpha = if window_focused {
            1.0
        } else {
            INACTIVE_WINDOW_RING_ALPHA
        };
        (RING_WIDTH, p.accent.alpha(alpha))
    } else {
        (1.0, p.bubble_border)
    };
    let gap = if active {
        RING_OFFSET - width / 2.0
    } else {
        0.0
    };
    let outset = gap + width;
    el().key(format!("ring-{view}"))
        .absolute(page.x - outset, page.y - outset)
        .w(page.w + 2.0 * outset)
        .h(page.h + 2.0 * outset)
        .radius(PAGE_RADIUS + outset)
        .border(width, color)
        .inert()
        .a11y_hidden()
}

fn divider(s: &Browser, rect: Rect, p: &Palette) -> El {
    let grip = if s.split.divider_active || s.split.divider_hovered {
        p.accent
    } else {
        p.text_muted.alpha(DIVIDER_IDLE_ALPHA)
    };
    el().key("split-divider")
        .role("separator")
        .label("Split view divider")
        .absolute(rect.x, rect.y)
        .w(rect.w)
        .h(rect.h)
        .center()
        .focusable(false)
        .cursor(Cursor::ResizeColumn)
        .help("Drag to resize, or double-click to reverse views")
        .on_hover_change(|s: &mut Browser, hovered, _| s.split.divider_hovered = hovered)
        .on_drag(split_view::divider_drag)
        .child(
            el().w(GRIP_WIDTH)
                .h(GRIP_HEIGHT)
                .radius(GRIP_WIDTH / 2.0)
                .fill(grip)
                .inert()
                .a11y_hidden(),
        )
}

fn favicon(tab: &Tab, p: &Palette) -> El {
    let slot = el().size(16.0, 16.0).center().a11y_hidden();
    match &tab.favicon {
        Some(favicon) => slot.child(el().size(16.0, 16.0).radius(3.0).pixels(favicon)),
        None => slot.child(icon(&icons::GLOBE, 15.0).color(p.text_muted)),
    }
}

fn mini_toolbar(tab: &Tab, p: &Palette) -> El {
    let view = tab.view;
    let label = host_of(&tab.url)
        .map(str::to_owned)
        .unwrap_or_else(|| tab.display_title().to_owned());
    row()
        .key("split-mini-toolbar")
        .role("button")
        .label(format!("Show {label}"))
        .pin(None, None, Some(0.0), Some(0.0))
        .h(MINI_TOOLBAR_HEIGHT)
        .pl(9.0)
        .pr(3.0)
        .gap(6.0)
        .center_y()
        .fill(p.chrome)
        .corners([PAGE_RADIUS, 0.0, 0.0, 0.0])
        .cursor(Cursor::Pointer)
        .focusable(false)
        .help(tab.display_title())
        .on_press_tx(move |s: &mut Browser, tx| s.activate_tab(view, tx))
        .child(favicon(tab, p))
        .child(
            text(label)
                .font(LABEL_FONT)
                .color(p.text)
                .max_w(MINI_TOOLBAR_MAX_LABEL_WIDTH)
                .truncate()
                .a11y_hidden(),
        )
        .child(
            el().key(MINI_TOOLBAR_MENU_KEY)
                .role("button")
                .label("Split view")
                .help("Split view")
                .size(22.0, 22.0)
                .radius(6.0)
                .center()
                .focusable(false)
                .cursor(Cursor::Default)
                .hover(p.hover)
                .press(p.press)
                .on_press_tx(move |s: &mut Browser, tx| {
                    split_view::show_inactive_view_menu(s, view, MINI_TOOLBAR_MENU_KEY, tx)
                })
                .child(icon(&MORE, 18.0).color(p.icon).a11y_hidden()),
        )
}

pub fn drop_indicator(s: &Browser, split_shown: bool, p: &Palette) -> Option<El> {
    let side = armed_drop_side(s)?;
    let rect = drop_indicator_rect(shown_area(s), split_shown, side);
    Some(
        col()
            .key("split-drop-indicator")
            .absolute(rect.x, rect.y)
            .w(rect.w)
            .h(rect.h)
            .padding(RING_OFFSET)
            .fill(p.chrome)
            .inert()
            .child(
                col()
                    .key("split-drop-card")
                    .w_full()
                    .grow(1.0)
                    .center()
                    .gap(8.0)
                    .radius(PAGE_RADIUS)
                    .fill(p.accent.alpha(0.1))
                    .border(RING_WIDTH, p.accent)
                    .border_dash(3.0, 3.0)
                    .child(icon(&icons::PLUS, 22.0).color(p.accent).a11y_hidden())
                    .child(text("Create split view").font(INDICATOR_FONT).color(p.text)),
            ),
    )
}

fn split_icon(side: Side, p: &Palette) -> El {
    const WIDTH: f32 = 17.0;
    const HEIGHT: f32 = 13.0;
    const RADIUS: f32 = 2.5;
    const STROKE: f32 = 1.5;
    let half = WIDTH / 2.0;
    let (x, corners) = match side {
        Side::Left => (0.0, [RADIUS, 0.0, 0.0, RADIUS]),
        Side::Right => (half, [0.0, RADIUS, RADIUS, 0.0]),
    };
    el().size(WIDTH, HEIGHT)
        .a11y_hidden()
        .child(
            el().absolute(x, 0.0)
                .w(half)
                .h(HEIGHT)
                .corners(corners)
                .fill(p.accent),
        )
        .child(
            el().absolute(0.0, 0.0)
                .size(WIDTH, HEIGHT)
                .radius(RADIUS)
                .border(STROKE, p.icon),
        )
        .child(
            el().absolute(half - STROKE / 2.0, 0.0)
                .w(STROKE)
                .h(HEIGHT)
                .fill(p.icon),
        )
}

pub fn toolbar_button(s: &Browser, p: &Palette) -> Option<El> {
    let active = s.tabs.active_view()?;
    let side = s.split.side(active)?;
    let label = match side {
        Side::Left => "Split view: left view active",
        Side::Right => "Split view: right view active",
    };
    Some(
        el().key(TOOLBAR_BUTTON_KEY)
            .role("button")
            .label(label)
            .help(label)
            .size(32.0, 32.0)
            .radius(8.0)
            .center()
            .focusable(false)
            .hover(p.hover)
            .press(p.press)
            .on_press_tx(move |s: &mut Browser, tx| {
                split_view::show_split_menu(s, active, TOOLBAR_BUTTON_KEY, tx)
            })
            .child(split_icon(side, p)),
    )
}

pub fn pair_split_tabs(
    s: &Browser,
    tabs: Vec<(ViewId, El)>,
    vertical: bool,
    radius: f32,
    p: &Palette,
) -> Vec<El> {
    let mut paired = Vec::with_capacity(tabs.len());
    let mut tabs = tabs.into_iter().peekable();
    while let Some((view, tab_el)) = tabs.next() {
        let split = s
            .split
            .split_of(view)
            .filter(|split| split.left == view)
            .copied();
        match split {
            Some(split) if tabs.peek().is_some_and(|(next, _)| *next == split.right) => {
                let (_, right) = tabs.next().expect("the peeked partner");
                let shown = s.split.is_shown(&s.tabs, view);
                paired.push(split_tab_frame(
                    split, tab_el, right, shown, vertical, radius, p,
                ));
            }
            _ => paired.push(tab_el),
        }
    }
    paired
}

fn split_tab_frame(
    split: Split,
    left: El,
    right: El,
    shown: bool,
    vertical: bool,
    radius: f32,
    p: &Palette,
) -> El {
    let frame = if vertical {
        col().w_full().radius(radius)
    } else {
        row()
            .h_full()
            .items_end()
            .shrink()
            .corners([radius, radius, 0.0, 0.0])
    };
    let frame = frame
        .key(format!("split-tabs-{}", split.left))
        .role("group")
        .label("Split view")
        .gap(2.0);
    let frame = if shown {
        frame.fill(p.surface.alpha(0.5))
    } else {
        frame.fill(p.tab_hover).border(p.hairline, p.separator)
    };
    frame.child(left).child(right)
}
