/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::cmp::Ordering;
use std::collections::HashMap;
use std::ffi::c_void;
use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};
use std::sync::{Arc, OnceLock};

use rune::{ImageCache, Pixels, PointerPress, TextEdit, Tx};

use crate::chrome_popup::{self, PopupItem, PopupMenu};
use crate::ffi::XenonStr;
use crate::ffi_task_manager::{
    self, XENON_PROCESS_ICON_BROWSER, XENON_PROCESS_ICON_PAGE, XENON_PROCESS_ICON_SPARE,
    XenonProcess, XenonTaskManagerCallbacks,
};
use crate::state::Browser;
use crate::window_slots::Slots;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Column {
    Name,
    Pid,
    Cpu,
    Memory,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProcessIcon {
    Browser,
    Page,
    Spare,
    Service,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Process {
    pub child_id: u64,
    pub pid: i64,
    pub icon: ProcessIcon,
    pub is_browser: bool,
    pub can_end: bool,
    pub name: String,
    pub cpu_percent: f64,
    pub memory_bytes: u64,
    pub favicon_hash: String,
}

pub struct Row<'a> {
    pub process: &'a Process,
    pub depth: usize,
    pub parent: bool,
}

pub struct TaskManager {
    pub open: bool,
    pub processes: Vec<Process>,
    pub sort_column: Column,
    pub ascending: bool,
    pub selected: Option<u64>,
    pub search: TextEdit,
    pub collapsed: bool,
    pub error: Option<String>,
    favicons: HashMap<String, Pixels>,
}

impl Default for TaskManager {
    fn default() -> TaskManager {
        TaskManager {
            open: false,
            processes: Vec::new(),
            sort_column: Column::Pid,
            ascending: true,
            selected: None,
            search: TextEdit::new(""),
            collapsed: false,
            error: None,
            favicons: HashMap::new(),
        }
    }
}

fn compare(column: Column, a: &Process, b: &Process) -> Ordering {
    match column {
        Column::Name => a.name.to_lowercase().cmp(&b.name.to_lowercase()),
        Column::Pid => a.pid.cmp(&b.pid),
        Column::Cpu => a.cpu_percent.total_cmp(&b.cpu_percent),
        Column::Memory => a.memory_bytes.cmp(&b.memory_bytes),
    }
}

impl TaskManager {
    fn matches(&self, process: &Process) -> bool {
        let query = self.search.text.trim();
        query.is_empty()
            || process.name.to_lowercase().contains(&query.to_lowercase())
            || process.pid.to_string().contains(query)
    }

    fn sorted<'a>(&self, mut processes: Vec<&'a Process>) -> Vec<&'a Process> {
        processes.sort_by(|a, b| {
            let ordering = compare(self.sort_column, a, b);
            if self.ascending {
                ordering
            } else {
                ordering.reverse()
            }
        });
        processes
    }

    pub fn rows(&self) -> Vec<Row<'_>> {
        let parents: Vec<&Process> = self.processes.iter().filter(|p| p.is_browser).collect();
        let children: Vec<&Process> = if parents.is_empty() {
            Vec::new()
        } else {
            self.processes.iter().filter(|p| !p.is_browser).collect()
        };
        let top_level = if parents.is_empty() {
            self.processes.iter().collect()
        } else {
            parents
        };
        let visible_children: Vec<&Process> = self.sorted(
            children
                .into_iter()
                .filter(|process| self.matches(process))
                .collect(),
        );
        let searching = !self.search.text.trim().is_empty();
        let mut rows = Vec::new();
        for process in self.sorted(top_level) {
            let has_children = process.is_browser && !visible_children.is_empty();
            if !self.matches(process) && !has_children {
                continue;
            }
            rows.push(Row {
                process,
                depth: 0,
                parent: has_children,
            });
            if has_children && (searching || !self.collapsed) {
                rows.extend(visible_children.iter().map(|child| Row {
                    process: child,
                    depth: 1,
                    parent: false,
                }));
            }
        }
        rows
    }

    pub fn count_label(&self) -> String {
        let total = self.processes.len();
        let visible = if self.search.text.trim().is_empty() {
            total
        } else {
            let children = self
                .processes
                .iter()
                .filter(|p| !p.is_browser && self.matches(p))
                .count();
            let parents = self
                .processes
                .iter()
                .filter(|p| p.is_browser && (children > 0 || self.matches(p)))
                .count();
            children + parents
        };
        if visible == total {
            format!("{total} processes")
        } else {
            format!("{visible} of {total} processes")
        }
    }

    pub fn summary(&self) -> String {
        let cpu = self
            .processes
            .iter()
            .fold(0.0, |total, p| total + p.cpu_percent);
        let memory: u64 = self.processes.iter().map(|p| p.memory_bytes).sum();
        format!(
            "CPU: {}    Memory: {}",
            format_cpu(cpu),
            format_data_size(memory)
        )
    }

    pub fn favicon(&self, hash: &str) -> Option<&Pixels> {
        if hash.is_empty() {
            return None;
        }
        self.favicons.get(hash)
    }

    pub fn selected_process(&self) -> Option<&Process> {
        let selected = self.selected?;
        self.rows()
            .into_iter()
            .find(|row| row.process.child_id == selected)
            .map(|row| row.process)
    }

    pub fn sort_by(&mut self, column: Column) {
        if self.sort_column == column {
            self.ascending = !self.ascending;
        } else {
            self.sort_column = column;
            self.ascending = true;
        }
    }

    fn processes_changed(&mut self, processes: Vec<Process>, favicons: Vec<(String, Pixels)>) {
        self.favicons.extend(favicons);
        self.processes = processes;
        if let Some(selected) = self.selected
            && !self.processes.iter().any(|p| p.child_id == selected)
        {
            self.selected = None;
        }
    }
}

