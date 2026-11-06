# The Rune frontend (experimental)

The `rune-frontend` branch replaces Xenon's Qt chrome with one drawn by [Rune](https://github.com/anomalyco/rune), a
GPU-rendered Rust UI framework. The engine side stays C++: LibWebView, the helper processes, and Xenon's patches are
unchanged. Until the Rune frontend reaches parity, the Qt frontend (`UI/`, the `xenon` target) keeps building beside it,
so the two can be compared and the tests keep passing.

## Layout

| Path | What it is |
| --- | --- |
| `Rune/Bridge/XenonBridge.h` | The C ABI between the Rust UI and the C++ engine host. The only header both sides share. |
| `Rune/Bridge/` | The engine host, in C++: `main()`, Xenon's `WebView::Application` and `ViewImplementation` for Rune, and the engine thread. |
| `Rune/ui/` | The `xenon_ui` crate: the browser chrome in Rune, built as a static library and linked into the `xenon-rune` executable. |
| `Rune/Rune.cmake` | Defines the `xenon-rune` target. `UI/Xenon.cmake` includes it. |

## Threads

```
main thread (Rust, Rune)                 engine thread (C++, LibWebView)
------------------------                 -------------------------------
main() -> xenon_ui_main()
  App::new(..), handle = app.handle()
  xenon_engine_start(callbacks) ------->  WebView::Application::create(), Core::EventLoop
  app.run()                               ...
    xenon_engine_load(view, "..") ------> posted to the engine's event loop
                                          on_title_change -> callbacks.view_title_changed
    handle.update(|s, tx| ..)  <--------- (callback runs on the engine thread)
    Pixels::set(..)            <--------- callbacks.view_frame, copies BGRA -> RGBA
  app.run() returns
  xenon_engine_shutdown() ------------->  event loop quits, thread joined
```

- Rune (winit) has to own the main thread, as AppKit requires. LibWebView runs on its own thread, with Ladybird's
  ordinary `Core::EventLoop`. Neither side's event loop is integrated with the other's.
- Every `xenon_engine_*` function can be called from any thread. Each posts its work to the engine's event loop and
  returns at once. `xenon_engine_create_view` gives back the new view's id straight away, and the view is created on the
  engine thread after that.
- Every callback in `XenonUiCallbacks` runs on the engine thread. The Rust side never touches UI state there: it hands
  the event to `AppHandle::update`, or, for frames, copies the pixels into the view's `rune::Pixels`, which takes frames
  from any thread.
- Strings cross as UTF-8 `XenonStr` (pointer and length) and are valid only during the call.
- A frame's `bgra` is the page's bitmap, premultiplied BGRA (Ladybird's `BGRx8888`/`BGRA8888`), valid only during the
  callback. Its size is in device pixels.

## Coordinates and input

- `xenon_engine_create_view` and `xenon_engine_set_viewport` take the page area's size in device pixels and the
  window's device pixel ratio.
- Mouse positions are in device pixels from the page area's top-left. Wheel deltas are in device pixels.
- Key events carry the UI Events `code` (`"KeyA"`, `"ArrowLeft"`, `"Enter"`) and `key` (the character typed, or the key's
  name: `"a"`, `"A"`, `"ArrowLeft"`). The bridge maps them onto Ladybird's `Web::UIEvents::KeyCode`.
- A key-down whose `key` is one character types it. `xenon_engine_insert_text` is only for what an input method
  composes; sending typed characters through it as well would insert them twice.
- Wheel deltas follow the DOM: a positive `y` scrolls down.
- The second press of a double click is sent as `XENON_MOUSE_EVENT_DOUBLE_CLICK` in place of `DOWN`, not beside it.
- If another Xenon already runs with the same profile, the engine hands it the URLs and sends `engine_exited(0)`.
- `xenon_engine_start` can be called once per process: LibCore ties its signal pipe to the first thread that registers a
  signal.
- Colour and date pickers are dismissed for now. Everything else a page asks of the UI goes through the page ABI below.

## Windows and tabs

The engine thread owns the browser's windows and tabs, in `Xenon::BrowserModel` (`Rune/Bridge/BrowserModel.*`, Qt-free
and unit tested by `TestXenonBrowserModel`). Session restore, and later the extensions' `chrome.tabs`/`windows`,
hibernation and tab groups, run on that thread and need to read windows and tabs synchronously, which they couldn't do
from the UI thread without a deadlock. The Rust UI keeps a mirror and changes it only when the model says so.

