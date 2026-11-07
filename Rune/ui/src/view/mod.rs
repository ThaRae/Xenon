/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

mod bookmarks;
#[cfg(all(test, feature = "mock-engine"))]
mod chrome_accessibility_tests;
mod crash_offer;
mod downloads;
mod element_picker;
mod extensions;
mod icons;
mod location;
mod page;
mod page_accessibility;
mod page_overlays;
pub mod palette;
mod picture_in_picture;
mod shield;
mod split_view;
mod tab_groups;
mod tabs;
mod task_manager;
mod toolbar;
mod update_button;
mod window;
mod window_overlays;

pub use window::browser_view;
