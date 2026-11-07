/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{
    Color, Cursor, Cx, Edge, El, Key, PointerButton, Transition, col, edit_access, el, icon, row,
    text, text_input,
};

use crate::bookmarks::{
    BAR_ANCHOR_PREFIX, BarVisibility, BookmarkDialog, BookmarkNode, DIALOG_FOLDER_KEY,
    OVERFLOW_KEY, visible_item_count,
};
use crate::state::Browser;
use crate::view::icons;
use crate::view::page_overlays::controls::{
    BODY_FONT, ButtonStyle, CHEVRON_DOWN, TITLE_FONT, button, panel_shadow,
};
use crate::view::palette::Palette;

const BAR_KEY: &str = "bookmarks-bar";
const HOVER_BAR_KEY: &str = "bookmarks-hover-bar";
const BAR_HEIGHT: f32 = 30.0;
const BAR_PADDING: f32 = 4.0;
const ITEM_GAP: f32 = 3.0;
const BUTTON_HEIGHT: f32 = 24.0;
const BUTTON_MAX_WIDTH: f32 = 150.0;
const BUTTON_PADDING: f32 = 7.0;
const ICON_SIZE: f32 = 16.0;
const ICON_TEXT_SPACING: f32 = 6.0;
const ELISION_PADDING: f32 = 2.0;
const OVERFLOW_WIDTH: f32 = 24.0;
const ITEM_FONT: TextStyle = TextStyle::new(12.5, 450.0);
const FIELD_HEIGHT: f32 = 30.0;
const LABEL_WIDTH: f32 = 52.0;
const BOOKMARK_DIALOG_WIDTH: f32 = 500.0;
const FOLDER_DIALOG_WIDTH: f32 = 400.0;

static CHEVRONS: rune::Icon = rune::Icon {
    name: "xenon-chevrons-right",
    view: 24.0,
    body: r#"<path d="M6 7l5 5-5 5M13 7l5 5-5 5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
};

fn has_icon(item: &BookmarkNode) -> bool {
    match item {
        BookmarkNode::Bookmark { favicon, .. } => favicon.is_some(),
        BookmarkNode::Folder { .. } => true,
    }
}

fn item_width(item: &BookmarkNode) -> f32 {
    let title = item.title();
    let icon_width = if has_icon(item) { ICON_SIZE } else { 0.0 };
    let spacing = if icon_width > 0.0 && !title.is_empty() {
        ICON_TEXT_SPACING
    } else {
        0.0
    };
    let text_width = if title.is_empty() {
        0.0
    } else {
        rune::text::advance(title, ITEM_FONT) + ELISION_PADDING
    };
    (BUTTON_PADDING * 2.0 + icon_width + spacing + text_width)
        .min(BUTTON_MAX_WIDTH)
        .ceil()
}

fn item_button(index: usize, item: &BookmarkNode, width: f32, p: &Palette) -> El {
    let label = match item {
        BookmarkNode::Bookmark { title, url, .. } if title.is_empty() => url.clone(),
        _ => item.title().to_owned(),
    };
    let mut button = row()
        .key(format!("{BAR_ANCHOR_PREFIX}{index}"))
        .role("button")
        .label(label)
        .w(width)
        .h(BUTTON_HEIGHT)
        .px(BUTTON_PADDING)
        .gap(ICON_TEXT_SPACING)
        .center_y()
        .radius(BUTTON_HEIGHT / 2.0)
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .pointer_buttons([PointerButton::Middle])
        .on_press_tx(move |s: &mut Browser, tx| s.bar_item_pressed(index, tx))
        .on_secondary_press(move |s: &mut Browser, at, _| {
            s.bar_item_secondary_pressed(Some(index), at)
        });
    match item {
        BookmarkNode::Bookmark { url, favicon, .. } => {
            button = button.help(url.clone());
            if let Some(favicon) = favicon {
                button = button.child(
                    el().size(ICON_SIZE, ICON_SIZE)
                        .radius(3.0)
                        .pixels(favicon)
                        .a11y_hidden(),
                );
            }
        }
        BookmarkNode::Folder { .. } => {
            button = button.child(icon(&icons::FOLDER, ICON_SIZE).color(p.icon).a11y_hidden());
        }
    }
    if !item.title().is_empty() {
        button = button.child(
            text(item.title().to_owned())
                .font(ITEM_FONT)
                .color(p.text)
                .truncate()
                .shrink()
                .min_w(0.0)
                .a11y_hidden(),
        );
    }
    button
}

