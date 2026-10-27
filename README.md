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
| `UI/Resources/` | Xenon's versions of Ladybird's internal pages (welcome, new tab, and the shared web UI stylesheet), with `xenon.css` for what its own pages share, the bundled filter lists, and the !bang list. The build ships these in place of upstream's copies, which stay untouched. |
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
lists (AGPL-3.0, and almost nothing survives conversion). They're converted for Ladybird with the content blocker
patches (`Patches/ladybird` `0004`–`0009`), so they keep scriptlets, procedural filters, and `$redirect`,
`$removeparam`, `$popup` and CNAME filters. uBlock Origin's own lists go in `trusted-lists/`, and its scriptlets and
redirect surrogates in `resources/`. `UI/Resources/filter-lists/NOTICE.txt` credits every list's source and license.
The GPL-3.0 text ships beside them. `Meta/check.py` fails if a list ships without a cleared license. `./Meta/xenon.py test` checks that Xenon
applies them, given no list arguments, with blocking on and off: requests that EasyList and EasyPrivacy block, and a
scriptlet and a hiding rule for a listed site, on a page served locally at that site's address.

Between releases, Xenon fetches the lists daily from
[tharae/xenon-filter-lists](https://git.avx512.dev/tharae/xenon-filter-lists). That public repository's workflow (kept
here as `Meta/ci/filter-lists-workflow.yml`) runs the same script every day with `--output`, through
`Meta/ci/publish-filter-lists.sh`, and signs the manifest. See
[Documentation/FilterListUpdates.md](Documentation/FilterListUpdates.md).

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

## Where Xenon keeps its data

The default profile is split across the platform's usual directories, each in a `Xenon` folder of its own:

| What | macOS | Linux |
| --- | --- | --- |
| Settings and bookmarks | `~/Library/Preferences/Xenon/Profiles/default` | `~/.config/Xenon/Profiles/default` |
| History, cookies, sessions, extensions | `~/Library/Application Support/Xenon/Profiles/default` | `~/.local/share/Xenon/Profiles/default` |
| HTTP cache | `~/Library/Caches/Xenon/Profiles/default` | `~/.cache/Xenon/Profiles/default` |
| The running browser's lock and socket | `~/Library/Application Support/Xenon/Profiles/default` | `$XDG_RUNTIME_DIR/Xenon/Profiles/default` |
| Crash reports | `~/Library/Application Support/Xenon/CrashReports` | `~/.local/share/Xenon/CrashReports` |
| Xenon's own settings (layout and the like) | `~/Library/Preferences/com.xenon.Xenon.plist` | `~/.config/Xenon/Xenon.conf` |

`XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME` move them, on macOS too. `--profile <name>` uses
`Profiles/<name>` instead of `Profiles/default`, and `--profile-path <dir>` keeps a whole profile in `config/`, `data/`,
`cache/` and `runtime/` under one directory, which the tests use. Crash reports and Xenon's own settings stay where they
are with either. Ladybird's engine still names its database in the profile `Ladybird.db`.

LibWebView put every profile under `Ladybird`, until engine patch `0020` let the browser name the directory.
Xenon names it in `UI/main.cpp`, before anything reads it.

Xenon doesn't bring over the profile that earlier builds kept in Ladybird's directories. Nothing there says
whether Xenon or Ladybird wrote it, so moving it could take Ladybird's history and cookies away, and copying it would
give them to Xenon. Xenon starts on a new profile instead, with the welcome flow. If only Xenon ever used the old one,
move it yourself before this version of Xenon first starts (on Linux, with `~/.config`, `~/.local/share` and
`~/.cache`):

```bash
for root in ~/Library/Preferences ~/Library/"Application Support" ~/Library/Caches; do mkdir -p "$root/Xenon/Profiles" && mv -n "$root/Ladybird/Profiles/default" "$root/Xenon/Profiles/"; done
```

## Features

What exists now and what's planned, measured against Helium:

- [x] **Layouts**: right-click the tab strip or window frame and pick **Layout → Classic / Compact / Vertical Tabs**.
  - Classic: the tab strip above the toolbar.
  - Compact: the toolbar and the tabs in one row.
  - Vertical: tabs in a collapsible, resizable sidebar (from Ladybird).
