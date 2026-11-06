/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#[path = "mock_model.rs"]
mod model;

use std::collections::HashMap;
use std::ffi::{c_char, c_int};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::{Mutex, MutexGuard};
use std::thread::JoinHandle;

#[path = "mock_page.rs"]
mod page;

use crate::ffi::{
    XENON_CURSOR_DEFAULT, XENON_CURSOR_POINTER, XENON_MOUSE_BUTTON_MIDDLE,
    XENON_MOUSE_BUTTON_PRIMARY, XENON_MOUSE_EVENT_LEAVE, XENON_MOUSE_EVENT_MOVE,
    XENON_MOUSE_EVENT_UP, XENON_WINDOW_NONE, XenonFrame, XenonKeyEvent, XenonMouseEvent, XenonStr,
    XenonTabInfo, XenonUiCallbacks, XenonViewId, XenonWindowId,
};
use model::{MockModel, ModelEvent};

const NEW_TAB_URL: &str = "about:newtab";
const STILL_LOADING_MARKER: &str = "slow";
const ROW_PADDING_BYTES: usize = 32;

enum Command {
    Ready,
    WindowViewport {
        window: XenonWindowId,
        width: u32,
        height: u32,
        scale: f64,
    },
    NewWindow(XenonWindowId, bool),
    ActivateWindow(XenonWindowId),
    CloseWindow(XenonWindowId),
    NewTab {
        window: XenonWindowId,
        view: XenonViewId,
        input: String,
        index: Option<usize>,
        activate: bool,
    },
    ActivateTab(XenonViewId),
    CloseTab(XenonViewId),
    MoveTab(XenonViewId, XenonWindowId, Option<usize>),
    SetPinned(XenonViewId, bool),
    ReopenClosedTab(XenonWindowId),
    CycleRecentTabs(XenonWindowId, bool),
    EndRecentTabCycle(XenonWindowId),
    Load(XenonViewId, String),
    Back(XenonViewId),
    Forward(XenonViewId),
    Reload(XenonViewId),
    Stop(XenonViewId),
    Resize {
        view: XenonViewId,
        width: u32,
        height: u32,
        scale: f64,
    },
    Mouse(XenonViewId, XenonMouseEvent),
    Shutdown,
}

struct SendableCallbacks(XenonUiCallbacks);

unsafe impl Send for SendableCallbacks {}

struct Running {
    commands: Sender<(Command, Sender<()>)>,
    thread: JoinHandle<()>,
}

static RUNNING: Mutex<Option<Running>> = Mutex::new(None);
static NEXT_VIEW: AtomicU64 = AtomicU64::new(1);
static NEXT_WINDOW: AtomicU64 = AtomicU64::new(1);

