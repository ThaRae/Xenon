/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::HashMap;
use std::ffi::c_void;
use std::sync::Arc;

use rune::Pixels;

use crate::chrome_popup::{PopupItem, PopupMenu};
use crate::engine::ViewId;
use crate::ffi_hibernation::{
    self, XENON_TAB_HIBERNATED, XENON_TAB_NOT_LOADED, XenonHibernationCallbacks,
    XenonHibernationSettings, XenonTabIcon,
};
use crate::state::Browser;
use crate::window_slots::Slots;

pub const HIBERNATE_TAB_ITEM: &str = "hibernate";
const OFF_ITEM: &str = "hibernation-off";
const TIMEOUT_ITEM_PREFIX: &str = "hibernation-after-";
const OTHER_TIMEOUT_ITEM: &str = "hibernation-other";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LoadState {
    NotLoaded,
    Hibernated,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Settings {
    pub enabled: bool,
    pub timeout_seconds: u64,
    pub timeout_choices_in_minutes: Vec<u32>,
    pub on_memory_pressure: bool,
}

impl Default for Settings {
    fn default() -> Settings {
        Settings {
            enabled: true,
            timeout_seconds: 2 * 60 * 60,
            timeout_choices_in_minutes: vec![15, 30, 60, 2 * 60, 4 * 60, 6 * 60],
            on_memory_pressure: true,
        }
    }
}

struct UnloadedTab {
    state: LoadState,
    colour_favicon: Option<Pixels>,
}

#[derive(Default)]
pub struct TabHibernation {
    unloaded: HashMap<ViewId, UnloadedTab>,
    pub settings: Settings,
}

impl TabHibernation {
    pub fn state_of(&self, view: ViewId) -> Option<LoadState> {
        self.unloaded.get(&view).map(|tab| tab.state)
    }

    pub fn is_loaded(&self, view: ViewId) -> bool {
        self.state_of(view).is_none()
    }
}

pub fn timeout_description(seconds: u64) -> String {
    const HOUR: u64 = 60 * 60;
    if seconds.is_multiple_of(HOUR) {
        return if seconds == HOUR {
            "After 1 Hour".to_owned()
        } else {
            format!("After {} Hours", seconds / HOUR)
        };
    }
    if seconds.is_multiple_of(60) {
        return if seconds == 60 {
            "After 1 Minute".to_owned()
        } else {
            format!("After {} Minutes", seconds / 60)
        };
    }
    format!("After {seconds} Seconds")
}

pub fn settings_menu(settings: &Settings) -> PopupMenu {
    let mut menu = PopupMenu::new()
        .item(PopupItem::new(OFF_ITEM, "Off").checked(!settings.enabled))
        .separator();
    let mut matched = false;
    for minutes in &settings.timeout_choices_in_minutes {
        let seconds = u64::from(*minutes) * 60;
        let checked = settings.enabled && settings.timeout_seconds == seconds;
        matched |= checked;
        menu = menu.item(
            PopupItem::new(
                format!("{TIMEOUT_ITEM_PREFIX}{seconds}"),
                timeout_description(seconds),
            )
            .checked(checked),
        );
    }
    if settings.enabled && !matched {
        menu = menu.item(
            PopupItem::new(
                OTHER_TIMEOUT_ITEM,
                timeout_description(settings.timeout_seconds),
            )
            .enabled(false)
            .checked(true),
        );
    }
    menu
}

pub fn run_settings_menu_item(picked: &str) -> bool {
    if picked == OFF_ITEM {
        unsafe { ffi_hibernation::xenon_engine_set_tab_hibernation(false, 0) };
        return true;
    }
    let Some(seconds) = picked
        .strip_prefix(TIMEOUT_ITEM_PREFIX)
        .and_then(|seconds| seconds.parse::<u64>().ok())
    else {
        return false;
    };
    unsafe { ffi_hibernation::xenon_engine_set_tab_hibernation(true, seconds) };
    true
}

pub fn may_hibernate(s: &Browser, view: ViewId) -> bool {
    let Some(tab) = s.tabs.get(view) else {
        return false;
    };
    if s.split.is_shown(&s.tabs, view) || tab.pinned || tab.loading || tab.playing_audio {
        return false;
    }
    if !s.hibernation.is_loaded(view) {
        return false;
    }
    s.page_prompts
        .of(view)
        .is_none_or(|prompts| prompts.dialog.is_none() && prompts.permissions.is_empty())
}

pub fn hibernate_tab(s: &mut Browser, view: ViewId) {
    if may_hibernate(s, view) {
        s.hover_card = None;
        unsafe { ffi_hibernation::xenon_engine_hibernate_tab(view) };
    }
}

pub fn hibernate_item(s: &Browser, view: ViewId) -> PopupItem {
    PopupItem::new(HIBERNATE_TAB_ITEM, "Hibernate Tab").enabled(may_hibernate(s, view))
}

fn load_state_changed(
    s: &mut Browser,
    view: ViewId,
    state: Option<LoadState>,
    icon: Option<Pixels>,
) {
    let window = s.windows.window_of(view, &s.tabs);
    let tab = window.and_then(|window| {
        s.windows
            .tabs_mut(window, &mut s.tabs)
            .and_then(|tabs| tabs.get_mut(view))
    });
    match state {
        None => {
            let Some(unloaded) = s.hibernation.unloaded.remove(&view) else {
                return;
            };
            if unloaded.state == LoadState::Hibernated
                && let Some(tab) = tab
            {
                tab.favicon = unloaded.colour_favicon;
            }
        }
        Some(LoadState::NotLoaded) => {
            s.hibernation.unloaded.insert(
                view,
                UnloadedTab {
                    state: LoadState::NotLoaded,
                    colour_favicon: None,
                },
            );
        }
        Some(LoadState::Hibernated) => {
            let mut colour_favicon = None;
            if let Some(tab) = tab {
                tab.sent_viewport.set(None);
                tab.loading = false;
                tab.unresponsive = false;
                tab.hovered_link.clear();
                colour_favicon = tab.favicon.clone();
                if icon.is_some() {
                    tab.favicon = icon;
                }
            }
            s.hibernation.unloaded.insert(
                view,
                UnloadedTab {
                    state: LoadState::Hibernated,
                    colour_favicon,
                },
            );
        }
    }
}

pub struct HibernationContext {
    handle: Arc<Slots>,
}

pub fn install(handle: Arc<Slots>) -> Arc<HibernationContext> {
    let context = Arc::new(HibernationContext { handle });
    let callbacks = XenonHibernationCallbacks {
        context: Arc::as_ptr(&context).cast_mut().cast(),
        tab_load_state_changed: Some(tab_load_state_changed),
        settings_changed: Some(settings_changed),
    };
    unsafe { ffi_hibernation::xenon_engine_set_hibernation_callbacks(&callbacks) };
    context
}

unsafe fn handle<'a>(context: *mut c_void) -> &'a Slots {
    unsafe { &(*context.cast::<HibernationContext>()).handle }
}

unsafe fn icon_pixels(icon: *const XenonTabIcon) -> Option<Pixels> {
    let icon = unsafe { icon.as_ref() }?;
    if icon.rgba.is_null() || icon.width == 0 || icon.height == 0 {
        return None;
    }
    let length = icon.width as usize * icon.height as usize * 4;
    let pixels = Pixels::new();
    pixels.set(
        icon.width,
        icon.height,
        unsafe { std::slice::from_raw_parts(icon.rgba, length) }.to_vec(),
    );
    Some(pixels)
}

unsafe extern "C" fn tab_load_state_changed(
    context: *mut c_void,
    view: ViewId,
    state: u32,
    icon: *const XenonTabIcon,
) {
    let state = match state {
        XENON_TAB_NOT_LOADED => Some(LoadState::NotLoaded),
        XENON_TAB_HIBERNATED => Some(LoadState::Hibernated),
        _ => None,
    };
    let icon = unsafe { icon_pixels(icon) };
    unsafe { handle(context) }.update(move |s, _| load_state_changed(s, view, state, icon.clone()));
}

unsafe extern "C" fn settings_changed(context: *mut c_void, raw: *const XenonHibernationSettings) {
    let Some(raw) = (unsafe { raw.as_ref() }) else {
        return;
    };
    let choices = if raw.timeout_choices_in_minutes.is_null() {
        Vec::new()
    } else {
        unsafe {
            std::slice::from_raw_parts(raw.timeout_choices_in_minutes, raw.timeout_choice_count)
        }
        .to_vec()
    };
    let settings = Settings {
        enabled: raw.enabled,
        timeout_seconds: raw.timeout_seconds,
        timeout_choices_in_minutes: choices,
        on_memory_pressure: raw.hibernate_on_memory_pressure,
    };
    unsafe { handle(context) }.update(move |s, _| s.hibernation.settings = settings.clone());
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tab_list::{Tab, WindowInfo};

    fn icon(size: u32) -> Pixels {
        let pixels = Pixels::new();
        pixels.set(size, size, vec![0; (size * size * 4) as usize]);
        pixels
    }

    fn browser() -> Browser {
        let mut s = crate::detached_browser();
        s.windows.opened(WindowInfo {
            id: 1,
            private: false,
        });
        for view in [1, 2] {
            let mut tab = Tab::new(view, "https://example.com", Pixels::new());
            tab.favicon = Some(icon(16));
            tab.loading = false;
            let end = s.tabs.iter().count();
            s.tabs.insert(tab, end);
        }
        s.tabs.activate(1);
        s
    }

    fn favicon_size(s: &Browser, view: ViewId) -> Option<(u32, u32)> {
        s.tabs.get(view)?.favicon.as_ref().map(Pixels::size)
    }

    #[test]
    fn a_hibernated_tab_shows_the_grey_favicon_until_it_loads_again() {
        let mut s = browser();
        assert!(hibernate_item(&s, 2).is_enabled());
        load_state_changed(&mut s, 2, Some(LoadState::Hibernated), Some(icon(8)));
        assert_eq!(s.hibernation.state_of(2), Some(LoadState::Hibernated));
        assert_eq!(favicon_size(&s, 2), Some((8, 8)));
        assert!(!hibernate_item(&s, 2).is_enabled());

        load_state_changed(&mut s, 2, None, None);
        assert!(s.hibernation.is_loaded(2));
        assert_eq!(favicon_size(&s, 2), Some((16, 16)));
    }

    #[test]
    fn a_tab_from_the_session_is_not_loaded_and_keeps_its_favicon() {
        let mut s = browser();
        load_state_changed(&mut s, 2, Some(LoadState::NotLoaded), None);
        assert_eq!(s.hibernation.state_of(2), Some(LoadState::NotLoaded));
        assert!(!may_hibernate(&s, 2));
        load_state_changed(&mut s, 2, None, None);
        assert_eq!(favicon_size(&s, 2), Some((16, 16)));
        assert!(may_hibernate(&s, 2));
    }

    #[test]
    fn the_active_pinned_loading_or_audible_tab_cannot_hibernate() {
        let mut s = browser();
        assert!(!may_hibernate(&s, 1));
        assert!(may_hibernate(&s, 2));
        let flags: [fn(&mut Tab) -> &mut bool; 3] = [
            |tab| &mut tab.pinned,
            |tab| &mut tab.loading,
            |tab| &mut tab.playing_audio,
        ];
        for flag in flags {
            *flag(s.tabs.get_mut(2).unwrap()) = true;
            assert!(!may_hibernate(&s, 2));
            *flag(s.tabs.get_mut(2).unwrap()) = false;
        }
        assert!(may_hibernate(&s, 2));
        assert!(!may_hibernate(&s, 42));
    }

    #[test]
    fn a_tab_shown_in_a_split_view_cannot_hibernate() {
        let mut s = browser();
        s.split.replace(&[(1, 2)]);
        assert!(!may_hibernate(&s, 2));
        s.split.replace(&[]);
        assert!(may_hibernate(&s, 2));
    }

    #[test]
    fn timeouts_are_described_in_the_largest_whole_unit() {
        assert_eq!(timeout_description(15 * 60), "After 15 Minutes");
        assert_eq!(timeout_description(60), "After 1 Minute");
        assert_eq!(timeout_description(60 * 60), "After 1 Hour");
        assert_eq!(timeout_description(6 * 60 * 60), "After 6 Hours");
        assert_eq!(timeout_description(90 * 60), "After 90 Minutes");
        assert_eq!(timeout_description(5), "After 5 Seconds");
    }

    #[test]
    fn the_settings_menu_checks_the_current_choice() {
        let menu = settings_menu(&Settings::default());
        assert!(!menu.find(OFF_ITEM).unwrap().is_checked());
        assert!(menu.find("hibernation-after-7200").unwrap().is_checked());
        assert_eq!(
            menu.find("hibernation-after-900").unwrap().title(),
            "After 15 Minutes"
        );
        assert!(menu.find(OTHER_TIMEOUT_ITEM).is_none());

        let off = settings_menu(&Settings {
            enabled: false,
            ..Settings::default()
        });
        assert!(off.find(OFF_ITEM).unwrap().is_checked());
        assert!(!off.find("hibernation-after-7200").unwrap().is_checked());
    }

    #[test]
    fn a_timeout_that_is_not_a_choice_shows_as_its_own_disabled_item() {
        let menu = settings_menu(&Settings {
            timeout_seconds: 5,
            ..Settings::default()
        });
        let other = menu.find(OTHER_TIMEOUT_ITEM).unwrap();
        assert_eq!(other.title(), "After 5 Seconds");
        assert!(other.is_checked());
        assert!(!other.is_enabled());
    }
}
