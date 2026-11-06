/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Align, Color, El, Icon, Key, Side, col, edit_access, el, icon, row, text, text_input};

use crate::engine::ViewId;
use crate::state::Browser;
use crate::tab_groups::{self, EditorAction, Group, GroupColor, GroupEngine, StripKey};
use crate::view::palette::{Palette, srgb};

const CHIP_HEIGHT: f32 = 20.0;
const CHIP_PADDING: f32 = 8.0;
const CHIP_MAX_WIDTH: f32 = 160.0;
const DOT_SIZE: f32 = 12.0;
const HEADER_MARGIN: f32 = 3.0;
const CHIP_HOVER_PADDING: f32 = 4.0;
const LINE_WIDTH: f32 = 2.0;
pub const VERTICAL_HEADER_HEIGHT: f32 = 26.0;
pub const VERTICAL_INDENT: f32 = 6.0;
const CHEVRON_SIZE: f32 = 16.0;
const CHIP_FONT: TextStyle = TextStyle::new(12.0, 600.0);
const EDITOR_WIDTH: f32 = 264.0;
const SWATCH_SIZE: f32 = 24.0;
const FIELD_FONT: TextStyle = TextStyle::new(13.0, 400.0);
const ACTION_FONT: TextStyle = TextStyle::new(13.0, 400.0);

static CHEVRON_UP: Icon = Icon {
    name: "xenon-group-chevron-up",
    view: 24.0,
    body: r#"<path d="M6 15l6-6 6 6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
};
static CHEVRON_DOWN: Icon = Icon {
    name: "xenon-group-chevron-down",
    view: 24.0,
    body: r#"<path d="M6 9l6 6 6-6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
};

pub fn palette_is_dark(p: &Palette) -> bool {
    p.text.r > p.chrome.r
}

pub fn group_color(color: GroupColor, p: &Palette) -> Color {
    srgb(color.rgb(palette_is_dark(p)))
}

pub fn group_key(group: &Group) -> String {
    format!("group-{}", group.id)
}

pub fn track_layout(element: El, key: StripKey) -> El {
    element.on_layout_tx(move |s: &mut Browser, rect, tx| {
        s.tab_groups.record_rect(key, rect);
        tx.live_only();
    })
}

fn chip(group: &Group, with_title: bool, p: &Palette) -> El {
    let color = group_color(group.color, p);
    if !with_title || group.title.is_empty() {
        return el()
            .key("chip")
            .size(DOT_SIZE, DOT_SIZE)
            .radius(DOT_SIZE / 2.0)
            .fill(color)
            .a11y_hidden();
    }
    row()
        .key("chip")
        .h(CHIP_HEIGHT)
        .max_w(CHIP_MAX_WIDTH)
        .px(CHIP_PADDING)
        .radius(CHIP_HEIGHT / 2.0)
        .center()
        .fill(color)
        .a11y_hidden()
        .child(
            text(group.title.as_str())
                .font(CHIP_FONT)
                .color(srgb(GroupColor::title_rgb(palette_is_dark(p))))
                .truncate()
                .shrink(),
        )
}

fn with_header_events(header: El, group: &Group) -> El {
    let id = group.id;
    track_layout(header, StripKey::Group(id))
        .role("button")
        .label(group.tooltip())
        .help(group.tooltip())
        .expanded(!group.collapsed)
        .focusable(false)
        .on_press(move |s: &mut Browser| tab_groups::toggle_collapsed(s, id))
        .on_secondary_press(move |s: &mut Browser, _, _| tab_groups::show_editor(s, id))
}

pub fn horizontal_header(group: &Group, tab_height: f32, p: &Palette) -> El {
    let line = (!group.collapsed).then(|| {
        el().key("group-line")
            .pin(
                Some(HEADER_MARGIN + CHIP_HOVER_PADDING),
                None,
                Some(-2.0),
                Some(0.0),
            )
            .h(LINE_WIDTH)
            .fill(group_color(group.color, p))
            .a11y_hidden()
    });
    let header = row()
        .key(group_key(group))
        .h(tab_height)
        .px(HEADER_MARGIN)
        .center_y()
        .child(
            row()
                .key("chip-hover")
                .padding(CHIP_HOVER_PADDING)
                .radius((CHIP_HEIGHT + 2.0 * CHIP_HOVER_PADDING) / 2.0)
                .hover(p.tab_hover)
                .child(chip(group, true, p)),
        )
        .children(line);
    with_header_events(header, group)
}

pub fn horizontal_line(
    group: Option<&Group>,
    last_in_group: bool,
    radius: f32,
    p: &Palette,
) -> Option<El> {
    let group = group?;
    Some(
        el().key("group-line")
            .pin(
                Some(0.0),
                None,
                Some(if last_in_group { radius } else { -2.0 }),
                Some(0.0),
            )
            .h(LINE_WIDTH)
            .fill(group_color(group.color, p))
            .a11y_hidden(),
    )
}

pub fn vertical_header(group: &Group, expanded: bool, p: &Palette) -> El {
    let header = row()
        .key(group_key(group))
        .w_full()
        .h(VERTICAL_HEADER_HEIGHT)
        .radius(VERTICAL_HEADER_HEIGHT / 2.0)
        .center_y()
        .hover(p.tab_hover);
    let header = if expanded {
        header
            .pl(VERTICAL_INDENT + 4.0)
            .pr(5.0)
            .gap(6.0)
            .child(chip(group, true, p))
            .child(el().key("spacer").grow(1.0))
            .child(
                icon(
                    if group.collapsed {
                        &CHEVRON_DOWN
                    } else {
                        &CHEVRON_UP
                    },
                    CHEVRON_SIZE,
                )
                .color(p.icon.alpha(0.7))
                .a11y_hidden(),
            )
    } else {
        header.center().child(chip(group, false, p))
    };
    with_header_events(header, group)
}

