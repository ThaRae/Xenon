/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;
use std::sync::Arc;

use rune::{TextEdit, Tx};

use crate::ffi::{XenonStr, XenonViewId};
use crate::state::Browser;
use crate::window_slots::Slots;

pub const TITLE: &str = "Block Element";
pub const MESSAGE: &str = "Hide this element on this site from now on. The filter uses uBlock Origin's syntax, and you can edit it, or remove it later in Settings > Content Blocking.";

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XenonElementPickerCallbacks {
    pub context: *mut c_void,
    pub filter_confirmation_requested: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            view: XenonViewId,
            request: u64,
            filter: XenonStr,
        ),
    >,
}

unsafe extern "C" {
    pub fn xenon_engine_set_element_picker_callbacks(callbacks: *const XenonElementPickerCallbacks);
    pub fn xenon_engine_confirm_element_filter(request: u64, accepted: bool, filter: XenonStr);
}

pub struct ElementFilter {
    pub request: u64,
    pub filter: TextEdit,
}

impl Browser {
    pub fn shown_element_filter(&self) -> Option<&ElementFilter> {
        self.element_filter
            .as_ref()
            .filter(|dialog| dialog.request != 0)
    }

    pub fn element_filter_input(&mut self) -> &mut TextEdit {
        &mut self
            .element_filter
            .get_or_insert_with(|| ElementFilter {
                request: 0,
                filter: TextEdit::new(""),
            })
            .filter
    }

    pub fn element_filter_requested(&mut self, request: u64, filter: String, tx: &mut Tx) {
        if let Some(previous) = self.element_filter.take() {
            if previous.filter.focused {
                tx.blur();
            }
            if previous.request == 0 {
                return self.element_filter_requested(request, filter, tx);
            }
            unsafe {
                xenon_engine_confirm_element_filter(previous.request, false, XenonStr::borrowed(""))
            }
        }
        let mut filter = TextEdit::new(filter);
        let end = filter.text.len();
        filter.set_selection(0, end);
        filter.focused = true;
        self.hover_card = None;
        self.element_filter = Some(ElementFilter { request, filter });
    }

    pub fn answer_element_filter(&mut self, accepted: bool, tx: &mut Tx) {
        let Some(dialog) = self
            .element_filter
            .take()
            .filter(|dialog| dialog.request != 0)
        else {
            return;
        };
        if dialog.filter.focused {
            tx.blur();
        }
        unsafe {
            xenon_engine_confirm_element_filter(
                dialog.request,
                accepted,
                XenonStr::borrowed(&dialog.filter.text),
            )
        }
    }

    pub fn cancel_element_filter(&mut self) {
        if let Some(dialog) = self
            .element_filter
            .take()
            .filter(|dialog| dialog.request != 0)
        {
            unsafe {
                xenon_engine_confirm_element_filter(dialog.request, false, XenonStr::borrowed(""))
            }
        }
    }
}

pub struct ElementPickerContext {
    slots: Arc<Slots>,
}

pub fn install(slots: Arc<Slots>) -> Box<ElementPickerContext> {
    let context = Box::new(ElementPickerContext { slots });
    let callbacks = XenonElementPickerCallbacks {
        context: std::ptr::from_ref::<ElementPickerContext>(&context)
            .cast_mut()
            .cast(),
        filter_confirmation_requested: Some(filter_confirmation_requested),
    };
    unsafe { xenon_engine_set_element_picker_callbacks(&callbacks) };
    context
}

unsafe extern "C" fn filter_confirmation_requested(
    context: *mut c_void,
    view: XenonViewId,
    request: u64,
    filter: XenonStr,
) {
    let filter = unsafe { filter.to_string_lossy() };
    let slots = unsafe { &(*context.cast::<ElementPickerContext>()).slots };
    slots.update_view(view, move |browser, tx| {
        browser.element_filter_requested(request, filter, tx)
    });
}
