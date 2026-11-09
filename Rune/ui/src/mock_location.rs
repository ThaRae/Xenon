/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::HashMap;
use std::sync::{Mutex, MutexGuard};

use crate::ffi::{XenonStr, XenonViewId};
use crate::ffi_location::{
    XENON_LOCATION_ACCEPT_COMPLETION, XENON_LOCATION_DISMISS_BANG, XENON_LOCATION_DISMISS_POPUP,
    XENON_LOCATION_ESCAPE, XENON_LOCATION_INPUT_EMPTY, XENON_LOCATION_INPUT_SEARCH,
    XENON_LOCATION_INPUT_URL, XENON_LOCATION_SELECT_NEXT, XENON_LOCATION_SELECT_PREVIOUS,
    XENON_SITE_SECURITY_NONE, XENON_SITE_SECURITY_NOT_SECURE, XENON_SITE_SECURITY_SECURE,
    XENON_SUGGESTION_HISTORY, XENON_SUGGESTION_SEARCH, XENON_SUGGESTION_URL,
    XenonLocationCallbacks, XenonLocationInput, XenonLocationSuggestion, XenonLocationUrl,
    XenonSiteSecurity, XenonTextRange,
};

const NEW_TAB_URL: &str = "about:newtab";
const SEARCH_ENGINE: &str = "DuckDuckGo";
const HISTORY: &[(&str, &str)] = &[
    ("ladybird.org", "Ladybird"),
    (
        "github.com/LadybirdBrowser/ladybird",
        "LadybirdBrowser/ladybird",
    ),
];
const BANGS: &[(&str, &str)] = &[("!w", "Wikipedia"), ("!gh", "GitHub"), ("!yt", "YouTube")];

struct SendableCallbacks(XenonLocationCallbacks);

unsafe impl Send for SendableCallbacks {}

#[derive(Default)]
struct Session {
    generation: u64,
    query: String,
    rows: Vec<Row>,
    visible: bool,
    selected: Option<usize>,
    bang_dismissed: bool,
}

#[derive(Clone)]
struct Row {
    kind: u32,
    text: String,
    title: String,
}

#[derive(Default)]
struct MockLocation {
    callbacks: Option<SendableCallbacks>,
    sessions: HashMap<XenonViewId, Session>,
}

static STATE: Mutex<Option<MockLocation>> = Mutex::new(None);

fn state() -> MutexGuard<'static, Option<MockLocation>> {
    STATE
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn callbacks() -> Option<XenonLocationCallbacks> {
    state()
        .as_ref()
        .and_then(|mock| mock.callbacks.as_ref().map(|callbacks| callbacks.0))
}

fn with_session<R>(view: XenonViewId, f: impl FnOnce(&mut Session) -> R) -> R {
    let mut guard = state();
    let mock = guard.get_or_insert_with(MockLocation::default);
    f(mock.sessions.entry(view).or_default())
}

fn looks_like_url(text: &str) -> bool {
    !text.contains(' ') && (text.contains('.') || text.contains(':'))
}

fn leading_bang(text: &str) -> Option<(&'static str, usize)> {
    let (word, _) = text.split_once(' ')?;
    BANGS
        .iter()
        .find(|(trigger, _)| *trigger == word)
        .map(|(_, name)| (*name, word.len() + 1))
}

fn rows_for(query: &str) -> Vec<Row> {
    let query = query.trim();
    if query.is_empty() {
        return Vec::new();
    }
    let mut rows = Vec::new();
    for (url, title) in HISTORY {
        if url.starts_with(&query.to_ascii_lowercase()) {
            rows.push(Row {
                kind: XENON_SUGGESTION_HISTORY,
                text: format!("https://{url}/"),
                title: (*title).to_owned(),
            });
        }
    }
    if looks_like_url(query) {
        rows.push(Row {
            kind: XENON_SUGGESTION_URL,
            text: query.to_owned(),
            title: String::new(),
        });
    }
    rows.push(Row {
        kind: XENON_SUGGESTION_SEARCH,
        text: query.to_owned(),
        title: String::new(),
    });
    rows
}

