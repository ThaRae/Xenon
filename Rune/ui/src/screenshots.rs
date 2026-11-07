/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;
use std::path::PathBuf;
use std::time::Duration;

use std::sync::Arc;

use rune::{Drag, FileDialogOptions, FileDialogResult, Phase, Pixels, Tx, WheelEvent};

use crate::engine::{Engine, Modifiers, MouseEvent, MouseEventKind, ViewId};
use crate::ffi::XenonStr;
use crate::ffi_screenshots::{self, XenonScreenshotCallbacks};
use crate::state::Browser;
use crate::window_slots::Slots;

pub const MINIMUM_SELECTION_SIZE: f32 = 8.0;
const SCROLL_SETTLE_TIME: f64 = 0.1;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Answer {
    Selection,
    VisiblePage,
    FullPage,
    Cancel,
}

pub struct ScreenshotSelection {
    pub view: ViewId,
    pub surface_id: ViewId,
    pub frame: Pixels,
    pub drag: Option<((f32, f32), (f32, f32))>,
    pub reveals_page: bool,
    settle_due: Option<f64>,
}

impl ScreenshotSelection {
    pub fn selection(&self) -> Option<[f32; 4]> {
        let ((x0, y0), (x1, y1)) = self.drag?;
        Some(normalized([x0, y0, x1, y1]))
    }
}

pub fn normalized([x0, y0, x1, y1]: [f32; 4]) -> [f32; 4] {
    [x0.min(x1), y0.min(y1), (x1 - x0).abs(), (y1 - y0).abs()]
}

pub fn clipped([x, y, width, height]: [f32; 4], (area_width, area_height): (f32, f32)) -> [f32; 4] {
    let left = x.clamp(0.0, area_width);
    let top = y.clamp(0.0, area_height);
    let right = (x + width).clamp(0.0, area_width);
    let bottom = (y + height).clamp(0.0, area_height);
    [left, top, right - left, bottom - top]
}

fn answer_raw(answer: Answer) -> u32 {
    match answer {
        Answer::Selection => ffi_screenshots::XENON_SCREENSHOT_SELECTION,
        Answer::VisiblePage => ffi_screenshots::XENON_SCREENSHOT_VISIBLE_PAGE,
        Answer::FullPage => ffi_screenshots::XENON_SCREENSHOT_FULL_PAGE,
        Answer::Cancel => ffi_screenshots::XENON_SCREENSHOT_CANCEL,
    }
}

pub fn selection_requested(s: &mut Browser, view: ViewId, surface_id: ViewId) {
    if s.tabs.active_view() != Some(view) {
        finish_with(view, Answer::Cancel, [0.0; 4]);
        s.close_surface(surface_id);
        return;
    }
    if let Some(previous) = s.screenshot.take() {
        finish_with(previous.view, Answer::Cancel, [0.0; 4]);
        s.close_surface(previous.surface_id);
    }
    s.screenshot = Some(ScreenshotSelection {
        view,
        surface_id,
        frame: s.surface_for(surface_id),
        drag: None,
        reveals_page: false,
        settle_due: None,
    });
}

fn finish_with(view: ViewId, answer: Answer, [x, y, width, height]: [f32; 4]) {
    unsafe {
        ffi_screenshots::xenon_engine_finish_screenshot(
            view,
            answer_raw(answer),
            x,
            y,
            width,
            height,
        )
    }
}

pub fn finish(s: &mut Browser, answer: Answer) {
    let Some(selection) = s.screenshot.take() else {
        return;
    };
    let scale = s.page.scale();
    let device = match (answer, selection.selection()) {
        (Answer::Selection, Some(area)) => {
            let [x, y, width, height] = clipped(area, s.page_size());
            if width < MINIMUM_SELECTION_SIZE || height < MINIMUM_SELECTION_SIZE {
                s.screenshot = Some(ScreenshotSelection {
                    drag: None,
                    ..selection
                });
                return;
            }
            [x * scale, y * scale, width * scale, height * scale]
        }
        (Answer::Selection, None) => return s.screenshot = Some(selection),
        _ => [0.0; 4],
    };
    finish_with(selection.view, answer, device);
    s.close_surface(selection.surface_id);
}

fn cover_page(s: &mut Browser) {
    if let Some(selection) = s.screenshot.as_mut()
        && selection.reveals_page
    {
        selection.reveals_page = false;
        selection.settle_due = None;
        unsafe { ffi_screenshots::xenon_engine_refresh_screenshot_frame(selection.view) };
    }
}

