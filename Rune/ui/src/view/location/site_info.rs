/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Align, Color, El, Side, col, row, text};

use crate::location::{SiteInfo, SiteSecurity, SiteSecurityState};
use crate::state::Browser;
use crate::view::location::style::{
    LocationColors, POPOVER_BODY_FONT, POPOVER_HEADING_FONT, POPOVER_MONO_FONT, POPOVER_TITLE_FONT,
};
use crate::view::palette::Palette;

const CARD_WIDTH: f32 = 340.0;
const NAME_WIDTH: f32 = 92.0;
const ANCHOR_GAP: f32 = 6.0;
const CHAIN_INDENT: f32 = 12.0;

pub struct Verdict {
    pub state: &'static str,
    pub summary: &'static str,
    pub warning: bool,
}

pub fn verdict(state: SiteSecurityState) -> Verdict {
    match state {
        SiteSecurityState::NotSecure | SiteSecurityState::None => Verdict {
            state: "Connection isn't secure",
            summary: "Don't enter passwords or payment details on this site. Others on the network can see and change what you send and receive here.",
            warning: true,
        },
        SiteSecurityState::AcceptedCertificate => Verdict {
            state: "Certificate not verified",
            summary: "You chose to proceed to this site although its certificate couldn't be verified, so Xenon can't tell that the connection is really to this site.",
            warning: true,
        },
        SiteSecurityState::Secure => Verdict {
            state: "Connection is secure",
            summary: "Information you send to or receive from this site is private to it.",
            warning: false,
        },
    }
}

fn section(key: &str, heading: &str, muted: Color) -> El {
    col().key(key.to_owned()).w_full().gap(4.0).mt(8.0).child(
        text(heading.to_owned())
            .font(POPOVER_HEADING_FONT)
            .color(muted)
            .role("heading"),
    )
}

fn detail(name: &str, value: &str, p: &Palette, mono: bool) -> Option<El> {
    if value.is_empty() {
        return None;
    }
    Some(
        row()
            .w_full()
            .gap(12.0)
            .child(
                text(name.to_owned())
                    .font(POPOVER_BODY_FONT)
                    .color(p.text_muted)
                    .w(NAME_WIDTH),
            )
            .child(
                text(value.to_owned())
                    .font(if mono {
                        POPOVER_MONO_FONT
                    } else {
                        POPOVER_BODY_FONT
                    })
                    .color(p.text)
                    .wrap()
                    .selectable(true)
                    .grow(1.0)
                    .shrink()
                    .min_w(0.0),
            ),
    )
}

fn body(security: &SiteSecurity, p: &Palette, colors: &LocationColors) -> Vec<El> {
    let verdict = verdict(security.state);
    let mut parts = vec![
        text(verdict.state)
            .key("site-info-state")
            .font(POPOVER_TITLE_FONT)
            .color(if verdict.warning {
                colors.warning
            } else {
                p.text
            }),
        text(verdict.summary)
            .font(POPOVER_BODY_FONT)
            .color(p.text_muted)
            .wrap()
            .w_full(),
    ];
    if security.state != SiteSecurityState::NotSecure {
        let connection = section("site-info-connection", "Connection", p.text_muted);
        parts.push(match &security.connection {
            Some((protocol, cipher)) => connection
                .children(detail("Protocol", protocol, p, false))
                .children(detail("Cipher", cipher, p, false)),
            None => connection.child(
                text(if security.is_final {
                    "Xenon has no details of this connection. The page may have come from the cache."
                } else {
                    "Looking up this connection…"
                })
                .font(POPOVER_BODY_FONT)
                .color(p.text_muted)
                .wrap()
                .w_full(),
            ),
        });
    }
    if let Some(certificate) = &security.certificate {
        parts.push(
            section("site-info-certificate", "Certificate", p.text_muted)
                .children(detail("Issued to", &certificate.issued_to, p, false))
                .children(detail("Issued by", &certificate.issued_by, p, false))
                .children(detail("Valid from", &certificate.valid_from, p, false))
                .children(detail("Valid until", &certificate.valid_until, p, false))
                .children(detail("SHA-256", &certificate.fingerprint, p, true)),
        );
    }
    if !security.chain.is_empty() {
        parts.push(
            section("site-info-chain", "Certificate chain", p.text_muted).children(
                security.chain.iter().enumerate().map(|(depth, name)| {
                    text(name.clone())
                        .font(POPOVER_BODY_FONT)
                        .color(p.text)
                        .ml(depth as f32 * CHAIN_INDENT)
                }),
            ),
        );
    }
    parts
}

pub fn site_info_popover(
    s: &Browser,
    site_info: &SiteInfo,
    p: &Palette,
    colors: &LocationColors,
) -> El {
    let host = site_info
        .security
        .as_ref()
        .map(|security| security.host.clone())
        .or_else(|| s.location.display().map(|display| display.text))
        .unwrap_or_default();
    let card = col()
        .key("site-info")
        .role("dialog")
        .label("Site information")
        .w(CARD_WIDTH)
        .px(16.0)
        .pt(14.0)
        .pb(16.0)
        .gap(6.0)
        .fill(p.surface)
        .radius(10.0)
        .border(p.hairline, colors.popup_border)
        .shadow(8.0, 24.0, colors.shadow)
        .anchored("location-security", Side::Bottom, Align::Start, ANCHOR_GAP)
        .modal(true, |s: &mut Browser| s.location.close_site_info())
        .child(
            text(host)
                .key("site-info-host")
                .font(POPOVER_TITLE_FONT)
                .color(p.text)
                .truncate()
                .w_full(),
        );
    match &site_info.security {
        Some(security) => card.children(body(security, p, colors)),
        None => card.child(
            text("Looking up this connection…")
                .font(POPOVER_BODY_FONT)
                .color(p.text_muted),
        ),
    }
}
