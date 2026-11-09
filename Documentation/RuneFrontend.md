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
| `Patches/rune/` | Xenon's changes to Rune, applied to a prepared checkout. |
| `Meta/rune.py` | Prepares the patched Rune checkout under `Build/rune` for the build. |

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
- What a page asks of the UI goes through the page ABI below, and through the slices for prompts, media, screenshots,
  drops, reports and the updater ("Pickers, media and reports").

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
  and runs when one is picked; commands only the UI can do come back through `ui_command`. The macOS menu bar is
  AppKit's.
- Chrome menus (the window, tab, main and back/forward history menus) go through `chrome_popup.rs`. Where
  `rune::popup_menu_supported()` (macOS) they are native AppKit menus. Elsewhere, and on macOS with
  `XENON_RUNE_DRAWN_MENUS=1`, the window draws them with the renderer page context menus use
  (`view/page_overlays/menu.rs`): modal, below the button that opened them or at the right-clicked point, submenus open
  on hover, and Escape, a click outside or the window losing focus closes them. Its builder (`chrome_popup::PopupMenu`,
  `PopupItem`) has the shape of Rune's, items keep their string ids, and engine menus use their numeric ids as strings.
  A drawn item's element key is `chrome-menu-item-<id>`, so a headless script picks one with
  `{"type": "click", "key": "chrome-menu-item-new-tab"}`, where macOS takes `{"type": "menu-pick", "id": "new-tab"}`.
- Layouts: Classic, Compact (toolbar and tabs in one row) and Vertical (a sidebar that collapses to icons), read from and
  written to `XenonSettings::chrome_layout`, which the engine reports through `chrome_settings_changed`.
- Resting on a tab shows a hover card with its last frame, address and memory use (`Bridge/TabMemory.cpp`, a Qt-free
  port of `UI/TabMemory.cpp`). A tab playing audio has a mute button.
- The toolbar has the downloads button and popover (`view/downloads.rs`, from a `FileDownloaderObserver`), the content
  blocking shield and popover (`view/shield.rs`), a Private badge in private windows, and the Update pill when an update
  is ready ("Pickers, media and reports").
- Full screen and Always on Top act on the key AppKit window through `Bridge/Platform.h`. Elsewhere Always on Top goes
  through Rune's `WindowHandle::set_always_on_top` (X11 and Windows; Wayland lets no app do it), and full screen does
  nothing: Rune has no full-screen call and keeps winit's window to itself.
