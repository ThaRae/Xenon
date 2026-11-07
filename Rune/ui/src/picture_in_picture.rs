/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Drag, Phase, Pixels, PointerEvent, Tx};

use crate::engine::{Modifiers, MouseButton, MouseEvent, MouseEventKind, ViewId};
use crate::media_bridge::MediaEngine;
use crate::state::Browser;

pub const EDGE_MARGIN: f32 = 16.0;
pub const RESIZE_MARGIN: f32 = 6.0;
const DRAG_DISTANCE: f32 = 4.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Shown {
    pub id: ViewId,
    pub owner: ViewId,
    pub size: (f32, f32),
    pub minimum: (f32, f32),
    pub maximum: (f32, f32),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum PressClaim {
    Unknown,
    Page,
    Window,
}

#[derive(Clone, Copy, Debug)]
struct Press {
    claim: PressClaim,
    moving: bool,
    margins_at_start: (f32, f32),
}

pub struct PictureInPicture {
    pub id: ViewId,
    pub size: (f32, f32),
    pub minimum: (f32, f32),
    pub maximum: (f32, f32),
    pub margins: (f32, f32),
    pub surface: Pixels,
    press: Option<Press>,
    resize_start: Option<((f32, f32), (f32, f32))>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Edges {
    pub left: bool,
    pub right: bool,
    pub top: bool,
    pub bottom: bool,
}

impl Edges {
    pub const ALL: [Edges; 8] = [
        Edges::new(true, false, false, false),
        Edges::new(false, true, false, false),
        Edges::new(false, false, true, false),
        Edges::new(false, false, false, true),
        Edges::new(true, false, true, false),
        Edges::new(false, true, true, false),
        Edges::new(true, false, false, true),
        Edges::new(false, true, false, true),
    ];

    pub const fn new(left: bool, right: bool, top: bool, bottom: bool) -> Edges {
        Edges {
            left,
            right,
            top,
            bottom,
        }
    }

    pub fn name(self) -> String {
        let mut name = String::new();
        if self.top {
            name.push_str("top");
        }
        if self.bottom {
            name.push_str("bottom");
        }
        if self.left {
            name.push_str("left");
        }
        if self.right {
            name.push_str("right");
        }
        name
    }
}

pub fn size_for_drag(
    start: (f32, f32),
    edges: Edges,
    (dx, dy): (f32, f32),
    minimum: (f32, f32),
    maximum: (f32, f32),
) -> (f32, f32) {
    let aspect = start.0 / start.1.max(1.0);
    let horizontal = if edges.left {
        -dx
    } else if edges.right {
        dx
    } else {
        0.0
    };
    let vertical = if edges.top {
        -dy
    } else if edges.bottom {
        dy
    } else {
        0.0
    };
    let width = if edges.left || edges.right {
        start.0 + horizontal
    } else {
        (start.1 + vertical) * aspect
    };
    let width = width.clamp(minimum.0, maximum.0.max(minimum.0));
    (width.round(), (width / aspect).round())
}

fn window_mut(s: &mut Browser, id: ViewId) -> Option<&mut PictureInPicture> {
    s.picture_in_picture
        .iter_mut()
        .find(|window| window.id == id)
}

pub fn shown(s: &mut Browser, shown: Shown) {
    let surface = s.surface_for(shown.id);
    if let Some(window) = window_mut(s, shown.id) {
        let grew_from = window.size;
        window.size = shown.size;
        window.minimum = shown.minimum;
        window.maximum = shown.maximum;
        if grew_from != shown.size {
            window.resize_start = None;
        }
        return;
    }
    s.picture_in_picture.push(PictureInPicture {
        id: shown.id,
        size: shown.size,
        minimum: shown.minimum,
        maximum: shown.maximum,
        margins: (EDGE_MARGIN, EDGE_MARGIN),
        surface,
        press: None,
        resize_start: None,
    });
}

pub fn hidden(s: &mut Browser, id: ViewId) {
    s.picture_in_picture.retain(|window| window.id != id);
}

pub fn press_handled(s: &mut Browser, id: ViewId, claimed_by_page: bool) {
    if let Some(press) = window_mut(s, id).and_then(|window| window.press.as_mut())
        && press.claim == PressClaim::Unknown
    {
        press.claim = if claimed_by_page {
            PressClaim::Page
        } else {
            PressClaim::Window
        };
    }
}

fn send(
    s: &Browser,
    id: ViewId,
    kind: MouseEventKind,
    x: f32,
    y: f32,
    button: Option<MouseButton>,
    tx: &Tx,
) {
    let scale = s.page.scale();
    let event = MouseEvent {
        button,
        buttons: if kind == MouseEventKind::Up || button.is_none() {
            0
        } else {
            MouseButton::Primary.bit()
        },
        modifiers: Modifiers::from_rune(tx.mods()),
        ..MouseEvent::new(kind, x * scale, y * scale)
    };
    MediaEngine.picture_in_picture_mouse_event(id, &event);
}

fn clamp_margins(s: &Browser, size: (f32, f32), margins: (f32, f32)) -> (f32, f32) {
    let (area_width, area_height) = s.page_size();
    (
        margins.0.clamp(0.0, (area_width - size.0).max(0.0)),
        margins.1.clamp(0.0, (area_height - size.1).max(0.0)),
    )
}

pub fn drag(s: &mut Browser, id: ViewId, drag: &Drag, tx: &mut Tx) {
    let Some(window) = window_mut(s, id) else {
        return;
    };
    match drag.phase {
        Phase::Down => {
            window.press = Some(Press {
                claim: PressClaim::Unknown,
                moving: false,
                margins_at_start: window.margins,
            });
            let kind = if drag.clicks == 2 {
                MouseEventKind::DoubleClick
            } else {
                MouseEventKind::Down
            };
            send(s, id, kind, drag.x, drag.y, Some(MouseButton::Primary), tx);
        }
        Phase::Move => {
            let Some(mut press) = window.press else {
                return;
            };
            if press.moving {
                let margins = (
                    press.margins_at_start.0 - drag.dx,
                    press.margins_at_start.1 - drag.dy,
                );
                let size = window.size;
                let margins = clamp_margins(s, size, margins);
                if let Some(window) = window_mut(s, id) {
                    window.margins = margins;
                }
                return;
            }
            if press.claim == PressClaim::Window && drag.dx.hypot(drag.dy) >= DRAG_DISTANCE {
                press.moving = true;
                window.press = Some(press);
                MediaEngine.picture_in_picture_cancel_press(id);
                return;
            }
            send(s, id, MouseEventKind::Move, drag.x, drag.y, None, tx);
        }
        Phase::Up => {
            let moved = window.press.take().is_some_and(|press| press.moving);
            if !moved {
                send(
                    s,
                    id,
                    MouseEventKind::Up,
                    drag.x,
                    drag.y,
                    Some(MouseButton::Primary),
                    tx,
                );
            }
        }
    }
}

pub fn pointer_moved(s: &mut Browser, id: ViewId, pointer: PointerEvent, tx: &Tx) {
    if window_mut(s, id).is_some_and(|window| window.press.is_none()) {
        send(s, id, MouseEventKind::Move, pointer.x, pointer.y, None, tx);
    }
}

pub fn hover_changed(s: &mut Browser, id: ViewId, hovered: bool, tx: &Tx) {
    if !hovered && window_mut(s, id).is_some_and(|window| window.press.is_none()) {
        send(s, id, MouseEventKind::Leave, 0.0, 0.0, None, tx);
    }
}

pub fn resize_drag(s: &mut Browser, id: ViewId, edges: Edges, drag: &Drag) {
    let scale = s.page.scale();
    let Some(window) = window_mut(s, id) else {
        return;
    };
    if drag.phase == Phase::Down {
        window.resize_start = Some((window.size, window.margins));
    }
    let Some((start_size, start_margins)) = window.resize_start else {
        return;
    };
    let size = size_for_drag(
        start_size,
        edges,
        (drag.dx, drag.dy),
        window.minimum,
        window.maximum,
    );
    let mut margins = start_margins;
    if edges.right {
        margins.0 = start_margins.0 - (size.0 - start_size.0);
    }
    if edges.bottom {
        margins.1 = start_margins.1 - (size.1 - start_size.1);
    }
    window.size = size;
    let margins = clamp_margins(s, size, margins);
    if let Some(window) = window_mut(s, id) {
        window.margins = margins;
        if drag.phase == Phase::Up {
            window.resize_start = None;
        }
    }
    MediaEngine.picture_in_picture_resize(id, size.0, size.1, scale);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resizing_keeps_the_videos_shape_and_limits() {
        let minimum = (160.0, 90.0);
        let maximum = (640.0, 360.0);
        let right = Edges::new(false, true, false, false);
        assert_eq!(
            size_for_drag((320.0, 180.0), right, (32.0, 0.0), minimum, maximum),
            (352.0, 198.0)
        );
        let top = Edges::new(false, false, true, false);
        assert_eq!(
            size_for_drag((320.0, 180.0), top, (0.0, -36.0), minimum, maximum),
            (384.0, 216.0)
        );
        let top_left = Edges::new(true, false, true, false);
        assert_eq!(
            size_for_drag((320.0, 180.0), top_left, (-1000.0, 0.0), minimum, maximum),
            (640.0, 360.0)
        );
        assert_eq!(
            size_for_drag((320.0, 180.0), right, (-1000.0, 0.0), minimum, maximum),
            (160.0, 90.0)
        );
    }
}