pub fn format_cpu(percent: f64) -> String {
    format!("{percent:.2}%")
}

pub fn format_data_size(bytes: u64) -> String {
    const UNITS: [&str; 6] = ["KiB", "MiB", "GiB", "TiB", "PiB", "EiB"];
    if bytes < 1024 {
        return format!("{bytes} bytes");
    }
    let mut value = bytes as f64 / 1024.0;
    let mut unit = 0;
    while value >= 1024.0 && unit + 1 < UNITS.len() {
        value /= 1024.0;
        unit += 1;
    }
    format!("{value:.2} {}", UNITS[unit])
}

static OPEN_WINDOWS: AtomicUsize = AtomicUsize::new(0);

pub fn open(s: &mut Browser) {
    s.hover_card = None;
    if s.task_manager.open {
        return;
    }
    s.task_manager.open = true;
    s.task_manager.error = None;
    s.task_manager.favicons.clear();
    OPEN_WINDOWS.fetch_add(1, AtomicOrdering::Relaxed);
    unsafe { ffi_task_manager::xenon_engine_task_manager_set_visible(true) };
}

pub fn close(s: &mut Browser) {
    if !s.task_manager.open {
        return;
    }
    s.task_manager.open = false;
    s.task_manager.search.focused = false;
    if OPEN_WINDOWS.fetch_sub(1, AtomicOrdering::Relaxed) == 1 {
        unsafe { ffi_task_manager::xenon_engine_task_manager_set_visible(false) };
    }
}

pub fn select(s: &mut Browser, child_id: u64) {
    s.task_manager.selected = Some(child_id);
}

pub fn end_selected_process(s: &mut Browser) {
    let Some(process) = s.task_manager.selected_process().filter(|p| p.can_end) else {
        return;
    };
    let child_id = process.child_id;
    s.task_manager.selected = None;
    s.task_manager.error = None;
    unsafe { ffi_task_manager::xenon_engine_task_manager_end_process(child_id) };
}

pub fn show_row_menu(s: &mut Browser, child_id: u64, at: PointerPress, tx: &mut Tx) {
    select(s, child_id);
    let Some(process) = s.task_manager.selected_process() else {
        return;
    };
    let mut menu = PopupMenu::new();
    if process.can_end {
        menu = menu.item(PopupItem::new("end-process", "End Process"));
    }
    menu = menu.item(PopupItem::new("copy-pid", "Copy PID"));
    chrome_popup::open(s, tx, menu, at, move |s, picked, _| match picked {
        "end-process" => end_selected_process(s),
        "copy-pid" => unsafe { ffi_task_manager::xenon_engine_task_manager_copy_pid(child_id) },
        _ => {}
    });
}

pub struct TaskManagerContext {
    handle: Arc<Slots>,
}

pub fn install(handle: Arc<Slots>) -> Arc<TaskManagerContext> {
    let context = Arc::new(TaskManagerContext { handle });
    let callbacks = XenonTaskManagerCallbacks {
        context: Arc::as_ptr(&context).cast_mut().cast(),
        open_requested: Some(open_requested),
        processes_changed: Some(processes_changed),
        end_process_failed: Some(end_process_failed),
    };
    unsafe { ffi_task_manager::xenon_engine_set_task_manager_callbacks(&callbacks) };
    context
}

unsafe fn handle<'a>(context: *mut c_void) -> &'a Slots {
    unsafe { &(*context.cast::<TaskManagerContext>()).handle }
}

fn favicon_decoder() -> &'static ImageCache {
    static DECODER: OnceLock<ImageCache> = OnceLock::new();
    DECODER.get_or_init(|| ImageCache::new(Arc::new(|| {})))
}

fn process_icon(raw: u32) -> ProcessIcon {
    match raw {
        XENON_PROCESS_ICON_BROWSER => ProcessIcon::Browser,
        XENON_PROCESS_ICON_PAGE => ProcessIcon::Page,
        XENON_PROCESS_ICON_SPARE => ProcessIcon::Spare,
        _ => ProcessIcon::Service,
    }
}

unsafe fn string(text: XenonStr) -> String {
    unsafe { text.to_string_lossy() }
}

