# Xenon

A web browser built on the [Ladybird](https://ladybird.org) engine, with a frontend that brings over
[Helium](https://helium.computer)'s feature set: browser layouts you can switch between, !bangs in the address bar,
and a privacy-first, low-noise setup. Xenon keeps its own look and doesn't copy Helium's.

## How the repo fits together

| Path | What it is |
| --- | --- |
| `ladybird/` | The Ladybird engine, as a shallow git submodule pinned to a known-good commit. It isn't modified. |
| `UI/` | Xenon's Qt frontend. It started as a copy of `ladybird/UI/Qt` and keeps that layout and the `Ladybird` C++ namespace, so upstream frontend fixes can still be diffed and merged in. |
| `UI/Resources/` | Xenon's versions of Ladybird's internal pages (the welcome page and the shared web UI stylesheet). The build bundles these in place of upstream's copies, which stay untouched. |
| `Meta/xenon.py` | Builds and runs Xenon. |
| `Meta/CMake/XenonHook.cmake` | Injected into Ladybird's CMake build. Once the engine's libraries and helper processes are defined, it pulls in `UI/Xenon.cmake`, which defines the `xenon` target. |

Ladybird stays an unmodified checkout. Its build is configured with
`-DCMAKE_PROJECT_ladybird_INCLUDE=Meta/CMake/XenonHook.cmake`, which defers an `include(UI/Xenon.cmake)` until the
end of Ladybird's top-level `CMakeLists.txt`. The `xenon` target links against LibWebView, and the engine's helper processes
(WebContent, RequestServer, ImageDecoder, ...) are built into `Xenon.app`.

## Building (macOS)

Requirements: Xcode (or Homebrew LLVM), CMake 3.30+, Rust via rustup (`rust-toolchain.toml` links to the submodule's,
so rustup picks Ladybird's pinned version), and:

```bash
brew install autoconf autoconf-archive automake ccache cmake libtool nasm ninja pkg-config
```

Then:

```bash
git submodule update --init --depth 1
./Meta/xenon.py run
```

The first build fetches and compiles about 74 third-party dependencies through vcpkg, including Qt and Skia, which
takes a while. vcpkg caches them, so later builds are incremental. Everything goes under `Build/`:

- `Build/vcpkg-release/`: the vcpkg checkout
- `Build/release/`: the CMake build tree
- `Build/release/bin/Xenon.app`: the app

Other commands:

```bash
./Meta/xenon.py build            # build Xenon without launching it
./Meta/xenon.py run -- <url>     # launch with arguments
./Meta/xenon.py --preset Debug run
./Meta/xenon.py clean
```

## Updating Ladybird

```bash
git -C ladybird fetch --depth 1 origin master
git -C ladybird checkout FETCH_HEAD
./Meta/xenon.py build
```

If the engine's API changed, compare `ladybird/UI/Qt` against the commit Xenon last synced with, and port the relevant
changes into `UI/`.

## Features

What exists now and what's planned, measured against Helium:

- [x] **Layouts**: right-click the tab strip or window frame and pick **Layout → Classic / Compact / Vertical Tabs**.
  - Classic: the tab strip above the toolbar.
  - Compact: the toolbar and the tabs in one row.
  - Vertical: tabs in a collapsible, resizable sidebar (from Ladybird).
- [x] **!bangs**: `!w ladybird` or `ladybird !w` goes straight to Wikipedia's results, and `!gh` alone opens GitHub.
  They resolve locally, with no request to a search engine. There's a built-in list of common bangs in
  `UI/Bangs.cpp`.
- [x] **Welcome/setup flow**: on first run (or any time at `about:welcome`), either take Xenon's picks (DuckDuckGo, with
  blocking on) or answer two questions: which search engine, from a private/mainstream list with a live preview of
  where a search goes, and whether to block ads and trackers (on by default, with all three filter lists). Nothing is
  saved until you finish.
- [x] **Quick link copy**: <kbd>⌘</kbd><kbd>⇧</kbd><kbd>C</kbd> copies the current page's URL.
  (DevTools stays on <kbd>⌘</kbd><kbd>⇧</kbd><kbd>I</kbd> and <kbd>F12</kbd>.)
- [x] **Content blocking** (from Ladybird, though it's off until you turn it on in settings).
- [ ] Helium's privacy defaults: content blocking on from first launch, and no background requests without consent.
  Ladybird currently defaults to the content blocker off and background networking on.
- [ ] Frameless mode (hide all chrome, so the page fills the window)
- [ ] Split view
- [ ] MRU tab switching (<kbd>Ctrl</kbd><kbd>Tab</kbd> in most-recently-used order)
- [ ] Tab hibernation
- [ ] Tab groups
- [ ] The full DuckDuckGo bang list (13,000+), plus a keyword chip in the address bar while a bang is typed
- [ ] A separate profile directory (LibWebView currently hardcodes `Ladybird` in profile paths)
- [x] Xenon's own palette, built around **#ff0050** (`UI/BrandColors.h` for the chrome, `UI/Resources/about-pages/webui.css`
  for the internal pages)
- [ ] Xenon's own app icon (the app still uses Ladybird's)
- [ ] uBlock Origin's filter set for Ladybird's content blocker (being built in a separate repo, `xenon-ublock`)

Helium features that depend on Chromium, like Chrome Web Store extensions and web app installs, are out of scope
until Ladybird supports them.

## License

Code in `UI/` that came from Ladybird keeps its original BSD-2-Clause license and copyright headers. New Xenon files
use BSD-2-Clause too.
