/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{
    Align, Color, Cursor, El, Icon, PointerButton, ScrollSpec, Side, SurfaceFit, col, el, icon,
    row, text,
};

use crate::engine::KeyEventKind;
use crate::extensions::{
    ActionButton, Confirmation, ErrorNotice, ExtensionInfo, PermissionPrompt, Popup,
    action_button_key,
};
use crate::state::Browser;
use crate::view::icons;
use crate::view::page_overlays::controls::{
    BODY_FONT, ButtonStyle, SMALL_FONT, TITLE_FONT, button, icon_button, panel_shadow,
};
use crate::view::palette::{Palette, srgb};

const BUTTON_SIZE: f32 = 32.0;
const ACTION_ICON_SIZE: f32 = 16.0;
const BADGE_FONT: TextStyle = TextStyle::new(9.0, 700.0);
const NAME_FONT: TextStyle = TextStyle::new(14.0, 650.0);
const HEADING_FONT: TextStyle = TextStyle::new(20.0, 650.0);
const DIALOG_WIDTH: f32 = 620.0;
const DIALOG_HEIGHT: f32 = 520.0;
const DIALOG_ICON_SIZE: f32 = 40.0;
const PROMPT_WIDTH: f32 = 360.0;

static PUZZLE: Icon = Icon {
    name: "xenon-extension",
    view: 24.0,
    body: r#"<path d="M10 4a2 2 0 0 1 4 0v2h4v4h-2a2 2 0 0 0 0 4h2v4h-4v-2a2 2 0 0 0-4 0v2H6v-4h2a2 2 0 0 0 0-4H6V6h4z" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"/>"#,
};
static CHEVRON_DOWN: Icon = Icon {
    name: "xenon-extension-chevron",
    view: 24.0,
    body: r#"<path d="M6 9l6 6 6-6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
};

fn color_from_rgba(rgba: u32) -> Color {
    srgb(rgba >> 8).alpha((rgba & 0xff) as f32 / 255.0)
}

pub fn action_buttons(s: &Browser, p: &Palette) -> Vec<El> {
    let actions = s.extensions.actions_for(s.tabs.active_view());
    actions
        .iter()
        .map(|action| action_button(s, action, p))
        .collect()
}

fn action_button(s: &Browser, action: &ActionButton, p: &Palette) -> El {
    let open = s.extensions.popup_open_for(&action.extension_id);
    let mut button = el()
        .key(action_button_key(&action.extension_id))
        .role("button")
        .label(action.title.clone())
        .help(action.title.clone())
        .checked(open)
        .size(BUTTON_SIZE, BUTTON_SIZE)
        .radius(8.0)
        .center()
        .focusable(false);
    let glyph = match &action.icon {
        Some(pixels) => el()
            .size(ACTION_ICON_SIZE, ACTION_ICON_SIZE)
            .pixels(pixels)
            .a11y_hidden(),
        None => icon(&PUZZLE, ACTION_ICON_SIZE).color(p.icon).a11y_hidden(),
    };
    button = button.child(if action.enabled {
        glyph
    } else {
        glyph.opacity(0.4)
    });
    if let Some(badge) = action.shown_badge() {
        button = button.child(
            row()
                .key("badge")
                .pin(None, None, Some(1.0), Some(2.0))
                .min_w(14.0)
                .h(13.0)
                .px(3.0)
                .radius(6.5)
                .center()
                .fill(color_from_rgba(action.badge_background))
                .inert()
                .child(
                    text(badge)
                        .font(BADGE_FONT)
                        .color(color_from_rgba(action.badge_ink()))
                        .a11y_hidden(),
                ),
        );
    }
    let extension_id = action.extension_id.clone();
    let menu_id = action.extension_id.clone();
    button = button.on_secondary_press(move |s: &mut Browser, at, tx| {
        s.show_extension_action_menu(&menu_id, at, tx)
    });
    if open {
        button = button.fill(p.press);
    }
    if action.enabled {
        button
            .hover(p.hover)
            .press(p.press)
            .on_press(move |s: &mut Browser| s.extension_action_pressed(&extension_id))
    } else {
        button.disabled(true)
    }
}

