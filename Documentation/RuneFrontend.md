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
- `xenon_engine_insert_text` commits text from an input method.

## Building

`Meta/xenon.py build` builds `xenon-rune` beside `xenon`. `ninja -C Build/release xenon-rune` rebuilds it alone: CMake
runs cargo for `Rune/ui` (through Ladybird's `import_rust_crate`), then links the static library into the executable
with LibWebView. During development, the UI also builds on its own, against a mock engine:

```bash
cargo build --manifest-path Rune/ui/Cargo.toml --features mock-engine --bin xenon-ui-preview
Rune/ui/target/debug/xenon-ui-preview --snap window.png --size 1280x800
```

Rune pins a revision of its `dev` branch, in `Rune/ui/Cargo.toml`.
