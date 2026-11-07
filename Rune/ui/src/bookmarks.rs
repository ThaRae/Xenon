/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::ffi::c_void;
use std::sync::{Arc, OnceLock};
use std::time::Duration;

use rune::{ImageCache, Pixels, PointerButton, PopupAnchor, TextEdit, Tx};

use crate::chrome_popup::{self, PopupItem, PopupMenu};
use crate::engine::ViewId;
use crate::ffi::{XenonStr, XenonViewId};
use crate::ffi_bookmarks::{
    self as ffi, XenonBookmarkDialog, XenonBookmarkItem, XenonBookmarksBarSettings,
    XenonBookmarksCallbacks,
};
use crate::ffi_page::XenonMenu;
use crate::page_bridge::raw_menu_items;
use crate::page_prompts::{MenuEntry, menu_entries};
use crate::state::Browser;
use crate::window_slots::Slots;

pub const BAR_ANCHOR_PREFIX: &str = "bookmark-";
pub const OVERFLOW_KEY: &str = "bookmarks-overflow";
pub const DIALOG_FOLDER_KEY: &str = "bookmark-dialog-folder";
const HOVER_LEAVE_DELAY: Duration = Duration::from_millis(250);
const ROOT_FOLDER_ITEM: &str = "folder-root";
const NO_TITLE: &str = "(no title)";

#[derive(Clone, Debug)]
pub enum BookmarkNode {
    Bookmark {
        id: String,
        title: String,
        url: String,
        favicon: Option<Pixels>,
    },
    Folder {
        id: String,
        title: String,
        children: Vec<BookmarkNode>,
    },
}

impl BookmarkNode {
    pub fn id(&self) -> &str {
        match self {
            BookmarkNode::Bookmark { id, .. } | BookmarkNode::Folder { id, .. } => id,
        }
    }

    pub fn title(&self) -> &str {
        match self {
            BookmarkNode::Bookmark { title, .. } | BookmarkNode::Folder { title, .. } => title,
        }
    }

    pub fn is_folder(&self) -> bool {
        matches!(self, BookmarkNode::Folder { .. })
    }

