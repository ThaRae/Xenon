/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

mod bubbles;
pub(crate) mod controls;
mod crash_review;
mod dialog;
mod find_bar;
pub(crate) mod menu;
mod pickers;
mod screenshot;

use rune::El;

use crate::engine::ViewId;
use crate::page_prompts;
use crate::state::Browser;
use crate::view::palette::Palette;

pub fn page_overlays(s: &Browser, view: ViewId, p: &Palette) -> Vec<El> {
    let mut overlays = Vec::new();
    if let Some(prompts) = s.page_prompts.of(view) {
        if let Some(find) = &prompts.find {
            overlays.push(find_bar::find_bar(view, find, p));
        }
        if !prompts.permissions.is_empty() {
            overlays.push(bubbles::permission_prompts(view, &prompts.permissions, p));
        }
        if let Some(review) = &prompts.crash_review {
            overlays.push(crash_review::crash_review(view, review, p));
        }
        if let Some(prompt) = &prompts.web_authn {
            overlays.push(pickers::web_authn(view, prompt, p));
        }
        if let Some(picker) = &prompts.color_picker {
            overlays.push(pickers::color_picker(view, picker, p));
        }
        if let Some(picker) = &prompts.date_picker {
            overlays.push(pickers::date_picker(view, picker, p));
        }
    }
    if let Some((dialog_view, dialog)) = page_prompts::active_dialog(s)
        && dialog_view == view
    {
        overlays.push(dialog::dialog_sheet(view, dialog, p));
    }
    if let Some(selection) = s
        .screenshot
        .as_ref()
        .filter(|selection| selection.view == view)
    {
        overlays.push(screenshot::selection_overlay(selection, p));
    }
    if let Some(shown) = s
        .page_prompts
        .menu
        .as_ref()
        .filter(|menu| menu.view == view)
    {
        overlays.extend(menu::menu_layers(shown, p));
    }
    if let Some(tooltip) = s
        .page_prompts
        .of(view)
        .and_then(|prompts| prompts.tooltip.as_ref())
    {
        overlays.push(bubbles::tooltip(tooltip, p));
    }
    overlays
}
