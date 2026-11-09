/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::tok::TextStyle;
use rune::{
    Color, Cursor, El, Icon, Key, ScrollSpec, TextEdit, col, edit_access, el, icon, row, text,
    text_input,
};

use crate::state::Browser;
use crate::task_manager::{self, Column, ProcessIcon, Row, TaskManager, format_cpu};
use crate::view::icons;
use crate::view::palette::Palette;

const PANEL_MAX_WIDTH: f32 = 760.0;
const PANEL_MAX_HEIGHT: f32 = 480.0;
const ROW_HEIGHT: f32 = 27.0;
const INDENT: f32 = 14.0;
const PID_WIDTH: f32 = 100.0;
const CPU_WIDTH: f32 = 100.0;
const MEMORY_WIDTH: f32 = 130.0;
const SEARCH_WIDTH: f32 = 220.0;
const TITLE_FONT: TextStyle = TextStyle::new(15.0, 600.0);
const BODY_FONT: TextStyle = TextStyle::new(13.0, 400.0);
const HEADER_FONT: TextStyle = TextStyle::new(12.5, 550.0);
const SMALL_FONT: TextStyle = TextStyle::new(12.0, 400.0);
const BUTTON_FONT: TextStyle = TextStyle::new(13.0, 550.0);

const fn stroked(name: &'static str, body: &'static str) -> Icon {
    Icon {
        name,
        view: 24.0,
        body,
    }
}