- A window is an ordered list of tabs, with an active tab, a privacy, a recently used order (`UI/RecentTabOrder.h`) and
  the `SessionStore` window it's saved as. A tab is a view id plus whether it's pinned, the view that opened it, its
  `SessionStore` tab and, until it first loads, the URL or history it starts from.
- The rules are the Qt frontend's: pinned tabs stay together at the start of a window; a new or moved unpinned tab never
  lands inside them; a tab a page opens goes right of its opener and of the opener's earlier tabs; closing the active tab
  activates its right neighbour, or its left one at the end; a window whose last tab closes closes; closing the active
  window activates the one used before it.
- The UI asks with intents, each posted to the engine thread: `xenon_engine_new_window`, `_new_tab` (empty input opens
  the new tab page, anything else goes through `load_user_input`), `_activate_tab`, `_close_tab`, `_move_tab`,
  `_set_tab_pinned`, `_reopen_closed_tab`, `_activate_window`, `_close_window`, `_cycle_recent_tabs` and
  `_end_recent_tab_cycle`. `_new_window` and `_new_tab` return the new id at once, as views did before.
- The model answers with events: `window_opened`, `window_closed`, `window_activated`, `tab_inserted` (a `XenonTabInfo`:
  window, view, index, pinned, privacy, URL, a title from history for a tab that hasn't loaded, whether it's the new tab
  page), `tab_removed`, `tab_moved`, `tab_activated`, `tab_pinned_changed` and `closed_tabs_changed`. Events rather than
  snapshots: each maps onto one change of the mirror, the UI keeps its per-tab state (title, favicon, frames) without
  diffing, and a move between windows stays a move rather than a close and an open.
- Closing the active tab sends `tab_activated` for its successor before `tab_removed`, so the UI is never without an
  active tab. A tab's view exists by the time its `tab_inserted` or `tab_activated` reaches the UI.
- The engine creates and destroys views as the model's events say, shows only each window's active tab, and gives a new
  view the window's last page size (`xenon_engine_set_window_viewport`, and every `set_viewport`). A view that a page opens
  (`window.open`, a link with a target) becomes a tab the model places; a page's `window.close()` closes its tab.
