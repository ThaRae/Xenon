/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;
use std::ops::Range;
use std::sync::{Arc, OnceLock};

use rune::{AppHandle, ImageCache, Pixels};

use crate::engine::{Modifiers, ViewId};
use crate::ffi::{XenonStr, XenonViewId};
use crate::location::{
    Certificate, InputKind, LocationInput, SiteSecurity, SiteSecurityState, Suggestion,
    SuggestionKind, UrlInfo, UrlParts,
};
use crate::state::Browser;

pub const XENON_SITE_SECURITY_NONE: u32 = 0;
pub const XENON_SITE_SECURITY_SECURE: u32 = 1;
pub const XENON_SITE_SECURITY_NOT_SECURE: u32 = 2;
pub const XENON_SITE_SECURITY_ACCEPTED_CERTIFICATE: u32 = 3;

pub const XENON_LOCATION_INPUT_EMPTY: u32 = 0;
pub const XENON_LOCATION_INPUT_URL: u32 = 1;
pub const XENON_LOCATION_INPUT_SEARCH: u32 = 2;
pub const XENON_LOCATION_INPUT_OTHER: u32 = 3;

pub const XENON_SUGGESTION_URL: u32 = 0;
pub const XENON_SUGGESTION_INTERNAL_PAGE: u32 = 1;
pub const XENON_SUGGESTION_HISTORY: u32 = 2;
pub const XENON_SUGGESTION_BOOKMARK: u32 = 3;
pub const XENON_SUGGESTION_ADAPTIVE: u32 = 4;
pub const XENON_SUGGESTION_SEARCH: u32 = 5;
pub const XENON_SUGGESTION_CHOOSE_SEARCH_ENGINE: u32 = 6;

pub const XENON_LOCATION_SELECT_NEXT: u32 = 0;
pub const XENON_LOCATION_SELECT_PREVIOUS: u32 = 1;
pub const XENON_LOCATION_ACCEPT_COMPLETION: u32 = 2;
pub const XENON_LOCATION_ESCAPE: u32 = 3;
pub const XENON_LOCATION_DISMISS_POPUP: u32 = 4;
pub const XENON_LOCATION_DISMISS_BANG: u32 = 5;
pub const XENON_LOCATION_WILL_DELETE: u32 = 6;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonLocationUrl {
    pub url: XenonStr,
    pub scheme: XenonStr,
    pub has_parts: bool,
    pub scheme_and_subdomain: XenonStr,
    pub registrable_domain: XenonStr,
    pub remainder: XenonStr,
    pub hidden: bool,
    pub security: u32,
    pub search_engine: XenonStr,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonLocationInput {
    pub kind: u32,
    pub bang_name: XenonStr,
    pub bang_length: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonTextRange {
    pub start: usize,
    pub length: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonLocationSuggestion {
    pub kind: u32,
    pub text: XenonStr,
    pub primary: XenonStr,
    pub primary_matches: *const XenonTextRange,
    pub primary_match_count: usize,
    pub secondary: XenonStr,
    pub secondary_matches: *const XenonTextRange,
    pub secondary_match_count: usize,
    pub emphasize_origin: bool,
    pub favicon_png: *const u8,
    pub favicon_png_length: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonSiteSecurity {
    pub state: u32,
    pub is_final: bool,
    pub has_connection: bool,
    pub host: XenonStr,
    pub protocol: XenonStr,
    pub cipher: XenonStr,
    pub has_certificate: bool,
    pub issued_to: XenonStr,
    pub issued_by: XenonStr,
    pub valid_from: XenonStr,
    pub valid_until: XenonStr,
    pub fingerprint: XenonStr,
    pub chain: *const XenonStr,
    pub chain_length: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonLocationCallbacks {
    pub context: *mut c_void,
    pub url_described: Option<
        unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, url: *const XenonLocationUrl),
    >,
    pub display_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            generation: u64,
            text: XenonStr,
            selection_start: i64,
        ),
    >,
    pub input_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            generation: u64,
            input: *const XenonLocationInput,
        ),
    >,
    pub suggestions_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            generation: u64,
            suggestions: *const XenonLocationSuggestion,
            count: usize,
            visible: bool,
            selected: i64,
        ),
    >,
    pub selection_changed: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            generation: u64,
            selected: i64,
        ),
    >,
    pub committed:
        Option<unsafe extern "C" fn(context: *mut c_void, view: XenonViewId, text: XenonStr)>,
    pub site_security: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            security: *const XenonSiteSecurity,
        ),
    >,
}