pub fn extension_overlays(s: &Browser, p: &Palette) -> Vec<El> {
    let mut overlays = Vec::new();
    if let Some(popup) = &s.extensions.popup {
        overlays.push(popup_overlay(popup, p));
    }
    if s.extensions.dialog_open {
        overlays.push(extensions_dialog(s, p));
    }
    if let Some(confirmation) = &s.extensions.confirmation {
        overlays.push(confirmation_card(confirmation, p));
    }
    if let Some(prompt) = s.extensions.prompts.front() {
        overlays.push(permission_prompt(prompt, p));
    }
    if let Some(error) = &s.extensions.error {
        overlays.push(error_card(error, p));
    }
    overlays
}

fn popup_overlay(popup: &Popup, p: &Palette) -> El {
    let page = el()
        .key("extension-popup-page")
        .role("document")
        .size(popup.size.0, popup.size.1)
        .pixels_fit(&popup.pixels, SurfaceFit::Fill)
        .surface_anchor(Align::Start, Align::Start)
        .surface_unscaled(true)
        .cursor(crate::view::page::cursor_for(popup.cursor))
        .focusable(true)
        .no_focus_ring()
        .pointer_buttons([PointerButton::Middle])
        .on_drag(|s: &mut Browser, drag, tx| {
            s.popup_drag(drag, tx);
            if drag.phase == rune::Phase::Move {
                tx.live_only();
            }
        })
        .on_pointer_move_local(|s: &mut Browser, pointer, tx| {
            s.popup_pointer_moved(pointer, tx);
            tx.live_only();
        })
        .on_hover_change(|s: &mut Browser, hovered, tx| s.popup_hover_changed(hovered, tx))
        .on_secondary_press_local(|s: &mut Browser, pointer, tx| {
            s.popup_secondary_click(pointer, tx)
        })
        .on_wheel(|s: &mut Browser, wheel, tx| {
            s.popup_wheel(wheel);
            tx.live_only();
            true
        })
        .on_key(|s: &mut Browser, event, tx| s.popup_key(event, tx, KeyEventKind::Down))
        .on_key_up(|s: &mut Browser, event, tx| s.popup_key(event, tx, KeyEventKind::Up))
        .on_layout(|s: &mut Browser, _| s.extension_popup_laid_out());
    panel_shadow(
        col()
            .key("extension-popup")
            .role("dialog")
            .label("Extension")
            .padding(1.0)
            .fill(p.surface)
            .radius(8.0)
            .border(p.hairline, p.bubble_border)
            .anchored(
                action_button_key(&popup.extension_id),
                Side::Bottom,
                Align::End,
                6.0,
            )
            .modal(true, |s: &mut Browser| s.close_extension_popup())
            .child(page),
    )
}

fn scrim(key: &'static str, panel: El) -> El {
    col()
        .key(key)
        .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
        .center()
        .padding(24.0)
        .fill(Color::BLACK.alpha(0.3))
        .cursor(Cursor::Default)
        .on_wheel(|_: &mut Browser, _, _| true)
        .child(panel)
}