- Each model window gets a Rune window of its own; see [Windows](#windows).

## The chrome's look

The chrome follows the Qt frontend's Classic layout, from `UI/ChromeStyle.cpp`, `UI/TabBar.cpp` and `UI/Tab.cpp`, so
the two can be compared side by side. The colours are the ones Qt arrives at when the system palette doesn't match the
colour scheme (`view/palette.rs`); Rune doesn't read the desktop's palette.

- **Tab strip** (`view/tabs.rs`): 42 points high. A tab is a 28 point pill, 232 points wide with its 5 point gap, down
  to 40, and 40 when pinned. The active tab takes the toolbar's colour, keeps only its top corners round, and reaches
  the toolbar with a 19 point flare on each side (`TAB_FOOT_LEFT` and `TAB_FOOT_RIGHT` in `view/icons.rs`). Titles are
  one weight; an inactive tab's is slightly faded. A 1.5 by 20 point separator stands between two inactive tabs and
  after the last one, and the new tab button is a 28 point circle. An unfocused window's strip moves 40% toward the
  toolbar's colour. The accent line the active tab used to carry is gone, as Qt has none.
- **Toolbar** (`view/toolbar.rs`, `view/location/field.rs`): 43 points high with its hairline, 12 points of padding,
  34 point round buttons with 20 point icons, 2 points apart in the navigation group and 6 elsewhere, in Qt's order
  (navigation, split view, address, downloads, Private, extensions, content blocking, update, menu). The address field
  is a 34 point pill with a 1 point border, and a rose border and glow when focused.
- **Not matched**: the font is Rune's, not the desktop's, and tab hover and separator fades are not animated.

### The window frame

Off macOS the window has no system title bar (`WindowOptions::borderless`), as the Qt frontend's doesn't, and draws
its own (`view/window_controls.rs`):

- Minimize, maximize or restore, and close sit at the end of the tab strip, each 40 by 40 points, and at the end of
  the one row the Compact and Vertical layouts have. Close goes through
  `request_close_shown_window`, so pages are asked first, as from the keyboard. They act through the Rune patch's
  `WindowHandle` calls.
- The strip and the toolbar drag the window, and a double click on them maximizes it (Rune's `window_drag`).
- A 1 point outline surrounds the window and the content is inset by it; a maximized window has neither.
- Eight invisible handles, 6 points along the edges and 12 at the corners, start a resize with
  `WindowHandle::drag_resize`. Rune has no diagonal resize cursors, so a corner shows the vertical one.
- The window has no shadow of its own.

## Windows
## Windows

Rune creates every window before `App::run` and can't add one afterwards (`App::window` only registers a window for the
loop to start; the pinned revision and upstream `dev` as of this writing have nothing newer). So `lib.rs` registers a
pool of `WINDOW_SLOTS` (12) Rune windows up front: the app's own window and eleven `App::window`s, each `windowless` with
`release_gpu_when_closed`, so a slot nobody uses has no native window or GPU surface. Every slot runs the same view with a
`Browser` state of its own (`window_slots.rs`, `window_state.rs`).

- `Slots` binds model windows to slots on the engine thread: `window_opened` takes the lowest free slot, posts
  `bind_window` to it and opens it; `window_closed` hides it; `window_activated` opens (raises) a slot that isn't
  focused. A model window past the twelfth waits, mirrored but not shown, and takes the first slot that frees up.
- Each slot's `Browser` keeps the whole mirror, as before, but in bound mode (`Windows::slotted`) it always shows its
  own window: activation never swaps it and closing it adopts no other. Model and tab events (`XenonUiCallbacks`) go to
  every slot, so a tab moved between windows arrives with its title, favicon and state; `UiContext::handle.update` takes
  an `Fn` closure for that. Page and location events go only to the slot that shows the view (`Slots::update_view`,
  `ViewRoute`), so a dialog, a permission prompt or a suggestion list is answered once. The main menu, `ui_command`, the
  downloads question and an approved quit go to the target slot: the focused window, else the model's active one.
- The feature bridges follow the same rules. Accessibility trees and focus go to the slot showing the view. Tab groups,
  hibernation states and settings, and task manager snapshots go to every slot (each keeps them per model window or
  shows them only while its panel is open), but a group editor opens only in the window whose group asked for it, and
  the engine's Open Task Manager action opens the panel in the target slot. The task manager keeps reporting while any
  window shows it.
- One Rune app owns the macOS menu bar, so its items run on the first slot's `Browser`; `run_shortcut` forwards them to
  the target slot. Keys typed in a window reach that window's own `Browser`.
- Focusing a window activates its model window (`activate_window`), so the session, new tabs from other apps and
  <kbd>Ctrl</kbd><kbd>`</kbd> follow the window in front.
- The close button closes the Rune window at once (Rune has no veto), and `on_close` asks the engine to close the model
  window with its checks; if a page keeps it open, the engine raises the window again. The request is posted to the
  slot's own queue, so when macOS quits the app (Dock, logout) and every `on_close` runs, the loop ends before any of
  them reaches the engine and the session keeps the windows open for the next launch.

### Window lifecycle

The engine side is `Bridge/WindowLifecycle.*`, with its own slice of the ABI in `Rune/Bridge/XenonBridgeWindows.h`
(`XenonBridgeWindows.cpp`; `ffi_windows.rs` and `windows_bridge.rs` on the Rust side). The UI registers
`XenonWindowCallbacks` with `xenon_engine_set_window_callbacks` before `xenon_engine_start`.

- **Closing** (`xenon_engine_request_close_window`, from the close button, <kbd>Ctrl</kbd><kbd>⇧</kbd><kbd>W</kbd> and
  File > Close Window) runs `UI/WindowCloseCoordinator` as `BrowserWindow` does: the active tab first, then the others,
  each loaded tab with a beforeunload handler activated and asked (`request_close_preflight`, which shows the page's
  **Leave**/**Stay** sheet), and the window brought to the front (`window_raise`) while it asks. **Stay** goes back to the
  tab that was active and leaves the window open. Closing the last window with downloads running asks first (below).
- **Quitting** (`xenon_engine_request_quit`, from <kbd>Ctrl</kbd><kbd>Q</kbd>, the Quit action, and on macOS the app
  menu's Quit item, which `PlatformWindowsMac.mm` points at the engine) asks every window the same way, the active one
  first, then the downloads; once all agree the engine sends `quit_approved` and the UI ends the loop, after which
  `xenon_engine_shutdown` tells the session store the app is quitting. A page that says **Stay** cancels the quit.
- **Downloads**: when unresumable downloads would be lost, `confirm_stop_downloads` asks the UI, which shows "Downloads
  are still in progress." with **Quit and Cancel Downloads** and **Cancel** over the target window and answers with
  `xenon_engine_answer_stop_downloads`. Resumable downloads are paused without asking, as in the Qt frontend.
- **Geometry**: the root element's `on_screen_layout` gives the window's content rect in screen points (Qt's
  `geometry()`), reported with `xenon_engine_report_window_geometry` and saved with `window_geometry_changed`. A maximized
  window reports the rect it had before, and nothing is reported in full screen. When a window closes, a non-private
  window's last geometry becomes `WebView::Settings::window_settings`. A window that opens gets `window_placement` before
  `window_opened`: its saved geometry when it's restored from the session or reopened (`BrowserModel::Window::geometry`),
  else the active window's geometry moved by 32 points, else the window settings. `Slots` applies it through Rune's
  `WindowHandle` (`set_position`, `resize_instant`) before the window shows, and maximizes it on macOS. Wayland lets
  no app place its windows, so there only the size applies.
- **Exposure**: on macOS `PlatformWindowsMac.mm` watches each slot's `NSWindow` (found by window number in `on_ready`).
  A minimized window hides its pages at once; a window covered for a second hides them too
  (`xenon_engine_set_window_exposed`), as `UI/WindowExposure.cpp` does. The engine keeps a hidden window's active tab
  hidden when its tab changes.
- **Page requests** (WebDriver's window rect commands, `on_reposition_window` and the rest) come back as
  `window_frame_requested`: move and resize through Rune's `WindowHandle`, minimize through `AppHandle::minimize`,
  maximize and restore through AppKit. Only WebDriver sends them: LibWeb's `window.moveTo`, `resizeTo` and friends
  return at their first step.
- **DevTools**: `Application::on_devtools_enabled`/`_disabled` send `devtools_changed`, and every window shows the Qt
  frontend's banner at its bottom ("DevTools is enabled on port …", **Open Client**, **Disable**).
- An inactive window's tab strip mixes 40% toward the toolbar colour (`Palette::for_window`), as `ChromeStyle` does.

### What Rune can't do yet

- Open a window after `App::run`: hence the fixed pool, and the twelve-window limit on what shows at once.
- Veto a close: the window disappears before the checks run, and comes back if a page keeps it.
- Report a window's position before it has drawn, or whether it's maximized, minimized or covered: geometry comes from a
  layout callback, and the rest from AppKit, so on Linux and Windows there is no maximized flag, no exposure tracking and
  no maximize or restore for pages.
- Full screen off macOS, and intercepting the OS's own quit (the Dock, logout), which closes every window unasked.
- Restored windows aren't moved back onto a screen that's still there, as the Qt frontend does.

Headless runs render one window unless they pass `--desktop BACKDROP.png`, which composites every visible window:

```bash
Rune/ui/target/debug/xenon-ui-preview --desktop backdrop.png --script windows.json --snap windows.png --size 2600x1000 --at mark:end
```

## Tab groups

Tab groups follow `UI/TabGroups.cpp`, `UI/TabGroupEditor.cpp` and the group handling in `UI/TabBar.cpp`. The groups live
in the engine's model (`BrowserModelTabGroups.cpp`, unit tested in `TestXenonBrowserModel`), and have their own slice of
the ABI in `Rune/Bridge/XenonBridgeTabGroups.h` (`XenonBridgeTabGroups.cpp`; `ffi_tab_groups.rs`, `tab_group_bridge.rs`
and `tab_groups.rs` on the Rust side, `view/tab_groups.rs` for the chips, headers and editor). The UI registers
`XenonTabGroupCallbacks` with `xenon_engine_set_tab_group_callbacks` before `xenon_engine_start`.

- A model tab has an optional group id; a window keeps its groups (`TabGroup`: id, title, one of nine colours,
  collapsed, and the tabs it spans) in tab order. After every change the model keeps each group's tabs together as Qt's
  `keep_groups_together` does: a group keeps its longest run of unpinned tabs, and the rest leave it. Pinned tabs are
  never grouped. A new group takes the colour fewest groups in the window use.
- The rules are Qt's: a tab inserted between two tabs of a group joins it, and so does a tab a grouped tab opens
  (`window.open`, links, Duplicate); moving a tab between two members joins their group, and moving one away from its
  group leaves it, unless it's the group's only tab, which carries its group along. Adding a tab to a group moves it
  beside the group; taking a tab out of the middle moves it past the group's end. Pinning takes a tab out.
- Collapsing the group of the active tab activates the next shown tab after the group, else before it, else a new tab
  page. Activating a tab of a collapsed group opens the group. Closing the last shown tab opens the nearest group. The
  tab that takes over from a closing tab is a shown one where there is one, and Ctrl+Tab (in either order) passes over
  collapsed groups.
- Close Group asks each tab to close as its close button does (beforeunload prompts included), after opening a new tab
  if the group held every tab or collapsing it if it held the active one.
- Move Group to New Window opens a model window (same privacy), moves the group's tabs into it with their title and
  colour, and activates it. With one Rune window per process the window shows that new window, and the old one waits
  behind it (<kbd>Ctrl</kbd><kbd>`</kbd> goes back). It's offered unless the group holds every tab of its window.
