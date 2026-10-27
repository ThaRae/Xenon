# Xenon

A web browser built on the [Ladybird](https://ladybird.org) engine, with a frontend that brings over
[Helium](https://helium.computer)'s feature set: browser layouts you can switch between, !bangs in the address bar,
and a privacy-first, low-noise setup. Xenon keeps its own look and doesn't copy Helium's.

## How the repo fits together

| Path | What it is |
| --- | --- |
| `ladybird/` | The Ladybird engine, as a shallow git submodule pinned to a known-good upstream commit. |
| `Patches/ladybird/` | Xenon's changes to the engine, as a patch series applied to the pinned commit at build time (`Meta/patches.py`). |
| `UI/` | Xenon's Qt frontend. It started as a copy of `ladybird/UI/Qt` and keeps that layout and the `Ladybird` C++ namespace, so upstream frontend fixes can still be diffed and merged in. |
| `UI/Resources/` | Xenon's versions of Ladybird's internal pages (welcome, new tab, and the shared web UI stylesheet), with `xenon.css` for what its own pages share, and the bundled filter lists. The build ships these in place of upstream's copies, which stay untouched. |
| `UI/Icons/` | The app icon. `xenon.svg` is the artwork and `xenon-small.svg` its version for 32px and below; `Meta/generate-app-icon.py` renders the macOS iconset and the window icon from them. |
| `Meta/xenon.py` | Builds and runs Xenon. |
| `Meta/CMake/XenonHook.cmake` | Injected into Ladybird's CMake build. Once the engine's libraries and helper processes are defined, it pulls in `UI/Xenon.cmake`, which defines the `xenon` target. |

The submodule always points at an upstream commit. Xenon's engine changes live in `Patches/ladybird`, as
`git format-patch` files that `Meta/xenon.py` applies with `git am` on a local `xenon-patches` branch before it
configures the build, the way Helium patches Chromium. Each patch is a self-contained change that could go upstream.
Ladybird's build is configured with
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

## Tests and CI

```bash
./Meta/check.py            # formatting and Xenon's invariants, in seconds
./Meta/check.py --fix      # apply the formatters first
./Meta/xenon.py test       # unit tests (Tests/), plus a headless render of about:welcome
git config core.hooksPath .githooks   # run Meta/check.py before every push
```

`Meta/check.py` formats C++ with clang-format, web pages with prettier, and Python with ruff, at the versions
Ladybird's CI uses, read from the submodule. It installs clang-format and ruff into `Build/tools` itself; prettier
needs `npx`. It also checks the things that break Xenon quietly:

- an include that resolves to Ladybird's header instead of ours
- a source file left out of the build
- a page override for a page upstream no longer ships
- a malformed or duplicate !bang
- Ladybird's violet coming back into the chrome
- engine changes in `ladybird/` that aren't exported to `Patches/ladybird`, or a pinned commit that doesn't exist
  upstream

CI runs on Gitea Actions at git.avx512.dev, on slab's ephemeral runners:

| Workflow | Runs | Does |
| --- | --- | --- |
| `.gitea/workflows/ci.yml` | pushes to `main`, pull requests, by hand | `Meta/check.py --online` on `slab`, then on `slab-large`, a Linux build, `Meta/xenon.py test`, and the welcome page render as an artifact |
| `.gitea/workflows/upstream.yml` | weekly, by hand | the same build against Ladybird's latest `master`, plus a list of what changed upstream in `UI/Qt` and the overridden pages, so a submodule bump holds no surprises |

The builds run in Ladybird's own CI image (`ghcr.io/ladybirdbrowser/ladybird-ci`, at the version in
`ladybird/Meta/Docker/ci/VERSION`), so the compilers and system libraries match what Ladybird is tested with.
The `CCACHE_TOKEN` repository secret points ccache and vcpkg's package cache at slab-ccache. Without it, every build
starts cold and takes a couple of hours.

## Updating the filter lists

```bash
./Meta/update-filter-lists.py              # rebuilds them in ../xenon-ublock, then copies them in
./Meta/update-filter-lists.py --no-build   # copies what ../xenon-ublock last built
```

It ships xenon-ublock's `helium` preset, except for Peter Lowe's list, which states no license, and Helium's own two
lists (AGPL-3.0, and almost nothing survives conversion). `UI/Resources/filter-lists/NOTICE.txt` credits every list's
source and license. The GPL-3.0 text ships beside them. `Meta/check.py` fails if a list ships without a cleared
license. `./Meta/xenon.py test` checks that Xenon blocks with them, given no list arguments, by loading a local page
with blocking on and off.

## Changing the engine

```bash
./Meta/patches.py apply     # ladybird/ at the pinned commit plus the series, one commit per patch
# edit in ladybird/ and commit there (or amend, or git rebase -i), then:
./Meta/patches.py export    # writes the commits back to Patches/ladybird
```

`Meta/xenon.py build` applies the series by itself when `ladybird/` is at the pinned commit, and builds whatever is
checked out otherwise, so a rebuild in the middle of an edit builds the edit. `Meta/check.py` fails while `ladybird/`
holds changes that aren't exported. `.gitmodules` sets the submodule to `ignore = all`, so the patched checkout never
shows up as a change in Xenon.

## Updating Ladybird

```bash
git -C ladybird fetch --depth 1 origin master
./Meta/patches.py apply --base FETCH_HEAD    # fix any patch that stops applying, then:
./Meta/patches.py export
./Meta/patches.py pin                        # stages the new commit as the pin
./Meta/xenon.py build
```

If the engine's API changed, compare `ladybird/UI/Qt` against the commit Xenon last synced with, and port the relevant
changes into `UI/`. The weekly Upstream workflow applies the series to `master`, names any patch that no longer
applies, builds, and lists those UI changes, so a bump holds no surprises.

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
- [x] **New tab page**: your eight most visited sites, one per site with its favicon, ranked by Ladybird's
  decaying visit score, plus a reminder of the !bangs. Private windows say so instead and show no history. The page is
  static, so Xenon's UI hands it the sites once it has loaded (`UI/NewTabPage.cpp`).
- [x] **Quick link copy**: <kbd>⌘</kbd><kbd>⇧</kbd><kbd>C</kbd> copies the current page's URL.
  (DevTools stays on <kbd>⌘</kbd><kbd>⇧</kbd><kbd>I</kbd> and <kbd>F12</kbd>.)
- [x] **uBlock Origin's filter lists, built in**: the lists Helium's uBlock Origin fork turns on (uBlock filters,
  EasyList, EasyPrivacy, AdGuard, URLhaus, and the cookie banner and annoyance lists), converted for Ladybird's content
  blocker by [xenon-ublock](../xenon-ublock). They block ads, trackers, malware sites, and cookie banners whenever
  content blocking is on, which the welcome flow turns on by default. They ship with Xenon in
  `UI/Resources/filter-lists`, so nothing is downloaded. Ladybird's own downloadable catalog stays off, since it holds
  raw copies of the same lists.
- [ ] **Chrome extensions** (Manifest V3), for password managers first, as Helium offers Bitwarden, Proton Pass, and
  1Password. In progress; see [Documentation/Extensions.md](Documentation/Extensions.md). So far, unpacked extensions
  load from the Extensions dialog or with `--load-extension=<dir>`, get a toolbar button and popup, and run their
  background service worker. They have `chrome.runtime` (with ports), `storage`, `i18n`, `alarms`, `tabs`, `windows`,
  `action`, `commands`, and `offscreen`, and fetch from the hosts they have permission for. Their content scripts run
  in isolated worlds of their own in web pages, and `chrome.scripting` injects scripts and stylesheets on request.
  Bitwarden logs in (with a security key too) and autofills logins.
- [ ] uBlock Origin features that need engine work: scriptlets (YouTube ads, anti-adblock walls), `$redirect`
  surrogates, `$removeparam`, popup blocking, procedural hiding (`:has-text()`), CNAME uncloaking, and the element
  picker. See xenon-ublock's `docs/gap-analysis.md`.
- [ ] Daily filter list updates, which need a host for xenon-ublock's converted lists.
- [ ] Frameless mode (hide all chrome, so the page fills the window)
- [ ] Split view
- [ ] MRU tab switching (<kbd>Ctrl</kbd><kbd>Tab</kbd> in most-recently-used order)
- [x] **Tab hibernation**, Xenon's take on the Memory Saver that Helium has from Chromium: a background tab left unused
  for two hours gives up its WebContent process, and its favicon turns grey. It keeps its title, favicon, URL, back/forward
  history, and scroll position, and reloads when you select it. A window's current tab never hibernates, and neither do
  pinned tabs, tabs playing audio (or that were a minute ago), with picture-in-picture or DevTools open, waiting on a
  dialog, or holding text typed into a form and not sent. Right-click the tab strip for **Tab Hibernation → Off** or a
  timeout from 15 minutes to 6 hours, or a background tab for **Hibernate Tab** (`UI/TabHibernation.cpp`, engine patch
  `0020`, which saves the scroll position first).
- [ ] Tab groups
- [ ] The full DuckDuckGo bang list (13,000+), plus a keyword chip in the address bar while a bang is typed
- [ ] A separate profile directory (LibWebView currently hardcodes `Ladybird` in profile paths)
- [x] Xenon's own palette, built around **#ff0050** (`UI/BrandColors.h` for the chrome, `UI/Resources/about-pages/webui.css`
  for the internal pages)
- [x] Xenon's own app icon: two orbits woven around a nucleus, in white and rose
- [ ] uBlock Origin's filter set for Ladybird's content blocker (being built in a separate repo, `xenon-ublock`)
- [x] **WebAuthn with security keys** (macOS): sites and password managers can register a USB security key and sign in
  with it (`navigator.credentials.create()`/`get()`), PINs included, and in cross-origin frames that are allowed to.
  A prompt over the page says what the key needs. Engine patches `0010`–`0015`; see `Documentation/WebAuthn.md`.
- [ ] WebAuthn, next: PIN setup and passkey management for security keys, and Linux. Platform passkeys (iCloud Keychain,
  Touch ID, phones) need an entitlement from Apple.

Web app installs, which depend on Chromium in Helium, are out of scope until Ladybird supports them.

## License

Code in `UI/` that came from Ladybird keeps its original BSD-2-Clause license and copyright headers. New Xenon files
use BSD-2-Clause too. The filter lists in `UI/Resources/filter-lists` are third-party data under their own licenses
(GPL-3.0, GPL-3.0 or CC BY-SA, and CC0), listed in its `NOTICE.txt`.
