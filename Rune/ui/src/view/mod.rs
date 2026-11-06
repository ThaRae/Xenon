/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#[cfg(all(test, feature = "mock-engine"))]
mod chrome_accessibility_tests;
mod downloads;
mod icons;
mod location;
mod page;
mod page_accessibility;
mod page_overlays;
pub mod palette;
mod shield;
mod tabs;
mod toolbar;
mod window;

pub use window::browser_view;