pub fn vertical_member(
    view: ViewId,
    tab_el: El,
    group: Option<&Group>,
    last_in_group: bool,
    expanded: bool,
    p: &Palette,
) -> El {
    let Some(group) = group else {
        return tab_el;
    };
    row()
        .key(format!("grouped-tab-{view}"))
        .w_full()
        .pl(if expanded { VERTICAL_INDENT } else { 4.0 })
        .child(tab_el)
        .child(
            el().key("group-line")
                .pin(
                    Some(1.0),
                    Some(0.0),
                    None,
                    Some(if last_in_group { 4.0 } else { -2.0 }),
                )
                .w(LINE_WIDTH)
                .fill(group_color(group.color, p))
                .a11y_hidden(),
        )
}

fn swatch(group: &Group, color: GroupColor, p: &Palette) -> El {
    let selected = group.color == color;
    let fill = group_color(color, p);
    let id = group.id;
    let inset = if selected { 5.0 } else { 4.0 };
    let outer = el()
        .key(format!("swatch-{}", color.name().to_lowercase()))
        .role("radio")
        .label(color.name())
        .checked(selected)
        .size(SWATCH_SIZE, SWATCH_SIZE)
        .radius(SWATCH_SIZE / 2.0)
        .center()
        .focusable(false)
        .on_press(move |_: &mut Browser| GroupEngine.set_color(id, color))
        .child(
            el().size(SWATCH_SIZE - 2.0 * inset, SWATCH_SIZE - 2.0 * inset)
                .radius(SWATCH_SIZE / 2.0 - inset)
                .fill(fill)
                .a11y_hidden(),
        );
    if selected {
        outer.border(2.0, fill)
    } else {
        outer
    }
}

fn action(key: &'static str, label: &'static str, action: EditorAction, p: &Palette) -> El {
    row()
        .key(key)
        .role("button")
        .label(label)
        .w_full()
        .h(30.0)
        .px(8.0)
        .radius(6.0)
        .center_y()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(move |s: &mut Browser| tab_groups::run_editor_action(s, action))
        .child(text(label).font(ACTION_FONT).color(p.text).a11y_hidden())
}

fn separator(key: &'static str, p: &Palette) -> El {
    el().key(key)
        .w_full()
        .h(p.hairline)
        .my(4.0)
        .fill(p.separator)
        .a11y_hidden()
}

pub fn group_editor(s: &Browser, p: &Palette, vertical: bool) -> Option<El> {
    let editor = s.tab_groups.editor.as_ref()?;
    let window = s.shown_window();
    let group = s.tab_groups.group(window, editor.group)?;
    let holds_every_tab = group.tabs.len() == s.tabs.iter().count();

    let input = text_input(&editor.title, FIELD_FONT)
        .key("group-title-input")
        .label("Tab group title")
        .single_line()
        .grow(1.0)
        .shrink()
        .color(p.text)
        .placeholder("Name this group", p.text_muted)
        .caret(p.accent, p.selection)
        .on_edit(edit_access(|s: &mut Browser| s.tab_groups.editor_title()))
        .on_input_key(|s: &mut Browser, event, tx| {
            if event.key == Key::Escape {
                s.tab_groups.close_editor();
                tx.blur();
                return true;
            }
            false
        })
        .on_submit(|s: &mut Browser, tx| {
            tab_groups::poll(s);
            s.tab_groups.close_editor();
            tx.blur();
        });
    let field = row()
        .key("group-title-field")
        .w_full()
        .h(32.0)
        .px(8.0)
        .center_y()
        .radius(6.0)
        .fill(p.surface)
        .border(
            1.0,
            if editor.title.focused {
                p.accent
            } else {
                p.separator
            },
        )
        .child(input);

    let mut swatches = row().key("group-colors").w_full().gap(2.0);
    for color in GroupColor::ALL {
        swatches = swatches.child(swatch(group, color, p));
    }

    let (side, align) = if vertical {
        (Side::Right, Align::Start)
    } else {
        (Side::Bottom, Align::Start)
    };
    let mut card = col()
        .key("tab-group-editor")
        .role("dialog")
        .label("Tab group")
        .w(EDITOR_WIDTH)
        .pt(12.0)
        .px(12.0)
        .pb(8.0)
        .gap(2.0)
        .fill(p.surface)
        .radius(8.0)
        .border(p.hairline, p.bubble_border)
        .shadow(8.0, 24.0, Color::BLACK.alpha(0.25))
        .anchored(group_key(group), side, align, 4.0)
        .modal(true, |s: &mut Browser| s.tab_groups.close_editor())
        .child(field.mb(8.0))
        .child(swatches.mb(4.0))
        .child(separator("group-separator-top", p))
        .child(action(
            "group-new-tab",
            "New Tab in Group",
            EditorAction::NewTab,
            p,
        ));
    if !holds_every_tab {
        card = card.child(action(
            "group-move-to-new-window",
            "Move Group to New Window",
            EditorAction::MoveToNewWindow,
            p,
        ));
    }
    Some(
        card.child(action("group-close", "Close Group", EditorAction::Close, p))
            .child(separator("group-separator-bottom", p))
            .child(action("group-ungroup", "Ungroup", EditorAction::Ungroup, p)),
    )
}