fn report_input(view: XenonViewId, generation: u64, text: &str, bang_dismissed: bool) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let trimmed = text.trim();
    let kind = if trimmed.is_empty() {
        XENON_LOCATION_INPUT_EMPTY
    } else if looks_like_url(trimmed) {
        XENON_LOCATION_INPUT_URL
    } else {
        XENON_LOCATION_INPUT_SEARCH
    };
    let bang = (!bang_dismissed).then(|| leading_bang(text)).flatten();
    let input = XenonLocationInput {
        kind,
        bang_name: XenonStr::borrowed(bang.map_or("", |(name, _)| name)),
        bang_length: bang.map_or(0, |(_, length)| length),
    };
    if let Some(input_changed) = callbacks.input_changed {
        unsafe { input_changed(callbacks.context, view, generation, &input) };
    }
}

fn display_text_of(row: &Row) -> String {
    row.text
        .trim_start_matches("https://")
        .trim_end_matches('/')
        .to_owned()
}

fn report_rows(view: XenonViewId) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let (generation, query, rows, visible, selected) = with_session(view, |session| {
        (
            session.generation,
            session.query.clone(),
            session.rows.clone(),
            session.visible,
            session.selected,
        )
    });
    let primaries: Vec<String> = rows
        .iter()
        .map(|row| {
            if row.title.is_empty() {
                display_text_of(row)
            } else {
                row.title.clone()
            }
        })
        .collect();
    let secondaries: Vec<String> = rows
        .iter()
        .map(|row| {
            if row.title.is_empty() {
                String::new()
            } else {
                display_text_of(row)
            }
        })
        .collect();
    let matches = |text: &str| -> Vec<XenonTextRange> {
        let lowered = text.to_ascii_lowercase();
        lowered
            .find(&query.trim().to_ascii_lowercase())
            .filter(|_| !query.trim().is_empty())
            .map(|start| XenonTextRange {
                start,
                length: query.trim().len(),
            })
            .into_iter()
            .collect()
    };
    let primary_matches: Vec<Vec<XenonTextRange>> = primaries.iter().map(|t| matches(t)).collect();
    let secondary_matches: Vec<Vec<XenonTextRange>> =
        secondaries.iter().map(|t| matches(t)).collect();
    let entries: Vec<XenonLocationSuggestion> = rows
        .iter()
        .enumerate()
        .map(|(index, row)| XenonLocationSuggestion {
            kind: row.kind,
            text: XenonStr::borrowed(&row.text),
            primary: XenonStr::borrowed(&primaries[index]),
            primary_matches: primary_matches[index].as_ptr(),
            primary_match_count: primary_matches[index].len(),
            secondary: XenonStr::borrowed(&secondaries[index]),
            secondary_matches: secondary_matches[index].as_ptr(),
            secondary_match_count: secondary_matches[index].len(),
            emphasize_origin: row.kind != XENON_SUGGESTION_SEARCH,
            favicon_png: std::ptr::null(),
            favicon_png_length: 0,
        })
        .collect();
    if let Some(suggestions_changed) = callbacks.suggestions_changed {
        unsafe {
            suggestions_changed(
                callbacks.context,
                view,
                generation,
                entries.as_ptr(),
                entries.len(),
                visible,
                selected.map_or(-1, |index| index as i64),
            )
        };
    }
}

fn display(view: XenonViewId, generation: u64, text: &str, selection_start: Option<usize>) {
    if let Some(callbacks) = callbacks()
        && let Some(display_changed) = callbacks.display_changed
    {
        unsafe {
            display_changed(
                callbacks.context,
                view,
                generation,
                XenonStr::borrowed(text),
                selection_start.map_or(-1, |start| start as i64),
            )
        };
    }
}

