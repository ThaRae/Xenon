/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#![cfg(target_os = "linux")]

use std::collections::HashMap;
use std::ffi::c_void;
use std::os::fd::{AsFd, OwnedFd};

use wayland_client::backend::{Backend, ObjectId};
use wayland_client::globals::{GlobalListContents, registry_queue_init};
use wayland_client::protocol::{
    wl_buffer::{self, WlBuffer},
    wl_compositor::WlCompositor,
    wl_region::WlRegion,
    wl_registry::{self, WlRegistry},
    wl_subcompositor::WlSubcompositor,
    wl_subsurface::WlSubsurface,
    wl_surface::{self, WlSurface},
};
use wayland_client::{Connection, Dispatch, EventQueue, Proxy, QueueHandle};
use wayland_protocols::wp::linux_dmabuf::zv1::client::{
    zwp_linux_buffer_params_v1::{self, Flags, ZwpLinuxBufferParamsV1},
    zwp_linux_dmabuf_v1::ZwpLinuxDmabufV1,
};
use wayland_protocols::wp::viewporter::client::{
    wp_viewport::WpViewport, wp_viewporter::WpViewporter,
};

use crate::engine::ViewId;

pub struct DmaBufPlane {
    pub fd: OwnedFd,
    pub offset: u32,
    pub pitch: u32,
}

pub struct DmaBufFrame {
    pub width: u32,
    pub height: u32,
    pub format: u32,
    pub modifier: u64,
    pub planes: Vec<DmaBufPlane>,
}

#[derive(Default)]
pub struct State {
    released: Vec<ObjectId>,
}

struct Page {
    surface: WlSurface,
    viewport: WpViewport,
    subsurface: WlSubsurface,
    sibling: WlSurface,
    sibling_subsurface: WlSubsurface,
    buffers: Vec<WlBuffer>,
    current: Option<WlBuffer>,
    has_buffer: bool,
}

pub struct WindowPage {
    connection: Connection,
    queue: EventQueue<State>,
    state: State,
    qh: QueueHandle<State>,
    compositor: WlCompositor,
    subcompositor: WlSubcompositor,
    viewporter: WpViewporter,
    dmabuf: ZwpLinuxDmabufV1,
    parent: WlSurface,
    pages: HashMap<ViewId, Page>,
    active: Option<ViewId>,
    rect: (i32, i32, i32, i32),
}

impl WindowPage {
    pub unsafe fn new(display: *mut c_void, surface: *mut c_void) -> Option<WindowPage> {
        let backend = unsafe { Backend::from_foreign_display(display.cast()) };
        let connection = Connection::from_backend(backend);
        let (globals, queue) = registry_queue_init::<State>(&connection).ok()?;
        let qh = queue.handle();
        let compositor = globals.bind::<WlCompositor, _, _>(&qh, 4..=6, ()).ok()?;
        let subcompositor = globals.bind::<WlSubcompositor, _, _>(&qh, 1..=1, ()).ok()?;
        let viewporter = globals.bind::<WpViewporter, _, _>(&qh, 1..=1, ()).ok()?;
        let dmabuf = globals
            .bind::<ZwpLinuxDmabufV1, _, _>(&qh, 3..=4, ())
            .ok()?;
        let parent = unsafe {
            let id = ObjectId::from_ptr(WlSurface::interface(), surface.cast()).ok()?;
            WlSurface::from_id(&connection, id).ok()?
        };
        Some(WindowPage {
            connection,
            queue,
            state: State::default(),
            qh,
            compositor,
            subcompositor,
            viewporter,
            dmabuf,
            parent,
            pages: HashMap::new(),
            active: None,
            rect: (0, 0, 0, 0),
        })
    }

    pub fn set_rect(&mut self, rect: (i32, i32, i32, i32)) {
        if self.rect == rect {
            return;
        }
        self.rect = rect;
        if rect.2 <= 0 || rect.3 <= 0 {
            return;
        }
        if let Some(view) = self.active
            && let Some(page) = self.pages.get(&view)
        {
            page.subsurface.set_position(rect.0, rect.1);
        }
    }

    pub fn show(&mut self, view: ViewId) {
        if self.active == Some(view) {
            return;
        }
        if let Some(previous) = self.active
            && let Some(page) = self.pages.get_mut(&previous)
        {
            page.surface.attach(None, 0, 0);
            page.surface.commit();
            page.has_buffer = false;
        }
        self.active = Some(view);
        let rect = self.rect;
        let page = self.page(view);
        page.subsurface.set_position(rect.0, rect.1);
        if let Some(buffer) = &page.current {
            page.surface.attach(Some(buffer), 0, 0);
            page.surface.damage_buffer(0, 0, i32::MAX, i32::MAX);
            page.viewport.set_destination(rect.2, rect.3);
            page.surface.commit();
            page.has_buffer = true;
        }
    }

    pub fn hide(&mut self) {
        if let Some(view) = self.active.take()
            && let Some(page) = self.pages.get_mut(&view)
        {
            page.surface.attach(None, 0, 0);
            page.surface.commit();
            page.has_buffer = false;
        }
    }

