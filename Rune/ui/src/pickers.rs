/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Key, KeyEvent, TextEdit, Tx};

use crate::engine::ViewId;
use crate::page_prompts::{PromptId, active_view, view_exists};
use crate::prompts_bridge::PromptEngine;
use crate::state::Browser;

pub const BASIC_COLORS: [u32; 16] = [
    0x000000, 0x808080, 0xc0c0c0, 0xffffff, 0x800000, 0xff0000, 0x808000, 0xffff00, 0x008000,
    0x00ff00, 0x008080, 0x00ffff, 0x000080, 0x0000ff, 0x800080, 0xff00ff,
];

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Hsv {
    pub hue: f32,
    pub saturation: f32,
    pub value: f32,
}

impl Hsv {
    pub fn from_rgb(rgb: u32) -> Hsv {
        let channel = |shift: u32| ((rgb >> shift) & 0xff) as f32 / 255.0;
        let (r, g, b) = (channel(16), channel(8), channel(0));
        let max = r.max(g).max(b);
        let min = r.min(g).min(b);
        let delta = max - min;
        let hue = if delta <= f32::EPSILON {
            0.0
        } else if max == r {
            60.0 * ((g - b) / delta).rem_euclid(6.0)
        } else if max == g {
            60.0 * ((b - r) / delta + 2.0)
        } else {
            60.0 * ((r - g) / delta + 4.0)
        };
        Hsv {
            hue,
            saturation: if max <= f32::EPSILON {
                0.0
            } else {
                delta / max
            },
            value: max,
        }
    }

    pub fn to_rgb(self) -> u32 {
        let hue = self.hue.rem_euclid(360.0);
        let chroma = self.value * self.saturation;
        let x = chroma * (1.0 - ((hue / 60.0).rem_euclid(2.0) - 1.0).abs());
        let (r, g, b) = match (hue / 60.0) as u32 {
            0 => (chroma, x, 0.0),
            1 => (x, chroma, 0.0),
            2 => (0.0, chroma, x),
            3 => (0.0, x, chroma),
            4 => (x, 0.0, chroma),
            _ => (chroma, 0.0, x),
        };
        let m = self.value - chroma;
        let byte = |c: f32| (((c + m) * 255.0).round().clamp(0.0, 255.0)) as u32;
        (byte(r) << 16) | (byte(g) << 8) | byte(b)
    }
}

pub fn hex_text(rgb: u32) -> String {
    format!("#{rgb:06x}")
}

pub fn parse_hex(text: &str) -> Option<u32> {
    let digits = text.trim().trim_start_matches('#');
    match digits.len() {
        6 => u32::from_str_radix(digits, 16).ok(),
        3 => {
            let short = u32::from_str_radix(digits, 16).ok()?;
            let expand = |shift: u32| ((short >> shift) & 0xf) * 17;
            Some((expand(8) << 16) | (expand(4) << 8) | expand(0))
        }
        _ => None,
    }
}

pub struct ColorPicker {
    pub id: PromptId,
    pub hsv: Hsv,
    pub hex: TextEdit,
    sent: u32,
}

impl ColorPicker {
    pub fn rgb(&self) -> u32 {
        self.hsv.to_rgb()
    }
}

pub fn color_picker_requested(s: &mut Browser, view: ViewId, id: PromptId, rgb: u32) {
    if !view_exists(s, view) {
        PromptEngine.color_picker_update(view, id, None, true);
        return;
    }
    let prompts = s.page_prompts.of_mut(view);
    if let Some(previous) = prompts.color_picker.take() {
        PromptEngine.color_picker_update(view, previous.id, None, true);
    }
    prompts.tooltip = None;
    prompts.color_picker = Some(ColorPicker {
        id,
        hsv: Hsv::from_rgb(rgb),
        hex: TextEdit::new(hex_text(rgb)),
        sent: rgb,
    });
}

fn color_picker_mut(s: &mut Browser, view: ViewId) -> Option<&mut ColorPicker> {
    s.page_prompts.of_mut(view).color_picker.as_mut()
}

pub fn color_hex_edit(s: &mut Browser, view: ViewId) -> &mut TextEdit {
    &mut s
        .page_prompts
        .of_mut(view)
        .color_picker
        .get_or_insert_with(|| ColorPicker {
            id: 0,
            hsv: Hsv::from_rgb(0),
            hex: TextEdit::new(""),
            sent: 0,
        })
        .hex
}

