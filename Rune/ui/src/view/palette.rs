/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Color, oklch, oklcha};

pub mod rose {
    pub const ROSE_20: u32 = 0xffc2d5;
    pub const ROSE_60: u32 = 0xff4d80;
    pub const ROSE_100: u32 = 0xff0050;
    pub const ROSE_900: u32 = 0x990032;
}

pub fn srgb(rgb: u32) -> Color {
    let channel = |shift: u32| rune::color::srgb_decode(((rgb >> shift) & 0xff) as f32 / 255.0);
    Color {
        r: channel(16),
        g: channel(8),
        b: channel(0),
        a: 1.0,
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Palette {
    pub hairline: f32,
    pub chrome: Color,
    pub surface: Color,
    pub tab_hover: Color,
    pub text: Color,
    pub text_muted: Color,
    pub icon: Color,
    pub icon_disabled: Color,
    pub separator: Color,
    pub hover: Color,
    pub press: Color,
    pub field: Color,
    pub field_focused: Color,
    pub accent: Color,
    pub focus_ring: Color,
    pub focus_glow: Color,
    pub selection: Color,
    pub page: Color,
    pub bubble: Color,
    pub bubble_border: Color,
}

impl Palette {
    pub fn new(dark: bool, scale: f32) -> Palette {
        let accent = srgb(rose::ROSE_100);
        let hairline = 1.0 / scale.max(1.0);
        if dark {
            Palette {
                hairline,
                chrome: oklch(0.185, 0.004, 265.0),
                surface: oklch(0.25, 0.005, 265.0),
                tab_hover: oklcha(1.0, 0.0, 0.0, 0.05),
                text: oklch(0.94, 0.003, 265.0),
                text_muted: oklch(0.7, 0.008, 265.0),
                icon: oklch(0.86, 0.004, 265.0),
                icon_disabled: oklcha(0.86, 0.004, 265.0, 0.3),
                separator: oklcha(1.0, 0.0, 0.0, 0.08),
                hover: oklcha(1.0, 0.0, 0.0, 0.08),
                press: oklcha(1.0, 0.0, 0.0, 0.13),
                field: oklch(0.205, 0.004, 265.0),
                field_focused: oklch(0.19, 0.004, 265.0),
                accent,
                focus_ring: srgb(rose::ROSE_60),
                focus_glow: srgb(rose::ROSE_100).alpha(0.28),
                selection: srgb(rose::ROSE_900).alpha(0.75),
                page: oklch(0.2, 0.004, 265.0),
                bubble: oklch(0.26, 0.005, 265.0),
                bubble_border: oklcha(1.0, 0.0, 0.0, 0.1),
            }
        } else {
            Palette {
                hairline,
                chrome: oklch(0.925, 0.004, 265.0),
                surface: oklch(0.99, 0.001, 265.0),
                tab_hover: oklcha(0.2, 0.01, 265.0, 0.06),
                text: oklch(0.24, 0.006, 265.0),
                text_muted: oklch(0.5, 0.01, 265.0),
                icon: oklch(0.36, 0.006, 265.0),
                icon_disabled: oklcha(0.36, 0.006, 265.0, 0.3),
                separator: oklcha(0.2, 0.01, 265.0, 0.12),
                hover: oklcha(0.2, 0.01, 265.0, 0.07),
                press: oklcha(0.2, 0.01, 265.0, 0.12),
                field: oklch(0.945, 0.004, 265.0),
                field_focused: oklch(1.0, 0.0, 0.0),
                accent,
                focus_ring: accent,
                focus_glow: srgb(rose::ROSE_100).alpha(0.2),
                selection: srgb(rose::ROSE_20),
                page: oklch(1.0, 0.0, 0.0),
                bubble: oklch(0.975, 0.002, 265.0),
                bubble_border: oklcha(0.2, 0.01, 265.0, 0.16),
            }
        }
    }
}

impl Palette {
    pub fn for_window(self, active: bool) -> Palette {
        if active {
            return self;
        }
        Palette {
            chrome: self.chrome.mix(self.surface, INACTIVE_CHROME_MIX),
            ..self
        }
    }
}

const INACTIVE_CHROME_MIX: f32 = 0.4;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_inactive_window_moves_its_tab_strip_toward_the_toolbar() {
        let active = Palette::new(false, 1.0);
        let inactive = active.for_window(false);
        assert_eq!(active.for_window(true).chrome.hex(), active.chrome.hex());
        assert_ne!(inactive.chrome.hex(), active.chrome.hex());
        assert_eq!(
            inactive.chrome.hex(),
            active.chrome.mix(active.surface, 0.4).hex()
        );
    }

    #[test]
    fn brand_accent_is_xenon_rose() {
        let accent = Palette::new(false, 1.0).accent;
        assert_eq!(accent.hex(), srgb(rose::ROSE_100).hex());
        assert_eq!(
            rune::color::hex("#ff0050").map(|c| c.hex()),
            Some(accent.hex())
        );
    }
}
