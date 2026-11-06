/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::HashMap;
use std::ops::Range;

use rune::{Key, KeyEvent, Mods, Pixels, TextEdit, Tx};

use crate::engine::{Engine, Modifiers, TabIndex, ViewId};
use crate::ffi_location::{LocationCommand, LocationEngine};
use crate::state::{Browser, NEW_TAB_URL};

pub const MAX_DISPLAY_BYTES: usize = 2048;
const ELLIPSIS: char = '…';

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum SiteSecurityState {
    #[default]
    None,
    Secure,
    NotSecure,
    AcceptedCertificate,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UrlParts {
    pub before: String,
    pub domain: String,
    pub after: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UrlInfo {
    pub url: String,
    pub scheme: String,
    pub parts: Option<UrlParts>,
    pub hidden: bool,
    pub security: SiteSecurityState,
    pub search_engine: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UrlDisplay {
    pub text: String,
    pub emphasis: Option<Range<usize>>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum InputKind {
    #[default]
    Empty,
    Url,
    Search,
    Other,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct LocationInput {
    pub kind: InputKind,
    pub bang: Option<(String, usize)>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SuggestionKind {
    Url,
    InternalPage,
    History,
    Bookmark,
    Adaptive,
    Search,
    ChooseSearchEngine,
}

#[derive(Clone)]
pub struct Suggestion {
    pub kind: SuggestionKind,
    pub text: String,
    pub primary: String,
    pub primary_matches: Vec<Range<usize>>,
    pub secondary: String,
    pub secondary_matches: Vec<Range<usize>>,
    pub emphasize_origin: bool,
    pub favicon: Option<Pixels>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Certificate {
    pub issued_to: String,
    pub issued_by: String,
    pub valid_from: String,
    pub valid_until: String,
    pub fingerprint: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SiteSecurity {
    pub state: SiteSecurityState,
    pub is_final: bool,
    pub host: String,
    pub connection: Option<(String, String)>,
    pub certificate: Option<Certificate>,
    pub chain: Vec<String>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SiteInfo {
    pub view: ViewId,
    pub security: Option<SiteSecurity>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LeadingIndicator {
    Lock,
    NotSecure,
    Globe,
    Search,
}

impl LeadingIndicator {
    pub fn opens_site_information(self) -> bool {
        matches!(self, LeadingIndicator::Lock | LeadingIndicator::NotSecure)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BangChip {
    pub name: String,
    pub prefix_length: usize,
}

struct Session {
    view: ViewId,
    sent_text: String,
    sent_at_end: bool,
}

#[derive(Default)]
pub struct LocationBar {
    pub edit: TextEdit,
    had_focus: bool,
    engine: LocationEngine,
    generation: u64,
    shown: Option<(ViewId, String)>,
    session: Option<Session>,
    urls: HashMap<ViewId, (String, UrlInfo)>,
    requested: HashMap<ViewId, String>,
    search_engine: Option<String>,
    pub suggestions: Vec<Suggestion>,
    pub popup_visible: bool,
    pub selected: Option<usize>,
    completion_from: Option<usize>,
    pub input: LocationInput,
    pub site_info: Option<SiteInfo>,
    pub copied_links: u64,
}

pub fn elide(text: &str, max_bytes: usize) -> String {
    if text.len() <= max_bytes {
        return text.to_owned();
    }
    let mut end = max_bytes.saturating_sub(ELLIPSIS.len_utf8());
    while !text.is_char_boundary(end) {
        end -= 1;
    }
    let mut elided = String::with_capacity(end + ELLIPSIS.len_utf8());
    elided.push_str(&text[..end]);
    elided.push(ELLIPSIS);
    elided
}

fn is_web_scheme(scheme: &str) -> bool {
    scheme == "http" || scheme == "https"
}

fn strip_prefix_ignoring_case<'a>(text: &'a str, prefix: &str) -> Option<&'a str> {
    let head = text.get(..prefix.len())?;
    head.eq_ignore_ascii_case(prefix)
        .then(|| &text[prefix.len()..])
}

pub fn display_for(url: &str, info: Option<&UrlInfo>) -> UrlDisplay {
    let Some(info) = info else {
        let text = if url == NEW_TAB_URL { "" } else { url };
        return UrlDisplay {
            text: elide(text, MAX_DISPLAY_BYTES),
            emphasis: None,
        };
    };
    if info.hidden {
        return UrlDisplay {
            text: String::new(),
            emphasis: None,
        };
    }
    let (text, emphasis) = match &info.parts {
        Some(parts) if is_web_scheme(&info.scheme) => {
            let mut before = parts.before.as_str();
            if let Some(rest) = before.strip_prefix(&format!("{}://", info.scheme)) {
                before = rest;
            }
            if let Some(rest) = strip_prefix_ignoring_case(before, "www.") {
                before = rest;
            }
            let after = if parts.after == "/" {
                ""
            } else {
                parts.after.as_str()
            };
            let start = before.len();
            let text = format!("{before}{}{after}", parts.domain);
            (text, Some(start..start + parts.domain.len()))
        }
        Some(parts) => {
            let start = parts.before.len();
            (info.url.clone(), Some(start..start + parts.domain.len()))
        }
        None => (info.url.clone(), None),
    };
    let text = elide(&text, MAX_DISPLAY_BYTES);
    let emphasis = emphasis
        .map(|range| range.start.min(text.len())..range.end.min(text.len()))
        .filter(|range| !range.is_empty());
    UrlDisplay { text, emphasis }
}

pub fn editing_text_for(url: &str, info: Option<&UrlInfo>) -> String {
    let hidden = info.map_or(url == NEW_TAB_URL, |info| info.hidden);
    if hidden {
        String::new()
    } else {
        url.to_owned()
    }
}

pub fn is_copy_link_chord(key: &Key, mods: Mods) -> bool {
    matches!(key, Key::Command('c' | 'C')) && mods.cmd && mods.shift && !mods.alt
}

impl LocationBar {
    fn next_generation(&mut self) -> u64 {
        self.generation += 1;
        self.generation
    }

    fn info_for(&self, view: ViewId, url: &str) -> Option<&UrlInfo> {
        self.urls
            .get(&view)
            .filter(|(requested, _)| requested == url)
            .map(|(_, info)| info)
    }

    pub fn shown_info(&self) -> Option<&UrlInfo> {
        let (view, url) = self.shown.as_ref()?;
        self.info_for(*view, url)
    }

    pub fn shown_view(&self) -> Option<ViewId> {
        self.shown.as_ref().map(|(view, _)| *view)
    }

    pub fn display(&self) -> Option<UrlDisplay> {
        let (view, url) = self.shown.as_ref()?;
        Some(display_for(url, self.info_for(*view, url)))
    }

    fn editing_text(&self) -> Option<String> {
        let (view, url) = self.shown.as_ref()?;
        Some(editing_text_for(url, self.info_for(*view, url)))
    }

    pub fn is_editing(&self) -> bool {
        self.session.is_some()
    }

    pub fn placeholder(&self) -> String {
        match self.search_engine.as_deref() {
            Some(engine) if !engine.is_empty() => {
                format!("Search with {engine} or enter web address")
            }
            Some(_) => "Enter web address".to_owned(),
            None => "Search or enter address".to_owned(),
        }
    }

    pub fn shows_current_url(&self) -> bool {
        let text = &self.edit.text;
        if text.is_empty() {
            return false;
        }
        self.editing_text().is_some_and(|editing| &editing == text)
            || self.display().is_some_and(|display| &display.text == text)
    }

    pub fn emphasis(&self) -> Option<Range<usize>> {
        if self.edit.focused {
            return None;
        }
        let display = self.display()?;
        (display.text == self.edit.text)
            .then_some(display.emphasis)
            .flatten()
    }

    pub fn bang_chip(&self) -> Option<BangChip> {
        let (name, length) = self.input.bang.as_ref()?;
        let text = &self.edit.text;
        if !self.is_editing() || *length > text.len() || !text.is_char_boundary(*length) {
            return None;
        }
        Some(BangChip {
            name: name.clone(),
            prefix_length: *length,
        })
    }

    pub fn leading_indicator(&self) -> Option<LeadingIndicator> {
        if self.bang_chip().is_some() {
            return None;
        }
        if self.shows_current_url() {
            return match self.shown_info()?.security {
                SiteSecurityState::Secure => Some(LeadingIndicator::Lock),
                SiteSecurityState::NotSecure | SiteSecurityState::AcceptedCertificate => {
                    Some(LeadingIndicator::NotSecure)
                }
                SiteSecurityState::None => None,
            };
        }
        if !self.is_editing() {
            return None;
        }
        match self.input.kind {
            InputKind::Url => Some(LeadingIndicator::Globe),
            InputKind::Search => Some(LeadingIndicator::Search),
            InputKind::Empty | InputKind::Other => None,
        }
    }

    fn ensure_described(&mut self, view: ViewId, url: &str) {
        if self.info_for(view, url).is_some()
            || self.requested.get(&view).is_some_and(|sent| sent == url)
        {
            return;
        }
        self.requested.insert(view, url.to_owned());
        self.engine.describe_url(view, url);
    }

    pub fn show(&mut self, view: Option<ViewId>, url: &str) {
        self.end_session();
        self.shown = view.map(|view| (view, url.to_owned()));
        if let Some(view) = view {
            self.ensure_described(view, url);
        }
        if self.edit.focused && self.had_focus {
            let text = self.editing_text().unwrap_or_default();
            self.edit.replace_text(text);
            self.select_all();
            self.begin_session();
        } else {
            self.show_display();
        }
    }

    fn show_display(&mut self) {
        let text = self
            .display()
            .map(|display| display.text)
            .unwrap_or_default();
        if self.edit.text != text || !self.edit.is_pristine() {
            self.edit.replace_text(text);
        }
        self.edit.set_caret(0);
    }

    fn select_all(&mut self) {
        let end = self.edit.text.len();
        self.edit.set_selection(0, end);
    }

    fn clear_popup(&mut self) {
        self.suggestions.clear();
        self.popup_visible = false;
        self.selected = None;
        self.completion_from = None;
        self.input = LocationInput::default();
    }

    fn begin_session(&mut self) {
        let Some((view, _)) = self.shown.clone() else {
            return;
        };
        let generation = self.next_generation();
        let text = self.edit.text.clone();
        self.engine.begin_editing(view, generation, &text);
        self.session = Some(Session {
            view,
            sent_text: text,
            sent_at_end: true,
        });
    }

    fn end_session(&mut self) {
        if let Some(session) = self.session.take() {
            let generation = self.next_generation();
            self.engine.end_editing(session.view, generation);
        }
        self.clear_popup();
    }

    fn focus_gained(&mut self) {
        let unedited = self.edit.text.is_empty() || self.shows_current_url();
        if unedited && let Some(text) = self.editing_text() {
            self.edit.replace_text(text);
        }
        self.select_all();
        self.begin_session();
    }

    fn focus_lost(&mut self) {
        self.end_session();
        self.site_info = None;
        if self.edit.text.is_empty() || self.shows_current_url() {
            self.show_display();
        } else {
            self.edit.set_caret(0);
        }
    }

    pub fn poll(&mut self) -> (bool, bool) {
        let mut changed = false;
        let mut gained = false;
        if self.edit.focused != self.had_focus {
            self.had_focus = self.edit.focused;
            changed = true;
            if self.edit.focused {
                gained = true;
                self.focus_gained();
            } else {
                self.focus_lost();
            }
        }
        changed |= self.send_edits();
        (changed, gained)
    }

    fn send_edits(&mut self) -> bool {
        if !self.edit.focused || self.edit.preedit.is_some() {
            return false;
        }
        let text = self.edit.text.clone();
        let at_end = self.edit.range().1 == text.len();
        let Some(session) = self.session.as_ref() else {
            return false;
        };
        let view = session.view;
        if session.sent_text != text {
            let generation = self.next_generation();
            self.completion_from = None;
            self.engine.text_edited(view, generation, &text, at_end);
        } else if session.sent_at_end != at_end {
            let generation = self.next_generation();
            self.engine.cursor_moved(view, generation, at_end);
        } else {
            return false;
        }
        if let Some(session) = self.session.as_mut() {
            session.sent_text = text;
            session.sent_at_end = at_end;
        }
        true
    }

    fn session_view(&self, view: ViewId) -> bool {
        self.session
            .as_ref()
            .is_some_and(|session| session.view == view)
    }

    fn current(&self, view: ViewId, generation: u64) -> bool {
        self.session_view(view) && generation == self.generation && self.edit.focused
    }

    fn command(&mut self, command: LocationCommand) {
        if let Some(session) = &self.session {
            self.engine.command(session.view, self.generation, command);
        }
    }

    pub fn url_described(&mut self, view: ViewId, info: UrlInfo) {
        let Some(requested) = self.requested.get(&view).cloned() else {
            return;
        };
        let shown_before = self.display();
        self.search_engine = Some(info.search_engine.clone());
        self.urls.insert(view, (requested, info));
        let is_shown = self.shown_view() == Some(view);
        if is_shown
            && !self.edit.focused
            && shown_before.is_some_and(|before| before.text == self.edit.text)
        {
            self.show_display();
        }
    }

    pub fn display_changed(
        &mut self,
        view: ViewId,
        generation: u64,
        text: String,
        selection_start: Option<usize>,
    ) {
        if !self.current(view, generation) {
            return;
        }
        let length = text.len();
        self.edit.replace_text(text.clone());
        self.completion_from =
            selection_start.filter(|start| *start < length && text.is_char_boundary(*start));
        match self.completion_from {
            Some(start) => self.edit.set_selection(start, length),
            None => self.edit.set_caret(length),
        }
        if let Some(session) = self.session.as_mut() {
            session.sent_text = text;
            session.sent_at_end = true;
        }
    }

    pub fn input_changed(&mut self, view: ViewId, generation: u64, input: LocationInput) {
        if self.current(view, generation) {
            self.input = input;
        }
    }

    pub fn suggestions_changed(
        &mut self,
        view: ViewId,
        suggestions: Vec<Suggestion>,
        visible: bool,
        selected: Option<usize>,
    ) {
        if !self.session_view(view) {
            return;
        }
        self.suggestions = suggestions;
        self.popup_visible = visible && !self.suggestions.is_empty();
        self.selected = selected.filter(|index| *index < self.suggestions.len());
    }

    pub fn selection_changed(&mut self, view: ViewId, selected: Option<usize>) {
        if self.session_view(view) {
            self.selected = selected.filter(|index| *index < self.suggestions.len());
        }
    }

    pub fn has_inline_completion(&self) -> bool {
        self.completion_from.is_some_and(|start| start > 0)
            && self.edit.range() == (self.completion_from.unwrap_or(0), self.edit.text.len())
    }

    pub fn submit(&mut self, modifiers: Modifiers) -> bool {
        self.send_edits();
        let Some(session) = &self.session else {
            return false;
        };
        self.engine.submit(session.view, self.generation, modifiers);
        true
    }

    pub fn activate_suggestion(&mut self, index: usize) {
        self.send_edits();
        if let Some(session) = &self.session {
            self.engine
                .activate_suggestion(session.view, self.generation, index);
        }
    }

    pub fn key(&mut self, event: &KeyEvent) -> Option<bool> {
        let plain = !event.mods.cmd && !event.mods.alt && !event.mods.ctrl;
        match &event.key {
            Key::Escape if self.popup_visible => {
                self.command(LocationCommand::Escape);
                Some(true)
            }
            Key::Escape => None,
            Key::Down if plain && !self.suggestions.is_empty() => {
                self.send_edits();
                self.command(LocationCommand::SelectNext);
                Some(true)
            }
            Key::Up if plain && !self.suggestions.is_empty() => {
                self.send_edits();
                self.command(LocationCommand::SelectPrevious);
                Some(true)
            }
            Key::Right | Key::End if plain && !event.mods.shift && self.has_inline_completion() => {
                self.command(LocationCommand::AcceptCompletion);
                Some(true)
            }
            Key::Backspace if self.bang_chip().is_some() && self.edit.range() == (0, 0) => {
                self.input.bang = None;
                self.command(LocationCommand::DismissBang);
                Some(true)
            }
            Key::Backspace | Key::Delete => {
                self.send_edits();
                self.command(LocationCommand::WillDelete);
                Some(false)
            }
            _ => Some(false),
        }
    }

    pub fn committed(&mut self, view: ViewId) -> bool {
        if !self.session_view(view) {
            return false;
        }
        self.end_session();
        true
    }

    pub fn toggle_site_info(&mut self) {
        if self.site_info.take().is_some() {
            return;
        }
        let Some(view) = self.shown_view() else {
            return;
        };
        if self
            .shown_info()
            .is_none_or(|info| info.security == SiteSecurityState::None)
        {
            return;
        }
        self.site_info = Some(SiteInfo {
            view,
            security: None,
        });
        self.engine.request_site_security(view);
    }

    pub fn close_site_info(&mut self) {
        self.site_info = None;
    }

    pub fn site_security(&mut self, view: ViewId, security: SiteSecurity) {
        if let Some(site_info) = self.site_info.as_mut()
            && site_info.view == view
        {
            site_info.security = Some(security);
        }
    }
}

impl Browser {
    pub fn focus_location(&mut self) {
        self.location.edit.focused = true;
        self.location.select_all();
        self.blur_page();
    }

    pub fn reset_location(&mut self) {
        let view = self.tabs.active_view();
        let url = self
            .tabs
            .active()
            .map(|tab| tab.url.clone())
            .unwrap_or_default();
        self.location.show(view, &url);
    }

    pub fn track_location_focus(&mut self) -> bool {
        let (changed, gained) = self.location.poll();
        if gained {
            self.blur_page();
        }
        changed
    }

    pub fn submit_location(&mut self, tx: &mut Tx) {
        self.submit_location_with(Modifiers::default(), tx);
    }

    fn submit_location_with(&mut self, modifiers: Modifiers, tx: &mut Tx) {
        if self.location.submit(modifiers) {
            return;
        }
        let input = self.location.edit.text.trim().to_owned();
        if input.is_empty() || self.tabs.active_view().is_some() {
            return;
        }
        Engine.new_tab(self.shown_window(), &input, TabIndex::End, true);
        self.leave_location(tx);
    }

    fn leave_location(&mut self, tx: &mut Tx) {
        tx.blur();
        self.location.edit.focused = false;
        self.focus_page();
    }

    pub fn revert_location(&mut self, tx: &mut Tx) {
        let text = self.location.editing_text().unwrap_or_default();
        self.location.edit.replace_text(text);
        self.leave_location(tx);
    }

    pub fn location_field_key(&mut self, event: &KeyEvent, tx: &mut Tx) -> bool {
        if self.location_shortcut(event, tx) {
            return true;
        }
        if event.key == Key::Enter {
            self.submit_location_with(Modifiers::from_rune(event.mods), tx);
            return true;
        }
        match self.location.key(event) {
            Some(handled) => handled,
            None => {
                self.revert_location(tx);
                true
            }
        }
    }

    pub fn location_shortcut(&mut self, event: &KeyEvent, tx: &mut Tx) -> bool {
        if !is_copy_link_chord(&event.key, event.mods) {
            return false;
        }
        if let Some(tab) = self.tabs.active()
            && !tab.url.is_empty()
        {
            tx.copy(tab.url.clone());
            self.location.copied_links += 1;
        }
        true
    }

    pub fn location_suggestion_pressed(&mut self, index: usize) {
        self.location.activate_suggestion(index);
    }

    pub fn location_url_described(&mut self, view: ViewId, info: UrlInfo) {
        self.location.url_described(view, info);
    }

    pub fn location_display_changed(
        &mut self,
        view: ViewId,
        generation: u64,
        text: String,
        selection_start: Option<usize>,
    ) {
        self.location
            .display_changed(view, generation, text, selection_start);
    }

    pub fn location_input_changed(&mut self, view: ViewId, generation: u64, input: LocationInput) {
        self.location.input_changed(view, generation, input);
    }

    pub fn location_suggestions_changed(
        &mut self,
        view: ViewId,
        _generation: u64,
        suggestions: Vec<Suggestion>,
        visible: bool,
        selected: Option<usize>,
    ) {
        self.location
            .suggestions_changed(view, suggestions, visible, selected);
    }

    pub fn location_selection_changed(
        &mut self,
        view: ViewId,
        _generation: u64,
        selected: Option<usize>,
    ) {
        self.location.selection_changed(view, selected);
    }

    pub fn location_committed(&mut self, view: ViewId, tx: &mut Tx) {
        if self.location.committed(view) {
            self.leave_location(tx);
            self.location.had_focus = false;
            self.reset_location();
        }
    }

    pub fn location_site_security(&mut self, view: ViewId, security: SiteSecurity) {
        self.location.site_security(view, security);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn web(url: &str, before: &str, domain: &str, after: &str) -> UrlInfo {
        UrlInfo {
            url: url.to_owned(),
            scheme: url.split(':').next().unwrap().to_owned(),
            parts: Some(UrlParts {
                before: before.to_owned(),
                domain: domain.to_owned(),
                after: after.to_owned(),
            }),
            hidden: false,
            security: SiteSecurityState::Secure,
            search_engine: String::new(),
        }
    }

    fn emphasized(display: &UrlDisplay) -> &str {
        &display.text[display.emphasis.clone().unwrap()]
    }

    #[test]
    fn a_web_address_drops_its_scheme_www_and_root_slash_and_emphasizes_the_site() {
        let info = web(
            "https://www.ladybird.org/",
            "https://www.",
            "ladybird.org",
            "/",
        );
        let display = display_for(&info.url, Some(&info));
        assert_eq!(display.text, "ladybird.org");
        assert_eq!(emphasized(&display), "ladybird.org");
    }

    #[test]
    fn a_subdomain_and_path_stay_but_only_the_registrable_domain_is_emphasized() {
        let info = web(
            "https://docs.github.com/en/get-started?q=1#top",
            "https://docs.",
            "github.com",
            "/en/get-started?q=1#top",
        );
        let display = display_for(&info.url, Some(&info));
        assert_eq!(display.text, "docs.github.com/en/get-started?q=1#top");
        assert_eq!(emphasized(&display), "github.com");
        assert_eq!(display.emphasis, Some(5..15));
    }

    #[test]
    fn www_is_dropped_whatever_its_case() {
        let info = web(
            "http://WWW.example.com/a",
            "http://WWW.",
            "example.com",
            "/a",
        );
        assert_eq!(display_for(&info.url, Some(&info)).text, "example.com/a");
    }

    #[test]
    fn an_internal_page_keeps_its_scheme_and_emphasizes_the_page() {
        let mut info = web("about:settings", "about:", "settings", "");
        info.security = SiteSecurityState::None;
        let display = display_for(&info.url, Some(&info));
        assert_eq!(display.text, "about:settings");
        assert_eq!(emphasized(&display), "settings");
    }

    #[test]
    fn an_address_without_parts_is_shown_whole_without_emphasis() {
        let mut info = web("xenon://certificate-error/?url=x", "", "", "");
        info.parts = None;
        let display = display_for(&info.url, Some(&info));
        assert_eq!(display.text, "xenon://certificate-error/?url=x");
        assert_eq!(display.emphasis, None);
    }

    #[test]
    fn the_new_tab_page_is_never_shown() {
        let mut info = web("about:newtab", "about:", "newtab", "");
        info.hidden = true;
        assert_eq!(display_for(&info.url, Some(&info)).text, "");
        assert_eq!(editing_text_for(&info.url, Some(&info)), "");
        assert_eq!(display_for(NEW_TAB_URL, None).text, "");
        assert_eq!(editing_text_for(NEW_TAB_URL, None), "");
        assert_eq!(editing_text_for("about:blank", None), "about:blank");
    }

    #[test]
    fn long_addresses_are_elided_on_a_character_boundary() {
        assert_eq!(elide("short", 10), "short");
        assert_eq!(elide("abcdefghij", 10), "abcdefghij");
        assert_eq!(elide("abcdefghijk", 10), "abcdefg…");
        let elided = elide("ééééééé", 8);
        assert!(elided.len() <= 8);
        assert_eq!(elided, "éé…");
    }

    #[test]
    fn emphasis_is_clipped_to_an_elided_address() {
        let long_path = format!("/{}", "a".repeat(MAX_DISPLAY_BYTES * 2));
        let url = format!("https://example.com{long_path}");
        let info = web(&url, "https://", "example.com", &long_path);
        let display = display_for(&url, Some(&info));
        assert!(display.text.len() <= MAX_DISPLAY_BYTES);
        assert!(display.text.ends_with(ELLIPSIS));
        assert_eq!(emphasized(&display), "example.com");
    }

    #[test]
    fn an_undescribed_address_shows_as_is() {
        let display = display_for("https://example.com/", None);
        assert_eq!(display.text, "https://example.com/");
        assert_eq!(display.emphasis, None);
    }

    #[test]
    fn the_copy_link_chord_is_primary_shift_c() {
        let chord = Mods {
            cmd: true,
            shift: true,
            ..Mods::default()
        };
        assert!(is_copy_link_chord(&Key::Command('c'), chord));
        assert!(!is_copy_link_chord(
            &Key::Command('c'),
            Mods {
                cmd: true,
                ..Mods::default()
            }
        ));
        assert!(!is_copy_link_chord(&Key::Text("C".into()), chord));
    }

    #[test]
    fn the_indicator_follows_the_security_of_the_page_shown() {
        let mut bar = LocationBar::default();
        let mut info = web("http://example.com/", "http://", "example.com", "/");
        info.security = SiteSecurityState::NotSecure;
        bar.shown = Some((7, info.url.clone()));
        bar.requested.insert(7, info.url.clone());
        bar.url_described(7, info.clone());
        bar.show_display();
        assert_eq!(bar.edit.text, "example.com");
        assert_eq!(bar.leading_indicator(), Some(LeadingIndicator::NotSecure));

        info.security = SiteSecurityState::Secure;
        bar.url_described(7, info);
        assert_eq!(bar.leading_indicator(), Some(LeadingIndicator::Lock));

        bar.edit.replace_text("something else");
        assert_eq!(bar.leading_indicator(), None);
    }

    #[test]
    fn a_stale_display_from_the_engine_is_dropped() {
        let mut bar = LocationBar::default();
        bar.edit.focused = true;
        bar.session = Some(Session {
            view: 3,
            sent_text: "lad".into(),
            sent_at_end: true,
        });
        bar.generation = 5;
        bar.edit.replace_text("lad");
        bar.display_changed(3, 4, "ladybird.org".into(), Some(2));
        assert_eq!(bar.edit.text, "lad");
        bar.display_changed(3, 5, "ladybird.org".into(), Some(3));
        assert_eq!(bar.edit.text, "ladybird.org");
        assert_eq!(bar.edit.range(), (3, 12));
        assert!(bar.has_inline_completion());
    }
}