- The model reports a window's groups as a whole with `groups_changed` (each group with its id, title, colour, collapsed
  state and tabs, in order) whenever they differ from what it last reported, after the tab events of the same change.
  Groups change together (one move can take several tabs out of a group), so the UI replaces a window's groups rather
  than patching them. `edit_requested` follows Add Tab to New Group, and the UI opens the editor with its title
  selected.
- Groups are saved in the profile's `TabGroups.json` as the Qt frontend saves them (shared format: `"window<session
  window id>"` to a list of `{title, color, collapsed, tabs: [session tab ids]}`), written on every change of a
  persistent, non-private window, and read back when the session restores a window. As in Qt, the first window to open
  schedules a pass that drops the entries of windows that didn't come back (after the startup windows are restored). Tabs
  other than pinned ones only come back with **Restore previous session** on, so without it the groups don't either.
- The tab menu has **Add Tab to New Group**, or **Add Tab to Group** (New Group and every other group, named by title or
  by its first tab) when other groups exist, and **Remove from Group** for a grouped tab. Native menus can't tint their
  images, so a group's item shows a plain dot rather than its colour.
- The strip draws a chip (the title on the group's colour, or a dot for an untitled group) before the group's tabs and a
  line in its colour under them; the sidebar draws a header row with the chip and a chevron above the group's tabs,
  which are indented with a line beside them (a dot alone in the collapsed sidebar). Clicking the chip or header
  collapses or opens the group; right-clicking it opens the editor: the title field ("Name this group", edited live,
  Enter or Escape closes), the colour swatches, New Tab in Group, Move Group to New Window, Close Group and Ungroup.
- Dragging a tab follows `TabBar::drop_slot_at` and `update_drop_group`: the tab goes to the slot nearest its centre (a
  collapsed group is one slot, its chip), between two members it joins their group, and at a group's edge it joins
  when it's a quarter of a tab past the gap's middle toward the group (or anywhere on the group's side for its own
  group). The tab views report their rects (`on_layout`), and a drag takes a snapshot of them and of the groups when it
  starts and measures against that, as Qt measures against the layout from before the drop: the tab moves while it's
  dragged, and the rects of the new order only come after the next layout, so measuring against them would mix the new
  order with old places. Each new target is one `xenon_engine_drop_tab` (the move, then Qt's `set_group_after_drop`),
  and a cancelled drag puts the tab back.
- The chip's tooltip is Qt's `tab_group_tooltip` ("Work (3 tabs)"), through `El::help`.
- Not yet: Qt's animated drop preview (tabs move as the drag passes
  them, as before), and dragging a group between windows.

## Tab hibernation and the task manager

Both have their own slices of the ABI: `Rune/Bridge/XenonBridgeHibernation.h` (`TabHibernation.cpp`,
`XenonBridgeHibernation.cpp`; `ffi_hibernation.rs` and `hibernation.rs`) and `Rune/Bridge/XenonBridgeTaskManager.h`
(`XenonBridgeTaskManager.cpp`; `ffi_task_manager.rs`, `task_manager.rs` and `view/task_manager.rs`). The UI registers
their callbacks with `xenon_engine_set_hibernation_callbacks` and `xenon_engine_set_task_manager_callbacks` before
`xenon_engine_start`.

- `Xenon::TabHibernation` is a Qt-free port of `UI/TabHibernation.cpp`, owned by the engine. Its timer checks every tab
  (a quarter of the timeout, between 1 and 30 seconds, at most 5 seconds when **Hibernate on memory pressure** is on)
  and hibernates a background tab once it has gone unused for `XenonSettings::tab_hibernation_timeout`, and, under
  memory pressure (`UI/MemoryPressure.cpp`), the least recently used tabs: one at the low level, all of them at the
  critical one. On Linux the level comes from `/proc/meminfo` (MemAvailable against MemTotal) and the `some avg10` line
  of `/proc/pressure/memory`; on macOS from the VM statistics. `XENON_TEST_MEMINFO` names a file to read in place of
  `/proc/meminfo` (and skips the pressure stall line). `MemoryPressureRelease` asks WebContent to drop what it doesn't
  use, as in Qt. Settings changed anywhere, about:settings included, apply at once through `XenonSettings::Observer`.
- A tab may hibernate under Qt's rules: it is loaded, not any window's active tab or shown beside it in a split view
  (`SplitViews::is_shown`), not pinned or loading, not playing audio and hasn't for a minute, has no developer
  tools or picture-in-picture window, and has no dialog, picker, permission prompt, pending close or page fullscreen
  open (`View::can_hibernate`). The engine asks the page with `prepare_for_discard`, keeps the tab if it has unsubmitted
  input, and checks the rules again before it hibernates.
- Hibernating is `BrowserModel::hibernate_tab`: the tab becomes deferred again, with the view's session history as its
  pending history, and the engine closes its view. Selecting the tab creates a new view with the same id, which
  restores that history, as a tab restored from the session loads when first selected.
- The engine tells the UI each tab's load state with `tab_load_state_changed`: not loaded (a deferred tab from the
  session), hibernated (with the favicon in greyscale, `hibernated_tab_icon`) or loaded again. The UI swaps the tab's
  favicon for the grey one until the tab loads, and the hover card shows no thumbnail for a tab that isn't loaded and
  says "Hibernated, using no memory (freed …)" or "Not loaded yet, using no memory", as `UI/TabMemory.cpp` does.
- The tab menu's **Hibernate Tab** is enabled by what the UI knows (not the active tab, not pinned, loading or playing
  audio, loaded, no dialog or permission prompt); the engine checks the rest before it acts. The window menu's **Tab
  Hibernation** submenu offers Off and the timeout choices, with the current timeout as a disabled item of its own when
  it isn't one of them (`settings_changed` carries the choices).
- The task manager is a port of `UI/ProcessManagerWindow.cpp`. Rune has one window, so it shows as a modal panel over
  the window rather than a window of its own: **Inspect ▸ Open Task Manager** in the main menu (the engine adds the
  action, as the Qt `Application` does), **Window ▸ Task Manager** in the macOS menu bar, or
  <kbd>Ctrl</kbd><kbd>⇧</kbd><kbd>M</kbd> (<kbd>⌘</kbd><kbd>⇧</kbd><kbd>M</kbd>). While it shows, the engine reports
  `process_snapshots` every second, with each process named and labelled by site as in Qt, and each page's favicon sent
  once per showing. The panel lists the processes under the browser process, sorts by a column header (PID first),
  filters by name or PID, and ends the selected process (not the browser) with **End Process** or a row's context
  menu, which also copies the PID. Escape, the close button or a click outside closes it.

## Split view