fn overflow_button(first_hidden: usize, p: &Palette) -> El {
    el().key(OVERFLOW_KEY)
        .role("button")
        .label("More bookmarks")
        .help("More bookmarks")
        .size(OVERFLOW_WIDTH, BUTTON_HEIGHT)
        .radius(BUTTON_HEIGHT / 2.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press_tx(move |s: &mut Browser, tx| s.bar_overflow_pressed(first_hidden, tx))
        .child(icon(&CHEVRONS, 14.0).color(p.icon).a11y_hidden())
}

fn bar_row(s: &Browser, p: &Palette, key: &'static str, cx: &mut Cx<Browser>) -> El {
    let items = &s.bookmarks.items;
    let widths: Vec<f32> = items.iter().map(item_width).collect();
    let width = cx
        .layout_size(key)
        .map_or_else(|| s.page_width(), |[width, _]| width);
    let visible = visible_item_count(&widths, width - BAR_PADDING * 2.0, ITEM_GAP, OVERFLOW_WIDTH);
    let mut bar = row()
        .key(key)
        .role("toolbar")
        .label("Bookmarks")
        .w_full()
        .h(BAR_HEIGHT)
        .px(BAR_PADDING)
        .gap(ITEM_GAP)
        .center_y()
        .fill(p.surface)
        .on_secondary_press(|s: &mut Browser, at, _| s.bar_item_secondary_pressed(None, at));
    for (index, (item, width)) in items.iter().zip(&widths).take(visible).enumerate() {
        bar = bar.child(item_button(index, item, *width, p));
    }
    if visible < items.len() {
        bar = bar
            .child(el().grow(1.0).a11y_hidden())
            .child(overflow_button(visible, p));
    }
    bar
}

pub fn bookmarks_bar(s: &Browser, p: &Palette, cx: &mut Cx<Browser>) -> Option<El> {
    if !s.bookmarks.shows_bar_in_layout() {
        return None;
    }
    Some(
        col()
            .key("bookmarks-bar-band")
            .w_full()
            .child(bar_row(s, p, BAR_KEY, cx))
            .child(
                el().key("bookmarks-bar-edge")
                    .w_full()
                    .h(p.hairline)
                    .fill(p.separator)
                    .a11y_hidden(),
            ),
    )
}

pub fn bookmarks_hover_bar(s: &Browser, p: &Palette, cx: &mut Cx<Browser>) -> Option<El> {
    if !s.bookmarks.shows_hover_bar() {
        return None;
    }
    let entrance = if s.bookmarks.hover_expand_animation.is_zero() {
        Transition::none()
    } else {
        Transition::asymmetric(Transition::reveal(Edge::Top), Transition::none())
    };
    Some(
        col()
            .key("bookmarks-hover-band")
            .pin(Some(0.0), Some(0.0), Some(0.0), None)
            .transition(entrance)
            .shadow(4.0, 12.0, Color::BLACK.alpha(0.18))
            .on_hover_change(|s: &mut Browser, hovered, _| s.bookmarks_bar_hovered(hovered))
            .hover_delays(std::time::Duration::ZERO, s.bookmarks.hover_leave_delay())
            .child(bar_row(s, p, HOVER_BAR_KEY, cx))
            .child(
                el().key("bookmarks-hover-edge")
                    .w_full()
                    .h(p.hairline)
                    .fill(p.separator)
                    .a11y_hidden(),
            ),
    )
}

pub fn bookmarks_hover_region(s: &Browser, band: El) -> El {
    if s.bookmarks.visibility != BarVisibility::OnHover {
        return band;
    }
    band.on_hover_change(|s: &mut Browser, hovered, _| s.bookmarks_toolbar_hovered(hovered))
        .hover_delays(
            s.bookmarks.hover_expand_delay,
            s.bookmarks.hover_leave_delay(),
        )
}

pub(super) fn scrim(key: &'static str, panel: El) -> El {
    col()
        .key(key)
        .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
        .center()
        .padding(24.0)
        .fill(Color::BLACK.alpha(0.3))
        .cursor(Cursor::Default)
        .on_press(|_: &mut Browser| {})
        .on_wheel(|_: &mut Browser, _, _| true)
        .child(panel)
}

pub(super) fn field_frame(key: &'static str, focused: bool, p: &Palette, content: El) -> El {
    let field = row()
        .key(key)
        .h(FIELD_HEIGHT)
        .grow(1.0)
        .shrink()
        .min_w(0.0)
        .px(10.0)
        .center_y()
        .radius(8.0)
        .child(content);
    if focused {
        field
            .fill(p.field_focused)
            .border(1.5, p.focus_ring)
            .outline(3.0, 0.0, p.focus_glow)
    } else {
        field.fill(p.field).border(p.hairline, p.separator)
    }
}

fn form_row(key: &'static str, label: &'static str, field: El, p: &Palette) -> El {
    row()
        .key(key)
        .gap(12.0)
        .center_y()
        .child(
            text(label)
                .font(BODY_FONT)
                .color(p.text)
                .w(LABEL_WIDTH)
                .a11y_hidden(),
        )
        .child(field)
}

fn dialog_input_key(s: &mut Browser, event: &rune::KeyEvent, tx: &mut rune::Tx) -> bool {
    if event.key == Key::Escape {
        s.answer_bookmark_dialog(false, tx);
        return true;
    }
    false
}

fn folder_picker(dialog: &BookmarkDialog, p: &Palette) -> El {
    row()
        .key(DIALOG_FOLDER_KEY)
        .role("button")
        .label(format!("Folder: {}", dialog.folder_path()))
        .h(FIELD_HEIGHT)
        .grow(1.0)
        .shrink()
        .min_w(0.0)
        .pl(10.0)
        .pr(6.0)
        .gap(6.0)
        .center_y()
        .radius(8.0)
        .fill(p.field)
        .border(p.hairline, p.separator)
        .hover(p.hover)
        .press(p.press)
        .focusable(false)
        .on_press_tx(|s: &mut Browser, tx| s.pick_bookmark_dialog_folder(tx))
        .child(
            text(dialog.folder_path().to_owned())
                .font(BODY_FONT)
                .color(p.text)
                .truncate()
                .grow(1.0)
                .shrink()
                .min_w(0.0)
                .a11y_hidden(),
        )
        .child(icon(&CHEVRON_DOWN, 14.0).color(p.icon).a11y_hidden())
}

pub(super) fn dialog_buttons(
    key: &'static str,
    p: &Palette,
    answer: fn(&mut Browser, bool, &mut rune::Tx),
) -> El {
    row()
        .key(key)
        .gap(8.0)
        .center_y()
        .child(el().grow(1.0).a11y_hidden())
        .child(button(
            format!("{key}-cancel"),
            "Cancel",
            ButtonStyle::Secondary,
            p,
            move |s, tx| answer(s, false, tx),
        ))
        .child(
            button(
                format!("{key}-ok"),
                "OK",
                ButtonStyle::Primary,
                p,
                move |s, tx| answer(s, true, tx),
            )
            .default_button(),
        )
}

pub(super) fn dialog_panel(key: &'static str, title: &str, width: f32, p: &Palette) -> El {
    col()
        .key(key)
        .role("dialog")
        .label(title.to_owned())
        .w(width)
        .shrink()
        .padding(20.0)
        .gap(12.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .items_stretch()
        .child(
            text(title.to_owned())
                .key(format!("{key}-title"))
                .font(TITLE_FONT)
                .color(p.text)
                .truncate(),
        )
}

pub fn bookmark_dialog(s: &Browser, p: &Palette) -> Option<El> {
    let dialog = s.bookmarks.shown_dialog()?;
    let width = if dialog.kind.has_url() {
        BOOKMARK_DIALOG_WIDTH
    } else {
        FOLDER_DIALOG_WIDTH
    };
    let mut panel = dialog_panel("bookmark-dialog", dialog.kind.title(), width, p)
        .modal(false, |s: &mut Browser| s.cancel_bookmark_dialog());
    if dialog.kind.has_url() {
        let input = text_input(&dialog.url, BODY_FONT)
            .key("bookmark-dialog-url-input")
            .label("URL")
            .single_line()
            .grow(1.0)
            .shrink()
            .color(p.text)
            .caret(p.accent, p.selection)
            .on_edit(edit_access(|s: &mut Browser| s.bookmarks.dialog_url()))
            .on_input_key(dialog_input_key)
            .on_submit(|s: &mut Browser, tx| s.answer_bookmark_dialog(true, tx));
        panel = panel.child(form_row(
            "bookmark-dialog-url",
            "URL:",
            field_frame("bookmark-dialog-url-field", dialog.url.focused, p, input),
            p,
        ));
    }
    let title = text_input(&dialog.title, BODY_FONT)
        .key("bookmark-dialog-title-input")
        .label("Title")
        .single_line()
        .grow(1.0)
        .shrink()
        .color(p.text)
        .caret(p.accent, p.selection)
        .on_edit(edit_access(|s: &mut Browser| s.bookmarks.dialog_title()))
        .on_input_key(dialog_input_key)
        .on_submit(|s: &mut Browser, tx| s.answer_bookmark_dialog(true, tx));
    panel = panel
        .child(form_row(
            "bookmark-dialog-name",
            "Title:",
            field_frame(
                "bookmark-dialog-title-field",
                dialog.title.focused,
                p,
                title,
            ),
            p,
        ))
        .child(form_row(
            "bookmark-dialog-folder-row",
            "Folder:",
            folder_picker(dialog, p),
            p,
        ))
        .child(dialog_buttons(
            "bookmark-dialog",
            p,
            Browser::answer_bookmark_dialog,
        ));
    Some(scrim("bookmark-dialog-scrim", panel_shadow(panel)))
}