pub fn drag(s: &mut Browser, drag: &Drag) {
    if drag.phase == Phase::Down {
        cover_page(s);
    }
    let Some(selection) = s.screenshot.as_mut() else {
        return;
    };
    match drag.phase {
        Phase::Down => selection.drag = Some(((drag.x, drag.y), (drag.x, drag.y))),
        Phase::Move => {
            if let Some((start, _)) = selection.drag {
                selection.drag = Some((start, (drag.x, drag.y)));
            }
        }
        Phase::Up => {
            if drag.cancelled() {
                selection.drag = None;
                return;
            }
            if let Some((start, _)) = selection.drag {
                selection.drag = Some((start, (drag.x, drag.y)));
            }
            finish(s, Answer::Selection);
        }
    }
}

pub fn wheel(s: &mut Browser, wheel: WheelEvent, tx: &mut Tx) {
    let now = s.page_prompts_now();
    let scale = s.page.scale();
    let Some(selection) = s.screenshot.as_mut() else {
        return;
    };
    if selection.drag.is_some() {
        return;
    }
    selection.reveals_page = true;
    selection.settle_due = Some(now + SCROLL_SETTLE_TIME);
    tx.wake_after(Duration::from_secs_f64(SCROLL_SETTLE_TIME));
    let event = MouseEvent {
        modifiers: Modifiers::from_rune(wheel.mods),
        wheel_delta: (-wheel.dx * scale, -wheel.dy * scale),
        ..MouseEvent::new(MouseEventKind::Wheel, wheel.x * scale, wheel.y * scale)
    };
    Engine.mouse_event(selection.view, &event);
}

pub fn poll(s: &mut Browser, now: f64, tx: &mut Tx) -> bool {
    let active = s.tabs.active_view();
    let Some(selection) = s.screenshot.as_ref() else {
        return false;
    };
    if active != Some(selection.view) {
        finish(s, Answer::Cancel);
        return true;
    }
    match selection.settle_due {
        Some(due) if now >= due => {
            cover_page(s);
            true
        }
        Some(due) => {
            tx.wake_after(Duration::from_secs_f64(due - now));
            false
        }
        None => false,
    }
}

pub fn key(s: &mut Browser, event: &rune::KeyEvent) -> bool {
    if s.screenshot.is_none() {
        return false;
    }
    match event.key {
        rune::Key::Escape => finish(s, Answer::Cancel),
        rune::Key::Enter => {
            cover_page(s);
            finish(s, Answer::VisiblePage);
        }
        _ => return false,
    }
    true
}

pub struct ScreenshotContext {
    slots: Arc<Slots>,
}

pub fn install(slots: Arc<Slots>) -> Box<ScreenshotContext> {
    let context = Box::new(ScreenshotContext { slots });
    let callbacks = XenonScreenshotCallbacks {
        context: std::ptr::from_ref::<ScreenshotContext>(&context)
            .cast_mut()
            .cast(),
        selection_requested: Some(selection_requested_callback),
        save_location_requested: Some(save_location_requested_callback),
    };
    unsafe { ffi_screenshots::xenon_engine_set_screenshot_callbacks(&callbacks) };
    context
}

unsafe extern "C" fn selection_requested_callback(
    context: *mut c_void,
    view: ViewId,
    surface_id: ViewId,
) {
    let slots = unsafe { &(*context.cast::<ScreenshotContext>()).slots };
    slots.update_view(view, move |browser, _| {
        selection_requested(browser, view, surface_id)
    });
}

pub fn ask_for_save_location(request: u64, directory: String, file_name: String, tx: &mut Tx) {
    let options = FileDialogOptions {
        title: "Select save location".to_owned(),
        directory: (!directory.is_empty()).then(|| PathBuf::from(directory)),
        file_name: Some(file_name),
        ..FileDialogOptions::default()
    };
    tx.save_file(
        options,
        move |_: &mut Browser, result: FileDialogResult, _: &mut Tx| {
            let path = match result {
                Ok(path) => path.map(|path| path.to_string_lossy().into_owned()),
                Err(error) => {
                    eprintln!("Xenon: Unable to ask where to save: {error}");
                    None
                }
            };
            unsafe {
                ffi_screenshots::xenon_engine_answer_save_location(
                    request,
                    path.is_some(),
                    XenonStr::borrowed(path.as_deref().unwrap_or_default()),
                )
            }
        },
    );
}

unsafe extern "C" fn save_location_requested_callback(
    context: *mut c_void,
    request: u64,
    directory: XenonStr,
    file_name: XenonStr,
) {
    let directory = unsafe { directory.to_string_lossy() };
    let file_name = unsafe { file_name.to_string_lossy() };
    let slots = unsafe { &(*context.cast::<ScreenshotContext>()).slots };
    slots.update_target(move |_, tx| ask_for_save_location(request, directory, file_name, tx));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn selections_are_normalized_and_kept_within_the_page() {
        assert_eq!(
            normalized([50.0, 40.0, 10.0, 20.0]),
            [10.0, 20.0, 40.0, 20.0]
        );
        assert_eq!(
            clipped([-10.0, 5.0, 50.0, 500.0], (100.0, 200.0)),
            [0.0, 5.0, 40.0, 195.0]
        );
    }
}