Two tabs of a window side by side, as `UI/SplitView.cpp` has them (see [Features.md](Features.md#split-view)). The engine
owns which tabs are split, as it owns the tabs: `Rune/Bridge/SplitViews.cpp` is a Qt-free port of the Qt `TabStack`'s
rules, unit tested by `TestXenonSplitViews`, and `SplitViewHost` (in `XenonBridgeSplitView.cpp`) ties it to the engine.
The ABI is its own slice, `Rune/Bridge/XenonBridgeSplitView.h`, registered with `xenon_engine_set_split_view_callbacks`
before `xenon_engine_start`; the Rust side is `ffi_split_view.rs`, `split_view.rs` (state, geometry and actions, unit
tested) and `view/split_view.rs`.

- A split is a pair of adjacent tabs with the same pinning, left then right. After every tab event the engine checks
  its splits on the next turn of the event loop, as `TabStack::validate_splits` does: a pair that's no longer adjacent
  (a tab opened between them, a tab moved to another window), that a close took a tab from, or whose pinning differs
  is dropped, and a pair whose tabs swapped places swaps sides. Closing either tab leaves the other a normal tab.
- A move the UI asks for (`xenon_engine_move_tab`, <kbd>Ctrl</kbd><kbd>⇧</kbd><kbd>PgUp</kbd>, or
  `xenon_engine_drop_tab`, dragging a tab) keeps splits together afterwards, as `BrowserWindow::move_tab` does: the
  partner follows its tab, and a tab that lands between a split moves past it.
- The UI asks with `xenon_engine_split_view_open_new_tab` (a new tab page beside a tab, active; **Add Tab to New Split
  View**, <kbd>⌘</kbd><kbd>⌥</kbd><kbd>N</kbd>/<kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>N</kbd>), `_create` (**New Split View
  with Current Tab**), `_reverse`, `_separate` and `_drop_tab`. The engine answers each change with `splits_changed`,
  every split of every window, which goes to every window's `Browser` (`Slots::update`); each mirrors the list in its
  `SplitViews` and shows only the pairs among its own tabs. The divider's ratio stays on the UI side, keyed by the pair,
  and flips when the engine swaps a pair's sides.
- The engine shows each window's active tab and its partner (`Engine::sync_shown_tabs`), creating a restored or
  hibernated tab's view when it's first shown, and hides what it showed before. A window that isn't exposed
  (`WindowLifecycle::set_exposed`) hides both. Engine code asks whether a view is on screen, its window's active tab or
  that tab's partner, with `Engine::split_views()->splits().is_shown(view)`; `TabHibernation::is_in_use` spares those
  from hibernation, as Qt's does, and the UI's **Hibernate Tab** is disabled for them. A hibernated tab's view comes
  back at the window's size, so the UI forgets the viewport it last sent that tab and sends its pane's size again once
  it's shown in a split.
- The UI lays the two pages out inside the page area, inset 5 points (4 beside the sidebar) with a 12 point gap, and
  sends each view its own viewport; a split view's viewport is never taken as the window's size for new views.
- Mouse events go to the page under the pointer, and a press on the other page activates its tab first; keys, the
  input method, the toolbar and the location bar follow the active tab.
- **Open Link in Split View** (**Open Link in Right/Left View** from a split page) is added to the link context menu
  on the engine side (`SplitViewHost::context_menu_for`): it runs the menu's **Open Link in New Tab** and splits the
  page with the tab that opens. **New Split View** is in the main menu (`AppMenus.cpp`) and the menu bar's File menu.
- Holding a dragged tab on the left or right edge of the page area (a fifth of its width, at least 64 points) for half
  a second arms a drop there, shown as a dashed "Create split view" card over that half; letting go asks
  `_drop_tab`. While the pointer is over the page, the tab strip stops reordering. The setting
  `split_view_drag_and_drop` turns it off; the engine reports it with `drag_and_drop_changed`.
- The inactive page's corner has a strip with its favicon and host (or title), which activates it, and a menu
  (**Close This View**, **Reverse Views**, **Close Left/Right View**, **Separate Views**). The split button left of the
  address bar has the same menu without the first item, and the tab menu the items `SplitView.cpp` shows. They open
  through `chrome_popup`, so on Linux they're drawn in the window; a script picks one with
  `{"type": "click", "key": "chrome-menu-item-split-reverse"}` (`split-add-to-new`, `split-with-current`,
  `split-close-left`, `split-close-right`, `split-close-this`, `split-separate`).
- A page's fullscreen shows only that page; the other stays shown to the engine.
- The mock engine keeps splits without moving tabs (`mock_split_view.rs`), enough to preview the layout.
- Not yet: split views in the session (the Qt frontend doesn't restore them either), and the bookmarks bar offset
  `layout_pages` gives the inactive page.

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
- The bar shows the page's zoom when it isn't 100% (a click resets it) and a star that bookmarks the page, through the
  bookmark dialog below.
- Switching tabs parks the bar's editing session, as the Qt bar's `park_editing_session` does: the text, the caret and
  selection, the inline completion and the suggestions stay with the tab (`ParkedSession` in `location.rs`), the engine's
  `Omnibox` is suspended (`xenon_engine_location_set_suspended`) so late suggestions don't touch it, and coming back to the
  tab gives the field its focus and all of that back. A navigation in a parked tab restarts its session on the new
  address. Leaving a tab while editing hands the keyboard to the next tab's page, or to its location bar when its
  address is hidden (the new tab page).
- A right click on the field opens the Qt bar's context menu (`location_menu.rs`, `Bridge/XenonBridgeLocationMenu.h`):
  Undo, Redo, Cut, Copy, Paste, **Paste and Go** / **Paste and Search** (labelled by
  `Omnibox::text_for_paste_and_go_action`, which the engine answers with `paste_and_go_described`), Delete and Select
  All, opened through `chrome_popup` like the other chrome menus (so it's drawn in the window on Linux, and has neither
  the Qt menu's arrow or search icon nor its shortcut hints). Paste and Go puts the text in the field and runs
  `Omnibox::navigate_directly_to_query` on the engine thread. While the menu is open the field keeps its editing session,
  suspended as the Qt bar's is when a popup borrows its focus, and gets the focus back when the menu closes. The
  clipboard is read through Rune's `Tx::read_clipboard` (`location_menu::read_clipboard_text`), which reads the general
  pasteboard on macOS and the X11 or Wayland clipboard on Linux off the UI thread. Headless Rune runs never read the
  system clipboard: they take the text from the `XENON_RUNE_CLIPBOARD` environment variable instead.

## Bookmarks

The bookmarks bar, its menus and the bookmark dialogs have their own slice of the ABI in
`Rune/Bridge/XenonBridgeBookmarks.h` (`XenonBridgeBookmarks.cpp`, with `Bookmarks.h` for `Application`'s overrides;
`ffi_bookmarks.rs` and `bookmarks.rs` on the Rust side, `view/bookmarks.rs` for the bar and the dialogs). The UI registers
`XenonBookmarksCallbacks` with `xenon_engine_set_bookmarks_callbacks` and asks for the bar with
`xenon_engine_bookmarks_start`, which runs once the engine accepts commands.

- The engine sends the whole bookmark tree (`bookmarks_changed`: bookmarks with their title, URL and favicon PNG, and
  folders as `FOLDER_BEGIN`/`FOLDER_END` around their children) at start and whenever the `BookmarkStore` changes, and
  the bar's settings (`bar_settings_changed`: LibWebView's **Show Bookmarks Bar** choice, and the hover delay the Qt
  frontend shares with the vertical tabs) whenever the appearance settings change. Every window's `Browser` gets both;
  a context menu goes to the window that shows its view, and a dialog to the target window.
- **Always** puts the bar under the toolbar in every layout (Classic, Compact and Vertical), **Never** hides it, and
  **Only When Hovering the Navigation Toolbar** lays it over the top of the page once the pointer has rested on the toolbar
  for the hover delay, without resizing the page, and takes it away a quarter of a second after the pointer leaves both
  (not while one of its menus is open, nor while the location bar's suggestions show). It wipes in from the top unless
  the hover animation's duration is set to 0, and leaves at once, as the Qt bar does; the wipe runs on Rune's transition
  spring rather than the configured duration.
- Bookmarks show their favicon and title, at most 150 points wide, as `UI/BookmarksBar.cpp` lays them out. A click opens
  one in the current tab; a middle click or <kbd>⌘</kbd>-click in a new tab (with <kbd>⇧</kbd>, in the background). A
  folder opens a menu of its children (`chrome_popup`: native on macOS, drawn in the window elsewhere), with submenus for
  folders and **Open All in Tabs** at the end, as
  LibWebView's bookmarks menu has. What doesn't fit goes behind a » button at the bar's end, as Qt's toolbar extension
  does.
- A right click on a bookmark, a folder or the bar's empty space asks the engine for LibWebView's bookmark, folder or bar
  context menu (`xenon_engine_show_bookmark_context_menu`); the engine notes the item for
  `bookmark_item_id_for_context_menu` and sends the flattened menu back. `show_bookmark_context_menu`, which
  about:bookmarks calls, sends the same menus at the page's position, offset by its pane when the page is in a split
  view.
- In a split view the bar runs across both pages, above them, so both start below it. The Qt frontend gets the same
  line-up by dropping the inactive page below the bar that sits in the active one (`TabStack::layout_pages`).
- `display_add_bookmark_dialog`, `display_edit_bookmark_dialog`, `display_add_bookmark_folder_dialog` and
  `display_edit_bookmark_folder_dialog` open an in-window dialog matching `UI/BookmarkDialog.cpp`: URL (bookmarks only),
  title, and a folder picker listing **Bookmarks Bar** and every folder by its path (a folder being edited and its
  descendants left out). OK resolves the promise as the Qt dialog does (an address `sanitize_url` rejects rejects it);
  Cancel or Escape rejects it, and a new dialog cancels one still open.
- While one of the bar's menus is open the hover bar stays, so `chrome_popup::open_tracked` tells the bar when a menu
  closes without a pick.
- Not yet: favicons in the folder menus (neither Rune's native menus nor the drawn ones take images), opening a bookmark
  from inside a folder menu in a new tab, and right-clicking an item inside a folder menu (the menus report only the item
  picked).

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
  macOS and Windows), filtered by the input's `accept`. Rune's dialog picks one file, so `multiple` inputs get one too;
  several files reach one by dropping them on it. Headless runs answer it from a script
  (`{"type": "file-dialog", "path": ...}`).
- **Find in page**: <kbd>Ctrl</kbd><kbd>F</kbd> (<kbd>⌘</kbd><kbd>F</kbd>) opens a find bar at the page's top right.
  It searches as you type (case-insensitively, as the Qt bar does by default); Enter and Shift+Enter step through the
  matches, Escape closes it and ends the search. **Highlight All** and **Match Case** (<kbd>Alt</kbd><kbd>A</kbd> and
  <kbd>Alt</kbd><kbd>C</kbd> off macOS, as the Qt bar's mnemonics) search again with the option. Opening the bar asks the
  engine for the page's selection (`xenon_engine_request_find_selection`) and fills the field with it, on one line,
  unless the field changed in the meantime. The result reads "Phrase not found" or "2 of 5 matches", as in Qt.
- **Tooltips** (`title` attributes) show at the pointer, in the pane under it, once it has rested for 0.6 seconds (`TOOLTIP_DELAY`, Qt's
  hover timer), and moving hides them until it rests again. A tooltip a page sets itself (`on_request_tooltip_override`,
  for example an extension's) shows at once where the page put it, and stays until the page takes it away.
- **Fullscreen**: Rune can't make its window fullscreen yet, so a page's fullscreen request hides the tab strip and
  toolbar and gives the page the whole window. Escape, or switching tabs, leaves it.
- **Permission prompts** (`on_request_permission`) show as a bubble at the page's top left with **Allow**, **Allow this
  time** and **Block**; a request the engine withdraws (`on_cancel_permission_prompt`) or a crash takes it away.
- **Block Element** (the shield's element picker) asks before adding its filter, as `UI/ElementPicker.cpp` does: the
  engine sends the filter through `Bridge/XenonBridgeElementPicker.h`, the UI shows an in-window dialog where it can be
  edited, and OK adds the edited filter to the custom filters (`xenon_engine_confirm_element_filter`). While the picker
  runs, the view hands the pointer to LibWebView's node picker instead of the page, as `UI/WebContentView.cpp` does:
  moving highlights, a primary press picks (with <kbd>Ctrl</kbd>, previews), leaving clears the highlight and Escape
  cancels.
- A crash drops every prompt of the view without answering it, as `UI/Tab.cpp` does.
- The mock engine (`mock_page.rs`) answers URLs containing `alert`, `confirm`, `prompt`, `permission`, `tooltip` or
  `fullscreen` with that prompt, a right click with a context menu, and a click on a `select` or `file` page with a
  dropdown or a file picker, and prints every answer the UI sends (`mock page: ...`).
- Pages follow the system's light or dark appearance: the UI reports it with `xenon_engine_set_system_dark`, and the
  engine gives every view a system theme with `IsDark` set, as the Qt frontend's `update_palette` does.
- On macOS, frames are copied from their IOSurface under its read lock, and pages' copy and paste use the general
  pasteboard (`Bridge/ClipboardMac.mm`).

## Pickers, media and reports

What the Qt chrome draws for a page beyond its dialogs and menus comes over in slices of the ABI of their own, each
with its callbacks struct, registered from `lib.rs` before `xenon_engine_start` as the page callbacks are:

| Header | Engine side | UI side |
| --- | --- | --- |
| `XenonBridgePrompts.h` | `XenonBridgePrompts.cpp` | `prompts_bridge.rs`, `pickers.rs`, `view/page_overlays/pickers.rs` |
| `XenonBridgeDrop.h` | `Drop.cpp` | `page_drop.rs` |
| `XenonBridgeMedia.h` | `PictureInPicture.cpp`, `ViewMedia.cpp` | `media_bridge.rs`, `picture_in_picture.rs`, `view/picture_in_picture.rs` |
| `XenonBridgeScreenshots.h` | `Screenshots.cpp` | `screenshots.rs`, `view/page_overlays/screenshot.rs` |
| `XenonBridgeReports.h` | `CrashReports.cpp` | `crash_reports.rs`, `view/crash_offer.rs`, `view/page_overlays/crash_review.rs` |
| `XenonBridgeUpdater.h` | `UpdaterBridge.cpp`, `Updater.cpp` (`UpdaterMac.mm`) | `updates.rs`, `view/update_button.rs` |
| `XenonBridgePlatform.h` | `PlatformServices.cpp` (`PlatformServicesOther.cpp`, `PlatformServicesMac.mm`) | `platform_services.rs` |

- **Colour picker** (`<input type="color">`): a sheet over the page with a saturation and value square, a hue strip,
  Qt's sixteen basic colours and a hex field. Every change goes to the engine as an update, so the page hears `input`
  as the colour moves; **OK** or Enter closes it with the colour (`change`), **Cancel** or Escape without.
- **Date picker** (`date`, `month`, `week`, `time`, `datetime-local`): a month calendar (a year of months for `month`,
  whole weeks for `week`) with a time field where the type has one, keeping to the input's `min` and `max`. A day picks
  and closes a date input; **Clear** empties the value. Values go back in HTML's formats (`2026-W45`,
  `2026-11-06T09:30`); the calendar arithmetic is unit tested in `pickers.rs`.
- **WebAuthn**: a sheet at the page's top with the states, wording and buttons of `UI/WebAuthnPopover.cpp`: insert and
  touch the key, the PIN (with the attempts left), choosing an account, attestation consent, and the errors.
- **Dropping files** on the page: Rune's `on_drop_files` on each page surface (each pane's, in a split) reports the
  drag's enter, moves, leave and drop to that pane's view, which the engine sends as `Web::DragEvent`s; a drop on the
  pane that isn't active activates its tab. The files are read on enter (`WebView::create_selected_file`), so
  the page sees them in `dataTransfer` and a file input takes them. A drop the engine hands back
  (`on_finish_handling_drag_event`) opens the files, the first in the tab and the rest in background tabs, as
  `UI/BrowserWindow.cpp` does; LibWeb reports a drop no page handler accepted as cancelled, though, so on the pinned
  engine that doesn't happen in either frontend. winit reports file drags on X11, macOS and Windows, not on Wayland.
- **Picture-in-picture**: the engine's `create_picture_in_picture_window` makes a `View` with an id of its own, whose
  frames reach the UI as a tab's do, and the window that shows the video's tab draws it over its page area at the
  bottom right (across both panes of a split), above whichever tab is active there. Its edges resize it, keeping the video's shape within the limits LibWebView gives;
  a drag where the page doesn't take the press (`picture_in_picture_press_handled`) moves it, and the press is
  cancelled in the page (`xenon_engine_picture_in_picture_cancel_press`). The compositor draws the Back to tab and
  Close controls. It stays in the window rather than taking a Rune window of its own from the pool (see
  [Windows](#windows)), which can't float above other apps' windows or follow the video's aspect ratio.
- **Wake lock**: a page's screen wake lock holds a `WakeLock::DisplaySleepInhibitor` while it's acquired. LibWakeLock
  asserts against display sleep on macOS (IOKit) and does nothing elsewhere, as in the Qt build.
- **Screenshots**: **Take Screenshot…** in the page's context menu freezes the page's current frame on a surface of its
  own and lays it over the page, with **Capture Visible Page** and **Capture Full Page** buttons. Dragging saves the
  part selected, Enter the visible page, Escape cancels; scrolling shows the live page and freezes it again a moment
  later. Saving asks where, as the Qt build's `ask_user_for_download_path` does: the engine sends
  `save_location_requested` with the downloads folder and a name, the window in front opens Rune's save dialog, and
  `xenon_engine_answer_save_location` answers. **Save Link As…** and **Save Image As…** ask the same way.
- **Crash reports**: a tab whose page crashed shows LibWebView's `CrashReportReview` over its crash page, as
  `UI/CrashReportReviewWidget.cpp` does: what the user was doing, whether to include the address, the report's
  details and full text, **Send report** or **Don't send**, then progress, retries and the next report. A helper process
  that crashes outside any tab (`display_crash_report_notification`) brings up **Xenon hit a snag!** under the toolbar
  (`UI/CrashReportNotifier.cpp`); **Review report** opens a tab with the review and **Dismiss** marks the reports seen.
- **Updates**: when an update is ready, the toolbar shows the rose **Update** pill and its popover from
  `UI/UpdateButton.cpp` (**Later** hides it until a newer version, **Relaunch to Update** installs and relaunches, and
  the session comes back whole). macOS updates through Sparkle (`UpdaterMac.mm`, and **Check for Updates…** in the app
  menu). Builds elsewhere don't update themselves: `XENON_TEST_UPDATE_READY=<version>` stands in for a ready update,
  as for the Qt build, and relaunching only quits.
- **External applications**: a link to a scheme another application handles (`mailto:`, `zoommtg:`) is resolved as
  the Qt build does: on Linux with GIO's default handler for the scheme, passing over dispatchers such as `xdg-open`
  (Qt's `UI/ExternalURLHandler.cpp`, with no XDG activation token since Rune has none to give:
  `Bridge/ExternalURLActivationToken.cpp`), and on macOS with `NSWorkspace`. LibWebView then asks with the external
  application dialog before launching it.
- **UI font**: the engine's `ui_font_family`, which the compositor draws its overlays in (the paused debugger, the
  picture-in-picture controls), is GNOME's `font-name` setting on Linux (without its size) and the system font on macOS.
  On Linux it's also the system font pages get, as Qt's `QGuiApplication::font()` is for the Qt build.
- **Spelling and dictionary**: on macOS the engine's spell checker is `UI/SpellCheckerMac.mm`, and a dictionary lookup
  from the context menu shows `UI/DictionaryLookup.mm`'s panel at the word, for which the UI reports where the page sits
  in the window (`xenon_engine_set_page_origin`). Elsewhere neither exists, as in the Qt build.
- The mock engine (`mock_misc.rs`) answers URLs containing `color-picker`, `date`, `webauthn`, `picture-in-picture`,
  `screenshot`, `crash-report` or `crash-offer` with that prompt, and `selection` with a selection for the find bar.
- Not yet: the web compatibility report (**Report Website Issue…** stays disabled), choosing several files in the file
  dialog (Rune's dialog returns one path), a confirmation once something is saved, and the macOS pieces, which haven't
  been built.

## Performance

The Rune frontend keeps its GPU work for what changes. A settled page draws nothing: Rune rests until a page frame,
input or state change wakes it, and a window that's minimized, covered or hidden draws nothing (its page is told to
stop when macOS reports it, see [Windows](#windows)). Slots nobody has bound have no native window or GPU surface.

- **Page frames** (`Rune/ui/src/frame.rs`, `view_frame` in `bridge.rs`): Ladybird's compositor hands over premultiplied
  BGRA, and `Pixels` needs straight RGBA. A row whose pixels are all opaque is swizzled 16 bytes at a time without
  touching alpha; any other row falls back to a 64 KiB table built once for `(channel * 255 + a / 2) / a`, which is
  exact for every alpha and channel pair. That's about 3× faster for full frames (4.5 ms → 1.4 ms at 2560×1600,
  9.2 ms → 2.9 ms at 4K, on the development machine; 2.7–3.8× for mixed-alpha frames), which matters because the
  conversion runs on the engine thread before the page's next frame. The output buffer is allocated once per frame and
  written in place. The page area paints its own background only until the tab has a frame (`view/page.rs`); after that
  the page covers it, and a page frame costs one full-window overdraw less.
- **Loading spinners** (`view/tabs.rs`): only the active tab's spinner rotates, and only while its window has focus.
  A tab still loading in the background or in an unfocused window shows the spinner's arc standing still, so a stalled
  or long-polling download can't hold the whole window at the display's frame rate; the Qt frontend repaints just its
  tab strip for the same animation, while Rune repaints the window.
- **View events** (`update_showing` in `window_slots.rs`): a view's title, URL, load state, favicon, cursor, hover,
  zoom, block state, audio and memory reach the state of every window's `Browser`, but `AppHandle::update_if` redraws
  only the window that shows that view (or its extension popup). Model events (tabs, windows, groups) still redraw
  every window they change.
- **Idle timers**: the accessibility check runs every two seconds rather than on every frame, and its wake is armed
  only when it runs.
- **Pointer moves**: Rune asks for a frame on every pointer move, and used to render and present the whole window for
  each one, changed or not, at the display's rate. Three things stop that. A patch makes any frame of a resting window
  build first and take no drawable when it would draw what is already on screen, as Rune already did for timer frames.
  A second patch lets the window come to rest after a live-only event when nothing in it is bound live, which is the
  case whenever the page is on its native surface; without it the page's pointer handler (`Tx::live_only`) left the
  window awake for good. And the engine's answers to a mouse move (cursor, hovered link, tooltip area), which arrive
  whether or not they differ from the last ones, redraw only when they change something (`update_showing_if` in
  `window_slots.rs`, `cursor_changed` and `link_hovered` in `state.rs`, `tooltip_changed` in `page_prompts.rs`).
- **Link status** (`link_status_size` in `view/page.rs`, `set_notch` in `wayland_page.rs`): the address of a hovered
  link no longer sends the window back to the CPU pixels path. The page gives up a notch at its bottom left corner the
  size of the status: the page's surface is cropped above it with `wp_viewport`, a second subsurface shows the strip
  to its right from a buffer of its own imported from the same dma-buf, and the window draws the status, square
  cornered and at an explicit width, in the gap. A change of the notch is committed in synchronized mode so it lands
  with the window's frame that draws or removes the status, and the subsurfaces go back to desynchronized 50 ms later.
  The status appears and goes without a fade (`Tx::instant`), since a fade over a row of links kept the window
  animating for as long as the pointer moved.
- **Native pages on Wayland** (`wayland_page.rs`, `view_dmabuf_frame` in `bridge.rs`, `xenon_engine_set_window_surface`):
  each page is presented as a `wl_subsurface` of the Rune window from the dma-buf the Compositor painted it into, so
  the page never enters Rune's image atlas and Rune never draws its quad. The window is transparent and its page area
  is left clear; the page surface is above the window's surface, because KWin does not composite a subsurface placed
  below its parent. While an overlay covers the page (a menu, dialog, permission prompt, picker, find bar, location
  suggestion list, hover card, tooltip, picture-in-picture or the task manager) the window falls back to the CPU
  pixels path, which keeps those overlays above the page; it returns to the native surface when they close. The page
  behind such an overlay is capped to 10 frames per second (`xenon_engine_set_view_frame_rate`), and its rate goes back
  to the display's when the overlay closes, so an animation there can't hold the whole fallback at 60.
  `Meta/rune.py` prepares a patched Rune checkout under `Build/rune` and `Patches/rune` holds the small patches it
  applies:
  1. `WindowHandle::with_raw_display_handle`, which winit's raw window handle does not expose.
  2. An `allow(dead_code)` for the tooling's performance meters in release builds.
  3. The IME cursor area goes to the window only when it moves (sending it every frame kept KWin repositioning its
     input-method popup at the display's rate, which cost about 7% GPU with a text field focused).
  4. Off macOS, any frame of a resting window is built before a drawable is taken, and takes none when it draws what
     is on screen ("Pointer moves" above).
  5. `WindowHandle::minimize`, `is_maximized`, `set_maximized` and `drag_resize`, for a borderless window that draws
     its own title bar ("The window frame" below).
  6. A live-only event with nothing bound live leaves the window able to rest ("Pointer moves" above).
  7. A Wayland window takes a requested size at once and sends no `Resized` for it, so Rune now resizes its drawable
     and lays out at that size itself. Before, a window opened with a saved size kept drawing at its first size and the
     compositor stretched it to fit, with the page out of place.

Measured in a KWin session with OpenGL compositing (`compositingType` `gl2`), 1280×800, a 60 fps CSS-animated page and
`gpu_busy_percent`: Rune with the native page surface **3.0%**, Qt **3.0%**, before the native surface 10.0%. With the
find bar open and focused, Rune measures **0.0%** on a settled page and **1.6%** over the same animation, Qt **3.0%**.
A page that scrolls itself measures Rune **4.0%** against Qt's **4.1%**.

Pointer movement, measured the same way with a pointer driven at 125 moves a second through KWin's fake input
protocol (`gpu_busy_percent`, and per process from the DRM `fdinfo` counters):

| Pointer moving over | Rune before | Rune | Qt |
| --- | --- | --- | --- |
| a settled page | 4.0% | **0.0%** | 0.0% |
| a page of links, each with a hover style | 6.5% | **0.8%** | 0.2% |
| the tab strip and toolbar | | **0.7%** | 0.0% |

The earlier figure of 0.0% for a pointer over a settled page was wrong: the pointer in that run wasn't reaching the
window. What is left over links and the chrome is the frame Rune draws when the status or a hover highlight changes;
it draws the whole window for it, where Qt repaints one widget on the CPU. CPU use is the same in both frontends (7 to
8% of a core over the settled page, 10 to 12% over the links, nearly all of it WebContent).

X11 and macOS keep the CPU pixels path, so the conversion above still applies there; on Linux and Windows
a window's pages also keep painting while the window is minimised or covered, because only the macOS side learns about
that from AppKit.

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

## Extensions

Extensions run on the engine thread exactly as in the Qt frontend: `Extensions::Registry`, the `Router`, background
workers, content scripts and `PasswordManagers` are `xenon-core`. What the Qt frontend does in its chrome
(`UI/Extensions/Browser.cpp`, `Toolbar.cpp`, `Popup.cpp`, `Dialog.cpp`) Rune does through its own slice of the ABI,
`Rune/Bridge/XenonBridgeExtensions.h` (`XenonBridgeExtensions.cpp`, with the host in `ExtensionsBrowser.cpp`;
`ffi_extensions.rs`, `extensions.rs` and `view/extensions.rs` on the Rust side). The UI registers
`XenonExtensionCallbacks` with `xenon_engine_set_extension_callbacks` before `xenon_engine_start`.

- **`chrome.tabs` and `chrome.windows`** are answered from `BrowserModel` (`Rune/Bridge/ExtensionsBrowser.cpp`, which
  defines the `Extensions::Browser` functions `xenon-core` calls). A tab's Chrome ID is its view ID and a window's is
  its window ID, so they stay the same when a tab moves between windows. `Router` names the calling page by its
  `ViewImplementation::view_id()`, which isn't the view ID, so `handle_call`, `tab_json`, `tab_and_window_ids`,
  `is_popup_view` and `prompt_for_permissions` look the `View` up first. `Engine`'s model observer and `View`'s title
  and loading callbacks feed `ExtensionsHost`, which sends `tabs.onCreated`, `onUpdated` (title, status and URL, as
  Qt does; pinning too), `onActivated`, `onRemoved`, `onMoved`, `onDetached`/`onAttached` for a move between windows,
  and `windows.onCreated`, `onRemoved` and `onFocusChanged` (when the model activates a window). `query`, `get`,
  `getCurrent`, `create`, `update`, `remove` (asking the page first, as the close button does), `reload`, `duplicate`,
  and `windows.get`, `getCurrent`, `getLastFocused`, `getAll`, `create` (with `tabId`, and `type: "popup"`, which is
  remembered for `windowType` but opens an ordinary window), `update` and `remove` follow `Browser.cpp`, including
  which tabs' URLs and titles an extension may see. Each model window is a Rune window of its own (see
  [Windows](#windows)): a window's `left`, `top`, `width`, `height` and `maximized` state are the geometry
  `WindowLifecycle` last had reported (its active page's size until the window has reported any, as in headless
  runs); `windows.update` moves, resizes, maximizes, minimizes and restores it through the same `window_frame_requested`
  a page's request uses (`WindowLifecycle::request_window_frame`), so Wayland ignores the position and Linux the
  maximize and restore, as for pages; and `windows.remove` closes it with the beforeunload checks, as Qt's
  `window->close()` does.
- **`action`**: badge text and colours, titles, icons from paths, popups and `enable`/`disable`, for all tabs and per
  tab, as `Browser.cpp` keeps them. The engine reports the buttons of each window's active tab with `actions_changed`
  (coalesced to one report per event-loop turn), with the icon as an absolute file inside the extension. The UI
  decodes icons with Rune's `ImageCache`, keeps the buttons per view and shows those of a tab it has none for yet from
  the last report, so switching tabs doesn't flicker.
- **The toolbar** shows a button per extension with an action, after the address bar, as Qt does: the icon (a puzzle
  piece without one), the title as its tooltip, up to four characters of badge with the badge colour (white or black
  text by the colour's lightness unless the extension sets one), and dimmed when disabled. Like the Qt toolbar there's
  no pinning or overflow; every extension with an action has a button. A right click opens a menu Qt doesn't have
  (the extension's name, Options when it has an options page, Manage Extensions, Remove from Xenon…), through
  `chrome_popup`, so it's drawn in the window where there are no native menus.
- **Popups**: a click asks `xenon_engine_extension_action_clicked`. Without a popup the engine fires `action.onClicked`;
  with one it creates a `View` that isn't a tab (`Engine::add_detached_view`, without the content blocker, as Qt's
  popup), loads the popup page in it and tells the UI with `popup_opened`. The popup is drawn under its button
  (`anchored`, `modal` so a click outside or Escape closes it) at 360×480 until the page reports its size, then
  `popup_resized` sizes it between 25×25 and 800×600, as Qt does. Frames reach it through `view_frame` like a tab's, and
  the UI sends it mouse, wheel and key events and its viewport with the ordinary view calls, using its view ID. Keys go
  to it while it's open even before it's clicked. `window.close()` in the popup, closing the button's tab, switching
  tabs or removing the extension closes it (`popup_closed`). `Router` sees it as a popup context (`is_popup_view`),
  and `tabs.query({currentWindow: true})` from it answers for its window. `action.openPopup` and the `_execute_action`
  command open it for the focused window's active tab.
- **Commands**: the engine reports each `suggested_key` with `commands_changed`; the UI parses them (`Ctrl` and `Command`
  are ⌘ on macOS and Ctrl elsewhere, `MacCtrl` is Control on macOS) and runs a match before its own shortcuts, from the
  page, the address bar or the window, with `xenon_engine_run_extension_command`.
- **Permission prompts** (`permissions.request()`): `permission_prompt` carries the extension's name, Chrome's warnings,
  the popup that asked, if any, and the tab it asked for (the popup's, the asking tab, or the focused window's active
  one). The UI shows a modal sheet with **Deny** and **Allow**; a click outside denies, as does closing the popup that
  asked. `--headless` runs still deny and print the warnings, as in Qt.
- **The Extensions dialog** opens from **Extensions** in the main menu, from about:settings (`SettingsPage`'s
  `show_extensions`) and from a button's menu: the password manager picker (a `chrome_popup` menu) with its status
  (installing, kept up to date, or the error), each extension's icon, name, version, description, ID and directory,
  warnings, **Reload**, **Options** and **Remove** (confirmed), and **Load Unpacked…**, which picks a directory with
  Rune's folder dialog.
  Changing the password manager away from an installed one asks first, as the Qt dialog does. Load and reload errors
  show as an alert. `extensions_changed` and `password_managers_changed` keep it current from `Registry` and
  `PasswordManagers`' change listeners.
- **Password managers** need nothing of their own: `PasswordManagers` already runs in `xenon-core`, installs and
  updates the chosen manager, and serves the welcome page's WebUI; the dialog's picker and `--password-manager`
  choose one, and its button, popup, tabs and commands work as above.
- **Windows**: each Rune window has its own `Browser`, so `ExtensionsContext` routes through `Slots`. Actions and a
  popup's opening go to the window showing the tab (`update_view`), and the popup's view is placed in the slot table
  while it's open, so its cursor, size and close reach that window too. A permission prompt goes to its tab's window.
  The list of extensions, the password managers and the commands go to every window (`Slots::update`); the dialog and
  load errors to the target window.
- `TestXenonRuneExtensions` loads `Tests/Extensions/basic` through the C ABI alone, checks its action (title, icon,
  popup), opens its popup and waits for the popup's frames, title and reported size, and closes it.
- The mock engine (`mock_extensions.rs`) makes up an extension when `XENON_MOCK_EXTENSIONS` is set, with a badge, a
  popup (blank, 320×200), a command and a password manager, so the preview can show the button, the popup and the
  dialog.
- Not yet: dialogs, menus and `<select>`s that a popup page opens (they're keyed to a tab, so they don't show), input
  methods in the popup, full screen and `alwaysOnTop` for `chrome.windows`, `tabs.captureVisibleTab`,
  `action.setIcon` with `imageData`, and WebAuthn from a popup (Qt attaches its `WebAuthnPopover`).

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
accessibility ABI. `TestXenonRuneWindows` closes a window whose page asks first (staying, then leaving), checks the
cascade placement and quits. `TestXenonRuneExtensions` does it for the extensions ABI. All are part of
`xenon-tests`.

The UI also builds on its own, against a mock engine on a thread of its own, for Rune's headless tools. It first
needs the patched Rune checkout, which `ninja -C Build/release xenon-rune` prepares; run it by hand once for a
standalone cargo build:

```bash
./Meta/rune.py prepare
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
