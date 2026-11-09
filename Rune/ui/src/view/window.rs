/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Cx, El, col, el, row};

use crate::chrome_menu;
use crate::engine::ChromeLayout;
use crate::page_prompts::chrome_hidden;
use crate::state::Browser;
use crate::view::bookmarks::{
    bookmark_dialog, bookmarks_bar, bookmarks_hover_bar, bookmarks_hover_region,
};
use crate::view::crash_offer::crash_report_popover;
use crate::view::downloads::downloads_popover;
use crate::view::element_picker::element_filter_dialog;
use crate::view::location::location_overlays;
use crate::view::page::page_area;
use crate::view::page_overlays::menu::chrome_menu_layers;
use crate::view::palette::Palette;
use crate::view::shield::shield_popover;
use crate::view::tab_groups::group_editor;
use crate::view::tabs::{
    horizontal_tabs, hover_card, new_tab_button, sidebar_toggle, tab_strip, vertical_tab_sidebar,
};
use crate::view::task_manager::task_manager_overlay;
use crate::view::toolbar::{toolbar, toolbar_band, toolbar_controls};
use crate::view::update_button::update_popover;
use crate::view::window_controls::{
    OUTLINE_WIDTH, resize_edges, toolbar_window_controls, window_outline,
};
use crate::view::window_overlays::{devtools_banner, stop_downloads_dialog};

const COMPACT_TOOLBAR_MIN_WIDTH: f32 = 380.0;
const COMPACT_TOOLBAR_MAX_WIDTH: f32 = 620.0;
const CHROME_PADDING: f32 = 8.0;

pub fn browser_view(s: &Browser, cx: &mut Cx<Browser>) -> El {
    let scale = cx.env().scale;
    s.observe_scale(scale);
    s.observe_system_dark(cx.system_dark());
    let palette = Palette::new(cx.system_dark(), scale).for_window(s.window_focused);
    let titlebar_inset = cx.titlebar_inset();
    let layout = s.chrome.layout;
    let window = col()
        .key("window")
        .role("window")
        .w_full()
        .h_full()
        .on_window_focus(|s: &mut Browser, focused, _| s.window_focus_changed(focused))
        .on_screen_layout(|s: &mut Browser, rect| s.window_rect_changed(rect));
    let window = if s.native_page_active() {
        window
    } else {
        window.fill(palette.chrome)
    };
    let outline = window_outline(s, &palette);
    let window = if outline.is_some() {
        window.padding(OUTLINE_WIDTH)
    } else {
        window
    };
    let window = if chrome_hidden(s) {
        window.child(page_area(s, &palette))
    } else {
        match layout {
            ChromeLayout::Classic => window
                .child(tab_strip(s, &palette, titlebar_inset))
                .child(bookmarks_hover_region(s, toolbar(s, &palette, cx)))
                .children(bookmarks_bar(s, &palette, cx))
                .child(page_area(s, &palette).children(bookmarks_hover_bar(s, &palette, cx))),
            ChromeLayout::Compact => window
                .child(bookmarks_hover_region(
                    s,
                    compact_bar(s, &palette, titlebar_inset, cx),
                ))
                .children(bookmarks_bar(s, &palette, cx))
                .child(page_area(s, &palette).children(bookmarks_hover_bar(s, &palette, cx))),
            ChromeLayout::Vertical => window
                .child(bookmarks_hover_region(
                    s,
                    vertical_toolbar(s, &palette, titlebar_inset, cx),
                ))
                .children(bookmarks_bar(s, &palette, cx))
                .child(
                    row()
                        .key("sidebar-and-page")
                        .w_full()
                        .grow(1.0)
                        .min_h(0.0)
                        .child(vertical_tab_sidebar(s, &palette))
                        .child(
                            el().key("sidebar-edge")
                                .w(palette.hairline)
                                .h_full()
                                .fill(palette.separator)
                                .a11y_hidden(),
                        )
                        .child(
                            col()
                                .key("page-column")
                                .h_full()
                                .grow(1.0)
                                .min_w(0.0)
                                .child(
                                    page_area(s, &palette)
                                        .children(bookmarks_hover_bar(s, &palette, cx)),
                                ),
                        ),
                ),
        }
    };
    let window = window
        .children(devtools_banner(s, &palette))
        .children(location_overlays(s, &palette, cx))
        .children(bookmark_dialog(s, &palette))
        .children(element_filter_dialog(s, &palette));
    if chrome_hidden(s) {
        return window
            .children(stop_downloads_dialog(s, &palette))
            .children(chrome_menu(s, &palette));
    }
    let frame = outline.into_iter().chain(resize_edges(s));
    window
        .children(hover_card(s, &palette, layout == ChromeLayout::Vertical))
        .children(downloads_popover(s, &palette))
        .children(shield_popover(s, &palette))
        .children(group_editor(s, &palette, layout == ChromeLayout::Vertical))
        .children(task_manager_overlay(s, &palette))
        .children(update_popover(s, &palette))
        .children(crash_report_popover(s, &palette))
        .children(stop_downloads_dialog(s, &palette))
        .children(crate::view::extensions::extension_overlays(s, &palette))
        .children(chrome_menu(s, &palette))
        .children(frame)
}

fn chrome_menu(s: &Browser, p: &Palette) -> Vec<El> {
    s.chrome_menu
        .as_ref()
        .map(|menu| chrome_menu_layers(menu, p))
        .unwrap_or_default()
}

fn compact_bar(s: &Browser, p: &Palette, titlebar_inset: f32, cx: &mut Cx<Browser>) -> El {
    let window_width = s.page_width();
    let toolbar_width =
        (window_width * 2.0 / 5.0).clamp(COMPACT_TOOLBAR_MIN_WIDTH, COMPACT_TOOLBAR_MAX_WIDTH);
    toolbar_band(
        row()
            .key("compact-row")
            .w_full()
            .pl(titlebar_inset + CHROME_PADDING)
            .pr(CHROME_PADDING)
            .gap(6.0)
            .center_y()
            .window_drag()
            .on_secondary_press(|s: &mut Browser, at, tx| chrome_menu::show_window_menu(s, at, tx))
            .child(toolbar_controls(s, p, cx).w(toolbar_width).shrink())
            .child(horizontal_tabs(s, p, false))
            .child(new_tab_button(p))
            .children(toolbar_window_controls(s, p, true)),
        p.chrome,
        p,
    )
}

fn vertical_toolbar(s: &Browser, p: &Palette, titlebar_inset: f32, cx: &mut Cx<Browser>) -> El {
    toolbar_band(
        row()
            .key("toolbar-row")
            .w_full()
            .pl(titlebar_inset + CHROME_PADDING)
            .pr(CHROME_PADDING)
            .gap(4.0)
            .center_y()
            .child(sidebar_toggle(s, p))
            .child(toolbar_controls(s, p, cx).grow(1.0).shrink())
            .children(toolbar_window_controls(s, p, false)),
        p.toolbar,
        p,
    )
}