unsafe extern "C" fn open_requested(context: *mut c_void) {
    unsafe { handle(context) }.update_target(|s, _| open(s));
}

unsafe extern "C" fn processes_changed(
    context: *mut c_void,
    raw: *const XenonProcess,
    count: usize,
) {
    let raw = if raw.is_null() || count == 0 {
        &[][..]
    } else {
        unsafe { std::slice::from_raw_parts(raw, count) }
    };
    let mut favicons = Vec::new();
    let processes: Vec<Process> = raw
        .iter()
        .map(|raw| {
            let favicon_hash = unsafe { string(raw.favicon_hash) };
            if !raw.favicon_png.is_null() && raw.favicon_png_length > 0 && !favicon_hash.is_empty()
            {
                let png =
                    unsafe { std::slice::from_raw_parts(raw.favicon_png, raw.favicon_png_length) };
                let image = favicon_decoder().load(png.to_vec());
                favicons.push((favicon_hash.clone(), image.pixels().clone()));
            }
            Process {
                child_id: raw.child_id,
                pid: raw.pid,
                icon: process_icon(raw.icon),
                is_browser: raw.is_browser,
                can_end: raw.can_end,
                name: unsafe { string(raw.name) },
                cpu_percent: raw.cpu_percent,
                memory_bytes: raw.memory_bytes,
                favicon_hash,
            }
        })
        .collect();
    unsafe { handle(context) }.update(move |s, _| {
        if s.task_manager.open {
            s.task_manager
                .processes_changed(processes.clone(), favicons.clone());
        }
    });
}

unsafe extern "C" fn end_process_failed(context: *mut c_void, message: XenonStr) {
    let message = unsafe { string(message) };
    unsafe { handle(context) }.update(move |s, _| {
        if s.task_manager.open {
            s.task_manager.error = Some(message.clone());
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    fn process(child_id: u64, pid: i64, name: &str, cpu: f64, memory: u64) -> Process {
        Process {
            child_id,
            pid,
            icon: if child_id == 0 {
                ProcessIcon::Browser
            } else {
                ProcessIcon::Service
            },
            is_browser: child_id == 0,
            can_end: child_id != 0,
            name: name.to_owned(),
            cpu_percent: cpu,
            memory_bytes: memory,
            favicon_hash: String::new(),
        }
    }

    fn task_manager() -> TaskManager {
        TaskManager {
            processes: vec![
                process(3, 300, "WebContent — example.com", 4.0, 3 << 20),
                process(0, 100, "Browser", 1.0, 1 << 20),
                process(2, 200, "RequestServer", 0.5, 2 << 20),
            ],
            ..TaskManager::default()
        }
    }

    fn row_ids(manager: &TaskManager) -> Vec<(u64, usize)> {
        manager
            .rows()
            .iter()
            .map(|row| (row.process.child_id, row.depth))
            .collect()
    }

    #[test]
    fn the_browser_heads_its_sorted_children() {
        let mut manager = task_manager();
        assert_eq!(row_ids(&manager), vec![(0, 0), (2, 1), (3, 1)]);
        manager.sort_by(Column::Cpu);
        manager.sort_by(Column::Cpu);
        assert_eq!(row_ids(&manager), vec![(0, 0), (3, 1), (2, 1)]);
        manager.collapsed = true;
        assert_eq!(row_ids(&manager), vec![(0, 0)]);
    }

    #[test]
    fn searching_keeps_the_parents_of_matches_and_counts_what_shows() {
        let mut manager = task_manager();
        assert_eq!(manager.count_label(), "3 processes");
        manager.search.replace_text("EXAMPLE");
        assert_eq!(row_ids(&manager), vec![(0, 0), (3, 1)]);
        assert_eq!(manager.count_label(), "2 of 3 processes");
        manager.search.replace_text("200");
        assert_eq!(row_ids(&manager), vec![(0, 0), (2, 1)]);
        manager.search.replace_text("nothing");
        assert!(manager.rows().is_empty());
        assert_eq!(manager.count_label(), "0 of 3 processes");
    }

    #[test]
    fn a_process_that_goes_away_is_no_longer_selected() {
        let mut manager = task_manager();
        manager.selected = Some(3);
        assert_eq!(manager.selected_process().map(|p| p.pid), Some(300));
        let remaining = manager.processes[1..].to_vec();
        manager.processes_changed(remaining, Vec::new());
        assert_eq!(manager.selected, None);
    }

    #[test]
    fn sizes_and_load_read_as_the_qt_task_manager_shows_them() {
        assert_eq!(format_data_size(512), "512 bytes");
        assert_eq!(format_data_size(1536), "1.50 KiB");
        assert_eq!(format_data_size(3 << 20), "3.00 MiB");
        assert_eq!(format_cpu(0.5), "0.50%");
        assert_eq!(task_manager().summary(), "CPU: 5.50%    Memory: 6.00 MiB");
        assert_eq!(
            TaskManager::default().summary(),
            "CPU: 0.00%    Memory: 0 bytes"
        );
    }
}
