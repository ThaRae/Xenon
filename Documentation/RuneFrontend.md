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
- `view_opened` arrives before the new view exists. Commands sent for it from inside the callback wait until it does.
- The UI opens the first tab itself, on `engine_ready`. If another Xenon already runs with the same profile, the engine
  hands it the URLs and sends `engine_exited(0)`.
- `xenon_engine_start` can be called once per process: LibCore ties its signal pipe to the first thread that registers a
  signal.
- Page dialogs (alert, confirm, prompt, beforeunload, select, file and colour pickers) are dismissed for now, until the
  ABI carries them.

## Where Rune falls short

- Rune has no secondary-button press: `on_secondary_press_local` fires on release, so a right click reaches the page as
  a down and an up together, and a right drag doesn't reach it at all.
- Primary, middle, back and forward presses come from `on_drag`, so there is no triple click.
- Rune has no wait, progress, help, diagonal resize or zoom cursors; those show the default cursor.
- Input methods show no composition yet, and the candidate window sits at the page's corner rather than at the caret.

## Building

`Meta/xenon.py build` builds `xenon-rune` beside `xenon`, as `Build/release/xenon/bin/xenon-rune` on Linux, sharing the
Qt build's helper processes and resources. `ninja -C Build/release xenon-rune` rebuilds it alone: CMake runs cargo for
`Rune/ui` with `-Dwarnings`, into `Build/release/cargo/xenon_ui`, then links the static library into the executable
with LibWebView. `Rune.cmake` builds the crate with Ladybird's cargo helpers rather than `import_rust_crate`, which
expects every crate to bring Ladybird's panic and demangling hooks. `Rune/ui/Cargo.lock` is checked in, and the build
stops without it.

`TestXenonRuneBridge` drives the engine through the C ABI alone, with no Rust: it loads a page and checks the colour of
the frame the UI would get. It is part of `xenon-tests`.

The UI also builds on its own, against a mock engine on a thread of its own, for Rune's headless tools:

```bash
cargo +1.98.0 test --manifest-path Rune/ui/Cargo.toml --features mock-engine
cargo +1.98.0 build --manifest-path Rune/ui/Cargo.toml --features mock-engine
Rune/ui/target/debug/xenon-ui-preview --snap window.png --size 1280x800 --theme dark --at 0.5
Rune/ui/target/debug/xenon-ui-preview --script two-tabs.json --snap end.png --at mark:end --tree tree.json
```

The mock answers each load with a URL, a title, a favicon and a drawn page. A URL with `slow` in it keeps loading, and
the coloured bar on its page is a link. `--snap` without `--at` captures the first frame, before the mock has answered.

Rune pins a revision of its `dev` branch, in `Rune/ui/Cargo.toml`.

`XENON_RUNE_ARGS` hands Rune's own flags to `xenon-rune`, so the real engine can be rendered and scripted headless, with
no display. Rune's headless clock is virtual and runs ahead of the engine, so give pages time with a late `--at` (each
virtual second takes a few real milliseconds):

```bash
XENON_RUNE_ARGS="--script load.json --snap page.png --size 1280x800 --at 600" Build/release/xenon/bin/xenon-rune
```
