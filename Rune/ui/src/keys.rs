/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Key, Modifier, Mods, PhysicalKeyCode};

use crate::engine::{KeyEvent, KeyEventKind, Modifiers};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Shortcut {
    NewTab,
    CloseTab,
    FocusLocation,
    Reload,
    Back,
    Forward,
    NextTab,
    PreviousTab,
    SelectTab(usize),
    LastTab,
    LessRecentTab,
    MoreRecentTab,
    ReopenClosedTab,
    NewWindow,
    NewPrivateWindow,
    CloseWindow,
    NextWindow,
    MoveTabLeft,
    MoveTabRight,
    ZoomIn,
    ZoomOut,
    ResetZoom,
    HardReload,
    ToggleBookmark,
    BookmarkAllTabs,
    ToggleBookmarksBar,
    OpenSettings,
    ShowHistory,
    ShowDownloads,
    ClearBrowsingData,
    ViewSource,
    ToggleDevTools,
    OpenFile,
    FindNext,
    FindPrevious,
    ToggleFullscreen,
    Quit,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Platform {
    Mac,
    Other,
}

impl Platform {
    pub const CURRENT: Platform = if cfg!(target_os = "macos") {
        Platform::Mac
    } else {
        Platform::Other
    };
}

pub fn shortcut_for(key: &Key, mods: Mods, platform: Platform) -> Option<Shortcut> {
    let primary_only = mods.cmd && !mods.alt && !mods.shift;
    let alt_only = mods.alt && !mods.cmd && !mods.ctrl && !mods.shift && !mods.logo;
    let primary_shift = mods.cmd && mods.shift && !mods.alt;
    let mac = platform == Platform::Mac;
    let primary_alt = mods.cmd && mods.alt && !mods.shift;
    match key {
        Key::Command('=' | '+') if mods.cmd && !mods.alt => Some(Shortcut::ZoomIn),
        Key::Command('-' | '_') if mods.cmd && !mods.alt => Some(Shortcut::ZoomOut),
        Key::Command('0') if primary_only => Some(Shortcut::ResetZoom),
        Key::Command('r') if primary_shift => Some(Shortcut::HardReload),
        Key::Command('r') if primary_alt && mac => Some(Shortcut::HardReload),
        Key::Function(5) if (mods.ctrl || mods.shift) && !mods.alt => Some(Shortcut::HardReload),
        Key::Command('d') if primary_only => Some(Shortcut::ToggleBookmark),
        Key::Command('d') if primary_shift => Some(Shortcut::BookmarkAllTabs),
        Key::Command('b') if primary_shift => Some(Shortcut::ToggleBookmarksBar),
        Key::Command(',') if primary_only => Some(Shortcut::OpenSettings),
        Key::Command('y') if primary_only && mac => Some(Shortcut::ShowHistory),
        Key::Command('h') if primary_only && !mac => Some(Shortcut::ShowHistory),
        Key::Command('j') if primary_only && !mac => Some(Shortcut::ShowDownloads),
        Key::Command('j') if primary_shift && mac => Some(Shortcut::ShowDownloads),
        Key::Command('u') if primary_only && !mac => Some(Shortcut::ViewSource),
        Key::Command('u') if primary_alt && mac => Some(Shortcut::ViewSource),
        Key::Command('i') if primary_shift && !mac => Some(Shortcut::ToggleDevTools),
        Key::Command('i') if primary_alt && mac => Some(Shortcut::ToggleDevTools),
        Key::Function(12) if !mods.cmd && !mods.alt && !mods.shift => {
            Some(Shortcut::ToggleDevTools)
        }
        Key::Command('o') if primary_only => Some(Shortcut::OpenFile),
        Key::Command('g') if primary_only => Some(Shortcut::FindNext),
        Key::Command('g') if primary_shift => Some(Shortcut::FindPrevious),
        Key::Function(3) if !mods.cmd && !mods.alt && !mods.shift => Some(Shortcut::FindNext),
        Key::Function(3) if mods.shift && !mods.cmd && !mods.alt => Some(Shortcut::FindPrevious),
        Key::Command('f') if mac && mods.cmd && mods.ctrl && !mods.shift && !mods.alt => {
            Some(Shortcut::ToggleFullscreen)
        }
        Key::Function(11) if !mac && !mods.cmd && !mods.alt && !mods.shift => {
            Some(Shortcut::ToggleFullscreen)
        }
        Key::Command('q') if primary_only && !mac => Some(Shortcut::Quit),
        Key::Command(']') if primary_shift => Some(Shortcut::NextTab),
        Key::Command('}') if mods.cmd && !mods.alt => Some(Shortcut::NextTab),
        Key::Command('[') if primary_shift => Some(Shortcut::PreviousTab),
        Key::Command('{') if mods.cmd && !mods.alt => Some(Shortcut::PreviousTab),
        Key::Command('t') if primary_shift => Some(Shortcut::ReopenClosedTab),
        Key::Command('n') if primary_only => Some(Shortcut::NewWindow),
        Key::Command('n') if primary_shift => Some(Shortcut::NewPrivateWindow),
        Key::Command('w') if primary_shift => Some(Shortcut::CloseWindow),
        Key::Command('`') if primary_only => Some(Shortcut::NextWindow),
        Key::Command('t') if primary_only => Some(Shortcut::NewTab),
        Key::Command('w') if primary_only => Some(Shortcut::CloseTab),
        Key::Command('l') if primary_only => Some(Shortcut::FocusLocation),
        Key::Command('r') if primary_only => Some(Shortcut::Reload),
        Key::Function(5) if !mods.cmd && !mods.alt => Some(Shortcut::Reload),
        Key::Function(6) if !mods.cmd && !mods.alt && !mods.shift => Some(Shortcut::FocusLocation),
        Key::Command(digit @ '1'..='8') if primary_only => {
            Some(Shortcut::SelectTab(*digit as usize - '1' as usize))
        }
        Key::Command('9') if primary_only => Some(Shortcut::LastTab),
        Key::Command('[') if platform == Platform::Mac && primary_only => Some(Shortcut::Back),
        Key::Command(']') if platform == Platform::Mac && primary_only => Some(Shortcut::Forward),
        Key::Left if alt_only && platform == Platform::Other => Some(Shortcut::Back),
        Key::Right if alt_only && platform == Platform::Other => Some(Shortcut::Forward),
        Key::Tab if mods.ctrl && !mods.alt && !mods.logo => Some(if mods.shift {
            Shortcut::MoreRecentTab
        } else {
            Shortcut::LessRecentTab
        }),
        Key::PageDown if mods.ctrl && mods.shift && !mods.alt => Some(Shortcut::MoveTabRight),
        Key::PageUp if mods.ctrl && mods.shift && !mods.alt => Some(Shortcut::MoveTabLeft),
        Key::PageDown if mods.ctrl && !mods.shift && !mods.alt => Some(Shortcut::NextTab),
        Key::PageUp if mods.ctrl && !mods.shift && !mods.alt => Some(Shortcut::PreviousTab),
        _ => None,
    }
}

pub fn page_key_event(
    kind: KeyEventKind,
    key: &Key,
    mods: Mods,
    physical: Option<PhysicalKeyCode>,
    repeat: bool,
) -> Option<KeyEvent> {
    if let Key::Modifier { modifier, pressed } = key {
        return Some(KeyEvent {
            kind: if *pressed {
                KeyEventKind::Down
            } else {
                KeyEventKind::Up
            },
            code: modifier_code(*modifier).to_owned(),
            key: modifier_key(*modifier).to_owned(),
            modifiers: Modifiers::from_rune(mods),
            repeat: false,
        });
    }
    let key_name = key_name(key, mods)?;
    let code = physical
        .map(code_name)
        .filter(|code| *code != UNIDENTIFIED)
        .map_or_else(|| code_from_key(&key_name), str::to_owned);
    Some(KeyEvent {
        kind,
        code,
        key: key_name,
        modifiers: Modifiers::from_rune(mods),
        repeat,
    })
}

const UNIDENTIFIED: &str = "Unidentified";

fn key_name(key: &Key, mods: Mods) -> Option<String> {
    let named = match key {
        Key::Text(text) => return Some(text.clone()),
        Key::Command(c) if mods.shift => return Some(c.to_uppercase().collect()),
        Key::Command(c) => return Some(c.to_string()),
        Key::Function(n) => return Some(format!("F{n}")),
        Key::Enter => "Enter",
        Key::Backspace => "Backspace",
        Key::Delete => "Delete",
        Key::Left => "ArrowLeft",
        Key::Right => "ArrowRight",
        Key::Up => "ArrowUp",
        Key::Down => "ArrowDown",
        Key::Home => "Home",
        Key::End => "End",
        Key::PageUp => "PageUp",
        Key::PageDown => "PageDown",
        Key::Insert => "Insert",
        Key::PrintScreen => "PrintScreen",
        Key::ScrollLock => "ScrollLock",
        Key::Pause => "Pause",
        Key::Escape => "Escape",
        Key::Tab => "Tab",
        Key::ContextMenu => "ContextMenu",
        _ => return None,
    };
    Some(named.to_owned())
}

fn code_from_key(key: &str) -> String {
    let mut chars = key.chars();
    match (chars.next(), chars.next()) {
        (Some(' '), None) => "Space".to_owned(),
        (Some(c), None) if c.is_ascii_alphabetic() => format!("Key{}", c.to_ascii_uppercase()),
        (Some(c), None) if c.is_ascii_digit() => format!("Digit{c}"),
        (Some(_), None) => UNIDENTIFIED.to_owned(),
        _ if key.starts_with('F') && key[1..].parse::<u8>().is_ok() => key.to_owned(),
        _ => match key {
            "Enter" | "Backspace" | "Delete" | "ArrowLeft" | "ArrowRight" | "ArrowUp"
            | "ArrowDown" | "Home" | "End" | "PageUp" | "PageDown" | "Insert" | "PrintScreen"
            | "ScrollLock" | "Pause" | "Escape" | "Tab" | "ContextMenu" => key.to_owned(),
            _ => UNIDENTIFIED.to_owned(),
        },
    }
}

fn modifier_code(modifier: Modifier) -> &'static str {
    match modifier {
        Modifier::ShiftLeft => "ShiftLeft",
        Modifier::ShiftRight => "ShiftRight",
        Modifier::ControlLeft => "ControlLeft",
        Modifier::ControlRight => "ControlRight",
        Modifier::AltLeft => "AltLeft",
        Modifier::AltRight => "AltRight",
        Modifier::SuperLeft => "MetaLeft",
        Modifier::SuperRight => "MetaRight",
        _ => "Fn",
    }
}

