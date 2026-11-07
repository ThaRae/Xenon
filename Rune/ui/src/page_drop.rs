/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cell::RefCell;
use std::path::{Path, PathBuf};

use rune::{PointerEvent, Tx};

use crate::engine::{Modifiers, ViewId};
use crate::ffi::XenonStr;
use crate::ffi_drop;
use crate::state::Browser;

thread_local! {
    static DRAGGED_FILES: RefCell<Vec<PathBuf>> = const { RefCell::new(Vec::new()) };
    static LEAVE_PENDING: RefCell<Option<ViewId>> = const { RefCell::new(None) };
}

pub fn accepts(path: &Path) -> bool {
    DRAGGED_FILES.with_borrow_mut(|files| {
        if !files.iter().any(|known| known == path) {
            files.push(path.to_path_buf());
        }
    });
    true
}

fn send(
    view: ViewId,
    kind: u32,
    point: Option<PointerEvent>,
    s: &Browser,
    tx: &Tx,
    paths: &[PathBuf],
) {
    let (x, y) = point
        .map(|point| s.page.to_device(point.x, point.y))
        .unwrap_or_default();
    let strings: Vec<String> = paths
        .iter()
        .map(|path| path.to_string_lossy().into_owned())
        .collect();
    let raw: Vec<XenonStr> = strings
        .iter()
        .map(|path| XenonStr::borrowed(path))
        .collect();
    unsafe {
        ffi_drop::xenon_engine_drag_event(
            view,
            kind,
            x,
            y,
            Modifiers::from_rune(tx.mods()).0,
            raw.as_ptr(),
            raw.len(),
        )
    }
}

pub fn hover_changed(s: &mut Browser, view: ViewId, over: bool, tx: &mut Tx) {
    if !over {
        LEAVE_PENDING.with_borrow_mut(|pending| *pending = Some(view));
        tx.wake_after(std::time::Duration::ZERO);
        return;
    }
    if let Some(left) = LEAVE_PENDING.with_borrow_mut(Option::take)
        && left != view
    {
        send(left, ffi_drop::XENON_DRAG_LEAVE, None, s, tx, &[]);
    }
    let files = DRAGGED_FILES.with_borrow(Clone::clone);
    send(
        view,
        ffi_drop::XENON_DRAG_ENTER,
        tx.drop_point(),
        s,
        tx,
        &files,
    );
}

pub fn moved(s: &mut Browser, view: ViewId, point: PointerEvent, tx: &mut Tx) {
    send(view, ffi_drop::XENON_DRAG_MOVE, Some(point), s, tx, &[]);
    tx.live_only();
}

pub fn dropped(s: &mut Browser, view: ViewId, files: &[PathBuf], tx: &mut Tx) {
    if let Some(left) = LEAVE_PENDING.with_borrow_mut(Option::take)
        && left != view
    {
        send(left, ffi_drop::XENON_DRAG_LEAVE, None, s, tx, &[]);
    }
    DRAGGED_FILES.with_borrow_mut(Vec::clear);
    if !s.tabs.is_active(view) {
        s.activate_tab(view, tx);
    }
    s.focus_page();
    send(
        view,
        ffi_drop::XENON_DRAG_DROP,
        tx.drop_point(),
        s,
        tx,
        files,
    );
}

pub fn poll(s: &mut Browser, tx: &mut Tx) -> bool {
    if let Some(view) = LEAVE_PENDING.with_borrow_mut(Option::take) {
        DRAGGED_FILES.with_borrow_mut(Vec::clear);
        send(view, ffi_drop::XENON_DRAG_LEAVE, None, s, tx, &[]);
    }
    false
}