fn card(key: &'static str, label: String, width: f32, p: &Palette) -> El {
    col()
        .key(key)
        .role("alertdialog")
        .label(label)
        .w(width)
        .shrink()
        .padding(20.0)
        .gap(12.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .items_stretch()
}

fn button_row(key: &'static str, buttons: Vec<El>) -> El {
    row()
        .key(key)
        .gap(8.0)
        .center_y()
        .child(el().grow(1.0).a11y_hidden())
        .children(buttons)
}

fn permission_prompt(prompt: &PermissionPrompt, p: &Palette) -> El {
    let question = format!("Allow “{}” to:", prompt.extension_name);
    let mut panel = card("extension-permissions", question.clone(), PROMPT_WIDTH, p)
        .modal(true, |s: &mut Browser| {
            s.answer_extension_permissions(false)
        })
        .child(
            text(question)
                .key("extension-permissions-title")
                .font(TITLE_FONT)
                .color(p.text)
                .wrap(),
        );
    for (index, warning) in prompt.warnings.iter().enumerate() {
        panel = panel.child(
            text(format!("• {warning}"))
                .key(format!("extension-permission-{index}"))
                .font(BODY_FONT)
                .color(p.text)
                .wrap(),
        );
    }
    panel = panel.child(button_row(
        "extension-permissions-buttons",
        vec![
            button(
                "extension-permissions-deny",
                "Deny",
                ButtonStyle::Secondary,
                p,
                |s, _| s.answer_extension_permissions(false),
            ),
            button(
                "extension-permissions-allow",
                "Allow",
                ButtonStyle::Primary,
                p,
                |s, _| s.answer_extension_permissions(true),
            ),
        ],
    ));
    scrim("extension-permissions-scrim", panel_shadow(panel))
}

fn confirmation_card(confirmation: &Confirmation, p: &Palette) -> El {
    let (title, message, accept) = match confirmation {
        Confirmation::RemoveExtension { name, .. } => (
            "Remove extension?",
            format!("Remove {name}? Its data in Xenon is deleted too."),
            "Remove",
        ),
        Confirmation::ChangePasswordManager { current, .. } => (
            "Change password manager?",
            format!(
                "Remove {current}? Its data in Xenon is deleted too, so it would need signing in again."
            ),
            "Change",
        ),
    };
    let panel = card("extension-confirmation", title.to_owned(), PROMPT_WIDTH, p)
        .modal(true, |s: &mut Browser| s.answer_confirmation(false))
        .child(text(title).font(TITLE_FONT).color(p.text))
        .child(text(message).font(BODY_FONT).color(p.text).wrap())
        .child(button_row(
            "extension-confirmation-buttons",
            vec![
                button(
                    "extension-confirmation-cancel",
                    "Cancel",
                    ButtonStyle::Secondary,
                    p,
                    |s, _| s.answer_confirmation(false),
                ),
                button(
                    "extension-confirmation-accept",
                    accept,
                    ButtonStyle::Primary,
                    p,
                    |s, _| s.answer_confirmation(true),
                ),
            ],
        ));
    scrim("extension-confirmation-scrim", panel_shadow(panel))
}

fn error_card(error: &ErrorNotice, p: &Palette) -> El {
    let panel = card("extension-error", error.title.clone(), PROMPT_WIDTH, p)
        .modal(true, |s: &mut Browser| s.dismiss_extension_error())
        .child(text(error.title.clone()).font(TITLE_FONT).color(p.text))
        .child(
            text(error.message.clone())
                .font(BODY_FONT)
                .color(p.text)
                .wrap()
                .selectable(true),
        )
        .child(button_row(
            "extension-error-buttons",
            vec![
                button(
                    "extension-error-ok",
                    "OK",
                    ButtonStyle::Primary,
                    p,
                    |s, _| s.dismiss_extension_error(),
                )
                .default_button(),
            ],
        ));
    scrim("extension-error-scrim", panel_shadow(panel))
}

fn extensions_dialog(s: &Browser, p: &Palette) -> El {
    let header = row()
        .key("extensions-header")
        .gap(10.0)
        .center_y()
        .child(
            text("Extensions")
                .font(HEADING_FONT)
                .color(p.text)
                .grow(1.0),
        )
        .child(
            button(
                "extensions-load-unpacked",
                "Load Unpacked…",
                ButtonStyle::Primary,
                p,
                |s, tx| s.load_unpacked_extension(tx),
            )
            .help("Load an extension from its directory, as Chrome's developer mode does"),
        )
        .child(icon_button(
            "extensions-close",
            "Close",
            &icons::CLOSE,
            true,
            p,
            |s, _| s.close_extensions_dialog(),
        ));

    let mut list = col().key("extensions-list").w_full().gap(10.0);
    if s.extensions.installed.is_empty() {
        list = list.child(
            text("No extensions yet. Load an unpacked extension from its directory to try one.")
                .key("extensions-empty")
                .font(BODY_FONT)
                .color(p.text_muted)
                .wrap(),
        );
    }
    for extension in &s.extensions.installed {
        list = list.child(extension_row(extension, p));
    }

    let panel = col()
        .key("extensions-dialog")
        .role("dialog")
        .label("Extensions")
        .w(DIALOG_WIDTH)
        .h(DIALOG_HEIGHT)
        .shrink()
        .min_h(0.0)
        .padding(20.0)
        .gap(14.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .items_stretch()
        .modal(true, |s: &mut Browser| s.close_extensions_dialog())
        .child(header)
        .child(password_manager_row(s, p))
        .child(
            col()
                .key("extensions-scroll")
                .w_full()
                .grow(1.0)
                .shrink()
                .min_h(0.0)
                .scroll(ScrollSpec {
                    stick_bottom: false,
                })
                .child(list),
        );
    scrim("extensions-scrim", panel_shadow(panel))
}

fn password_manager_row(s: &Browser, p: &Palette) -> El {
    let managers = &s.extensions.password_managers;
    let shown = managers.shown();
    let name = if shown.is_empty() {
        "None".to_owned()
    } else {
        managers.name_of(shown).unwrap_or(shown).to_owned()
    };
    let installing = !managers.installing.is_empty();
    let mut picker = row()
        .key("extensions-password-manager")
        .role("combobox")
        .label("Password manager")
        .h(30.0)
        .pl(12.0)
        .pr(8.0)
        .gap(6.0)
        .radius(8.0)
        .center_y()
        .fill(p.field)
        .border(p.hairline, p.separator)
        .child(text(name).font(BODY_FONT).color(p.text).a11y_hidden())
        .child(icon(&CHEVRON_DOWN, 14.0).color(p.icon).a11y_hidden());
    picker = if installing {
        picker.disabled(true).opacity(0.6)
    } else {
        picker
            .hover(p.hover)
            .press(p.press)
            .on_press_tx(|s: &mut Browser, tx| s.show_password_manager_menu(tx))
    };
    row()
        .key("extensions-password-manager-row")
        .gap(10.0)
        .center_y()
        .child(text("Password manager").font(BODY_FONT).color(p.text))
        .child(picker)
        .child(
            text(managers.status.clone())
                .key("extensions-password-manager-status")
                .font(SMALL_FONT)
                .color(p.text_muted)
                .wrap()
                .grow(1.0)
                .shrink(),
        )
}

fn extension_row(extension: &ExtensionInfo, p: &Palette) -> El {
    let key = |part: &str| format!("extension-{}-{part}", extension.id);
    let glyph = match &extension.icon {
        Some(pixels) => el()
            .size(DIALOG_ICON_SIZE, DIALOG_ICON_SIZE)
            .pixels(pixels)
            .a11y_hidden(),
        None => el()
            .size(DIALOG_ICON_SIZE, DIALOG_ICON_SIZE)
            .center()
            .child(icon(&PUZZLE, 28.0).color(p.text_muted))
            .a11y_hidden(),
    };
    let mut details = col()
        .key(key("text"))
        .gap(3.0)
        .grow(1.0)
        .shrink()
        .min_w(0.0)
        .child(
            row()
                .gap(8.0)
                .center_y()
                .child(text(extension.name.clone()).font(NAME_FONT).color(p.text))
                .child(
                    text(extension.version.clone())
                        .font(SMALL_FONT)
                        .color(p.text_muted),
                ),
        );
    if !extension.description.is_empty() {
        details = details.child(
            text(extension.description.clone())
                .font(BODY_FONT)
                .color(p.text)
                .wrap(),
        );
    }
    details = details.child(
        text(extension.details.clone())
            .key(key("details"))
            .font(SMALL_FONT)
            .color(p.text_muted)
            .wrap()
            .selectable(true),
    );
    for warning in &extension.warnings {
        details = details.child(
            text(warning.clone())
                .font(SMALL_FONT)
                .color(p.accent)
                .wrap(),
        );
    }

    let reload_id = extension.id.clone();
    let remove_id = extension.id.clone();
    let remove_name = extension.name.clone();
    let mut buttons = col().key(key("buttons")).gap(6.0);
    buttons = buttons.child(
        button(
            key("reload"),
            "Reload",
            ButtonStyle::Secondary,
            p,
            move |s, _| s.reload_extension(&reload_id),
        )
        .help("Load the extension again from its directory"),
    );
    if extension.has_options_page {
        let options_id = extension.id.clone();
        buttons = buttons.child(button(
            key("options"),
            "Options",
            ButtonStyle::Secondary,
            p,
            move |_, _| crate::extensions::ExtensionsEngine.open_options(&options_id),
        ));
    }
    buttons = buttons.child(button(
        key("remove"),
        "Remove",
        ButtonStyle::Secondary,
        p,
        move |s, _| s.confirm_remove_extension(&remove_id, &remove_name),
    ));

    row()
        .key(key("row"))
        .w_full()
        .padding(12.0)
        .gap(12.0)
        .radius(10.0)
        .border(p.hairline, p.separator)
        .child(glyph)
        .child(details)
        .child(buttons)
}