fn modifier_key(modifier: Modifier) -> &'static str {
    match modifier {
        Modifier::ShiftLeft | Modifier::ShiftRight => "Shift",
        Modifier::ControlLeft | Modifier::ControlRight => "Control",
        Modifier::AltLeft | Modifier::AltRight => "Alt",
        Modifier::SuperLeft | Modifier::SuperRight => "Meta",
        _ => "Fn",
    }
}

macro_rules! code_names {
    ($code:expr; $($same:ident),* $(,)?; $($renamed:ident => $name:literal),* $(,)?) => {
        match $code {
            $(PhysicalKeyCode::$same => stringify!($same),)*
            $(PhysicalKeyCode::$renamed => $name,)*
            _ => UNIDENTIFIED,
        }
    };
}

pub fn code_name(code: PhysicalKeyCode) -> &'static str {
    code_names!(code;
        Backquote, Backslash, BracketLeft, BracketRight, Comma, Equal, IntlBackslash, IntlRo, IntlYen, Minus,
        Period, Quote, Semicolon, Slash,
        Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
        KeyA, KeyB, KeyC, KeyD, KeyE, KeyF, KeyG, KeyH, KeyI, KeyJ, KeyK, KeyL, KeyM, KeyN, KeyO, KeyP, KeyQ,
        KeyR, KeyS, KeyT, KeyU, KeyV, KeyW, KeyX, KeyY, KeyZ,
        AltLeft, AltRight, Backspace, CapsLock, ContextMenu, ControlLeft, ControlRight, Enter, ShiftLeft,
        ShiftRight, Space, Tab, Convert, KanaMode, Lang1, Lang2, NonConvert,
        Delete, End, Help, Home, Insert, PageDown, PageUp, ArrowDown, ArrowLeft, ArrowRight, ArrowUp,
        NumLock, Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
        NumpadAdd, NumpadComma, NumpadDecimal, NumpadDivide, NumpadEnter, NumpadEqual, NumpadMultiply,
        NumpadSubtract,
        Escape, Fn, PrintScreen, ScrollLock, Pause,
        BrowserBack, BrowserForward, BrowserHome, BrowserRefresh, BrowserSearch, BrowserStop,
        AudioVolumeDown, AudioVolumeMute, AudioVolumeUp, MediaPlayPause, MediaStop, MediaTrackNext,
        MediaTrackPrevious,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22,
        F23, F24;
        SuperLeft => "MetaLeft",
        SuperRight => "MetaRight",
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn mods(f: impl FnOnce(&mut Mods)) -> Mods {
        let mut mods = Mods::default();
        f(&mut mods);
        mods
    }

    fn primary() -> Mods {
        mods(|m| {
            m.cmd = true;
            m.ctrl = true;
        })
    }

    fn down(key: Key, mods: Mods, physical: Option<PhysicalKeyCode>) -> KeyEvent {
        page_key_event(KeyEventKind::Down, &key, mods, physical, false).unwrap()
    }

    #[test]
    fn primary_letter_shortcuts() {
        let at = |c| shortcut_for(&Key::Command(c), primary(), Platform::Other);
        assert_eq!(at('t'), Some(Shortcut::NewTab));
        assert_eq!(at('w'), Some(Shortcut::CloseTab));
        assert_eq!(at('l'), Some(Shortcut::FocusLocation));
        assert_eq!(at('r'), Some(Shortcut::Reload));
        assert_eq!(at('x'), None);
    }

    #[test]
    fn shift_keeps_primary_letters_for_the_page() {
        let shifted = mods(|m| {
            m.cmd = true;
            m.shift = true;
        });
        assert_eq!(
            shortcut_for(&Key::Command('l'), shifted, Platform::Other),
            None
        );
    }

    #[test]
    fn digits_select_tabs_and_nine_selects_the_last() {
        assert_eq!(
            shortcut_for(&Key::Command('1'), primary(), Platform::Other),
            Some(Shortcut::SelectTab(0))
        );
        assert_eq!(
            shortcut_for(&Key::Command('8'), primary(), Platform::Other),
            Some(Shortcut::SelectTab(7))
        );
        assert_eq!(
            shortcut_for(&Key::Command('9'), primary(), Platform::Other),
            Some(Shortcut::LastTab)
        );
    }

    #[test]
    fn history_shortcuts_follow_the_platform() {
        let alt = mods(|m| m.alt = true);
        assert_eq!(
            shortcut_for(&Key::Left, alt, Platform::Other),
            Some(Shortcut::Back)
        );
        assert_eq!(
            shortcut_for(&Key::Right, alt, Platform::Other),
            Some(Shortcut::Forward)
        );
        assert_eq!(shortcut_for(&Key::Left, alt, Platform::Mac), None);
        let cmd = mods(|m| {
            m.cmd = true;
            m.logo = true;
        });
        assert_eq!(
            shortcut_for(&Key::Command('['), cmd, Platform::Mac),
            Some(Shortcut::Back)
        );
        assert_eq!(
            shortcut_for(&Key::Command(']'), cmd, Platform::Mac),
            Some(Shortcut::Forward)
        );
        assert_eq!(
            shortcut_for(&Key::Command('['), primary(), Platform::Other),
            None
        );
    }

    #[test]
    fn control_tab_cycles_tabs_in_recently_used_order() {
        let ctrl = mods(|m| m.ctrl = true);
        let ctrl_shift = mods(|m| {
            m.ctrl = true;
            m.shift = true;
        });
        assert_eq!(
            shortcut_for(&Key::Tab, ctrl, Platform::Mac),
            Some(Shortcut::LessRecentTab)
        );
        assert_eq!(
            shortcut_for(&Key::Tab, ctrl_shift, Platform::Other),
            Some(Shortcut::MoreRecentTab)
        );
        assert_eq!(
            shortcut_for(&Key::Tab, Mods::default(), Platform::Other),
            None
        );
    }

    #[test]
    fn shifted_shortcuts_reopen_tabs_and_open_windows() {
        let primary_shift = mods(|m| {
            m.cmd = true;
            m.shift = true;
        });
        assert_eq!(
            shortcut_for(&Key::Command('t'), primary_shift, Platform::Other),
            Some(Shortcut::ReopenClosedTab)
        );
        assert_eq!(
            shortcut_for(&Key::Command('n'), primary(), Platform::Other),
            Some(Shortcut::NewWindow)
        );
        assert_eq!(
            shortcut_for(&Key::Command('n'), primary_shift, Platform::Other),
            Some(Shortcut::NewPrivateWindow)
        );
        assert_eq!(
            shortcut_for(&Key::Command('w'), primary_shift, Platform::Other),
            Some(Shortcut::CloseWindow)
        );
        assert_eq!(
            shortcut_for(&Key::Command('`'), primary(), Platform::Mac),
            Some(Shortcut::NextWindow)
        );
        let ctrl_shift = mods(|m| {
            m.ctrl = true;
            m.shift = true;
        });
        assert_eq!(
            shortcut_for(&Key::PageUp, ctrl_shift, Platform::Other),
            Some(Shortcut::MoveTabLeft)
        );
        assert_eq!(
            shortcut_for(&Key::PageDown, ctrl_shift, Platform::Other),
            Some(Shortcut::MoveTabRight)
        );
    }

    #[test]
    fn function_keys_reload_and_focus_the_location() {
        assert_eq!(
            shortcut_for(&Key::Function(5), Mods::default(), Platform::Other),
            Some(Shortcut::Reload)
        );
        assert_eq!(
            shortcut_for(&Key::Function(6), Mods::default(), Platform::Other),
            Some(Shortcut::FocusLocation)
        );
    }

    #[test]
    fn typed_text_carries_its_physical_code() {
        let event = down(
            Key::Text("a".into()),
            Mods::default(),
            Some(PhysicalKeyCode::KeyA),
        );
        assert_eq!((event.code.as_str(), event.key.as_str()), ("KeyA", "a"));
        let shifted = down(
            Key::Text("&".into()),
            mods(|m| m.shift = true),
            Some(PhysicalKeyCode::Digit7),
        );
        assert_eq!(
            (shifted.code.as_str(), shifted.key.as_str()),
            ("Digit7", "&")
        );
        assert_eq!(
            shifted.modifiers,
            Modifiers(crate::ffi::XENON_MODIFIER_SHIFT)
        );
    }

    #[test]
    fn named_keys_use_ui_events_names() {
        let event = down(Key::Left, Mods::default(), Some(PhysicalKeyCode::ArrowLeft));
        assert_eq!(
            (event.code.as_str(), event.key.as_str()),
            ("ArrowLeft", "ArrowLeft")
        );
        let enter = down(
            Key::Enter,
            Mods::default(),
            Some(PhysicalKeyCode::NumpadEnter),
        );
        assert_eq!(
            (enter.code.as_str(), enter.key.as_str()),
            ("NumpadEnter", "Enter")
        );
        let f5 = down(Key::Function(5), Mods::default(), None);
        assert_eq!((f5.code.as_str(), f5.key.as_str()), ("F5", "F5"));
    }

    #[test]
    fn keys_without_a_physical_origin_derive_their_code() {
        assert_eq!(
            down(Key::Text("q".into()), Mods::default(), None).code,
            "KeyQ"
        );
        assert_eq!(
            down(Key::Text("4".into()), Mods::default(), None).code,
            "Digit4"
        );
        assert_eq!(
            down(Key::Text(" ".into()), Mods::default(), None).code,
            "Space"
        );
        assert_eq!(
            down(Key::Text("é".into()), Mods::default(), None).code,
            "Unidentified"
        );
        assert_eq!(down(Key::Escape, Mods::default(), None).code, "Escape");
    }

    #[test]
    fn command_characters_reach_the_page_as_their_letter() {
        let event = down(Key::Command('c'), primary(), Some(PhysicalKeyCode::KeyC));
        assert_eq!((event.code.as_str(), event.key.as_str()), ("KeyC", "c"));
        assert_eq!(event.modifiers, Modifiers(crate::ffi::XENON_MODIFIER_CTRL));
        let shifted = mods(|m| {
            m.cmd = true;
            m.ctrl = true;
            m.shift = true;
        });
        assert_eq!(down(Key::Command('i'), shifted, None).key, "I");
    }

    #[test]
    fn modifier_keys_alone_report_their_side() {
        let key = Key::Modifier {
            modifier: Modifier::SuperRight,
            pressed: false,
        };
        let event = page_key_event(KeyEventKind::Down, &key, Mods::default(), None, false).unwrap();
        assert_eq!(
            (event.kind, event.code.as_str(), event.key.as_str()),
            (KeyEventKind::Up, "MetaRight", "Meta")
        );
    }

    #[test]
    fn physical_codes_follow_ui_events() {
        assert_eq!(code_name(PhysicalKeyCode::SuperLeft), "MetaLeft");
        assert_eq!(code_name(PhysicalKeyCode::BracketLeft), "BracketLeft");
        assert_eq!(code_name(PhysicalKeyCode::Numpad7), "Numpad7");
    }
}