fn query_changed(view: XenonViewId, generation: u64, text: String, cursor_at_end: bool) {
    let (rows, dismissed) = with_session(view, |session| {
        session.generation = generation;
        session.query = text.clone();
        session.rows = rows_for(&text);
        session.visible = !session.rows.is_empty();
        session.selected = (!session.rows.is_empty()).then_some(0);
        (session.rows.clone(), session.bang_dismissed)
    });
    report_input(view, generation, &text, dismissed);
    if cursor_at_end
        && let Some(first) = rows
            .first()
            .filter(|row| row.kind == XENON_SUGGESTION_HISTORY)
    {
        let completion = display_text_of(first);
        if completion.len() > text.len() && completion.starts_with(&text) {
            display(view, generation, &completion, Some(text.len()));
        }
    }
    report_rows(view);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_set_location_callbacks(
    callbacks: *const XenonLocationCallbacks,
) {
    let callbacks = unsafe { callbacks.as_ref() }.map(|callbacks| SendableCallbacks(*callbacks));
    state().get_or_insert_with(MockLocation::default).callbacks = callbacks;
}

fn url_parts(url: &str) -> Option<(String, String, String, String)> {
    let (scheme, rest) = url.split_once(':')?;
    if let Some(after_slashes) = rest.strip_prefix("//") {
        let host_end = after_slashes
            .find(['/', '?', '#'])
            .unwrap_or(after_slashes.len());
        let host = &after_slashes[..host_end];
        let labels: Vec<&str> = host.split('.').collect();
        let domain_start = if labels.len() > 2 {
            host.len() - labels[labels.len() - 2..].join(".").len()
        } else {
            0
        };
        let before = format!("{scheme}://{}", &host[..domain_start]);
        return Some((
            scheme.to_owned(),
            before,
            host[domain_start..].to_owned(),
            after_slashes[host_end..].to_owned(),
        ));
    }
    Some((
        scheme.to_owned(),
        format!("{scheme}:"),
        rest.to_owned(),
        String::new(),
    ))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_location_describe_url(view: XenonViewId, url: XenonStr) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let url = unsafe { url.to_string_lossy() };
    let parts = url_parts(&url);
    let (scheme, before, domain, after) = parts.clone().unwrap_or_default();
    let security = match scheme.as_str() {
        "https" => XENON_SITE_SECURITY_SECURE,
        "http" => XENON_SITE_SECURITY_NOT_SECURE,
        _ => XENON_SITE_SECURITY_NONE,
    };
    let description = XenonLocationUrl {
        url: XenonStr::borrowed(&url),
        scheme: XenonStr::borrowed(&scheme),
        has_parts: parts.is_some(),
        scheme_and_subdomain: XenonStr::borrowed(&before),
        registrable_domain: XenonStr::borrowed(&domain),
        remainder: XenonStr::borrowed(&after),
        hidden: url == NEW_TAB_URL,
        security,
        search_engine: XenonStr::borrowed(SEARCH_ENGINE),
    };
    if let Some(url_described) = callbacks.url_described {
        unsafe { url_described(callbacks.context, view, &description) };
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_location_begin_editing(
    view: XenonViewId,
    generation: u64,
    text: XenonStr,
) {
    let text = unsafe { text.to_string_lossy() };
    with_session(view, |session| {
        *session = Session {
            generation,
            query: text.clone(),
            ..Session::default()
        };
    });
    report_input(view, generation, &text, false);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_location_end_editing(view: XenonViewId, generation: u64) {
    with_session(view, |session| {
        session.generation = generation;
        session.visible = false;
        session.selected = None;
        session.bang_dismissed = false;
    });
    report_rows(view);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_location_text_edited(
    view: XenonViewId,
    generation: u64,
    text: XenonStr,
    cursor_at_end: bool,
) {
    let text = unsafe { text.to_string_lossy() };
    with_session(view, |session| session.bang_dismissed = false);
    query_changed(view, generation, text, cursor_at_end);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_location_cursor_moved(
    view: XenonViewId,
    generation: u64,
    _cursor_at_end: bool,
) {
    with_session(view, |session| session.generation = generation);
}

fn select(view: XenonViewId, step: isize) {
    let (generation, row) = with_session(view, |session| {
        if session.rows.is_empty() {
            return (session.generation, None);
        }
        let count = session.rows.len() as isize;
        let next = match (session.visible, session.selected) {
            (true, Some(index)) => (index as isize + step).rem_euclid(count),
            _ if step > 0 => 0,
            _ => count - 1,
        } as usize;
        session.visible = true;
        session.selected = Some(next);
        (session.generation, session.rows.get(next).cloned())
    });
    if let Some(row) = row {
        display(view, generation, &display_text_of(&row), Some(0));
    }
    report_rows(view);
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_location_command(view: XenonViewId, generation: u64, command: u32) {
    with_session(view, |session| session.generation = generation);
    match command {
        XENON_LOCATION_SELECT_NEXT => select(view, 1),
        XENON_LOCATION_SELECT_PREVIOUS => select(view, -1),
        XENON_LOCATION_ACCEPT_COMPLETION => {
            let completion = with_session(view, |session| {
                session.rows.first().map(display_text_of).inspect(|text| {
                    session.query = text.clone();
                })
            });
            if let Some(text) = completion {
                display(view, generation, &text, None);
            }
        }
        XENON_LOCATION_ESCAPE | XENON_LOCATION_DISMISS_POPUP => {
            let query = with_session(view, |session| {
                session.visible = false;
                session.selected = None;
                session.query.clone()
            });
            display(view, generation, &query, None);
            report_rows(view);
        }
        XENON_LOCATION_DISMISS_BANG => {
            let query = with_session(view, |session| {
                session.bang_dismissed = true;
                session.query.clone()
            });
            report_input(view, generation, &query, true);
        }
        _ => {}
    }
}

fn commit(view: XenonViewId, text: String) {
    let destination = BANGS
        .iter()
        .find_map(|(trigger, _)| {
            text.strip_prefix(&format!("{trigger} "))
                .map(|query| format!("https://bang.example/{}?q={query}", &trigger[1..]))
        })
        .unwrap_or_else(|| text.clone());
    unsafe { crate::mock::xenon_engine_load(view, XenonStr::borrowed(&destination)) };
    if let Some(callbacks) = callbacks()
        && let Some(committed) = callbacks.committed
    {
        unsafe { committed(callbacks.context, view, XenonStr::borrowed(&text)) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_location_submit(
    view: XenonViewId,
    generation: u64,
    _modifiers: u32,
) {
    let text = with_session(view, |session| {
        session.generation = generation;
        let selected = session
            .visible
            .then_some(session.selected)
            .flatten()
            .and_then(|index| session.rows.get(index));
        selected.map_or_else(|| session.query.clone(), |row| row.text.clone())
    });
    if !text.trim().is_empty() {
        commit(view, text);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_location_activate_suggestion(
    view: XenonViewId,
    generation: u64,
    index: usize,
) {
    let row = with_session(view, |session| {
        session.generation = generation;
        session.rows.get(index).cloned()
    });
    if let Some(row) = row {
        commit(view, row.text);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_location_request_site_security(view: XenonViewId) {
    let Some(callbacks) = callbacks() else {
        return;
    };
    let chain = [
        XenonStr::borrowed("Mock Root CA"),
        XenonStr::borrowed("mock.example"),
    ];
    let security = XenonSiteSecurity {
        state: XENON_SITE_SECURITY_SECURE,
        is_final: true,
        has_connection: true,
        host: XenonStr::borrowed("mock.example"),
        protocol: XenonStr::borrowed("TLS 1.3"),
        cipher: XenonStr::borrowed("TLS_AES_256_GCM_SHA384"),
        has_certificate: true,
        issued_to: XenonStr::borrowed("mock.example"),
        issued_by: XenonStr::borrowed("Mock Root CA"),
        valid_from: XenonStr::borrowed("1 January 2026"),
        valid_until: XenonStr::borrowed("1 January 2027"),
        fingerprint: XenonStr::borrowed("AA:BB:CC:DD:EE:FF:00:11\n22:33:44:55:66:77:88:99"),
        chain: chain.as_ptr(),
        chain_length: chain.len(),
    };
    if let Some(site_security) = callbacks.site_security {
        unsafe { site_security(callbacks.context, view, &security) };
    }
}