static WINDOW_RESTORE: Icon = stroked(
    "xenon-task-service",
    r#"<path d="M8 8V5h11v11h-3M5 8h11v11H5z" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
static WINDOW_MAXIMIZE: Icon = stroked(
    "xenon-task-spare",
    r#"<path d="M5 5h14v14H5z" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
static BROWSER: Icon = stroked(
    "xenon-task-browser",
    r#"<path d="M4 6a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2zM4 9h16M7 6.5h.01M10 6.5h.01" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
static SEARCH: Icon = stroked(
    "xenon-task-search",
    r#"<path d="M10.5 17a6.5 6.5 0 1 0 0-13a6.5 6.5 0 0 0 0 13zM20 20l-4.8-4.8" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
static DISCLOSURE_OPEN: Icon = stroked(
    "xenon-task-disclosure-open",
    r#"<path d="M7 10l5 5 5-5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
static DISCLOSURE_CLOSED: Icon = stroked(
    "xenon-task-disclosure-closed",
    r#"<path d="M10 7l5 5-5 5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
static SORT_ASCENDING: Icon = stroked(
    "xenon-task-sort-ascending",
    r#"<path d="M8 14l4-4 4 4" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);
static SORT_DESCENDING: Icon = stroked(
    "xenon-task-sort-descending",
    r#"<path d="M8 10l4 4 4-4" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#,
);

pub fn task_manager_overlay(s: &Browser, p: &Palette) -> Option<El> {
    let manager = &s.task_manager;
    if !manager.open {
        return None;
    }
    let panel = col()
        .key("task-manager")
        .role("dialog")
        .label("Task Manager")
        .w_full()
        .max_w(PANEL_MAX_WIDTH)
        .h_full()
        .max_h(PANEL_MAX_HEIGHT)
        .pt(12.0)
        .px(16.0)
        .pb(12.0)
        .gap(8.0)
        .radius(12.0)
        .fill(p.surface)
        .border(p.hairline, p.bubble_border)
        .shadow(10.0, 30.0, Color::BLACK.alpha(0.3))
        .modal(true, task_manager::close)
        .child(title_bar(p))
        .child(toolbar(manager, p))
        .child(table(manager, p))
        .child(footer(manager, p));
    Some(
        col()
            .key("task-manager-scrim")
            .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
            .center()
            .padding(24.0)
            .fill(Color::BLACK.alpha(0.2))
            .cursor(Cursor::Default)
            .child(panel),
    )
}

fn title_bar(p: &Palette) -> El {
    row()
        .key("task-manager-title-bar")
        .w_full()
        .center_y()
        .child(
            text("Task Manager")
                .key("task-manager-title")
                .font(TITLE_FONT)
                .color(p.text)
                .grow(1.0),
        )
        .child(
            el().key("task-manager-close")
                .role("button")
                .label("Close Task Manager")
                .size(26.0, 26.0)
                .radius(6.0)
                .center()
                .focusable(false)
                .hover(p.hover)
                .press(p.press)
                .on_press(task_manager::close)
                .child(icon(&icons::CLOSE, 14.0).color(p.icon).a11y_hidden()),
        )
}

fn search_edit(s: &mut Browser) -> &mut TextEdit {
    &mut s.task_manager.search
}

fn toolbar(manager: &TaskManager, p: &Palette) -> El {
    let input = text_input(&manager.search, BODY_FONT)
        .key("task-manager-search-input")
        .label("Search processes")
        .single_line()
        .grow(1.0)
        .shrink()
        .color(p.text)
        .placeholder("Search processes", p.text_muted)
        .caret(p.accent, p.selection)
        .on_edit(edit_access(search_edit))
        .on_input_key(|s: &mut Browser, event, _| {
            if event.key == Key::Escape {
                task_manager::close(s);
                return true;
            }
            false
        });
    let field = row()
        .key("task-manager-search")
        .role("search")
        .w(SEARCH_WIDTH)
        .h(30.0)
        .px(9.0)
        .gap(6.0)
        .center_y()
        .radius(8.0)
        .child(icon(&SEARCH, 14.0).color(p.text_muted).a11y_hidden())
        .child(input);
    let field = if manager.search.focused {
        field
            .fill(p.field_focused)
            .border(1.5, p.focus_ring)
            .outline(3.0, 0.0, p.focus_glow)
    } else {
        field.fill(p.field).border(p.hairline, p.bubble_border)
    };
    row()
        .key("task-manager-toolbar")
        .w_full()
        .gap(24.0)
        .center_y()
        .child(
            text(manager.count_label())
                .key("task-manager-count")
                .role("status")
                .font(BODY_FONT)
                .color(p.text_muted)
                .grow(1.0)
                .truncate(),
        )
        .child(field)
}

fn header_cell(manager: &TaskManager, column: Column, title: &str, p: &Palette) -> El {
    let sorted = manager.sort_column == column;
    let mut cell = row()
        .key(format!("task-manager-sort-{}", column_key(column)))
        .role("button")
        .label(format!("Sort by {title}"))
        .checked(sorted)
        .h_full()
        .px(8.0)
        .gap(4.0)
        .center_y()
        .focusable(false)
        .hover(p.hover)
        .press(p.press)
        .on_press(move |s: &mut Browser| s.task_manager.sort_by(column));
    cell = match column {
        Column::Name => cell.grow(1.0).shrink().min_w(0.0),
        Column::Pid => cell.w(PID_WIDTH).justify_end(),
        Column::Cpu => cell.w(CPU_WIDTH).justify_end(),
        Column::Memory => cell.w(MEMORY_WIDTH).justify_end(),
    };
    let label = text(title.to_owned())
        .font(HEADER_FONT)
        .color(p.text)
        .truncate()
        .a11y_hidden();
    let label = if column == Column::Name {
        label.grow(1.0).shrink()
    } else {
        label
    };
    let arrow = sorted.then(|| {
        icon(
            if manager.ascending {
                &SORT_ASCENDING
            } else {
                &SORT_DESCENDING
            },
            12.0,
        )
        .color(p.text_muted)
        .a11y_hidden()
    });
    cell.child(label).children(arrow)
}

fn column_key(column: Column) -> &'static str {
    match column {
        Column::Name => "name",
        Column::Pid => "pid",
        Column::Cpu => "cpu",
        Column::Memory => "memory",
    }
}

fn table(manager: &TaskManager, p: &Palette) -> El {
    let header = row()
        .key("task-manager-header")
        .w_full()
        .h(30.0)
        .child(header_cell(manager, Column::Name, "Process", p))
        .child(header_cell(manager, Column::Pid, "PID", p))
        .child(header_cell(manager, Column::Cpu, "CPU", p))
        .child(header_cell(manager, Column::Memory, "Memory", p));
    let mut rows = col()
        .key("task-manager-rows")
        .role("tree")
        .label("Processes")
        .w_full();
    for entry in manager.rows() {
        rows = rows.child(process_row(manager, &entry, p));
    }
    col()
        .key("task-manager-table")
        .w_full()
        .grow(1.0)
        .shrink()
        .min_h(0.0)
        .radius(8.0)
        .fill(p.field)
        .border(p.hairline, p.separator)
        .child(header)
        .child(
            el().key("task-manager-header-rule")
                .w_full()
                .h(p.hairline)
                .fill(p.separator)
                .a11y_hidden(),
        )
        .child(
            col()
                .key("task-manager-scroll")
                .w_full()
                .grow(1.0)
                .shrink()
                .min_h(0.0)
                .scroll(ScrollSpec {
                    stick_bottom: false,
                })
                .child(rows),
        )
}

fn process_icon(manager: &TaskManager, entry: &Row, ink: Color, muted: Color) -> El {
    let slot = el().key("icon").size(16.0, 16.0).center().a11y_hidden();
    let process = entry.process;
    if let Some(favicon) = manager.favicon(&process.favicon_hash) {
        return slot.child(el().size(16.0, 16.0).radius(3.0).pixels(favicon));
    }
    match process.icon {
        ProcessIcon::Browser => slot.child(icon(&BROWSER, 15.0).color(ink)),
        ProcessIcon::Page => slot.child(icon(&icons::GLOBE, 15.0).color(ink)),
        ProcessIcon::Spare => slot.child(icon(&WINDOW_MAXIMIZE, 14.0).color(muted)),
        ProcessIcon::Service => slot.child(icon(&WINDOW_RESTORE, 14.0).color(muted)),
    }
}

fn numeric_cell(key: &'static str, value: String, width: f32, ink: Color) -> El {
    row()
        .key(key)
        .w(width)
        .h_full()
        .px(8.0)
        .center_y()
        .justify_end()
        .child(text(value).font(BODY_FONT).color(ink).truncate())
}

fn process_row(manager: &TaskManager, entry: &Row, p: &Palette) -> El {
    let process = entry.process;
    let child_id = process.child_id;
    let selected = manager.selected == Some(child_id);
    let (ink, muted) = if selected {
        (Color::WHITE, Color::WHITE.alpha(0.8))
    } else {
        (p.text, p.text_muted)
    };
    let disclosure = if entry.parent {
        el().key("disclosure")
            .role("button")
            .label(if manager.collapsed {
                "Expand"
            } else {
                "Collapse"
            })
            .size(16.0, 16.0)
            .center()
            .focusable(false)
            .on_press(|s: &mut Browser| s.task_manager.collapsed = !s.task_manager.collapsed)
            .child(
                icon(
                    if manager.collapsed {
                        &DISCLOSURE_CLOSED
                    } else {
                        &DISCLOSURE_OPEN
                    },
                    12.0,
                )
                .color(muted)
                .a11y_hidden(),
            )
    } else {
        el().size(16.0, 16.0).a11y_hidden()
    };
    let name = row()
        .key("name")
        .grow(1.0)
        .shrink()
        .min_w(0.0)
        .h_full()
        .pl(4.0 + entry.depth as f32 * INDENT)
        .pr(8.0)
        .gap(6.0)
        .center_y()
        .child(disclosure)
        .child(process_icon(manager, entry, ink, muted))
        .child(
            text(process.name.as_str())
                .font(BODY_FONT)
                .color(ink)
                .truncate()
                .grow(1.0)
                .shrink(),
        );
    let line = row()
        .key(format!("process-{child_id}"))
        .role("treeitem")
        .label(process.name.as_str())
        .checked(selected)
        .w_full()
        .h(ROW_HEIGHT)
        .center_y()
        .focusable(false)
        .on_press(move |s: &mut Browser| task_manager::select(s, child_id))
        .on_secondary_press(move |s: &mut Browser, at, tx| {
            task_manager::show_row_menu(s, child_id, at, tx)
        })
        .child(name)
        .child(numeric_cell("pid", process.pid.to_string(), PID_WIDTH, ink))
        .child(numeric_cell(
            "cpu",
            format_cpu(process.cpu_percent),
            CPU_WIDTH,
            ink,
        ))
        .child(numeric_cell(
            "memory",
            task_manager::format_data_size(process.memory_bytes),
            MEMORY_WIDTH,
            ink,
        ));
    if selected {
        line.fill(p.accent)
    } else {
        line.hover(p.hover)
    }
}

fn footer(manager: &TaskManager, p: &Palette) -> El {
    let can_end = manager
        .selected_process()
        .is_some_and(|process| process.can_end);
    let end_process = row()
        .key("task-manager-end-process")
        .role("button")
        .label("End Process")
        .h(30.0)
        .px(14.0)
        .radius(8.0)
        .center()
        .focusable(false)
        .fill(p.field)
        .border(p.hairline, p.bubble_border);
    let end_process = if can_end {
        end_process
            .hover(p.hover)
            .press(p.press)
            .on_press(task_manager::end_selected_process)
            .child(
                text("End Process")
                    .font(BUTTON_FONT)
                    .color(p.text)
                    .a11y_hidden(),
            )
    } else {
        end_process.child(
            text("End Process")
                .font(BUTTON_FONT)
                .color(p.text_muted.alpha(0.6))
                .a11y_hidden(),
        )
    };
    let status = match &manager.error {
        Some(error) => text(error.as_str())
            .key("task-manager-error")
            .role("alert")
            .font(SMALL_FONT)
            .color(p.accent),
        None => text(manager.summary())
            .key("task-manager-summary")
            .font(SMALL_FONT)
            .color(p.text_muted),
    };
    row()
        .key("task-manager-footer")
        .w_full()
        .gap(12.0)
        .center_y()
        .child(end_process)
        .child(rune::el().grow(1.0).a11y_hidden())
        .child(status.truncate().shrink())
}