fn send_color(view: ViewId, picker: &mut ColorPicker) {
    let rgb = picker.rgb();
    if !picker.hex.focused {
        picker.hex.replace_text(hex_text(rgb));
    }
    if rgb != picker.sent && picker.id != 0 {
        picker.sent = rgb;
        PromptEngine.color_picker_update(view, picker.id, Some(rgb), false);
    }
}

pub fn set_color_hsv(s: &mut Browser, view: ViewId, hsv: Hsv) {
    if let Some(picker) = color_picker_mut(s, view) {
        picker.hsv = Hsv {
            hue: hsv.hue.clamp(0.0, 359.999),
            saturation: hsv.saturation.clamp(0.0, 1.0),
            value: hsv.value.clamp(0.0, 1.0),
        };
        send_color(view, picker);
    }
}

pub fn set_color_rgb(s: &mut Browser, view: ViewId, rgb: u32) {
    if let Some(picker) = color_picker_mut(s, view) {
        picker.hsv = Hsv::from_rgb(rgb);
        picker.hex.focused = false;
        send_color(view, picker);
    }
}

pub fn apply_color_hex(s: &mut Browser, view: ViewId) -> bool {
    let Some(picker) = color_picker_mut(s, view) else {
        return false;
    };
    let Some(rgb) = parse_hex(&picker.hex.text) else {
        return false;
    };
    if rgb != picker.rgb() {
        picker.hsv = Hsv::from_rgb(rgb);
    }
    send_color(view, picker);
    true
}