- Session restore follows the Qt frontend. `Engine::launch_restore_policy()` (what Rune's `Application` returns) is
  `Everything` with **Restore previous session** set, else `PinnedOnly`. At startup the engine takes the `SessionStore`'s
  launch plan, restores each window with its active and pinned tabs loaded and the rest loading when first selected, and
  then opens the startup URLs (the new tab page by default) in the frontmost window unless the whole session came back
  and no URL was given. With nothing to restore it opens a window with the startup URLs. The UI no longer opens a tab
  itself.
- Every tab registers with the `SessionStore` as it opens; its view reports history to it. Activation and order changes
  flush after a second (a `Core::Timer`), closes write through, and quitting the UI calls `application_quitting()` before
  the views go, so the windows stay open in the store for the next launch to recover. `--headless` and WebDriver runs get a
  transient store, but a Rune run under `XENON_RUNE_ARGS` is an ordinary run and saves.
- <kbd>Ctrl</kbd><kbd>⇧</kbd><kbd>T</kbd> reopens the last closed tab or window (at its old place, with its history),
  <kbd>Ctrl</kbd><kbd>N</kbd> opens a window, <kbd>Ctrl</kbd><kbd>⇧</kbd><kbd>N</kbd> a private one,
  <kbd>Ctrl</kbd><kbd>⇧</kbd><kbd>W</kbd> closes the window, <kbd>Ctrl</kbd><kbd>`</kbd> shows the next window,
  <kbd>Ctrl</kbd><kbd>Tab</kbd> walks the recently used order until <kbd>Ctrl</kbd> is let go, and
  <kbd>Ctrl</kbd><kbd>⇧</kbd><kbd>PgUp</kbd>/<kbd>PgDn</kbd> move the tab, as dragging it does. (<kbd>⌘</kbd> in place of
  <kbd>Ctrl</kbd> on macOS, but <kbd>Control</kbd> for Tab.)
- Menus (`chrome_menu.rs`, `app_menu.rs`, `Bridge/AppMenus.cpp`): a right click on the empty tab strip, toolbar or
  sidebar opens the window menu (New Tab, Reopen Closed Tab, Always on Top, Layout, Cycle Tabs in Recently Used Order),
  and on a tab, the tab menu (Reload, Duplicate, Pin, Move, Close, Close Multiple Tabs). The toolbar's menu button opens
  the main menu, which the engine builds from LibWebView's menus and actions, flattens as it does page context menus,
  and runs when one is picked; commands only the UI can do come back through `ui_command`. These menus and the macOS
  menu bar are native AppKit menus; elsewhere Rune doesn't draw chrome menus yet.
- Layouts: Classic, Compact (toolbar and tabs in one row) and Vertical (a sidebar that collapses to icons), read from and
  written to `XenonSettings::chrome_layout`, which the engine reports through `chrome_settings_changed`.
- Resting on a tab shows a hover card with its last frame, address and memory use (`Bridge/TabMemory.cpp`, a Qt-free
  port of `UI/TabMemory.cpp`). A tab playing audio has a mute button.
- The toolbar has the downloads button and popover (`view/downloads.rs`, from a `FileDownloaderObserver`), the content
  blocking shield and popover (`view/shield.rs`), and a Private badge in private windows.
- Full screen and Always on Top act on the AppKit window through `Bridge/Platform.h`; elsewhere they do nothing yet.
- One Rune window per process for now: Rune adds windows only before `App::run`, so the single window shows the model's
  active window and switches when another is activated, and the others wait behind it. Each model window already has its
  own tab list in the mirror (`tab_list::Windows`), ready for a Rune window each.
- Not yet: tab groups, split view, hibernation, window geometry in the session, the beforeunload checks a window close
  runs (`WindowCloseCoordinator`), the bookmarks bar, extension buttons, the updater, picture-in-picture, the task
  manager.

## The location bar

The address bar runs on LibWebView's `WebView::Omnibox`, the same model the Qt bar uses, through its own slice of the
ABI in `Rune/Bridge/XenonBridgeLocation.h` (`XenonBridgeLocation.cpp`; `ffi_location.rs` and `location.rs` on the Rust
side, `view/location/` for the field, the suggestion popup and the site information popover).

- The engine keeps one `Omnibox` per view, made when the bar first edits for it and dropped once the view is gone. The UI
  registers `XenonLocationCallbacks` with `xenon_engine_set_location_callbacks` before `xenon_engine_start`.
- The field stays a Rune `text_input`. Each frame the UI compares it with what it last sent and reports a change of text
  (`text_edited`) or of whether the caret is at the end (`cursor_moved`). Arrow keys, Enter, Escape, Right/End over an
  inline completion, Backspace and Delete go through `xenon_engine_location_command`/`_submit`.
- Every text or caret change carries a new generation. The engine stamps `display_changed` (inline completions and
  previews of the selected row) and `input_changed` with the generation it last saw, and the UI drops one that isn't its
  current generation, so a completion computed for older text never overwrites newer typing. Suggestion lists and the
  selection are applied whatever their generation, since they don't touch the text.
- A commit (Enter, or a click on a row) resolves on the engine thread as the Qt bar does: a leading or trailing !bang
  (unless its chip was taken away with Backspace), then a search or `load_from_user_input`, with Ctrl (Cmd on macOS)
  adding `.com`. `committed` then tells the UI to give the page the keyboard.
- `input_changed` says whether the typed text looks like an address or a search, and names the site of a leading
  !bang, which the field shows as a chip over the hidden trigger.
- For each address it shows, the UI asks `xenon_engine_location_describe_url` for its parts (`break_url_into_parts`),
  whether it's hidden (the new tab page, `NewTabPage::should_hide_url`'s rule), its security state (padlock, **Not
  secure** for HTTP or an accepted certificate, nothing for internal pages) and the search engine for the placeholder.
  Out of focus the field shows the address without its scheme, `www.` and root slash, with the site's registrable
  domain at full strength and the rest dimmed (`location::display_for`, unit tested); focused, the whole URL, selected.
- Pressing the padlock or **Not secure** asks `xenon_engine_location_request_site_security`, which answers at once with
  what it knows and again once RequestServer reports the connection (`SiteSecurity::request_description`).
- Suggestion favicons cross as PNG bytes and are decoded by Rune's `ImageCache`, since LibGfx's decoders live in
  `LibImageDecoders`, which the UI process doesn't link.
- <kbd>Ctrl</kbd><kbd>Shift</kbd><kbd>C</kbd> (<kbd>⌘</kbd><kbd>⇧</kbd><kbd>C</kbd>) copies the page's URL and shows
  "Copied link" under the toolbar.
- The bar shows the page's zoom when it isn't 100% (a click resets it) and a star that bookmarks the page. Until Rune
  has a bookmark dialog, `display_add_bookmark_dialog` files the bookmark straight away under the page's title.
- Not yet: the Qt bar's parked editing session per tab (switching tabs ends the edit) and the paste-and-go context menu.

## Page interaction

Dialogs, menus, pickers, find, tooltips, fullscreen and permission prompts have their own slice of the ABI in
`Rune/Bridge/XenonBridgePage.h` (`XenonBridgePage.cpp`, with the engine side in `View.cpp`; `ffi_page.rs`,
`page_bridge.rs` and `page_prompts.rs` on the Rust side, `view/page_overlays/` for what's drawn over the page). The UI
registers `XenonPageCallbacks` with `xenon_engine_set_page_callbacks` before `xenon_engine_start`. The UI keeps each
view's prompts in `PagePrompts`, keyed by view, so a dialog, find bar or permission request stays with its tab while
another tab shows.

- Every request carries an id (`XenonPromptId`) that its answer repeats, and the engine drops an answer whose id isn't
  the one it waits for, so an answer to a dialog the page has since dismissed never reaches the next one.
- **JavaScript dialogs** (alert, confirm, prompt, beforeunload, and the external-application confirmation) show as a
  sheet over the page, not the window, so the tab strip and toolbar still work. The engine sends the title (the
  page's origin, as the Qt chrome does), the message, the prompt's default text and the button labels (**Leave** and
  **Stay** for beforeunload). Enter accepts, Escape cancels. `on_request_accept_dialog`/`_dismiss_dialog` (WebDriver)
  and a browser-initiated navigation complete the dialog on the engine side and tell the UI with `dialog_closed`; a
  browser-initiated navigation also keeps further dialogs from showing until the next one commits, as `UI/Tab.cpp`
  does.
- `xenon_engine_request_close` closes a tab the way the Qt chrome's close button does: it asks the page first when it
  has a beforeunload handler, and a second request closes it anyway.
- **Context menus**: `show_context_menu` flattens LibWebView's `Menu` (skipping hidden actions and empty submenus,
  collapsing separators) into `XenonMenuItem`s, with `SUBMENU_BEGIN`/`_END` around submenus, and the UI picks an item
  with `xenon_engine_activate_menu_item`, which runs the `Action` on the engine thread. Where
  `rune::popup_menu_supported()` (macOS) the menu is native; elsewhere it's drawn in the window, modal, at the
  pointer, with submenus opening on hover.
- **`<select>`** uses the same menu items (option groups become a heading and indented options) and the same two
  presentations; `xenon_engine_close_select_dropdown` answers with the option's id or none.
- **File inputs** open Rune's file dialog (the `native-file-dialog` feature: the XDG desktop portal on Linux, rfd on
  macOS and Windows), filtered by the input's `accept`. Rune's dialog picks one file, so `multiple` inputs get one too.
  Headless runs answer it from a script (`{"type": "file-dialog", "path": ...}`).
- **Find in page**: <kbd>Ctrl</kbd><kbd>F</kbd> (<kbd>⌘</kbd><kbd>F</kbd>) opens a find bar at the page's top right.
  It searches as you type (case-insensitively, as the Qt bar does by default); Enter and Shift+Enter step through the
  matches, Escape closes it and ends the search.
- **Tooltips** (`title` attributes) show beside the pointer, which the engine reports from the last mouse event.
- **Fullscreen**: Rune can't make its window fullscreen yet, so a page's fullscreen request hides the tab strip and
  toolbar and gives the page the whole window. Escape, or switching tabs, leaves it.
- **Permission prompts** (`on_request_permission`) show as a bubble at the page's top left with **Allow**, **Allow this
  time** and **Block**; a request the engine withdraws (`on_cancel_permission_prompt`) or a crash takes it away.
- A crash drops every prompt of the view without answering it, as `UI/Tab.cpp` does.
- The mock engine (`mock_page.rs`) answers URLs containing `alert`, `confirm`, `prompt`, `permission`, `tooltip` or
  `fullscreen` with that prompt, a right click with a context menu, and a click on a `select` or `file` page with a
  dropdown or a file picker, and prints every answer the UI sends (`mock page: ...`).
- Pages follow the system's light or dark appearance: the UI reports it with `xenon_engine_set_system_dark`, and the
  engine gives every view a system theme with `IsDark` set, as the Qt frontend's `update_palette` does.
- On macOS, frames are copied from their IOSurface under its read lock, and pages' copy and paste use the general
  pasteboard (`Bridge/ClipboardMac.mm`).
- Not yet: colour and date pickers, find's highlight-all option, filling the find bar from the selection, and a tooltip
  delay.

## Page accessibility

Pages reach screen readers (VoiceOver, and AT-SPI and UI Automation through the same code) through their own slice of
the ABI in `Rune/Bridge/XenonBridgeAccessibility.h` (`XenonBridgeAccessibility.cpp`, hooked into each view by
`install_accessibility` from `View.cpp`; `ffi_accessibility.rs`, `accessibility.rs` and `view/page_accessibility.rs` on
the Rust side). The UI registers `XenonAccessibilityCallbacks` with `xenon_engine_set_accessibility_callbacks` before
`xenon_engine_start`.

- Nothing is built until an assistive technology runs, as in the Qt frontend: each `poll` (every two seconds at least)
  asks whether one does and tells the engine with `xenon_engine_set_accessibility_enabled`. On Linux that's the
  question Qt and Gecko ask of AT-SPI: whether the accessibility bus's `org.a11y.Status` reports `IsEnabled` or
  `ScreenReaderEnabled` (`at_spi.rs`, reading both properties over the session bus with zbus, the D-Bus crate Rune's
  AccessKit adapter already uses, on a thread of its own every two seconds, so the UI thread never waits on D-Bus).
  On macOS it's `xenon_accessibility_assistive_technology_running()` (VoiceOver or Switch Control); Windows can't tell
  yet. `XENON_ACCESSIBILITY=1` turns it on regardless, for headless runs. Turning it off drops every tree.
- While enabled, the engine asks each view's WebContent for its tree (`ViewImplementation::request_accessibility_tree`),
  again when a page finishes loading and when the zoom changes. WebContent then pushes a new tree itself after DOM
  mutations, focus changes and actions (200 ms apart at most). A page's scrolling isn't a mutation, so the UI calls
  `xenon_engine_request_accessibility_tree` after each wheel or key it gives the page, and the engine coalesces those
  into one request 150 ms later.
- `tree_changed` carries the whole tree as `XenonAccessibilityNode`s (LibWebView's `AccessibilityNodeData`: role, name,
  description, value, live region, key binding, bounds in CSS pixels from the viewport's top-left, flags for focused,
  disabled, selected, editable, multi-line, password, read-only, required, invalid, checked/pressed and expanded states,
  heading or outline level, numeric value and range, caret and selection) plus the root, the focused node and the
  zoom. `focus_changed` reports focus moves as WebContent sends them.
- Rune builds the OS accessibility tree only from its own elements, and has no way to host a foreign subtree. So the UI
  mirrors the page's tree as invisible elements: `page_accessibility_layer` makes one element per node, keyed `ax<id>`,
  with the node's role (LibWebView's ARIA role names map onto Rune's, `text leaf` onto `text`, anything Rune doesn't
  know onto `group`), name, description, value, states and level, pinned at the node's bounds times the zoom, so their
  boxes are the page's in window points. Unnamed `generic` nodes that aren't live regions, blank text and `none` are
  left out and their children join their parent, as the Qt bridges do. Paragraphs stay, named or not, for the reason the Qt AT-SPI bridge
  keeps them: Orca reads a paragraph as one text block in Say All. The layer sits under the page's `document` element (`page`),
  behind the surface that draws the page (`page-surface`), and clips to it, so pointer input still reaches the page.
  It's built inside a `memo`, again only when a tree or a field changes.
- Actions go through the handlers Rune runs for an assistive client: a press (VoiceOver's VO-Space) on a button, link,
  checkbox, radio, switch, tab, menu item, option, tree item, combobox or anything expandable sends
  `XENON_ACCESSIBILITY_ACTION_PRESS` (`press`). Text fields are mirrored as transparent Rune `text_input`s, since only
  Rune's editors are text fields to the OS: they get the text field role, the field's value, caret and selection, and
  setting the value (VoiceOver, Voice Control, dictation) edits the hidden field, which the UI sends on with
  `xenon_engine_accessibility_set_value` (WebContent's `insert_text` over the whole value, so the page sees an `input`
  event). Focusing or pressing one sends `XENON_ACCESSIBILITY_ACTION_FOCUS` and gives the page the keyboard. Keys still
  go to the page, since the page element's `on_key` hears them first.
- The page's focus reaches the reader as the page element's active descendant (`El::active_descendant`), which
  AccessKit reports as the focused element while the page has the keyboard. A focus left on a hidden field when the page
  moves its focus elsewhere is handed back to the page element: the field's key changes (`ax<id>.<n>`) and its
  `focus_return("page")` moves Rune's focus there.
- Live regions are mirrored as `El::live_region`, so AccessKit announces their changes.
- WebContent puts a `<details>` element's open state on the details node (a `group`), where readers expect it on the
  `<summary>` button that toggles it. `PageTree::apply` hands it over: a `group` with an expanded state whose first
  child is a `button` loses the state, and the button gets it, so pressing, expanding or collapsing the summary is one
  `press` on it.
- `xenon-rune` with `XENON_ACCESSIBILITY=1` and Rune's `--tree` shows the mirror on a real page without a desktop
  session: every page node appears under the `page` document with its role, name, value, states and bounds, the
  summary's state flips when it's clicked, and the page's `activeDescendant` follows Tab.
- The mock engine (`mock_accessibility.rs`) answers with a small tree (a heading, the page's link, text inside an
  unnamed box, a search field, a checkbox and a button) and prints each action it gets (`mock accessibility: ...`).
- `TestXenonRuneAccessibility` drives the ABI alone: it enables accessibility, checks a page's roles, names, values,
  states and bounds, focuses a field, sets its value (the page's `input` handler sees it), presses a button and checks a
  navigation brings a new tree. The `page_accessibility` unit tests render the mirror in a Rune `Runtime` and check its
  accessibility tree and actions.
- Not yet: incrementing sliders and spin buttons (the engine has no such action), scroll into view (Rune only scrolls
  its own scroll views for it), links' URLs and tables' row and column counts (Rune elements have neither), text markers
  and per-character geometry for reading by line or word (the Qt macOS bridge's `AXTextMarker`s), a selection a reader
  sets in a field (it moves the hidden field's selection but not the page's), and telling whether an assistive
  technology runs on Windows.

The chrome's own elements carry roles and names too: tabs report `selected` rather than `checked`; the downloads and
content blocking buttons, the site information button and the sidebar toggle report whether what they open is
`expanded`; the content blocking button describes how many requests it blocked; the toolbar is named `Navigation`;
and a crashed tab's notice is an `alert`. `view/chrome_accessibility_tests.rs` renders the window in each layout and
checks that every control a reader can press has a name, that the tabs are a tab list with the active one selected,
and that the page is a `document` named after its tab.

## Where Rune falls short

- Rune has no secondary-button press: `on_secondary_press_local` fires on release, so a right click reaches the page as
  a down and an up together, and a right drag doesn't reach it at all.
- Primary, middle, back and forward presses come from `on_drag`, so there is no triple click.
- Rune has no wait, progress, help, diagonal resize or zoom cursors; those show the default cursor.
- Input methods show no composition yet, and the candidate window sits at the page's corner rather than at the caret.
- Accessibility: Rune can't host an external accessibility subtree (`Runtime::a11y_walk` builds AccessKit nodes only from
  elements, and the AccessKit adapter is private), has no programmatic focus for elements other than text fields and no
  focus-change callback, routes a reader's actions only to element handlers (`on_press`, `on_step`, a text field's
  editor, its own scroll views), and has no public way to tell whether an assistive client is listening. Hence the
  mirror of invisible elements, the active descendant for the page's focus, hidden text fields for values, and the
  platform checks (AT-SPI's status on Linux, VoiceOver on macOS).

## Building

`Meta/xenon.py build` builds `xenon-rune` beside `xenon`, as `Build/release/xenon/bin/xenon-rune` on Linux, sharing the
Qt build's helper processes and resources. `ninja -C Build/release xenon-rune` rebuilds it alone: CMake runs cargo for
`Rune/ui` with `-Dwarnings`, into `Build/release/cargo/xenon_ui`, then links the static library into the executable
with LibWebView. `Rune.cmake` builds the crate with Ladybird's cargo helpers rather than `import_rust_crate`, which
expects every crate to bring Ladybird's panic and demangling hooks. `Rune/ui/Cargo.lock` is checked in, and the build
stops without it.

`TestXenonRuneBridge` drives the engine through the C ABI alone, with no Rust: it loads a page and checks the colour of
the frame the UI would get. `TestXenonRunePage` does the same for the page ABI: it answers a confirm and a prompt,
picks a `<select>` option, opens a context menu and counts find matches. `TestXenonRuneAccessibility` does it for the
accessibility ABI. All three are part of `xenon-tests`.

The UI also builds on its own, against a mock engine on a thread of its own, for Rune's headless tools:

```bash
cargo +1.98.0 test --manifest-path Rune/ui/Cargo.toml --features mock-engine
cargo +1.98.0 build --manifest-path Rune/ui/Cargo.toml --features mock-engine
Rune/ui/target/debug/xenon-ui-preview --snap window.png --size 1280x800 --theme dark --at 0.5
Rune/ui/target/debug/xenon-ui-preview --script two-tabs.json --snap end.png --at mark:end --tree tree.json
```

The mock answers each load with a URL, a title, a favicon and a drawn page. It keeps windows and tabs as the engine's model does (`mock_model.rs`), opening one window with the new tab page at start. A URL with `slow` in it keeps loading, and
the coloured bar on its page is a link. `--snap` without `--at` captures the first frame, before the mock has answered.

Rune pins a revision of its `dev` branch, in `Rune/ui/Cargo.toml`.

`XENON_RUNE_ARGS` hands Rune's own flags to `xenon-rune`, so the real engine can be rendered and scripted headless, with
no display. Rune's headless clock is virtual and runs ahead of the engine, so give pages time with a late `--at` (each
virtual second takes a few real milliseconds):

```bash
XENON_RUNE_ARGS="--script load.json --snap page.png --size 1280x800 --at 600" Build/release/xenon/bin/xenon-rune
```
