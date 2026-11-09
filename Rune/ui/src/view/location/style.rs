/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{Color, Icon, oklch, oklcha};

use crate::view::palette::{Palette, srgb};

pub const LOCATION_FONT: TextStyle = TextStyle::new(13.5, 400.0);
pub const PILL_FONT: TextStyle = TextStyle::new(12.0, 500.0);
pub const ROW_PRIMARY_FONT: TextStyle = TextStyle::new(13.5, 400.0);
pub const ROW_SECONDARY_FONT: TextStyle = TextStyle::new(12.0, 400.0);
pub const POPOVER_TITLE_FONT: TextStyle = TextStyle::new(13.5, 600.0);
pub const POPOVER_BODY_FONT: TextStyle = TextStyle::new(12.5, 400.0);
pub const POPOVER_HEADING_FONT: TextStyle = TextStyle::new(11.0, 600.0);
pub const POPOVER_MONO_FONT: TextStyle = TextStyle::new(11.0, 400.0);

const ROSE_10: u32 = 0xffe0ea;
const ROSE_20: u32 = 0xffc2d5;
const ROSE_40: u32 = 0xff85aa;
const ROSE_60: u32 = 0xff4d80;
const ROSE_500: u32 = 0xcc0042;
const ROSE_900: u32 = 0x990032;
const ROSE_950: u32 = 0x3d0a1d;

#[derive(Clone, Copy, Debug)]
pub struct LocationColors {
    pub dimmed_text: Color,
    pub not_secure_text: Color,
    pub not_secure_fill: Color,
    pub not_secure_hover: Color,
    pub not_secure_border: Color,
    pub bang_text: Color,
    pub bang_fill: Color,
    pub bang_border: Color,
    pub popup: Color,
    pub popup_border: Color,
    pub row_selected: Color,
    pub row_hover: Color,
    pub warning: Color,
    pub shadow: Color,
}

impl LocationColors {
    pub fn new(dark: bool, p: &Palette) -> LocationColors {
        if dark {
            LocationColors {
                dimmed_text: p.text.alpha(0.7),
                not_secure_text: srgb(0xe08e88),
                not_secure_fill: srgb(0x4a3230),
                not_secure_hover: srgb(0x553634),
                not_secure_border: srgb(0x5c3a37),
                bang_text: srgb(ROSE_40),
                bang_fill: srgb(ROSE_950),
                bang_border: srgb(ROSE_900),
                popup: oklch(0.235, 0.005, 265.0),
                popup_border: oklcha(1.0, 0.0, 0.0, 0.1),
                row_selected: oklcha(1.0, 0.0, 0.0, 0.09),
                row_hover: oklcha(1.0, 0.0, 0.0, 0.06),
                warning: srgb(ROSE_60),
                shadow: oklcha(0.0, 0.0, 0.0, 0.45),
            }
        } else {
            LocationColors {
                dimmed_text: p.text.alpha(0.58),
                not_secure_text: srgb(0x903e38),
                not_secure_fill: srgb(0xf6ebe9),
                not_secure_hover: srgb(0xf2e2df),
                not_secure_border: srgb(0xe0cbc7),
                bang_text: srgb(ROSE_500),
                bang_fill: srgb(ROSE_10),
                bang_border: srgb(ROSE_20),
                popup: oklch(0.995, 0.0, 0.0),
                popup_border: oklcha(0.2, 0.01, 265.0, 0.14),
                row_selected: oklcha(0.2, 0.01, 265.0, 0.08),
                row_hover: oklcha(0.2, 0.01, 265.0, 0.05),
                warning: srgb(ROSE_500),
                shadow: oklcha(0.2, 0.01, 265.0, 0.18),
            }
        }
    }
}

macro_rules! stroked {
    ($name:literal, $path:literal) => {
        Icon {
            name: $name,
            view: 24.0,
            body: concat!(
                r#"<path d=""#,
                $path,
                r#"" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#
            ),
        }
    };
}

pub static LOCK: Icon = stroked!(
    "xenon-location-lock",
    "M7 11V8a5 5 0 0 1 10 0v3M6 11h12a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1v-8a1 1 0 0 1 1-1z"
);
pub static SEARCH: Icon = stroked!(
    "xenon-location-search",
    "M10.5 4a6.5 6.5 0 1 0 0 13a6.5 6.5 0 1 0 0-13M15.5 15.5L20 20"
);
pub static STAR: Icon = Icon {
    name: "xenon-location-star",
    view: 24.0,
    body: r#"<path d="M12 3l2.7 5.6 6.1.9-4.4 4.3 1 6.1L12 17l-5.4 2.9 1-6.1-4.4-4.3 6.1-.9z" fill="currentColor"/>"#,
};
