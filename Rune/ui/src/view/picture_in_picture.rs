/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::{Align, Color, Cursor, El, SurfaceFit, el};

use crate::picture_in_picture::{self, Edges, PictureInPicture, RESIZE_MARGIN};
use crate::state::Browser;

pub fn picture_in_picture_windows(s: &Browser) -> Vec<El> {
    s.picture_in_picture.iter().map(window).collect()
}

fn resize_handle(id: crate::engine::ViewId, edges: Edges, (width, height): (f32, f32)) -> El {
    let horizontal = edges.left || edges.right;
    let vertical = edges.top || edges.bottom;
    let place = |near: bool, far: bool, length: f32| {
        if near {
            (0.0, RESIZE_MARGIN)
        } else if far {
            (length - RESIZE_MARGIN, RESIZE_MARGIN)
        } else {
            (RESIZE_MARGIN, length - 2.0 * RESIZE_MARGIN)
        }
    };
    let (x, handle_width) = place(edges.left, edges.right, width);
    let (y, handle_height) = place(edges.top, edges.bottom, height);
    let cursor = match (horizontal, vertical) {
        (true, false) => Cursor::ResizeColumn,
        (false, true) => Cursor::ResizeRow,
        _ => Cursor::Move,
    };
    el().key(format!("picture-in-picture-resize-{}", edges.name()))
        .pin(Some(x), Some(y), None, None)
        .size(handle_width, handle_height)
        .cursor(cursor)
        .a11y_hidden()
        .on_drag(move |s: &mut Browser, drag, _| {
            picture_in_picture::resize_drag(s, id, edges, drag)
        })
}

fn window(window: &PictureInPicture) -> El {
    let id = window.id;
    let page = el()
        .key(format!("picture-in-picture-page-{id}"))
        .role("video")
        .label("Picture-in-Picture")
        .w_full()
        .h_full()
        .pixels_fit(&window.surface, SurfaceFit::Fill)
        .surface_anchor(Align::Start, Align::Start)
        .on_drag(move |s: &mut Browser, drag, tx| picture_in_picture::drag(s, id, drag, tx))
        .on_pointer_move_local(move |s: &mut Browser, pointer, tx| {
            picture_in_picture::pointer_moved(s, id, pointer, tx);
            tx.live_only();
        })
        .on_hover_change(move |s: &mut Browser, hovered, tx| {
            picture_in_picture::hover_changed(s, id, hovered, tx)
        })
        .on_wheel(|_: &mut Browser, _, _| true);
    let mut frame = el()
        .key(format!("picture-in-picture-{id}"))
        .role("dialog")
        .label("Picture-in-Picture")
        .pin(None, None, Some(window.margins.0), Some(window.margins.1))
        .size(window.size.0, window.size.1)
        .radius(10.0)
        .clip_rounded(10.0)
        .fill(Color::BLACK)
        .shadow(12.0, 32.0, Color::BLACK.alpha(0.35))
        .shadow(1.0, 3.0, Color::BLACK.alpha(0.2))
        .child(page);
    for edges in Edges::ALL {
        frame = frame.child(resize_handle(id, edges, window.size));
    }
    frame
}