- [x] **!bangs**: `!w ladybird` or `ladybird !w` goes straight to Wikipedia's results, and `!gh` alone opens GitHub.
  They resolve locally, with no request to a search engine, from the full list of about 13,400 triggers for 10,700
  sites (see "Updating the !bangs"). While a bang at the start of the address is typed, a chip names its site, as Chrome
  does in keyword mode. The chip appears once the space after the bang is typed, and Backspace at the start of the text
  takes it away, leaving the bang as plain text (Enter then searches the text as typed). A trailing bang like
  `ladybird !w` resolves on Enter, with no chip.
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
  `UI/Resources/filter-lists`, and update daily (below). Ladybird's own downloadable catalog stays off, since it holds
  raw copies of the same lists.
- [ ] **Chrome extensions** (Manifest V3), for password managers first, as Helium offers Bitwarden, Proton Pass, and
  1Password. In progress; see [Documentation/Extensions.md](Documentation/Extensions.md). So far, unpacked extensions
  load from the Extensions dialog or with `--load-extension=<dir>`, get a toolbar button and popup, and run their
  background service worker. They have `chrome.runtime` (with ports), `storage`, `i18n`, `alarms`, `tabs`, `windows`,
  `action`, `commands`, and `offscreen`, and fetch from the hosts they have permission for. Their content scripts run
  in isolated worlds of their own in web pages, and `chrome.scripting` injects scripts and stylesheets on request.
  Bitwarden logs in (with a security key too) and autofills logins. Proton Pass signs in through Proton's account page,
  unlocks with its extra password, autofills logins, and saves new ones.
- [x] **uBlock Origin's engine features**: scriptlets (YouTube ads, anti-adblock walls), `$redirect` surrogates,
  `$removeparam`, popup blocking, procedural hiding (`:has-text()`), CNAME uncloaking, and an element picker
  (**Block Element...** in a page's context menu). Engine patches `0004`–`0009`, from xenon-ublock. They follow the
  same on/off switch and per-site toggle as the rest of blocking. What's still missing is in xenon-ublock's
  `docs/gap-analysis.md`.
- [x] **Daily filter list updates**: [xenon-filter-lists](https://git.avx512.dev/tharae/xenon-filter-lists) rebuilds
  the lists every day and signs them. Once a day Xenon downloads the lists that changed, checks them against the signed
  manifest, and switches to them without a restart. It falls back to the bundled lists if anything fails. The welcome
  flow and Settings → Services turn updates off. See [Documentation/FilterListUpdates.md](Documentation/FilterListUpdates.md).
- [ ] Frameless mode (hide all chrome, so the page fills the window)
- [x] **Split view**: two tabs side by side in one window, each with its own page and history (`UI/SplitView.cpp`).
  - Open one from a tab's context menu (**New Split View with Current Tab**, or **Add Tab to New Split View** on the
    current tab), with <kbd>⌘</kbd><kbd>⌥</kbd><kbd>N</kbd> (<kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>N</kbd> elsewhere), with
    **Open Link in Split View**, or by holding a dragged tab on the left or right edge of a page for half a second.
  - The two tabs sit together in the tab strip. The active page has a rose ring and the toolbar; click the other page to
    switch. The inactive page's corner names it and has the split view's menu.
  - Drag the divider to resize (it snaps to the middle), or double-click it to swap the pages. **Reverse Views**,
    **Close Left/Right View** and **Separate Views** are in the split button left of the address bar and in the tab's
    context menu. Closing either tab leaves the other as a normal tab.
  - Not yet: restoring split views after a restart.
- [x] **MRU tab switching**: hold <kbd>Ctrl</kbd> and press <kbd>Tab</kbd> to walk back through a window's tabs in the
  order you last used them, and let go to stay on one. One quick press flips between the last two tabs, and
  <kbd>Ctrl</kbd><kbd>⇧</kbd><kbd>Tab</kbd> walks the other way, from the least recently used tab. On macOS it's the
  Control key, as in Helium. Tabs opened in the background join the end of the order. As in Helium, there's no switcher
  overlay: each press shows its tab. The other next and previous tab shortcuts (<kbd>Ctrl</kbd><kbd>PgDn</kbd>, and
  <kbd>⌘</kbd><kbd>⇧</kbd><kbd>]</kbd> on macOS) still follow the tab strip. So does Ctrl+Tab, if you untick **Cycle Tabs
  in Recently Used Order** in the tab strip's context menu.