    fn menu_title(&self) -> String {
        match self {
            BookmarkNode::Bookmark { title, url, .. } if title.is_empty() => url.clone(),
            _ => self.title().to_owned(),
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum BarVisibility {
    Always,
    #[default]
    Never,
    OnHover,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Disposition {
    CurrentTab,
    NewTab,
    NewBackgroundTab,
}

impl Disposition {
    fn raw(self) -> u32 {
        match self {
            Disposition::CurrentTab => ffi::XENON_BOOKMARK_OPEN_IN_CURRENT_TAB,
            Disposition::NewTab => ffi::XENON_BOOKMARK_OPEN_IN_NEW_TAB,
            Disposition::NewBackgroundTab => ffi::XENON_BOOKMARK_OPEN_IN_NEW_BACKGROUND_TAB,
        }
    }

    pub fn for_press(button: PointerButton, mods: rune::Mods) -> Disposition {
        let new_tab = button == PointerButton::Middle || mods.cmd;
        match (new_tab, mods.shift) {
            (false, _) => Disposition::CurrentTab,
            (true, false) => Disposition::NewTab,
            (true, true) => Disposition::NewBackgroundTab,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DialogKind {
    AddBookmark,
    EditBookmark,
    AddFolder,
    EditFolder,
}

impl DialogKind {
    fn from_raw(raw: u32) -> DialogKind {
        match raw {
            ffi::XENON_BOOKMARK_DIALOG_EDIT_BOOKMARK => DialogKind::EditBookmark,
            ffi::XENON_BOOKMARK_DIALOG_ADD_FOLDER => DialogKind::AddFolder,
            ffi::XENON_BOOKMARK_DIALOG_EDIT_FOLDER => DialogKind::EditFolder,
            _ => DialogKind::AddBookmark,
        }
    }

    pub fn title(self) -> &'static str {
        match self {
            DialogKind::AddBookmark => "Add Bookmark",
            DialogKind::EditBookmark => "Edit Bookmark",
            DialogKind::AddFolder => "Add Folder",
            DialogKind::EditFolder => "Edit Folder",
        }
    }

    pub fn has_url(self) -> bool {
        matches!(self, DialogKind::AddBookmark | DialogKind::EditBookmark)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FolderOption {
    pub id: String,
    pub path: String,
}

pub struct BookmarkDialog {
    pub id: u64,
    pub kind: DialogKind,
    pub url: TextEdit,
    pub title: TextEdit,
    pub folders: Vec<FolderOption>,
    pub folder: String,
}

impl BookmarkDialog {
    pub fn folder_path(&self) -> &str {
        self.folders
            .iter()
            .find(|option| option.id == self.folder)
            .map_or("Bookmarks Bar", |option| option.path.as_str())
    }

    fn folder_menu(&self) -> PopupMenu {
        let mut menu = PopupMenu::new().item(
            PopupItem::new(ROOT_FOLDER_ITEM, "Bookmarks Bar").checked(self.folder.is_empty()),
        );
        if !self.folders.is_empty() {
            menu = menu.separator();
        }
        for (index, option) in self.folders.iter().enumerate() {
            menu = menu.item(
                PopupItem::new(format!("folder-{index}"), option.path.clone())
                    .checked(option.id == self.folder),
            );
        }
        menu
    }

    fn pick_folder(&mut self, picked: &str) {
        if picked == ROOT_FOLDER_ITEM {
            self.folder.clear();
        } else if let Some(option) = picked
            .strip_prefix("folder-")
            .and_then(|index| index.parse::<usize>().ok())
            .and_then(|index| self.folders.get(index))
        {
            self.folder = option.id.clone();
        }
    }
}

#[derive(Default)]
pub struct Bookmarks {
    pub items: Vec<BookmarkNode>,
    pub visibility: BarVisibility,
    pub hover_expand_delay: Duration,
    pub hover_expand_animation: Duration,
    toolbar_hovered: bool,
    bar_hovered: bool,
    menu_open: bool,
    pub hover_expanded: bool,
    pub dialog: Option<BookmarkDialog>,
}

impl Bookmarks {
    pub fn shows_bar_in_layout(&self) -> bool {
        self.visibility == BarVisibility::Always
    }

    pub fn shows_hover_bar(&self) -> bool {
        self.visibility == BarVisibility::OnHover && self.hover_expanded
    }

    pub fn shown_dialog(&self) -> Option<&BookmarkDialog> {
        self.dialog.as_ref().filter(|dialog| dialog.id != 0)
    }

    fn dialog_or_placeholder(&mut self) -> &mut BookmarkDialog {
        self.dialog.get_or_insert_with(|| BookmarkDialog {
            id: 0,
            kind: DialogKind::AddFolder,
            url: TextEdit::new(""),
            title: TextEdit::new(""),
            folders: Vec::new(),
            folder: String::new(),
        })
    }

    pub fn dialog_url(&mut self) -> &mut TextEdit {
        &mut self.dialog_or_placeholder().url
    }

    pub fn dialog_title(&mut self) -> &mut TextEdit {
        &mut self.dialog_or_placeholder().title
    }

    pub fn hover_leave_delay(&self) -> Duration {
        HOVER_LEAVE_DELAY
    }

    fn update_hover(&mut self) {
        if self.visibility != BarVisibility::OnHover {
            self.hover_expanded = false;
            return;
        }
        if self.toolbar_hovered {
            self.hover_expanded = true;
        } else if !self.bar_hovered && !self.menu_open {
            self.hover_expanded = false;
        }
    }
}

pub fn visible_item_count(widths: &[f32], available: f32, gap: f32, overflow_width: f32) -> usize {
    let total: f32 = widths.iter().sum::<f32>() + gap * widths.len().saturating_sub(1) as f32;
    if total <= available {
        return widths.len();
    }
    let room = available - overflow_width - gap;
    let mut used = 0.0;
    for (index, width) in widths.iter().enumerate() {
        let next = used + if index > 0 { gap } else { 0.0 } + width;
        if next > room {
            return index;
        }
        used = next;
    }
    widths.len()
}

fn bookmark_count(children: &[BookmarkNode]) -> usize {
    children.iter().filter(|child| !child.is_folder()).count()
}

fn menu_for_items(items: &[BookmarkNode], folder_id: Option<&str>) -> PopupMenu {
    let mut menu = PopupMenu::new();
    for item in items {
        menu = match item {
            BookmarkNode::Bookmark { id, .. } => {
                menu.item(PopupItem::new(format!("b:{id}"), item.menu_title()))
            }
            BookmarkNode::Folder {
                id,
                title,
                children,
            } => menu.submenu(title.clone(), menu_for_items(children, Some(id))),
        };
    }
    if let Some(folder_id) = folder_id {
        let count = bookmark_count(items);
        if count > 0 {
            menu = menu.separator().item(PopupItem::new(
                format!("all:{folder_id}"),
                format!("Open All in Tabs ({count})"),
            ));
        }
    }
    menu
}

fn view_or_none(view: Option<ViewId>) -> ViewId {
    view.unwrap_or_default()
}

impl Browser {
    pub fn bookmarks_changed(&mut self, items: Vec<BookmarkNode>) {
        self.bookmarks.items = items;
    }

    pub fn bookmarks_bar_settings_changed(
        &mut self,
        visibility: BarVisibility,
        hover_expand_delay: Duration,
        hover_expand_animation: Duration,
    ) {
        self.bookmarks.visibility = visibility;
        self.bookmarks.hover_expand_delay = hover_expand_delay;
        self.bookmarks.hover_expand_animation = hover_expand_animation;
        self.bookmarks.update_hover();
    }

    pub fn bookmarks_toolbar_hovered(&mut self, hovered: bool) {
        self.bookmarks.toolbar_hovered = hovered && !self.location.popup_visible;
        self.bookmarks.update_hover();
    }

    pub fn bookmarks_bar_hovered(&mut self, hovered: bool) {
        self.bookmarks.bar_hovered = hovered;
        self.bookmarks.update_hover();
    }

    fn bookmark_menu_closed(&mut self) {
        self.bookmarks.menu_open = false;
        self.bookmarks.update_hover();
    }

    pub fn open_bookmark(&mut self, id: &str, disposition: Disposition) {
        let view = view_or_none(self.tabs.active_view());
        unsafe { ffi::xenon_engine_open_bookmark(view, XenonStr::borrowed(id), disposition.raw()) }
    }

    fn run_bookmark_menu_pick(&mut self, picked: &str) {
        if let Some(id) = picked.strip_prefix("b:") {
            self.open_bookmark(id, Disposition::CurrentTab);
        } else if let Some(id) = picked.strip_prefix("all:") {
            unsafe { ffi::xenon_engine_open_bookmark_folder(XenonStr::borrowed(id)) }
        }
    }

    fn show_bookmark_popup(
        &mut self,
        menu: PopupMenu,
        anchor: impl Into<PopupAnchor>,
        on_pick: impl FnOnce(&mut Browser, &str) + 'static,
        tx: &mut Tx,
    ) {
        if menu.is_empty() {
            self.bookmark_menu_closed();
            return;
        }
        chrome_popup::open_tracked(
            self,
            tx,
            menu,
            anchor,
            |s: &mut Browser, picked: &str, _: &mut Tx| {
                s.bookmark_menu_closed();
                on_pick(s, picked);
            },
            Browser::bookmark_menu_closed,
        );
        self.bookmarks.menu_open = true;
    }

    pub fn bar_item_pressed(&mut self, index: usize, tx: &mut Tx) {
        let Some(item) = self.bookmarks.items.get(index) else {
            return;
        };
        match item {
            BookmarkNode::Bookmark { id, .. } => {
                let id = id.clone();
                self.open_bookmark(&id, Disposition::for_press(tx.button(), tx.mods()));
            }
            BookmarkNode::Folder { id, children, .. } => {
                if Disposition::for_press(tx.button(), tx.mods()) != Disposition::CurrentTab
                    || children.is_empty()
                {
                    return;
                }
                let menu = menu_for_items(children, Some(id));
                self.show_bookmark_popup(
                    menu,
                    format!("{BAR_ANCHOR_PREFIX}{index}"),
                    Browser::run_bookmark_menu_pick,
                    tx,
                );
            }
        }
    }

    pub fn bar_overflow_pressed(&mut self, first_hidden: usize, tx: &mut Tx) {
        let hidden = self.bookmarks.items.get(first_hidden..).unwrap_or_default();
        if hidden.is_empty() {
            return;
        }
        let menu = menu_for_items(hidden, None);
        self.show_bookmark_popup(menu, OVERFLOW_KEY, Browser::run_bookmark_menu_pick, tx);
    }

    pub fn bar_item_secondary_pressed(&mut self, index: Option<usize>, at: rune::PointerPress) {
        let Some(view) = self.tabs.active_view() else {
            return;
        };
        let (id, target_folder_id) = match index.and_then(|index| self.bookmarks.items.get(index)) {
            Some(item) if item.is_folder() => (item.id().to_owned(), item.id().to_owned()),
            Some(item) => (item.id().to_owned(), String::new()),
            None => (String::new(), String::new()),
        };
        unsafe {
            ffi::xenon_engine_show_bookmark_context_menu(
                view,
                XenonStr::borrowed(&id),
                XenonStr::borrowed(&target_folder_id),
                XenonStr::borrowed(""),
                at.x,
                at.y,
            )
        }
    }

    pub fn bookmark_context_menu_requested(
        &mut self,
        view: ViewId,
        menu: u64,
        (x, y): (f32, f32),
        at_page_position: bool,
        entries: Vec<MenuEntry>,
        tx: &mut Tx,
    ) {
        if self.tabs.active_view() != Some(view) || entries.is_empty() {
            self.bookmark_menu_closed();
            return;
        }
        let anchor = if at_page_position {
            let (left, top) = self.page.origin();
            (left + x, top + y)
        } else {
            (x, y)
        };
        self.show_bookmark_popup(
            PopupMenu::from_entries(entries),
            anchor,
            move |_: &mut Browser, picked: &str| {
                if let Ok(item) = picked.parse::<u32>() {
                    unsafe { ffi::xenon_engine_activate_bookmark_menu_item(menu, item) }
                }
            },
            tx,
        );
    }

    pub fn bookmark_dialog_opened(&mut self, mut dialog: BookmarkDialog, tx: &mut Tx) {
        if let Some(previous) = self.bookmarks.dialog.take() {
            if previous.url.focused || previous.title.focused {
                tx.blur();
            }
            if previous.id != 0 {
                unsafe {
                    ffi::xenon_engine_close_bookmark_dialog(
                        previous.id,
                        false,
                        XenonStr::borrowed(""),
                        XenonStr::borrowed(""),
                        XenonStr::borrowed(""),
                    )
                }
            }
        }
        let field = if dialog.kind.has_url() {
            &mut dialog.url
        } else {
            &mut dialog.title
        };
        let end = field.text.len();
        field.set_selection(0, end);
        field.focused = true;
        self.hover_card = None;
        self.bookmarks.dialog = Some(dialog);
    }

    pub fn answer_bookmark_dialog(&mut self, accepted: bool, tx: &mut Tx) {
        let Some(dialog) = self.bookmarks.dialog.take().filter(|dialog| dialog.id != 0) else {
            return;
        };
        if dialog.url.focused || dialog.title.focused {
            tx.blur();
        }
        unsafe {
            ffi::xenon_engine_close_bookmark_dialog(
                dialog.id,
                accepted,
                XenonStr::borrowed(&dialog.url.text),
                XenonStr::borrowed(&dialog.title.text),
                XenonStr::borrowed(&dialog.folder),
            )
        }
    }

    pub fn cancel_bookmark_dialog(&mut self) {
        if let Some(dialog) = self.bookmarks.dialog.take().filter(|dialog| dialog.id != 0) {
            unsafe {
                ffi::xenon_engine_close_bookmark_dialog(
                    dialog.id,
                    false,
                    XenonStr::borrowed(""),
                    XenonStr::borrowed(""),
                    XenonStr::borrowed(""),
                )
            }
        }
    }

    pub fn pick_bookmark_dialog_folder(&mut self, tx: &mut Tx) {
        let Some(dialog) = &self.bookmarks.dialog else {
            return;
        };
        let id = dialog.id;
        let menu = dialog.folder_menu();
        chrome_popup::open(
            self,
            tx,
            menu,
            DIALOG_FOLDER_KEY,
            move |s: &mut Browser, picked: &str, _: &mut Tx| {
                if let Some(dialog) = s.bookmarks.dialog.as_mut()
                    && dialog.id == id
                {
                    dialog.pick_folder(picked);
                }
            },
        );
    }
}

pub struct BookmarksContext {
    slots: Arc<Slots>,
}

pub fn install(slots: Arc<Slots>) -> Box<BookmarksContext> {
    let context = Box::new(BookmarksContext { slots });
    let callbacks = XenonBookmarksCallbacks {
        context: std::ptr::from_ref::<BookmarksContext>(&context)
            .cast_mut()
            .cast(),
        bookmarks_changed: Some(bookmarks_changed),
        bar_settings_changed: Some(bar_settings_changed),
        context_menu_requested: Some(context_menu_requested),
        dialog_opened: Some(dialog_opened),
    };
    unsafe {
        ffi::xenon_engine_set_bookmarks_callbacks(&callbacks);
        ffi::xenon_engine_bookmarks_start();
    }
    context
}

unsafe fn slots<'a>(context: *mut c_void) -> &'a Arc<Slots> {
    unsafe { &(*context.cast::<BookmarksContext>()).slots }
}

fn text(string: XenonStr) -> String {
    unsafe { string.to_string_lossy() }
}

fn favicon_decoder() -> &'static ImageCache {
    static DECODER: OnceLock<ImageCache> = OnceLock::new();
    DECODER.get_or_init(|| ImageCache::new(Arc::new(|| {})))
}

fn favicon(item: &XenonBookmarkItem) -> Option<Pixels> {
    if item.favicon_png.is_null() || item.favicon_png_length == 0 {
        return None;
    }
    let png = unsafe { std::slice::from_raw_parts(item.favicon_png, item.favicon_png_length) };
    Some(favicon_decoder().load(png.to_vec()).pixels().clone())
}

pub fn tree_from(items: &[XenonBookmarkItem]) -> Vec<BookmarkNode> {
    let mut stack: Vec<(String, String, Vec<BookmarkNode>)> =
        vec![(String::new(), String::new(), Vec::new())];
    for item in items {
        match item.kind {
            ffi::XENON_BOOKMARK_ITEM_FOLDER_BEGIN => {
                let title = text(item.title);
                stack.push((
                    text(item.id),
                    if title.is_empty() {
                        NO_TITLE.to_owned()
                    } else {
                        title
                    },
                    Vec::new(),
                ));
            }
            ffi::XENON_BOOKMARK_ITEM_FOLDER_END => {
                if stack.len() > 1
                    && let Some((id, title, children)) = stack.pop()
                    && let Some((_, _, parent)) = stack.last_mut()
                {
                    parent.push(BookmarkNode::Folder {
                        id,
                        title,
                        children,
                    });
                }
            }
            _ => {
                if let Some((_, _, list)) = stack.last_mut() {
                    list.push(BookmarkNode::Bookmark {
                        id: text(item.id),
                        title: text(item.title),
                        url: text(item.url),
                        favicon: favicon(item),
                    });
                }
            }
        }
    }
    while stack.len() > 1 {
        if let Some((id, title, children)) = stack.pop()
            && let Some((_, _, parent)) = stack.last_mut()
        {
            parent.push(BookmarkNode::Folder {
                id,
                title,
                children,
            });
        }
    }
    stack.pop().map(|(_, _, items)| items).unwrap_or_default()
}

unsafe extern "C" fn bookmarks_changed(
    context: *mut c_void,
    items: *const XenonBookmarkItem,
    count: usize,
) {
    let items = if items.is_null() || count == 0 {
        Vec::new()
    } else {
        tree_from(unsafe { std::slice::from_raw_parts(items, count) })
    };
    unsafe { slots(context) }.update(move |browser, _| browser.bookmarks_changed(items.clone()));
}

unsafe extern "C" fn bar_settings_changed(
    context: *mut c_void,
    settings: *const XenonBookmarksBarSettings,
) {
    let Some(settings) = (unsafe { settings.as_ref() }) else {
        return;
    };
    let visibility = match settings.visibility {
        ffi::XENON_BOOKMARKS_BAR_ALWAYS => BarVisibility::Always,
        ffi::XENON_BOOKMARKS_BAR_ON_HOVER => BarVisibility::OnHover,
        _ => BarVisibility::Never,
    };
    let delay = Duration::from_millis(settings.hover_expand_delay_ms.into());
    let animation = Duration::from_millis(settings.hover_expand_animation_ms.into());
    unsafe { slots(context) }.update(move |browser, _| {
        browser.bookmarks_bar_settings_changed(visibility, delay, animation)
    });
}

unsafe extern "C" fn context_menu_requested(
    context: *mut c_void,
    view: XenonViewId,
    menu: *const XenonMenu,
    at_page_position: bool,
) {
    let Some(menu) = (unsafe { menu.as_ref() }) else {
        return;
    };
    let id = menu.id;
    let position = (menu.x, menu.y);
    let entries = menu_entries(&raw_menu_items(menu));
    unsafe { slots(context) }.update_view(view, move |browser, tx| {
        browser.bookmark_context_menu_requested(view, id, position, at_page_position, entries, tx)
    });
}

unsafe extern "C" fn dialog_opened(context: *mut c_void, dialog: *const XenonBookmarkDialog) {
    let Some(raw) = (unsafe { dialog.as_ref() }) else {
        return;
    };
    let folders = if raw.folders.is_null() || raw.folder_count == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(raw.folders, raw.folder_count) }
            .iter()
            .map(|option| FolderOption {
                id: text(option.id),
                path: text(option.path),
            })
            .collect()
    };
    let dialog = BookmarkDialog {
        id: raw.id,
        kind: DialogKind::from_raw(raw.kind),
        url: TextEdit::new(text(raw.url)),
        title: TextEdit::new(text(raw.title)),
        folders,
        folder: text(raw.selected_folder_id),
    };
    unsafe { slots(context) }
        .update_target(move |browser, tx| browser.bookmark_dialog_opened(dialog, tx));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn find(menu: &PopupMenu, name: &str) -> Option<(String, bool)> {
        fn search(menu: &PopupMenu, entries: &[MenuEntry], name: &str) -> Option<(String, bool)> {
            entries.iter().find_map(|entry| match entry {
                MenuEntry::Action {
                    id, title, checked, ..
                } if menu.name(*id) == Some(name) => Some((title.clone(), *checked)),
                MenuEntry::Submenu { entries, .. } => search(menu, entries, name),
                _ => None,
            })
        }
        search(menu, menu.entries(), name)
    }

    fn raw(kind: u32, id: &'static str, title: &'static str) -> XenonBookmarkItem {
        XenonBookmarkItem {
            kind,
            id: XenonStr::borrowed(id),
            title: XenonStr::borrowed(title),
            url: XenonStr::borrowed(""),
            favicon_png: std::ptr::null(),
            favicon_png_length: 0,
        }
    }

    #[test]
    fn folders_nest_their_children() {
        let items = [
            raw(ffi::XENON_BOOKMARK_ITEM_BOOKMARK, "a", "A"),
            raw(ffi::XENON_BOOKMARK_ITEM_FOLDER_BEGIN, "f", ""),
            raw(ffi::XENON_BOOKMARK_ITEM_BOOKMARK, "b", "B"),
            raw(ffi::XENON_BOOKMARK_ITEM_FOLDER_BEGIN, "g", "Inner"),
            raw(ffi::XENON_BOOKMARK_ITEM_FOLDER_END, "g", ""),
            raw(ffi::XENON_BOOKMARK_ITEM_FOLDER_END, "f", ""),
            raw(ffi::XENON_BOOKMARK_ITEM_BOOKMARK, "c", "C"),
        ];
        let tree = tree_from(&items);
        assert_eq!(tree.len(), 3);
        let BookmarkNode::Folder {
            title, children, ..
        } = &tree[1]
        else {
            panic!("expected a folder");
        };
        assert_eq!(title, NO_TITLE);
        assert_eq!(children.len(), 2);
        assert!(children[1].is_folder());
        assert_eq!(tree[2].id(), "c");
    }

    #[test]
    fn the_hover_bar_stays_while_the_pointer_is_on_it_or_one_of_its_menus_is_open() {
        let mut bookmarks = Bookmarks {
            visibility: BarVisibility::OnHover,
            ..Bookmarks::default()
        };
        bookmarks.toolbar_hovered = true;
        bookmarks.update_hover();
        assert!(bookmarks.shows_hover_bar());
        bookmarks.toolbar_hovered = false;
        bookmarks.bar_hovered = true;
        bookmarks.update_hover();
        assert!(bookmarks.shows_hover_bar());
        bookmarks.bar_hovered = false;
        bookmarks.menu_open = true;
        bookmarks.update_hover();
        assert!(bookmarks.shows_hover_bar());
        bookmarks.menu_open = false;
        bookmarks.update_hover();
        assert!(!bookmarks.shows_hover_bar());
        bookmarks.toolbar_hovered = true;
        bookmarks.visibility = BarVisibility::Always;
        bookmarks.update_hover();
        assert!(!bookmarks.shows_hover_bar());
        assert!(bookmarks.shows_bar_in_layout());
    }

    #[test]
    fn items_that_do_not_fit_go_behind_the_overflow_button() {
        assert_eq!(visible_item_count(&[50.0, 50.0, 50.0], 200.0, 3.0, 24.0), 3);
        assert_eq!(visible_item_count(&[50.0, 50.0, 50.0], 140.0, 3.0, 24.0), 2);
        assert_eq!(visible_item_count(&[50.0, 50.0, 50.0], 100.0, 3.0, 24.0), 1);
        assert_eq!(visible_item_count(&[150.0], 100.0, 3.0, 24.0), 0);
    }

    #[test]
    fn a_folder_menu_ends_with_open_all_when_it_holds_bookmarks() {
        let children = vec![BookmarkNode::Bookmark {
            id: "b".into(),
            title: String::new(),
            url: "https://example.com/".into(),
            favicon: None,
        }];
        let menu = menu_for_items(&children, Some("f"));
        assert_eq!(
            find(&menu, "b:b").map(|(title, _)| title),
            Some("https://example.com/".to_owned())
        );
        assert_eq!(
            find(&menu, "all:f").map(|(title, _)| title),
            Some("Open All in Tabs (1)".to_owned())
        );
        assert!(find(&menu_for_items(&[], Some("f")), "all:f").is_none());
    }

    #[test]
    fn middle_and_command_clicks_open_new_tabs() {
        let plain = rune::Mods::default();
        let command = rune::Mods {
            cmd: true,
            ..rune::Mods::default()
        };
        let command_shift = rune::Mods {
            cmd: true,
            shift: true,
            ..rune::Mods::default()
        };
        assert_eq!(
            Disposition::for_press(PointerButton::Primary, plain),
            Disposition::CurrentTab
        );
        assert_eq!(
            Disposition::for_press(PointerButton::Middle, plain),
            Disposition::NewTab
        );
        assert_eq!(
            Disposition::for_press(PointerButton::Primary, command),
            Disposition::NewTab
        );
        assert_eq!(
            Disposition::for_press(PointerButton::Primary, command_shift),
            Disposition::NewBackgroundTab
        );
    }

    #[test]
    fn the_folder_picker_starts_on_the_selected_folder() {
        let mut dialog = BookmarkDialog {
            id: 1,
            kind: DialogKind::AddBookmark,
            url: TextEdit::new(""),
            title: TextEdit::new(""),
            folders: vec![
                FolderOption {
                    id: "x".into(),
                    path: "Work".into(),
                },
                FolderOption {
                    id: "y".into(),
                    path: "Work / Docs".into(),
                },
            ],
            folder: "y".into(),
        };
        assert_eq!(dialog.folder_path(), "Work / Docs");
        assert!(find(&dialog.folder_menu(), "folder-1").is_some_and(|(_, checked)| checked));
        dialog.pick_folder(ROOT_FOLDER_ITEM);
        assert_eq!(dialog.folder_path(), "Bookmarks Bar");
        dialog.pick_folder("folder-0");
        assert_eq!(dialog.folder, "x");
    }
}