    pub fn present(&mut self, view: ViewId, frame: DmaBufFrame) {
        self.poll();
        if let Some(previous) = self.active
            && previous != view
            && let Some(page) = self.pages.get_mut(&previous)
        {
            page.surface.attach(None, 0, 0);
            page.surface.commit();
            page.has_buffer = false;
        }
        self.active = Some(view);
        let rect = self.rect;
        let (qh, dmabuf) = (self.qh.clone(), self.dmabuf.clone());
        let params = dmabuf.create_params(&qh, ());
        for (index, plane) in frame.planes.into_iter().enumerate() {
            params.add(
                plane.fd.as_fd(),
                index as u32,
                plane.offset,
                plane.pitch,
                (frame.modifier >> 32) as u32,
                (frame.modifier & 0xffff_ffff) as u32,
            );
        }
        let buffer = params.create_immed(
            frame.width as i32,
            frame.height as i32,
            frame.format,
            Flags::empty(),
            &qh,
            (),
        );
        params.destroy();
        let page = self.page(view);
        page.surface.attach(Some(&buffer), 0, 0);
        page.viewport
            .set_source(0.0, 0.0, f64::from(frame.width), f64::from(frame.height));
        page.viewport.set_destination(rect.2, rect.3);
        page.surface.damage_buffer(0, 0, i32::MAX, i32::MAX);
        page.surface.commit();
        page.subsurface.set_position(rect.0, rect.1);
        page.current = Some(buffer.clone());
        page.has_buffer = true;
        page.buffers.push(buffer);
    }

    pub fn poll(&mut self) {
        if self.queue.dispatch_pending(&mut self.state).is_err() {
            return;
        }
        if self.state.released.is_empty() {
            return;
        }
        let released = std::mem::take(&mut self.state.released);
        for page in self.pages.values_mut() {
            page.buffers
                .retain(|buffer| !released.contains(&buffer.id()));
            if let Some(current) = &page.current
                && released.contains(&current.id())
            {
                page.current = None;
                page.has_buffer = false;
            }
        }
    }

    fn page(&mut self, view: ViewId) -> &mut Page {
        if !self.pages.contains_key(&view) {
            let surface = self.compositor.create_surface(&self.qh, ());
            let viewport = self.viewporter.get_viewport(&surface, &self.qh, ());
            let subsurface =
                self.subcompositor
                    .get_subsurface(&surface, &self.parent, &self.qh, ());
            let sibling = self.compositor.create_surface(&self.qh, ());
            let sibling_subsurface =
                self.subcompositor
                    .get_subsurface(&sibling, &self.parent, &self.qh, ());
            subsurface.place_below(&sibling);
            subsurface.set_desync();
            let region = self.compositor.create_region(&self.qh, ());
            region.add(0, 0, 0, 0);
            surface.set_input_region(Some(&region));
            region.destroy();
            self.pages.insert(
                view,
                Page {
                    surface,
                    viewport,
                    subsurface,
                    sibling,
                    sibling_subsurface,
                    buffers: Vec::new(),
                    current: None,
                    has_buffer: false,
                },
            );
        }
        self.pages.get_mut(&view).unwrap()
    }
}

impl Drop for WindowPage {
    fn drop(&mut self) {
        for (_, page) in self.pages.drain() {
            for buffer in page.buffers {
                buffer.destroy();
            }
            page.viewport.destroy();
            page.subsurface.destroy();
            page.sibling_subsurface.destroy();
            page.sibling.destroy();
            page.surface.destroy();
        }
        let _ = &self.connection;
    }
}

impl Dispatch<WlRegistry, GlobalListContents> for State {
    fn event(
        _: &mut Self,
        _: &WlRegistry,
        _: wl_registry::Event,
        _: &GlobalListContents,
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
    }
}

impl Dispatch<WlBuffer, ()> for State {
    fn event(
        state: &mut Self,
        buffer: &WlBuffer,
        event: wl_buffer::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let wl_buffer::Event::Release = event {
            state.released.push(buffer.id());
            buffer.destroy();
        }
    }
}

impl Dispatch<WlCompositor, ()> for State {
    fn event(
        _: &mut Self,
        _: &WlCompositor,
        _: <WlCompositor as Proxy>::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
    }
}

impl Dispatch<WlSubcompositor, ()> for State {
    fn event(
        _: &mut Self,
        _: &WlSubcompositor,
        _: <WlSubcompositor as Proxy>::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
    }
}

impl Dispatch<WlSurface, ()> for State {
    fn event(
        _: &mut Self,
        _: &WlSurface,
        _: wl_surface::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
    }
}

impl Dispatch<WlSubsurface, ()> for State {
    fn event(
        _: &mut Self,
        _: &WlSubsurface,
        _: <WlSubsurface as Proxy>::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
    }
}

impl Dispatch<WlRegion, ()> for State {
    fn event(
        _: &mut Self,
        _: &WlRegion,
        _: <WlRegion as Proxy>::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
    }
}

impl Dispatch<WpViewporter, ()> for State {
    fn event(
        _: &mut Self,
        _: &WpViewporter,
        _: <WpViewporter as Proxy>::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
    }
}

impl Dispatch<WpViewport, ()> for State {
    fn event(
        _: &mut Self,
        _: &WpViewport,
        _: <WpViewport as Proxy>::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
    }
}

impl Dispatch<ZwpLinuxDmabufV1, ()> for State {
    fn event(
        _: &mut Self,
        _: &ZwpLinuxDmabufV1,
        _: <ZwpLinuxDmabufV1 as Proxy>::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
    }
}

impl Dispatch<ZwpLinuxBufferParamsV1, ()> for State {
    fn event(
        _: &mut Self,
        _: &ZwpLinuxBufferParamsV1,
        _: zwp_linux_buffer_params_v1::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
    }
}