unsafe extern "C" {
    pub fn xenon_engine_set_location_callbacks(callbacks: *const XenonLocationCallbacks);
    pub fn xenon_engine_location_describe_url(view: XenonViewId, url: XenonStr);
    pub fn xenon_engine_location_begin_editing(view: XenonViewId, generation: u64, text: XenonStr);
    pub fn xenon_engine_location_end_editing(view: XenonViewId, generation: u64);
    pub fn xenon_engine_location_text_edited(
        view: XenonViewId,
        generation: u64,
        text: XenonStr,
        cursor_at_end: bool,
    );
    pub fn xenon_engine_location_cursor_moved(
        view: XenonViewId,
        generation: u64,
        cursor_at_end: bool,
    );
    pub fn xenon_engine_location_command(view: XenonViewId, generation: u64, command: u32);
    pub fn xenon_engine_location_submit(view: XenonViewId, generation: u64, modifiers: u32);
    pub fn xenon_engine_location_activate_suggestion(
        view: XenonViewId,
        generation: u64,
        index: usize,
    );
    pub fn xenon_engine_location_request_site_security(view: XenonViewId);
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LocationCommand {
    SelectNext,
    SelectPrevious,
    AcceptCompletion,
    Escape,
    DismissPopup,
    DismissBang,
    WillDelete,
}

impl LocationCommand {
    fn raw(self) -> u32 {
        match self {
            LocationCommand::SelectNext => XENON_LOCATION_SELECT_NEXT,
            LocationCommand::SelectPrevious => XENON_LOCATION_SELECT_PREVIOUS,
            LocationCommand::AcceptCompletion => XENON_LOCATION_ACCEPT_COMPLETION,
            LocationCommand::Escape => XENON_LOCATION_ESCAPE,
            LocationCommand::DismissPopup => XENON_LOCATION_DISMISS_POPUP,
            LocationCommand::DismissBang => XENON_LOCATION_DISMISS_BANG,
            LocationCommand::WillDelete => XENON_LOCATION_WILL_DELETE,
        }
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct LocationEngine;

impl LocationEngine {
    pub fn describe_url(self, view: ViewId, url: &str) {
        unsafe { xenon_engine_location_describe_url(view, XenonStr::borrowed(url)) }
    }

    pub fn begin_editing(self, view: ViewId, generation: u64, text: &str) {
        unsafe { xenon_engine_location_begin_editing(view, generation, XenonStr::borrowed(text)) }
    }

    pub fn end_editing(self, view: ViewId, generation: u64) {
        unsafe { xenon_engine_location_end_editing(view, generation) }
    }

    pub fn text_edited(self, view: ViewId, generation: u64, text: &str, cursor_at_end: bool) {
        unsafe {
            xenon_engine_location_text_edited(
                view,
                generation,
                XenonStr::borrowed(text),
                cursor_at_end,
            )
        }
    }

    pub fn cursor_moved(self, view: ViewId, generation: u64, cursor_at_end: bool) {
        unsafe { xenon_engine_location_cursor_moved(view, generation, cursor_at_end) }
    }

    pub fn command(self, view: ViewId, generation: u64, command: LocationCommand) {
        unsafe { xenon_engine_location_command(view, generation, command.raw()) }
    }

    pub fn submit(self, view: ViewId, generation: u64, modifiers: Modifiers) {
        unsafe { xenon_engine_location_submit(view, generation, modifiers.0) }
    }

    pub fn activate_suggestion(self, view: ViewId, generation: u64, index: usize) {
        unsafe { xenon_engine_location_activate_suggestion(view, generation, index) }
    }

    pub fn request_site_security(self, view: ViewId) {
        unsafe { xenon_engine_location_request_site_security(view) }
    }
}

pub struct LocationContext {
    handle: AppHandle<Browser>,
}

impl LocationContext {
    pub fn new(handle: AppHandle<Browser>) -> Arc<LocationContext> {
        Arc::new(LocationContext { handle })
    }

    pub fn register(self: &Arc<Self>) {
        let callbacks = XenonLocationCallbacks {
            context: Arc::as_ptr(self).cast_mut().cast(),
            url_described: Some(url_described),
            display_changed: Some(display_changed),
            input_changed: Some(input_changed),
            suggestions_changed: Some(suggestions_changed),
            selection_changed: Some(selection_changed),
            committed: Some(committed),
            site_security: Some(site_security),
        };
        unsafe { xenon_engine_set_location_callbacks(&callbacks) }
    }
}

unsafe fn handle<'a>(context: *mut c_void) -> &'a AppHandle<Browser> {
    unsafe { &(*context.cast::<LocationContext>()).handle }
}

unsafe fn string(text: XenonStr) -> String {
    unsafe { text.to_string_lossy() }
}

fn index_from(raw: i64) -> Option<usize> {
    usize::try_from(raw).ok()
}

unsafe fn ranges(ranges: *const XenonTextRange, count: usize, text: &str) -> Vec<Range<usize>> {
    if ranges.is_null() || count == 0 {
        return Vec::new();
    }
    unsafe { std::slice::from_raw_parts(ranges, count) }
        .iter()
        .map(|range| range.start..range.start.saturating_add(range.length))
        .filter(|range| {
            range.start < range.end
                && range.end <= text.len()
                && text.is_char_boundary(range.start)
                && text.is_char_boundary(range.end)
        })
        .collect()
}

fn suggestion_kind(raw: u32) -> SuggestionKind {
    match raw {
        XENON_SUGGESTION_URL => SuggestionKind::Url,
        XENON_SUGGESTION_INTERNAL_PAGE => SuggestionKind::InternalPage,
        XENON_SUGGESTION_HISTORY => SuggestionKind::History,
        XENON_SUGGESTION_BOOKMARK => SuggestionKind::Bookmark,
        XENON_SUGGESTION_ADAPTIVE => SuggestionKind::Adaptive,
        XENON_SUGGESTION_CHOOSE_SEARCH_ENGINE => SuggestionKind::ChooseSearchEngine,
        _ => SuggestionKind::Search,
    }
}

fn site_security_state(raw: u32) -> SiteSecurityState {
    match raw {
        XENON_SITE_SECURITY_SECURE => SiteSecurityState::Secure,
        XENON_SITE_SECURITY_NOT_SECURE => SiteSecurityState::NotSecure,
        XENON_SITE_SECURITY_ACCEPTED_CERTIFICATE => SiteSecurityState::AcceptedCertificate,
        _ => SiteSecurityState::None,
    }
}

fn input_kind(raw: u32) -> InputKind {
    match raw {
        XENON_LOCATION_INPUT_EMPTY => InputKind::Empty,
        XENON_LOCATION_INPUT_URL => InputKind::Url,
        XENON_LOCATION_INPUT_SEARCH => InputKind::Search,
        _ => InputKind::Other,
    }
}

fn favicon_decoder() -> &'static ImageCache {
    static DECODER: OnceLock<ImageCache> = OnceLock::new();
    DECODER.get_or_init(|| ImageCache::new(Arc::new(|| {})))
}

unsafe fn favicon(suggestion: &XenonLocationSuggestion) -> Option<Pixels> {
    if suggestion.favicon_png.is_null() || suggestion.favicon_png_length == 0 {
        return None;
    }
    let png = unsafe {
        std::slice::from_raw_parts(suggestion.favicon_png, suggestion.favicon_png_length)
    };
    Some(favicon_decoder().load(png.to_vec()).pixels().clone())
}

unsafe fn suggestion_from(raw: &XenonLocationSuggestion) -> Suggestion {
    let primary = unsafe { string(raw.primary) };
    let secondary = unsafe { string(raw.secondary) };
    Suggestion {
        kind: suggestion_kind(raw.kind),
        text: unsafe { string(raw.text) },
        primary_matches: unsafe { ranges(raw.primary_matches, raw.primary_match_count, &primary) },
        secondary_matches: unsafe {
            ranges(raw.secondary_matches, raw.secondary_match_count, &secondary)
        },
        primary,
        secondary,
        emphasize_origin: raw.emphasize_origin,
        favicon: unsafe { favicon(raw) },
    }
}

unsafe extern "C" fn url_described(
    context: *mut c_void,
    view: XenonViewId,
    url: *const XenonLocationUrl,
) {
    let Some(url) = (unsafe { url.as_ref() }) else {
        return;
    };
    let parts = url.has_parts.then(|| UrlParts {
        before: unsafe { string(url.scheme_and_subdomain) },
        domain: unsafe { string(url.registrable_domain) },
        after: unsafe { string(url.remainder) },
    });
    let info = UrlInfo {
        url: unsafe { string(url.url) },
        scheme: unsafe { string(url.scheme) },
        parts,
        hidden: url.hidden,
        security: site_security_state(url.security),
        search_engine: unsafe { string(url.search_engine) },
    };
    unsafe { handle(context) }.update(move |browser, _| browser.location_url_described(view, info));
}

unsafe extern "C" fn display_changed(
    context: *mut c_void,
    view: XenonViewId,
    generation: u64,
    text: XenonStr,
    selection_start: i64,
) {
    let text = unsafe { string(text) };
    let selection_start = index_from(selection_start);
    unsafe { handle(context) }.update(move |browser, _| {
        browser.location_display_changed(view, generation, text, selection_start)
    });
}

unsafe extern "C" fn input_changed(
    context: *mut c_void,
    view: XenonViewId,
    generation: u64,
    input: *const XenonLocationInput,
) {
    let Some(input) = (unsafe { input.as_ref() }) else {
        return;
    };
    let bang_name = unsafe { string(input.bang_name) };
    let input = LocationInput {
        kind: input_kind(input.kind),
        bang: (!bang_name.is_empty() && input.bang_length > 0)
            .then_some((bang_name, input.bang_length)),
    };
    unsafe { handle(context) }
        .update(move |browser, _| browser.location_input_changed(view, generation, input));
}

unsafe extern "C" fn suggestions_changed(
    context: *mut c_void,
    view: XenonViewId,
    generation: u64,
    suggestions: *const XenonLocationSuggestion,
    count: usize,
    visible: bool,
    selected: i64,
) {
    let suggestions = if suggestions.is_null() || count == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(suggestions, count) }
            .iter()
            .map(|raw| unsafe { suggestion_from(raw) })
            .collect()
    };
    let selected = index_from(selected);
    unsafe { handle(context) }.update(move |browser, _| {
        browser.location_suggestions_changed(view, generation, suggestions, visible, selected)
    });
}