fn running() -> MutexGuard<'static, Option<Running>> {
    RUNNING
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn send(command: Command) {
    let (done, finished) = mpsc::channel();
    let sent = running()
        .as_ref()
        .is_some_and(|engine| engine.commands.send((command, done)).is_ok());
    if sent {
        let _ = finished.recv();
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_start(
    callbacks: *const XenonUiCallbacks,
    _argc: c_int,
    _argv: *const *const c_char,
) -> bool {
    let Some(callbacks) = (unsafe { callbacks.as_ref() }) else {
        return false;
    };
    let callbacks = SendableCallbacks(*callbacks);
    let (commands, received) = mpsc::channel();
    let thread = std::thread::Builder::new()
        .name("mock-engine".into())
        .spawn(move || MockEngine::new(callbacks).run(received))
        .expect("the mock engine thread starts");
    *running() = Some(Running { commands, thread });
    send(Command::Ready);
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_shutdown() {
    send(Command::Shutdown);
    if let Some(engine) = running().take() {
        drop(engine.commands);
        let _ = engine.thread.join();
    }
}

fn tab_index(index: i64) -> Option<usize> {
    usize::try_from(index).ok()
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_window_viewport(
    window: XenonWindowId,
    width: u32,
    height: u32,
    scale: f64,
) {
    send(Command::WindowViewport {
        window,
        width,
        height,
        scale,
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_system_dark(_dark: bool) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_chrome_layout(_layout: u8) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_cycle_tabs_in_recently_used_order(_enabled: bool) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_activate_action(_action: u32) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_request_app_menu(_which: u32) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_activate_app_menu_item(_menu: u64, _item: u32) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_new_window(is_private: bool) -> XenonWindowId {
    let window = NEXT_WINDOW.fetch_add(1, Ordering::Relaxed);
    send(Command::NewWindow(window, is_private));
    window
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_activate_window(window: XenonWindowId) {
    send(Command::ActivateWindow(window));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_close_window(window: XenonWindowId) {
    send(Command::CloseWindow(window));
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_new_tab(
    window: XenonWindowId,
    url: XenonStr,
    index: i64,
    activate: bool,
) -> XenonViewId {
    let view = NEXT_VIEW.fetch_add(1, Ordering::Relaxed);
    send(Command::NewTab {
        window,
        view,
        input: unsafe { url.to_string_lossy() },
        index: tab_index(index),
        activate,
    });
    view
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_activate_tab(view: XenonViewId) {
    send(Command::ActivateTab(view));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_close_tab(view: XenonViewId) {
    send(Command::CloseTab(view));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_move_tab(view: XenonViewId, window: XenonWindowId, index: i64) {
    send(Command::MoveTab(view, window, tab_index(index)));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_tab_pinned(view: XenonViewId, pinned: bool) {
    send(Command::SetPinned(view, pinned));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_reopen_closed_tab(window: XenonWindowId) {
    send(Command::ReopenClosedTab(window));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_duplicate_tab(_view: XenonViewId) -> XenonViewId {
    let view = NEXT_VIEW.fetch_add(1, Ordering::Relaxed);
    send(Command::NewTab {
        window: crate::ffi::XENON_WINDOW_NONE,
        view,
        input: String::new(),
        index: None,
        activate: true,
    });
    view
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_request_tab_memory(_view: XenonViewId) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_cycle_recent_tabs(window: XenonWindowId, toward_less_recent: bool) {
    send(Command::CycleRecentTabs(window, toward_less_recent));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_end_recent_tab_cycle(window: XenonWindowId) {
    send(Command::EndRecentTabCycle(window));
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_load(view: XenonViewId, input: XenonStr) {
    send(Command::Load(view, unsafe { input.to_string_lossy() }));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_go_back(view: XenonViewId) {
    send(Command::Back(view));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_go_forward(view: XenonViewId) {
    send(Command::Forward(view));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_reload(view: XenonViewId) {
    send(Command::Reload(view));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_stop(view: XenonViewId) {
    send(Command::Stop(view));
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_viewport(
    view: XenonViewId,
    width: u32,
    height: u32,
    scale: f64,
) {
    send(Command::Resize {
        view,
        width,
        height,
        scale,
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_visible(_view: XenonViewId, _visible: bool) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_engine_set_focused(_view: XenonViewId, _focused: bool) {}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_mouse_event(
    view: XenonViewId,
    event: *const XenonMouseEvent,
) {
    if let Some(event) = unsafe { event.as_ref() } {
        send(Command::Mouse(view, *event));
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_key_event(_view: XenonViewId, _event: *const XenonKeyEvent) {}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xenon_engine_insert_text(_view: XenonViewId, _text: XenonStr) {}

#[derive(Clone, Copy, Default)]
struct DeviceRect {
    x: f32,
    y: f32,
    width: f32,
    height: f32,
}

impl DeviceRect {
    fn contains(&self, x: f32, y: f32) -> bool {
        x >= self.x && y >= self.y && x < self.x + self.width && y < self.y + self.height
    }
}

struct MockView {
    history: Vec<String>,
    position: usize,
    width: u32,
    height: u32,
    scale: f64,
    link: DeviceRect,
    hovering_link: bool,
}

impl MockView {
    fn url(&self) -> &str {
        self.history
            .get(self.position)
            .map_or(NEW_TAB_URL, String::as_str)
    }

    fn link_url(&self) -> String {
        format!("{}/next", self.url().trim_end_matches('/'))
    }
}

struct MockEngine {
    callbacks: XenonUiCallbacks,
    views: HashMap<XenonViewId, MockView>,
    model: MockModel,
    viewports: HashMap<XenonWindowId, (u32, u32, f64)>,
    default_viewport: (u32, u32, f64),
}

impl MockEngine {
    fn new(callbacks: SendableCallbacks) -> MockEngine {
        MockEngine {
            callbacks: callbacks.0,
            views: HashMap::new(),
            model: MockModel::default(),
            viewports: HashMap::new(),
            default_viewport: (800, 600, 1.0),
        }
    }

    fn run(mut self, commands: Receiver<(Command, Sender<()>)>) {
        for (command, done) in commands {
            let shutting_down = matches!(command, Command::Shutdown);
            self.handle(command);
            let _ = done.send(());
            if shutting_down {
                break;
            }
        }
    }

    fn handle(&mut self, command: Command) {
        match command {
            Command::Ready => {
                if let Some(ready) = self.callbacks.engine_ready {
                    unsafe { ready(self.callbacks.context) };
                }
                let window = NEXT_WINDOW.fetch_add(1, Ordering::Relaxed);
                self.open_window(window, false);
            }
            Command::WindowViewport {
                window,
                width,
                height,
                scale,
            } => {
                self.default_viewport = (width, height, scale);
                if window != XENON_WINDOW_NONE {
                    self.viewports.insert(window, (width, height, scale));
                }
            }
            Command::NewWindow(window, private) => self.open_window(window, private),
            Command::ActivateWindow(window) => {
                self.model.activate_window(window);
                self.dispatch();
            }
            Command::CloseWindow(window) => {
                for view in self.model.close_window(window) {
                    self.views.remove(&view);
                }
                self.dispatch();
            }
            Command::NewTab {
                window,
                view,
                input,
                index,
                activate,
            } => {
                let window = Some(window)
                    .filter(|w| self.model.has_window(*w))
                    .or(self.model.active_window());
                let window = window.unwrap_or_else(|| {
                    let window = NEXT_WINDOW.fetch_add(1, Ordering::Relaxed);
                    self.model.open_window(window, false);
                    window
                });
                let url = if input.trim().is_empty() {
                    NEW_TAB_URL.to_owned()
                } else {
                    normalized_url(&input)
                };
                self.model
                    .open_tab(window, view, &url, index, None, activate);
                self.dispatch();
            }
            Command::ActivateTab(view) => {
                self.model.activate_tab(view);
                self.dispatch();
            }
            Command::CloseTab(view) => {
                let url = self
                    .views
                    .get(&view)
                    .map(|state| state.url().to_owned())
                    .unwrap_or_default();
                self.model.close_tab(view, &url);
                self.dispatch();
            }
            Command::MoveTab(view, window, index) => {
                let index = index.unwrap_or(usize::MAX);
                self.model.move_tab(view, window, index);
                self.dispatch();
            }
            Command::SetPinned(view, pinned) => {
                self.model.set_pinned(view, pinned);
                self.dispatch();
            }
            Command::ReopenClosedTab(window) => {
                self.model.reopen_closed_tab(
                    Some(window),
                    || NEXT_VIEW.fetch_add(1, Ordering::Relaxed),
                    || NEXT_WINDOW.fetch_add(1, Ordering::Relaxed),
                );
                self.dispatch();
            }
            Command::CycleRecentTabs(window, toward_less_recent) => {
                self.model.cycle_recent(window, toward_less_recent);
                self.dispatch();
            }
            Command::EndRecentTabCycle(window) => self.model.end_recent_cycle(window),
            Command::Load(view, input) => self.load(view, &normalized_url(&input)),
            Command::Back(view) => self.traverse(view, -1),
            Command::Forward(view) => self.traverse(view, 1),
            Command::Reload(view) => self.traverse(view, 0),
            Command::Stop(view) => self.loading(view, false),
            Command::Resize {
                view,
                width,
                height,
                scale,
            } => {
                if let Some(window) = self.model.window_of(view) {
                    self.viewports.insert(window, (width, height, scale));
                }
                if let Some(state) = self.views.get_mut(&view) {
                    (state.width, state.height, state.scale) = (width, height, scale);
                    self.paint(view);
                }
            }
            Command::Mouse(view, event) => self.mouse(view, event),
            Command::Shutdown => self.views.clear(),
        }
    }

    fn open_window(&mut self, window: XenonWindowId, private: bool) {
        self.model.open_window(window, private);
        let view = NEXT_VIEW.fetch_add(1, Ordering::Relaxed);
        self.model
            .open_tab(window, view, NEW_TAB_URL, None, None, true);
        self.dispatch();
    }

    fn dispatch(&mut self) {
        for event in self.model.take_events() {
            self.notify(event);
        }
    }

    fn notify(&mut self, event: ModelEvent) {
        let context = self.callbacks.context;
        let callbacks = self.callbacks;
        match event {
            ModelEvent::WindowOpened(window, private) => {
                if let Some(opened) = callbacks.window_opened {
                    unsafe { opened(context, window, private) };
                }
            }
            ModelEvent::WindowClosed(window) => {
                if let Some(closed) = callbacks.window_closed {
                    unsafe { closed(context, window) };
                }
            }
            ModelEvent::WindowActivated(window) => {
                if let Some(activated) = callbacks.window_activated {
                    unsafe { activated(context, window) };
                }
            }
            ModelEvent::TabInserted {
                window,
                view,
                index,
                opener,
                pinned,
                url,
            } => {
                let info = XenonTabInfo {
                    window,
                    view,
                    opener: opener.unwrap_or(0),
                    index: index as u32,
                    pinned,
                    is_private: false,
                    loaded: true,
                    is_new_tab_page: url == NEW_TAB_URL,
                    url: XenonStr::borrowed(&url),
                    title: XenonStr::borrowed(""),
                };
                if let Some(inserted) = callbacks.tab_inserted {
                    unsafe { inserted(context, &info) };
                }
                let (width, height, scale) = self
                    .viewports
                    .get(&window)
                    .copied()
                    .unwrap_or(self.default_viewport);
                self.create(view, &url, width, height, scale);
            }
            ModelEvent::TabRemoved(window, view) => {
                self.views.remove(&view);
                if let Some(removed) = callbacks.tab_removed {
                    unsafe { removed(context, window, view) };
                }
            }
            ModelEvent::TabMoved {
                view,
                from,
                to,
                index,
            } => {
                if let Some(moved) = callbacks.tab_moved {
                    unsafe { moved(context, view, from, to, index as u32) };
                }
            }
            ModelEvent::TabActivated(window, view) => {
                if let Some(activated) = callbacks.tab_activated {
                    unsafe { activated(context, window, view) };
                }
            }
            ModelEvent::PinnedChanged(view, pinned) => {
                if let Some(changed) = callbacks.tab_pinned_changed {
                    unsafe { changed(context, view, pinned) };
                }
            }
            ModelEvent::ClosedTabsChanged(can_reopen) => {
                if let Some(changed) = callbacks.closed_tabs_changed {
                    unsafe { changed(context, can_reopen) };
                }
            }
        }
    }

    fn create(&mut self, view: XenonViewId, url: &str, width: u32, height: u32, scale: f64) {
        let state = MockView {
            history: Vec::new(),
            position: 0,
            width,
            height,
            scale,
            link: DeviceRect::default(),
            hovering_link: false,
        };
        self.views.insert(view, state);
        self.load(view, &normalized_url(url));
    }

    fn load(&mut self, view: XenonViewId, url: &str) {
        let Some(state) = self.views.get_mut(&view) else {
            return;
        };
        state.history.truncate(state.position + 1);
        state.history.push(url.to_owned());
        state.position = state.history.len() - 1;
        self.navigated(view);
    }

    fn traverse(&mut self, view: XenonViewId, offset: isize) {
        let Some(state) = self.views.get_mut(&view) else {
            return;
        };
        let Some(position) = state
            .position
            .checked_add_signed(offset)
            .filter(|p| *p < state.history.len())
        else {
            return;
        };
        state.position = position;
        self.navigated(view);
    }

    fn navigated(&mut self, view: XenonViewId) {
        let Some(state) = self.views.get(&view) else {
            return;
        };
        let url = state.url().to_owned();
        let (can_go_back, can_go_forward) =
            (state.position > 0, state.position + 1 < state.history.len());
        self.loading(view, true);
        self.string(self.callbacks.view_url_changed, view, &url);
        if let Some(history) = self.callbacks.view_history_changed {
            unsafe { history(self.callbacks.context, view, can_go_back, can_go_forward) };
        }
        self.string(self.callbacks.view_title_changed, view, &title_for(&url));
        self.favicon(view, &url);
        self.paint(view);
        page::navigated(view, &url);
        if !url.contains(STILL_LOADING_MARKER) {
            self.loading(view, false);
        }
    }

    fn loading(&self, view: XenonViewId, loading: bool) {
        if let Some(changed) = self.callbacks.view_loading_changed {
            unsafe { changed(self.callbacks.context, view, loading) };
        }
    }

    fn string(
        &self,
        callback: Option<unsafe extern "C" fn(*mut std::ffi::c_void, XenonViewId, XenonStr)>,
        view: XenonViewId,
        text: &str,
    ) {
        if let Some(callback) = callback {
            unsafe { callback(self.callbacks.context, view, XenonStr::borrowed(text)) };
        }
    }

    fn cursor(&self, view: XenonViewId, cursor: u32) {
        if let Some(changed) = self.callbacks.view_cursor_changed {
            unsafe { changed(self.callbacks.context, view, cursor) };
        }
    }

    fn favicon(&self, view: XenonViewId, url: &str) {
        let Some(changed) = self.callbacks.view_favicon_changed else {
            return;
        };
        if !url.starts_with("http") {
            return;
        }
        let icon = favicon_rgba(hue_for(url));
        unsafe {
            changed(
                self.callbacks.context,
                view,
                FAVICON_SIZE,
                FAVICON_SIZE,
                icon.as_ptr(),
            )
        };
    }

    fn paint(&mut self, view: XenonViewId) {
        let Some(state) = self.views.get_mut(&view) else {
            return;
        };
        let page = MockPage::render(state.url(), state.width, state.height, state.scale);
        state.link = page.link;
        let frame = XenonFrame {
            width: state.width,
            height: state.height,
            stride: page.stride,
            bgra: page.bgra.as_ptr(),
            device_pixel_ratio: state.scale,
        };
        if let Some(present) = self.callbacks.view_frame {
            unsafe { present(self.callbacks.context, view, &frame) };
        }
    }

    fn mouse(&mut self, view: XenonViewId, event: XenonMouseEvent) {
        let Some(state) = self.views.get_mut(&view) else {
            return;
        };
        let over_link =
            event.r#type != XENON_MOUSE_EVENT_LEAVE && state.link.contains(event.x, event.y);
        let link = state.link_url();
        page::mouse(view, state.url(), &event);
        if (event.r#type == XENON_MOUSE_EVENT_MOVE || event.r#type == XENON_MOUSE_EVENT_LEAVE)
            && over_link != state.hovering_link
        {
            state.hovering_link = over_link;
            self.string(
                self.callbacks.view_link_hovered,
                view,
                if over_link { &link } else { "" },
            );
            self.cursor(
                view,
                if over_link {
                    XENON_CURSOR_POINTER
                } else {
                    XENON_CURSOR_DEFAULT
                },
            );
        }
        if event.r#type != XENON_MOUSE_EVENT_UP || !over_link {
            return;
        }
        match event.button {
            XENON_MOUSE_BUTTON_PRIMARY => self.load(view, &link),
            XENON_MOUSE_BUTTON_MIDDLE => self.open_in_background(view, &link),
            _ => {}
        }
    }

    fn open_in_background(&mut self, opener: XenonViewId, url: &str) {
        let Some(window) = self.model.window_of(opener) else {
            return;
        };
        let view = NEXT_VIEW.fetch_add(1, Ordering::Relaxed);
        self.model
            .open_tab(window, view, url, None, Some(opener), false);
        self.dispatch();
    }
}

fn normalized_url(input: &str) -> String {
    let input = input.trim();
    if input.contains("://") || input.starts_with("about:") {
        input.to_owned()
    } else if input.contains('.') && !input.contains(' ') {
        format!("https://{input}")
    } else {
        format!("https://search.example/?q={}", input.replace(' ', "+"))
    }
}

fn host_of(url: &str) -> &str {
    let rest = url.split_once("://").map_or(url, |(_, rest)| rest);
    rest.split(['/', '?', '#']).next().unwrap_or(rest)
}

fn title_for(url: &str) -> String {
    if url == NEW_TAB_URL {
        return "New Tab".to_owned();
    }
    let host = host_of(url).trim_start_matches("www.");
    let path = url
        .split_once("://")
        .map_or("", |(_, rest)| rest.get(host_of(url).len()..).unwrap_or(""));
    match path
        .trim_matches('/')
        .rsplit('/')
        .next()
        .filter(|segment| !segment.is_empty())
    {
        Some(segment) => format!("{segment} · {host}"),
        None => host.to_owned(),
    }
}

fn hue_for(url: &str) -> f32 {
    let hash = url.bytes().fold(0x811c9dc5u32, |hash, byte| {
        (hash ^ u32::from(byte)).wrapping_mul(0x01000193)
    });
    (hash % 360) as f32
}

fn hsv(hue: f32, saturation: f32, value: f32) -> [u8; 3] {
    let sector = (hue / 60.0).rem_euclid(6.0);
    let chroma = value * saturation;
    let x = chroma * (1.0 - (sector % 2.0 - 1.0).abs());
    let (r, g, b) = match sector as u32 {
        0 => (chroma, x, 0.0),
        1 => (x, chroma, 0.0),
        2 => (0.0, chroma, x),
        3 => (0.0, x, chroma),
        4 => (x, 0.0, chroma),
        _ => (chroma, 0.0, x),
    };
    let m = value - chroma;
    [r, g, b].map(|c| ((c + m) * 255.0).round() as u8)
}

const FAVICON_SIZE: u32 = 16;

fn favicon_rgba(hue: f32) -> Vec<u8> {
    let size = FAVICON_SIZE as usize;
    let [r, g, b] = hsv(hue, 0.7, 0.85);
    let center = (size as f32 - 1.0) / 2.0;
    let mut rgba = Vec::with_capacity(size * size * 4);
    for y in 0..size {
        for x in 0..size {
            let distance = ((x as f32 - center).powi(2) + (y as f32 - center).powi(2)).sqrt();
            let alpha = (size as f32 / 2.0 - distance).clamp(0.0, 1.0);
            rgba.extend_from_slice(&[r, g, b, (alpha * 255.0) as u8]);
        }
    }
    rgba
}

struct MockPage {
    bgra: Vec<u8>,
    stride: usize,
    width: usize,
    height: usize,
    link: DeviceRect,
}

impl MockPage {
    fn render(url: &str, width: u32, height: u32, scale: f64) -> MockPage {
        let (width, height) = (width as usize, height as usize);
        let scale = scale as f32;
        let stride = width * 4 + ROW_PADDING_BYTES;
        let mut page = MockPage {
            bgra: vec![0; stride * height],
            stride,
            width,
            height,
            link: DeviceRect::default(),
        };
        let new_tab = url == NEW_TAB_URL;
        let hue = hue_for(url);
        page.fill(
            0.0,
            0.0,
            width as f32,
            height as f32,
            if new_tab {
                [246, 246, 248]
            } else {
                [255, 255, 255]
            },
        );
        let margin = 64.0 * scale;
        if new_tab {
            let field_width = (560.0 * scale).min(width as f32 - 2.0 * margin);
            let x = (width as f32 - field_width) / 2.0;
            let y = height as f32 * 0.38;
            page.fill(
                x + field_width / 2.0 - 28.0 * scale,
                y - 96.0 * scale,
                56.0 * scale,
                56.0 * scale,
                [255, 0, 80],
            );
            page.fill(x, y, field_width, 48.0 * scale, [232, 232, 236]);
            return page;
        }
        let hero = (height as f32 * 0.34).min(260.0 * scale);
        for row in 0..hero as usize {
            let t = row as f32 / hero.max(1.0);
            page.fill(
                0.0,
                row as f32,
                width as f32,
                1.0,
                hsv(hue + 24.0 * t, 0.55, 0.96 - 0.12 * t),
            );
        }
        page.fill(
            margin,
            hero * 0.42,
            (420.0 * scale).min(width as f32 - 2.0 * margin),
            30.0 * scale,
            [255, 255, 255],
        );
        page.fill(
            margin,
            hero * 0.42 + 44.0 * scale,
            (260.0 * scale).min(width as f32 - 2.0 * margin),
            14.0 * scale,
            hsv(hue, 0.25, 1.0),
        );
        let line = 30.0 * scale;
        let mut y = hero + 48.0 * scale;
        let mut index = 0usize;
        while y + line < height as f32 {
            let column = width as f32 - 2.0 * margin;
            let fraction = [0.92, 0.85, 0.97, 0.6, 0.88, 0.79, 0.95, 0.4][index % 8];
            let color = if index == 1 {
                hsv(hue, 0.75, 0.75)
            } else {
                [218, 218, 222]
            };
            let bar = DeviceRect {
                x: margin,
                y,
                width: (column * fraction).max(0.0),
                height: 12.0 * scale,
            };
            page.fill(bar.x, bar.y, bar.width, bar.height, color);
            if index == 1 {
                page.link = DeviceRect {
                    y: y - 8.0 * scale,
                    height: bar.height + 16.0 * scale,
                    ..bar
                };
            }
            y += if index % 4 == 3 { line * 2.0 } else { line };
            index += 1;
        }
        page
    }

    fn fill(&mut self, x: f32, y: f32, w: f32, h: f32, [r, g, b]: [u8; 3]) {
        let clamp = |v: f32, max: usize| (v.max(0.0) as usize).min(max);
        let (x0, y0) = (clamp(x, self.width), clamp(y, self.height));
        let (x1, y1) = (
            clamp(x + w, self.width).max(x0),
            clamp(y + h, self.height).max(y0),
        );
        for row in y0..y1 {
            let start = row * self.stride;
            for pixel in self.bgra[start + x0 * 4..start + x1 * 4]
                .as_chunks_mut::<4>()
                .0
            {
                pixel.copy_from_slice(&[b, g, r, 255]);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn addresses_without_a_scheme_become_https() {
        assert_eq!(normalized_url("example.com"), "https://example.com");
        assert_eq!(normalized_url("about:newtab"), "about:newtab");
        assert_eq!(
            normalized_url("rune ui"),
            "https://search.example/?q=rune+ui"
        );
    }

    #[test]
    fn titles_name_the_last_path_segment_and_host() {
        assert_eq!(title_for("https://www.example.com/"), "example.com");
        assert_eq!(
            title_for("https://example.com/docs/next"),
            "next · example.com"
        );
        assert_eq!(title_for(NEW_TAB_URL), "New Tab");
    }

    #[test]
    fn pages_render_at_the_requested_size_with_padded_rows() {
        let page = MockPage::render("https://example.com", 40, 30, 1.0);
        assert_eq!(page.stride, 40 * 4 + ROW_PADDING_BYTES);
        assert_eq!(page.bgra.len(), page.stride * 30);
        assert!(
            page.bgra.chunks(page.stride).all(|row| row[..160]
                .as_chunks::<4>()
                .0
                .iter()
                .all(|p| p[3] == 255))
        );
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_platform_is_fullscreen() -> bool {
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_platform_set_fullscreen(_fullscreen: bool) {}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_platform_is_always_on_top() -> bool {
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn xenon_platform_set_always_on_top(_always_on_top: bool) {}