- [x] **Tab hibernation**, Xenon's take on the Memory Saver that Helium has from Chromium: a background tab left unused
  for two hours gives up its WebContent process, and its favicon turns grey. It keeps its title, favicon, URL, back/forward
  history, and scroll position, and reloads when you select it. A window's current tab never hibernates, and neither do
  pinned tabs, tabs playing audio (or that were a minute ago), with picture-in-picture or DevTools open, waiting on a
  dialog, or holding text typed into a form and not sent. Right-click the tab strip for **Tab Hibernation → Off** or a
  timeout from 15 minutes to 6 hours, or a background tab for **Hibernate Tab** (`UI/TabHibernation.cpp`, engine patch
  `0021`, which saves the scroll position first).
- [x] **Tab groups**: right-click a tab and pick **Add Tab to New Group** (or **Add Tab to Group** when there are
  groups already). A group has a title and one of nine colors picked to sit with the rose, and shows as a chip before its
  tabs with a line below them, or as a header row above them in the sidebar. Click the chip to collapse or open the
  group; right-click it to rename, recolor, ungroup, or close the group, open a new tab in it, or move it to a new
  window. Drag a tab between two of a group's tabs to add it, or out of the group to take it out. Tabs opened from a
  grouped tab join its group, Ctrl+Tab passes over collapsed groups, and groups come back with a restored session
  (`UI/TabGroups.cpp`).
- [x] The DuckDuckGo-style bang list (13,000+ triggers), plus a keyword chip in the address bar while a bang is typed.
  The list is Kagi's (MIT), which has 12,484 of DuckDuckGo's 13,566 triggers. Xenon doesn't ship the other 1,082: 889 are
  not in Kagi's list, and 193 are Kagi's own services or site-restricted searches that the script leaves out. DuckDuckGo
  publishes `bang.js` without a license, so it isn't a source.
- [x] **Its own profile directory**: Xenon keeps its settings, history, cookies, extensions, cache, and crash reports
  apart from Ladybird's, so the two can be installed side by side. See [Where Xenon keeps its data](#where-xenon-keeps-its-data).
- [x] Xenon's own palette, built around **#ff0050** (`UI/BrandColors.h` for the chrome, `UI/Resources/about-pages/webui.css`
  for the internal pages)
- [x] Xenon's own app icon: two orbits woven around a nucleus, in white and rose
- [x] uBlock Origin's filter set for Ladybird's content blocker, from `xenon-ublock`: the bundled lists keep 99.3% of
  their filters (378,644 of 381,140), with uBlock Origin's scriptlets and redirect resources
- [x] **WebAuthn with security keys** (macOS): sites and password managers can register a USB security key and sign in
  with it (`navigator.credentials.create()`/`get()`), PINs included, and in cross-origin frames that are allowed to.
  A prompt over the page says what the key needs. Engine patches `0010`–`0015`; see `Documentation/WebAuthn.md`.
- [ ] WebAuthn, next: PIN setup and passkey management for security keys, and Linux. Platform passkeys (iCloud Keychain,
  Touch ID, phones) need an entitlement from Apple.

Web app installs, which depend on Chromium in Helium, are out of scope until Ladybird supports them.

## Updating the !bangs

```bash
./Meta/update-bangs.py                      # the latest release of kagisearch/bangs
./Meta/update-bangs.py --tag 202610261529   # or a given release
```

It writes `UI/Resources/bangs/bangs.json` and a `NOTICE.txt` with the release's checksum and license. The list is Kagi's
(MIT), which Helium uses too. DuckDuckGo's own `bang.js` has no license, so Xenon doesn't ship it. The build turns
`bangs.json` into a table with `Meta/generate-bang-table.py`, and `UI/Bangs.cpp` binary-searches it, so nothing parses
at startup. `Meta/check.py` checks the list. The script leaves out Kagi's own services (under triggers like `!help`), its
site-restricted searches, and the few that rewrite the query with a regular expression. It adds `!k` for Kagi itself.

## License

Code in `UI/` that came from Ladybird keeps its original BSD-2-Clause license and copyright headers. New Xenon files
use BSD-2-Clause too. The filter lists in `UI/Resources/filter-lists` are third-party data under their own licenses
(GPL-3.0, GPL-3.0 or CC BY-SA, and CC0), listed in its `NOTICE.txt`.