unsafe extern "C" fn selection_changed(
    context: *mut c_void,
    view: XenonViewId,
    generation: u64,
    selected: i64,
) {
    let selected = index_from(selected);
    unsafe { handle(context) }
        .update(move |browser, _| browser.location_selection_changed(view, generation, selected));
}

unsafe extern "C" fn committed(context: *mut c_void, view: XenonViewId, _: XenonStr) {
    unsafe { handle(context) }.update(move |browser, tx| browser.location_committed(view, tx));
}

unsafe extern "C" fn site_security(
    context: *mut c_void,
    view: XenonViewId,
    security: *const XenonSiteSecurity,
) {
    let Some(raw) = (unsafe { security.as_ref() }) else {
        return;
    };
    let chain = if raw.chain.is_null() || raw.chain_length == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(raw.chain, raw.chain_length) }
            .iter()
            .map(|name| unsafe { string(*name) })
            .collect()
    };
    let security = SiteSecurity {
        state: site_security_state(raw.state),
        is_final: raw.is_final,
        host: unsafe { string(raw.host) },
        connection: raw
            .has_connection
            .then(|| unsafe { (string(raw.protocol), string(raw.cipher)) }),
        certificate: raw.has_certificate.then(|| unsafe {
            Certificate {
                issued_to: string(raw.issued_to),
                issued_by: string(raw.issued_by),
                valid_from: string(raw.valid_from),
                valid_until: string(raw.valid_until),
                fingerprint: string(raw.fingerprint),
            }
        }),
        chain,
    };
    unsafe { handle(context) }
        .update(move |browser, _| browser.location_site_security(view, security));
}
