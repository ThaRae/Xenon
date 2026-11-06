/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Align, El, Icon, ScrollSpec, Side, col, el, icon, row, text};

use crate::engine::Action;
use crate::ffi;
use crate::state::{Browser, Download};
use crate::view::icons;
use crate::view::palette::Palette;

const POPOVER_WIDTH: f32 = 340.0;
const POPOVER_MAX_HEIGHT: f32 = 420.0;
const TITLE_FONT: TextStyle = TextStyle::new(14.0, 600.0);
const NAME_FONT: TextStyle = TextStyle::new(13.0, 500.0);
const STATUS_FONT: TextStyle = TextStyle::new(12.0, 400.0);
const BUTTON_FONT: TextStyle = TextStyle::new(13.0, 500.0);

/// The toolbar button, shown once this window's session has downloads, with a ring of the active ones' progress.
pub fn downloads_button(s: &Browser, p: &Palette) -> Option<El> {
    let mut downloads = s.shown_downloads().peekable();
    downloads.peek()?;
    let active: Vec<&Download> = downloads.filter(|download| download.is_active()).collect();
    let progress = if active.is_empty() {
        None
    } else {
        let known: Vec<f32> = active
            .iter()
            .filter_map(|download| download.progress)
            .collect();
        Some(if known.is_empty() {
            0.0
        } else {
            known.iter().sum::<f32>() / known.len() as f32
        })
    };
    let button = el()
        .key("downloads-button")
        .role("button")
        .label("Downloads")
        .checked(s.downloads_popover_open)
        .size(32.0, 32.0)
        .radius(8.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(|s: &mut Browser| s.toggle_downloads_popover())
        .child(
            icon(&icons::DOWNLOAD, 18.0)
                .color(if progress.is_some() { p.accent } else { p.icon })
                .a11y_hidden(),
        );
    let button = match progress {
        Some(progress) => button.child(
            el().key("downloads-progress")
                .pin(None, Some(5.0), None, Some(26.0))
                .w(22.0 * progress.clamp(0.04, 1.0))
                .ml(5.0)
                .h(2.0)
                .radius(1.0)
                .fill(p.accent)
                .a11y_hidden(),
        ),
        None => button,
    };
    Some(button)
}

pub fn downloads_popover(s: &Browser, p: &Palette) -> Option<El> {
    if !s.downloads_popover_open {
        return None;
    }
    let mut list = col().key("downloads-list").w_full().gap(2.0);
    let mut any = false;
    for download in s.shown_downloads() {
        any = true;
        list = list.child(download_row(download, p));
    }
    if !any {
        list = list.child(
            text("No downloads")
                .font(STATUS_FONT)
                .color(p.text_muted)
                .px(8.0)
                .py(12.0),
        );
    }
    Some(
        col()
            .key("downloads-popover")
            .role("dialog")
            .label("Downloads")
            .w(POPOVER_WIDTH)
            .max_h(POPOVER_MAX_HEIGHT)
            .padding(10.0)
            .gap(8.0)
            .fill(p.surface)
            .radius(10.0)
            .border(p.hairline, p.bubble_border)
            .shadow(8.0, 24.0, rune::Color::BLACK.alpha(0.25))
            .anchored("downloads-button", Side::Bottom, Align::End, 6.0)
            .modal(true, |s: &mut Browser| s.close_downloads_popover())
            .child(text("Downloads").font(TITLE_FONT).color(p.text).px(6.0))
            .child(
                col()
                    .key("downloads-scroll")
                    .w_full()
                    .shrink()
                    .min_h(0.0)
                    .scroll(ScrollSpec {
                        stick_bottom: false,
                    })
                    .child(list),
            )
            .child(
                row()
                    .key("downloads-show-all")
                    .role("button")
                    .label("Show All Downloads")
                    .w_full()
                    .h(32.0)
                    .radius(8.0)
                    .center()
                    .focusable(false)
                    .fill(p.field)
                    .hover(p.hover)
                    .press(p.press)
                    .on_press(|s: &mut Browser| {
                        s.close_downloads_popover();
                        s.activate_action(Action::OpenDownloads);
                    })
                    .child(
                        text("Show All Downloads")
                            .font(BUTTON_FONT)
                            .color(p.text)
                            .a11y_hidden(),
                    ),
            ),
    )
}

fn row_button(
    key: &'static str,
    label: &'static str,
    glyph: &'static Icon,
    id: u64,
    action: u32,
    p: &Palette,
) -> El {
    el().key(key)
        .role("button")
        .label(label)
        .size(26.0, 26.0)
        .radius(6.0)
        .center()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(move |s: &mut Browser| s.download_action(id, action))
        .child(icon(glyph, 14.0).color(p.icon).a11y_hidden())
}

fn download_row(download: &Download, p: &Palette) -> El {
    let id = download.id;
    let completed = download.status == ffi::XENON_DOWNLOAD_COMPLETED;
    let mut details = col()
        .key("details")
        .grow(1.0)
        .shrink()
        .min_w(0.0)
        .gap(3.0)
        .child(
            text(download.file_name.as_str())
                .key("name")
                .font(NAME_FONT)
                .color(p.text)
                .truncate(),
        )
        .child(
            text(download.status_text.as_str())
                .key("status")
                .font(STATUS_FONT)
                .color(p.text_muted)
                .truncate(),
        );
    if download.is_active()
        && let Some(progress) = download.progress
    {
        details = details.child(
            el().key("track")
                .w_full()
                .h(3.0)
                .radius(1.5)
                .fill(p.separator)
                .child(
                    el().key("bar")
                        .h_full()
                        .w((POPOVER_WIDTH - 110.0) * progress.clamp(0.0, 1.0))
                        .radius(1.5)
                        .fill(p.accent),
                ),
        );
    }
    let mut actions = row().key("actions").gap(2.0).center_y();
    if download.is_active() {
        if download.can_resume {
            actions = if download.status == ffi::XENON_DOWNLOAD_PAUSED {
                actions.child(row_button(
                    "resume",
                    "Resume",
                    &icons::PLAY,
                    id,
                    ffi::XENON_DOWNLOAD_ACTION_RESUME,
                    p,
                ))
            } else {
                actions.child(row_button(
                    "pause",
                    "Pause",
                    &icons::PAUSE,
                    id,
                    ffi::XENON_DOWNLOAD_ACTION_PAUSE,
                    p,
                ))
            };
        }
        actions = actions.child(row_button(
            "cancel",
            "Cancel",
            &icons::CLOSE,
            id,
            ffi::XENON_DOWNLOAD_ACTION_CANCEL,
            p,
        ));
    } else if completed {
        actions = actions.child(row_button(
            "show",
            "Show in folder",
            &icons::FOLDER,
            id,
            ffi::XENON_DOWNLOAD_ACTION_SHOW_IN_FOLDER,
            p,
        ));
    }
    let row_el = row()
        .key(format!("download-{id}"))
        .w_full()
        .px(8.0)
        .py(7.0)
        .gap(10.0)
        .center_y()
        .radius(8.0)
        .child(
            icon(&icons::DOWNLOAD, 18.0)
                .color(if completed { p.icon } else { p.text_muted })
                .a11y_hidden(),
        )
        .child(details)
        .child(actions);
    if completed {
        row_el
            .role("button")
            .label(format!("Open {}", download.file_name))
            .focusable(false)
            .hover(p.hover)
            .press(p.press)
            .on_press(move |s: &mut Browser| s.download_action(id, ffi::XENON_DOWNLOAD_ACTION_OPEN))
    } else {
        row_el
    }
}