pub fn close_color_picker(s: &mut Browser, view: ViewId, accepted: bool, tx: &mut Tx) {
    if accepted {
        apply_color_hex(s, view);
    }
    let Some(picker) = s.page_prompts.of_mut(view).color_picker.take() else {
        return;
    };
    if picker.hex.focused {
        tx.blur();
    }
    if picker.id != 0 {
        PromptEngine.color_picker_update(view, picker.id, accepted.then(|| picker.rgb()), true);
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DateKind {
    Date,
    Month,
    Week,
    Time,
    DateTime,
}

impl DateKind {
    pub fn from_input_type(input_type: &str) -> Option<DateKind> {
        match input_type.to_ascii_lowercase().as_str() {
            "date" => Some(DateKind::Date),
            "month" => Some(DateKind::Month),
            "week" => Some(DateKind::Week),
            "time" => Some(DateKind::Time),
            "datetime-local" => Some(DateKind::DateTime),
            _ => None,
        }
    }

    pub fn has_calendar(self) -> bool {
        self != DateKind::Time
    }

    pub fn has_time(self) -> bool {
        matches!(self, DateKind::Time | DateKind::DateTime)
    }

    pub fn title(self) -> &'static str {
        match self {
            DateKind::Date => "Choose a date",
            DateKind::Month => "Choose a month",
            DateKind::Week => "Choose a week",
            DateKind::Time => "Choose a time",
            DateKind::DateTime => "Choose a date and time",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Day {
    pub year: i32,
    pub month: u32,
    pub day: u32,
}

pub fn is_leap_year(year: i32) -> bool {
    (year % 4 == 0 && year % 100 != 0) || year % 400 == 0
}

pub fn days_in_month(year: i32, month: u32) -> u32 {
    match month {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 if is_leap_year(year) => 29,
        _ => 28,
    }
}

fn days_from_civil(year: i32, month: u32, day: u32) -> i64 {
    let year = if month <= 2 { year - 1 } else { year } as i64;
    let era = year.div_euclid(400);
    let year_of_era = year - era * 400;
    let month = month as i64;
    let day_of_year =
        (153 * (if month > 2 { month - 3 } else { month + 9 }) + 2) / 5 + day as i64 - 1;
    let day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    era * 146097 + day_of_era - 719468
}

fn civil_from_days(days: i64) -> Day {
    let days = days + 719468;
    let era = days.div_euclid(146097);
    let day_of_era = days - era * 146097;
    let year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    let day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    let mp = (5 * day_of_year + 2) / 153;
    let day = (day_of_year - (153 * mp + 2) / 5 + 1) as u32;
    let month = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    let year = (year_of_era + era * 400 + if month <= 2 { 1 } else { 0 }) as i32;
    Day { year, month, day }
}

impl Day {
    pub fn weekday_from_monday(self) -> u32 {
        (days_from_civil(self.year, self.month, self.day) + 3).rem_euclid(7) as u32
    }

    pub fn plus_days(self, days: i64) -> Day {
        civil_from_days(days_from_civil(self.year, self.month, self.day) + days)
    }

    pub fn iso_week(self) -> (i32, u32) {
        let thursday = self.plus_days(3 - self.weekday_from_monday() as i64);
        let first = Day {
            year: thursday.year,
            month: 1,
            day: 1,
        };
        let ordinal = days_from_civil(thursday.year, thursday.month, thursday.day)
            - days_from_civil(first.year, first.month, first.day);
        (thursday.year, (ordinal / 7 + 1) as u32)
    }

    pub fn monday_of_iso_week(year: i32, week: u32) -> Day {
        let january_fourth = Day {
            year,
            month: 1,
            day: 4,
        };
        january_fourth.plus_days(
            -(january_fourth.weekday_from_monday() as i64) + (week.max(1) as i64 - 1) * 7,
        )
    }

    pub fn today() -> Day {
        let seconds = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|elapsed| elapsed.as_secs() as i64)
            .unwrap_or_default();
        civil_from_days(seconds.div_euclid(86400))
    }
}

pub const MONTH_NAMES: [&str; 12] = [
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December",
];

fn number<T: std::str::FromStr>(text: &str, digits: std::ops::RangeInclusive<usize>) -> Option<T> {
    (digits.contains(&text.len()) && text.bytes().all(|b| b.is_ascii_digit()))
        .then(|| text.parse().ok())
        .flatten()
}

pub fn parse_day(text: &str) -> Option<Day> {
    let mut parts = text.split('-');
    let year = number(parts.next()?, 4..=6)?;
    let month = number(parts.next()?, 2..=2)?;
    let day = number(parts.next()?, 2..=2)?;
    if parts.next().is_some()
        || !(1..=12).contains(&month)
        || day == 0
        || day > days_in_month(year, month)
    {
        return None;
    }
    Some(Day { year, month, day })
}

pub fn parse_month(text: &str) -> Option<(i32, u32)> {
    let (year, month) = text.split_once('-')?;
    let month = number(month, 2..=2)?;
    (1..=12)
        .contains(&month)
        .then_some((number(year, 4..=6)?, month))
}

pub fn parse_week(text: &str) -> Option<(i32, u32)> {
    let (year, week) = text.split_once("-W")?;
    let week = number(week, 2..=2)?;
    (1..=53)
        .contains(&week)
        .then_some((number(year, 4..=6)?, week))
}

pub fn parse_time(text: &str) -> Option<(u32, u32)> {
    let mut parts = text.split(':');
    let hour = number(parts.next()?, 2..=2)?;
    let minute = number(parts.next()?, 2..=2)?;
    (hour < 24 && minute < 60).then_some((hour, minute))
}

pub fn format_day(day: Day) -> String {
    format!("{:04}-{:02}-{:02}", day.year, day.month, day.day)
}

pub fn format_value(kind: DateKind, day: Day, time: (u32, u32)) -> String {
    let time_text = format!("{:02}:{:02}", time.0, time.1);
    match kind {
        DateKind::Date => format_day(day),
        DateKind::Month => format!("{:04}-{:02}", day.year, day.month),
        DateKind::Week => {
            let (year, week) = day.iso_week();
            format!("{year:04}-W{week:02}")
        }
        DateKind::Time => time_text,
        DateKind::DateTime => format!("{}T{time_text}", format_day(day)),
    }
}

pub fn parse_value(kind: DateKind, text: &str) -> Option<(Day, (u32, u32))> {
    let midnight = (0, 0);
    match kind {
        DateKind::Date => parse_day(text).map(|day| (day, midnight)),
        DateKind::Month => parse_month(text).map(|(year, month)| {
            (
                Day {
                    year,
                    month,
                    day: 1,
                },
                midnight,
            )
        }),
        DateKind::Week => {
            parse_week(text).map(|(year, week)| (Day::monday_of_iso_week(year, week), midnight))
        }
        DateKind::Time => parse_time(text).map(|time| (Day::today(), time)),
        DateKind::DateTime => {
            let (day, time) = text.split_once(['T', ' '])?;
            Some((parse_day(day)?, parse_time(time)?))
        }
    }
}

pub struct DatePicker {
    pub id: PromptId,
    pub kind: DateKind,
    pub shown_year: i32,
    pub shown_month: u32,
    pub selected: Option<Day>,
    pub time: TextEdit,
    pub minimum: String,
    pub maximum: String,
}

impl DatePicker {
    pub fn value_for(&self, day: Day) -> Option<String> {
        let time = if self.kind.has_time() {
            parse_time(&self.time.text)?
        } else {
            (0, 0)
        };
        Some(format_value(self.kind, day, time))
    }

    pub fn allows(&self, value: &str) -> bool {
        let comparable = |bound: &str| bound.len() == value.len();
        (self.minimum.is_empty() || !comparable(&self.minimum) || value >= self.minimum.as_str())
            && (self.maximum.is_empty()
                || !comparable(&self.maximum)
                || value <= self.maximum.as_str())
    }

    pub fn day_allowed(&self, day: Day) -> bool {
        let probe = match self.kind {
            DateKind::DateTime => format_day(day),
            _ => format_value(self.kind, day, (0, 0)),
        };
        let bound = |bound: &str| bound.get(..probe.len()).unwrap_or(bound).to_owned();
        (self.minimum.is_empty() || probe >= bound(&self.minimum))
            && (self.maximum.is_empty() || probe <= bound(&self.maximum))
    }
}

pub fn date_picker_requested(
    s: &mut Browser,
    view: ViewId,
    id: PromptId,
    input_type: String,
    value: String,
    minimum: String,
    maximum: String,
) {
    let Some(kind) = DateKind::from_input_type(&input_type).filter(|_| view_exists(s, view)) else {
        PromptEngine.close_date_picker(view, id, None);
        return;
    };
    let parsed = parse_value(kind, &value);
    let today = Day::today();
    let shown = parsed.map(|(day, _)| day).unwrap_or(today);
    let time = parsed.map(|(_, time)| time).unwrap_or((0, 0));
    let prompts = s.page_prompts.of_mut(view);
    if let Some(previous) = prompts.date_picker.take() {
        PromptEngine.close_date_picker(view, previous.id, None);
    }
    prompts.tooltip = None;
    prompts.date_picker = Some(DatePicker {
        id,
        kind,
        shown_year: shown.year,
        shown_month: shown.month,
        selected: parsed
            .filter(|_| kind != DateKind::Time)
            .map(|(day, _)| day),
        time: TextEdit::new(if parsed.is_some() || kind.has_time() {
            format!("{:02}:{:02}", time.0, time.1)
        } else {
            String::new()
        }),
        minimum,
        maximum,
    });
}

pub fn date_time_edit(s: &mut Browser, view: ViewId) -> &mut TextEdit {
    &mut s
        .page_prompts
        .of_mut(view)
        .date_picker
        .get_or_insert_with(|| DatePicker {
            id: 0,
            kind: DateKind::Time,
            shown_year: 1970,
            shown_month: 1,
            selected: None,
            time: TextEdit::new(""),
            minimum: String::new(),
            maximum: String::new(),
        })
        .time
}

pub fn show_other_month(s: &mut Browser, view: ViewId, offset: i32) {
    if let Some(picker) = s.page_prompts.of_mut(view).date_picker.as_mut() {
        let step = if picker.kind == DateKind::Month {
            offset * 12
        } else {
            offset
        };
        let index = picker.shown_year * 12 + picker.shown_month as i32 - 1 + step;
        picker.shown_year = index.div_euclid(12);
        picker.shown_month = index.rem_euclid(12) as u32 + 1;
    }
}

pub fn pick_day(s: &mut Browser, view: ViewId, day: Day, tx: &mut Tx) {
    let Some(picker) = s.page_prompts.of_mut(view).date_picker.as_mut() else {
        return;
    };
    if !picker.day_allowed(day) {
        return;
    }
    picker.selected = Some(day);
    picker.shown_year = day.year;
    picker.shown_month = day.month;
    if !picker.kind.has_time() {
        close_date_picker(s, view, true, tx);
    }
}

pub fn date_picker_value(picker: &DatePicker) -> Option<String> {
    let day = match picker.kind {
        DateKind::Time => Day::today(),
        _ => picker.selected?,
    };
    picker.value_for(day).filter(|value| picker.allows(value))
}

pub fn close_date_picker(s: &mut Browser, view: ViewId, accepted: bool, tx: &mut Tx) {
    let Some(prompts) = s.page_prompts.views_mut().get_mut(&view) else {
        return;
    };
    if accepted
        && prompts
            .date_picker
            .as_ref()
            .is_some_and(|picker| date_picker_value(picker).is_none())
    {
        return;
    }
    let Some(picker) = prompts.date_picker.take() else {
        return;
    };
    if picker.time.focused {
        tx.blur();
    }
    let value = accepted.then(|| date_picker_value(&picker)).flatten();
    if picker.id != 0 {
        PromptEngine.close_date_picker(view, picker.id, value.as_deref());
    }
}

pub fn clear_date_picker(s: &mut Browser, view: ViewId, tx: &mut Tx) {
    let Some(picker) = s.page_prompts.of_mut(view).date_picker.take() else {
        return;
    };
    if picker.time.focused {
        tx.blur();
    }
    if picker.id != 0 {
        PromptEngine.close_date_picker(view, picker.id, Some(""));
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WebAuthnState {
    InsertKey,
    EnterPin,
    TouchKey,
    ChooseAccount,
    AttestationConsent,
    KeyNotRegistered,
    KeyAlreadyRegistered,
    PinBlocked,
    PinNotSet,
    KeyNotSupported,
    NoPlatformAuthenticator,
}

pub struct WebAuthn {
    pub state: WebAuthnState,
    pub registering: bool,
    pub relying_party: String,
    pub user_name: String,
    pub caller: String,
    pub pin_was_wrong: bool,
    pub pin_retries: Option<u32>,
    pub accounts: Vec<String>,
    pub chosen_account: usize,
    pub pin: TextEdit,
}

impl WebAuthn {
    pub fn title(&self) -> String {
        if self.registering {
            format!("Register a security key with {}", self.relying_party)
        } else {
            format!("Sign in to {} with a security key", self.relying_party)
        }
    }

    pub fn body(&self) -> String {
        let rp = &self.relying_party;
        match self.state {
            WebAuthnState::InsertKey if self.registering && !self.user_name.is_empty() => format!(
                "Insert your security key and touch it, to register it for {}.",
                self.user_name
            ),
            WebAuthnState::InsertKey => "Insert your security key and touch it.".to_owned(),
            WebAuthnState::EnterPin => {
                let mut body = if self.pin_was_wrong {
                    "That PIN was wrong. Enter your security key's PIN.".to_owned()
                } else {
                    "Enter your security key's PIN.".to_owned()
                };
                if let Some(retries) = self.pin_retries {
                    body.push_str(&format!(
                        " {retries} {} left.",
                        if retries == 1 { "attempt" } else { "attempts" }
                    ));
                }
                body
            }
            WebAuthnState::TouchKey => "Touch your security key to finish.".to_owned(),
            WebAuthnState::ChooseAccount => format!(
                "Your security key has more than one account for {rp}. Choose the one to sign in with."
            ),
            WebAuthnState::AttestationConsent => format!(
                "{rp} wants to know your security key's make and model, which could help identify you."
            ),
            WebAuthnState::KeyNotRegistered => {
                format!("This security key isn't registered with {rp}. Try another one.")
            }
            WebAuthnState::KeyAlreadyRegistered => {
                format!("This security key is already registered with {rp}.")
            }
            WebAuthnState::PinBlocked => "This security key's PIN is blocked after too many wrong attempts. Try another key, or reset this one with its maker's app.".to_owned(),
            WebAuthnState::PinNotSet => format!(
                "{rp} needs your security key to have a PIN, and it has none. Set one with the key maker's app, then try again."
            ),
            WebAuthnState::KeyNotSupported => {
                format!("This security key can't do what {rp} asked for. Try another one.")
            }
            WebAuthnState::NoPlatformAuthenticator => format!(
                "{rp} asked for an authenticator built into this device, such as Touch ID, which Xenon can't use yet."
            ),
        }
    }

    pub fn accept_label(&self) -> Option<&'static str> {
        match self.state {
            WebAuthnState::EnterPin => Some("Continue"),
            WebAuthnState::ChooseAccount => Some("Sign in"),
            WebAuthnState::AttestationConsent => Some("Share"),
            _ => None,
        }
    }

    pub fn dismiss_label(&self) -> &'static str {
        match self.state {
            WebAuthnState::AttestationConsent => "Don't share",
            WebAuthnState::KeyAlreadyRegistered => "OK",
            _ => "Cancel",
        }
    }
}

pub fn web_authn_prompt(s: &mut Browser, view: ViewId, mut prompt: WebAuthn) {
    if !view_exists(s, view) {
        PromptEngine.web_authn_cancel(view);
        return;
    }
    let focus_pin = prompt.state == WebAuthnState::EnterPin && active_view(s) == Some(view);
    prompt.pin.focused = focus_pin;
    let prompts = s.page_prompts.of_mut(view);
    prompts.tooltip = None;
    prompts.web_authn = Some(prompt);
}

pub fn web_authn_prompt_closed(s: &mut Browser, view: ViewId) {
    if let Some(prompts) = s.page_prompts.views_mut().get_mut(&view) {
        prompts.web_authn = None;
    }
}

pub fn web_authn_pin(s: &mut Browser, view: ViewId) -> &mut TextEdit {
    &mut s
        .page_prompts
        .of_mut(view)
        .web_authn
        .get_or_insert_with(|| WebAuthn {
            state: WebAuthnState::EnterPin,
            registering: false,
            relying_party: String::new(),
            user_name: String::new(),
            caller: String::new(),
            pin_was_wrong: false,
            pin_retries: None,
            accounts: Vec::new(),
            chosen_account: 0,
            pin: TextEdit::new("").secure(),
        })
        .pin
}

pub fn choose_web_authn_account(s: &mut Browser, view: ViewId, index: usize) {
    if let Some(prompt) = s.page_prompts.of_mut(view).web_authn.as_mut() {
        prompt.chosen_account = index.min(prompt.accounts.len().saturating_sub(1));
    }
}

pub fn submit_web_authn(s: &mut Browser, view: ViewId) {
    let Some(prompt) = s.page_prompts.of_mut(view).web_authn.as_mut() else {
        return;
    };
    match prompt.state {
        WebAuthnState::EnterPin => {
            let pin = prompt.pin.text.clone();
            prompt.pin.replace_text(String::new());
            if !pin.is_empty() {
                PromptEngine.web_authn_provide_pin(view, &pin);
            }
        }
        WebAuthnState::ChooseAccount if !prompt.accounts.is_empty() => {
            PromptEngine.web_authn_choose_account(view, prompt.chosen_account);
        }
        WebAuthnState::AttestationConsent => PromptEngine.web_authn_allow_attestation(view, true),
        _ => {}
    }
}

pub fn dismiss_web_authn(s: &mut Browser, view: ViewId, tx: &mut Tx) {
    let Some(prompts) = s.page_prompts.views_mut().get_mut(&view) else {
        return;
    };
    if prompts
        .web_authn
        .as_ref()
        .is_some_and(|prompt| prompt.state == WebAuthnState::AttestationConsent)
    {
        PromptEngine.web_authn_allow_attestation(view, false);
        return;
    }
    if let Some(prompt) = prompts.web_authn.take() {
        if prompt.pin.focused {
            tx.blur();
        }
        PromptEngine.web_authn_cancel(view);
    }
}

pub fn key(s: &mut Browser, event: &KeyEvent, tx: &mut Tx) -> bool {
    let Some(view) = active_view(s) else {
        return false;
    };
    let Some(prompts) = s.page_prompts.of(view) else {
        return false;
    };
    if prompts.color_picker.is_some() {
        match event.key {
            Key::Escape => close_color_picker(s, view, false, tx),
            Key::Enter => close_color_picker(s, view, true, tx),
            _ => return false,
        }
        return true;
    }
    if prompts.date_picker.is_some() {
        match event.key {
            Key::Escape => close_date_picker(s, view, false, tx),
            Key::Enter => close_date_picker(s, view, true, tx),
            _ => return false,
        }
        return true;
    }
    if prompts.web_authn.is_some() {
        match event.key {
            Key::Escape => {
                if let Some(prompts) = s.page_prompts.views_mut().get_mut(&view)
                    && let Some(prompt) = prompts.web_authn.take()
                {
                    if prompt.pin.focused {
                        tx.blur();
                    }
                    PromptEngine.web_authn_cancel(view);
                }
            }
            Key::Enter => submit_web_authn(s, view),
            Key::Up | Key::Down => {
                let delta: isize = if event.key == Key::Up { -1 } else { 1 };
                if let Some(prompt) = s.page_prompts.of_mut(view).web_authn.as_mut() {
                    prompt.chosen_account = prompt
                        .chosen_account
                        .saturating_add_signed(delta)
                        .min(prompt.accounts.len().saturating_sub(1));
                }
            }
            _ => return false,
        }
        return true;
    }
    false
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn colours_round_trip_through_hsv() {
        for rgb in [0x000000, 0xffffff, 0xff0050, 0x123456, 0x80ff00, 0x0000ff] {
            assert_eq!(Hsv::from_rgb(rgb).to_rgb(), rgb, "{rgb:06x}");
        }
        assert_eq!(parse_hex("#FF0050"), Some(0xff0050));
        assert_eq!(parse_hex("f05"), Some(0xff0055));
        assert_eq!(parse_hex("#12345"), None);
        assert_eq!(hex_text(0x0a0b0c), "#0a0b0c");
    }

    #[test]
    fn calendar_arithmetic_follows_the_gregorian_calendar() {
        assert_eq!(days_in_month(2024, 2), 29);
        assert_eq!(days_in_month(1900, 2), 28);
        assert_eq!(days_in_month(2000, 2), 29);
        let day = Day {
            year: 2026,
            month: 11,
            day: 6,
        };
        assert_eq!(day.weekday_from_monday(), 4);
        assert_eq!(
            day.plus_days(30),
            Day {
                year: 2026,
                month: 12,
                day: 6
            }
        );
        assert_eq!(day.iso_week(), (2026, 45));
        assert_eq!(
            Day {
                year: 2021,
                month: 1,
                day: 3
            }
            .iso_week(),
            (2020, 53)
        );
        assert_eq!(
            Day::monday_of_iso_week(2026, 45),
            Day {
                year: 2026,
                month: 11,
                day: 2
            }
        );
    }

    #[test]
    fn input_values_parse_and_format_as_html_does() {
        let (day, time) = parse_value(DateKind::DateTime, "2026-02-28T09:05").unwrap();
        assert_eq!(
            format_value(DateKind::DateTime, day, time),
            "2026-02-28T09:05"
        );
        assert_eq!(parse_value(DateKind::Date, "2026-02-30"), None);
        let (day, _) = parse_value(DateKind::Week, "2026-W45").unwrap();
        assert_eq!(format_value(DateKind::Week, day, (0, 0)), "2026-W45");
        let (day, _) = parse_value(DateKind::Month, "2026-07").unwrap();
        assert_eq!(format_value(DateKind::Month, day, (0, 0)), "2026-07");
        assert_eq!(parse_time("23:59:30"), Some((23, 59)));
        assert_eq!(parse_time("24:00"), None);
        assert_eq!(parse_time("07:30"), Some((7, 30)));
    }

    #[test]
    fn date_pickers_keep_to_the_inputs_range() {
        let picker = DatePicker {
            id: 1,
            kind: DateKind::Date,
            shown_year: 2026,
            shown_month: 11,
            selected: None,
            time: TextEdit::new(""),
            minimum: "2026-11-05".to_owned(),
            maximum: "2026-11-20".to_owned(),
        };
        let day = |day| Day {
            year: 2026,
            month: 11,
            day,
        };
        assert!(!picker.day_allowed(day(4)));
        assert!(picker.day_allowed(day(5)));
        assert!(picker.day_allowed(day(20)));
        assert!(!picker.day_allowed(day(21)));
    }

    #[test]
    fn web_authn_prompts_read_as_the_qt_popover_does() {
        let mut prompt = WebAuthn {
            state: WebAuthnState::EnterPin,
            registering: false,
            relying_party: "example.com".to_owned(),
            user_name: String::new(),
            caller: String::new(),
            pin_was_wrong: true,
            pin_retries: Some(1),
            accounts: Vec::new(),
            chosen_account: 0,
            pin: TextEdit::new(""),
        };
        assert_eq!(prompt.title(), "Sign in to example.com with a security key");
        assert_eq!(
            prompt.body(),
            "That PIN was wrong. Enter your security key's PIN. 1 attempt left."
        );
        assert_eq!(prompt.accept_label(), Some("Continue"));
        prompt.state = WebAuthnState::AttestationConsent;
        assert_eq!(prompt.dismiss_label(), "Don't share");
        prompt.registering = true;
        prompt.state = WebAuthnState::InsertKey;
        prompt.user_name = "ada".to_owned();
        assert_eq!(
            prompt.body(),
            "Insert your security key and touch it, to register it for ada."
        );
    }
}
