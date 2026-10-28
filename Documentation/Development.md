# Developing Xenon

How the repository is put together, how to build and test it, and how to update the parts that come from elsewhere:
the Ladybird engine, the filter lists, and the !bang list. [CLAUDE.md](../CLAUDE.md) has the rules for changes.

- [How the repo fits together](#how-the-repo-fits-together)
- [Building](#building)
- [Tests and CI](#tests-and-ci)
- [Changing the engine](#changing-the-engine)
- [Updating Ladybird](#updating-ladybird)
- [Updating the filter lists](#updating-the-filter-lists)
- [Updating the !bangs](#updating-the-bangs)
- [The app icon](#the-app-icon)
- [Releases](#releases)

## How the repo fits together

| Path | What it is |
| --- | --- |
| `ladybird/` | The Ladybird engine, as a shallow git submodule pinned to a known-good upstream commit. |
| `Patches/ladybird/` | Xenon's changes to the engine, as a patch series applied to the pinned commit at build time. |
| `UI/` | Xenon's Qt frontend. It started as a copy of `ladybird/UI/Qt` and keeps that layout and the `Ladybird` C++ namespace, so upstream frontend fixes can still be diffed and merged in. |
| `UI/Resources/` | Xenon's versions of Ladybird's internal pages (welcome, new tab, settings, and the shared web UI stylesheet) and of its logo, with `xenon.css` for what its own pages share, the bundled filter lists, and the !bang list. The build ships these in place of upstream's copies, which stay untouched. |
| `UI/Icons/` | The app icon's artwork. |
| `Tests/` | Unit tests, built as `TestXenon*` targets. |
| `Meta/xenon.py` | Builds, runs, and tests Xenon. |
| `Meta/check.py` | Fast checks: formatting, and the mistakes that break Xenon quietly. |
| `Meta/patches.py` | Applies and exports the engine patch series. |
| `Meta/release.py` | Packages, publishes and tests releases, on macOS. |
| `VERSION.txt` | Xenon's version. |
| `Meta/CMake/XenonHook.cmake` | Injected into Ladybird's CMake build. Once the engine's libraries and helper processes are defined, it pulls in `UI/Xenon.cmake`, which defines the `xenon` target. |
| `Documentation/` | This file, the [feature notes](Features.md), and the deeper write-ups on extensions, WebAuthn, filter list updates, and releases. |

The submodule always points at an upstream commit. Xenon's engine changes live in `Patches/ladybird` as
`git format-patch` files, which `Meta/xenon.py` applies with `git am` on a local `xenon-patches` branch before it
configures the build, the way Helium patches Chromium. Each patch is a self-contained change that could go upstream.

Ladybird's build is configured with `-DCMAKE_PROJECT_ladybird_INCLUDE=Meta/CMake/XenonHook.cmake`, which defers an
`include(UI/Xenon.cmake)` until the end of Ladybird's top-level `CMakeLists.txt`. The `xenon` target links against
LibWebView, and the engine's helper processes (WebContent, RequestServer, ImageDecoder, ...) are built into
`Xenon.app`.

## Building

On macOS you need Xcode (or Homebrew LLVM), CMake 3.30+, Rust via rustup, and:

```bash
brew install autoconf autoconf-archive automake ccache cmake libtool nasm ninja pkg-config
```

`rust-toolchain.toml` links to the submodule's, so rustup picks Ladybird's pinned Rust version. Then:

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
ninja -C Build/release xenon     # an incremental rebuild once configured
```

## Tests and CI

```bash
./Meta/check.py            # formatting and Xenon's invariants, in seconds
./Meta/check.py --fix      # apply the formatters first
./Meta/xenon.py test       # unit tests, and Xenon run against local test pages
git config core.hooksPath .githooks   # run Meta/check.py before every push
```

`Meta/check.py` formats C++ with clang-format, web pages with prettier, and Python with ruff, at the versions
Ladybird's CI uses, read from the submodule. It installs clang-format, ruff, and PyYAML into `Build/tools` itself;
prettier needs `npx`. It also checks for the things that break Xenon quietly:

- an include that resolves to Ladybird's header instead of Xenon's
- a source file left out of the build
- a page override for a page upstream no longer ships
- a malformed or duplicate !bang
- Ladybird's violet coming back into the chrome
- a format string that would crash on non-ASCII text
- a workflow step whose shell script doesn't parse
- engine changes in `ladybird/` that aren't exported to `Patches/ladybird`, or a pinned commit that doesn't exist
  upstream

`Meta/xenon.py test` runs the unit tests in `Tests/`, then runs Xenon headless: it renders `about:welcome`, checks that
a fresh start writes only to Xenon's directories, and checks content blocking, daily filter list updates, the blocking
dashboard, extensions, WebAuthn, and password manager installs against pages and servers it starts locally. Session
restore needs Xenon's windows, since a headless run never saves its session, so that check opens them: it kills Xenon,
and then quits it, and checks that the tabs come back with their pages. Linux CI runs the tests under Xvfb, so the
windows are real X11 ones; without a display, they go to Qt's offscreen platform.

CI runs on GitHub Actions, on GitHub's own runners:

| Workflow | Runs on | Does |
| --- | --- | --- |
| `.github/workflows/ci.yml` | pushes to `main`, pull requests, by hand | `Meta/check.py --online`, then a Linux build and a macOS build side by side, each running `Meta/xenon.py test`, with the welcome page render as an artifact |
| `.github/workflows/upstream.yml` | weekly, by hand | the Linux build against Ladybird's latest `master`, plus a list of what changed upstream in `UI/Qt` and the overridden pages, so a submodule bump holds no surprises |
| `.github/workflows/release.yml` | by hand, pull requests that change it | `Meta/release.py test` and `package` on macOS, from `main`'s build caches, with the release's installer, archive and appcast as an artifact, and with **publish** ticked, `Meta/release.py publish` to ThaRae/xenon-releases |

- **Linux** builds in Ladybird's own CI image (`ghcr.io/ladybirdbrowser/ladybird-ci`, at the version in
  `ladybird/Meta/Docker/ci/VERSION`), so the compilers and system libraries match what Ladybird is tested with.
- **macOS** builds natively on `macos-26`, with the Xcode version and Homebrew packages that Ladybird's own CI uses
  (`ladybird/.github/actions/setup`).
- **Caches**: ccache's objects (`Build/caches`) and vcpkg's prebuilt packages (`ladybird/Build/caches`) are kept in
  GitHub's cache, one entry per platform. Builds on `main` save them, and a pull request starts from `main`'s. A pull
  request whose build misses ccache more than 500 times (a Ladybird bump, say) saves its own too, so its next push
  starts from there instead of recompiling the same files; smaller ones don't, to leave room in GitHub's 10 GB. A cold
  build takes a few hours, so the build step stops itself in time to save the caches, and the next run carries on from
  there.

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

`git am --3way` falls back to a merge with the blobs a patch was made from, which a shallow `ladybird/` has only for the
first patch. If a later one fails with "Repository lacks necessary blobs", apply the series to the old pin once, in a
throwaway worktree (`git -C ladybird worktree add --detach /tmp/old-series <old pin>`, then `git am` the patches there),
and retry; the blobs are then in the shared object store.

If the engine's API changed, compare `ladybird/UI/Qt` against the commit Xenon last synced with, and port the relevant
changes into `UI/`. The weekly Upstream workflow applies the series to `master`, names any patch that no longer
applies, builds, and lists those UI changes, so a bump holds no surprises.

## Updating the filter lists

```bash
./Meta/update-filter-lists.py              # rebuilds them in ../xenon-ublock, then copies them in
./Meta/update-filter-lists.py --no-build   # copies what ../xenon-ublock last built
```

The lists come from [xenon-ublock](https://github.com/ThaRae/xenon-ublock), which converts uBlock Origin's lists
for Ladybird's content blocker. The script expects its checkout next to this one, at `../xenon-ublock`. Xenon ships its `helium` preset, except for Peter Lowe's list, which states no license, and Helium's own two
lists (AGPL-3.0, and almost nothing survives conversion). The content blocker patches (`Patches/ladybird`
`0004`–`0009`) let the lists keep scriptlets, procedural filters, and `$redirect`, `$removeparam`, `$popup` and CNAME
filters. uBlock Origin's own lists go in `trusted-lists/`, and its scriptlets and redirect surrogates in
`resources/`.

A list costs memory in every WebContent process, since each builds its own engine from all of them: roughly 5 to 12 MiB
per process for a large list like EasyList or AdGuard's. `||host^$document` and `||host/path^$document` filters are the
exception: engine patch `0027` keeps those as hashes (8 bytes each), so a list made of them, like URLhaus, costs almost
nothing.

`UI/Resources/filter-lists/NOTICE.txt` credits every list's source and license, and the GPL-3.0 text ships beside
them. `Meta/check.py` fails if a list ships without a cleared license. `Meta/xenon.py test` checks that Xenon applies
the lists without being given any, with blocking on and off: requests that EasyList and EasyPrivacy block, and a
scriptlet and a hiding rule for a listed site, on a page served locally at that site's address.

Between releases, Xenon fetches the lists daily from
[ThaRae/xenon-filter-lists](https://github.com/ThaRae/xenon-filter-lists). That public repository's GitHub Actions
workflow (kept here as `Meta/ci/filter-lists-workflow.yml`) runs the same script every day with `--output`, through
`Meta/ci/publish-filter-lists.sh`, and signs the manifest. See [FilterListUpdates.md](FilterListUpdates.md).

## Updating the !bangs

```bash
./Meta/update-bangs.py                      # the latest release of kagisearch/bangs
./Meta/update-bangs.py --tag 202610261529   # or a given release
```

It writes `UI/Resources/bangs/bangs.json` and a `NOTICE.txt` with the release's checksum and license. The list is
Kagi's (MIT), which Helium uses too. DuckDuckGo's own `bang.js` has no license, so Xenon doesn't ship it. The build
turns `bangs.json` into a table with `Meta/generate-bang-table.py`, and `UI/Bangs.cpp` binary-searches it, so nothing
parses at startup. `Meta/check.py` checks the list.

The script leaves out Kagi's own services (under triggers like `!help`), its site-restricted searches, and the few
that rewrite the query with a regular expression. It adds `!k` for Kagi itself. Of DuckDuckGo's 13,566 triggers, Xenon
ships 12,484. Of the other 1,082, 889 aren't in Kagi's list, and 193 are Kagi's own services or site-restricted
searches that the script leaves out.

## The app icon

`UI/Icons/xenon.svg` is the artwork, and `xenon-small.svg` its version for 32px and below. After changing either,
rerun:

```bash
./Meta/generate-app-icon.py
```

It renders the macOS iconset, the window icon, and Xenon's versions of Ladybird's logo in `UI/Resources/icons`, which
the build ships in place of Ladybird's (internal pages use them as their favicon). It needs `rsvg-convert`
(`brew install librsvg`).

## Releases

Xenon updates itself on macOS with Sparkle, from releases published to
[ThaRae/xenon-releases](https://github.com/ThaRae/xenon-releases) on GitHub. They're made on a Mac:

```bash
./Meta/release.py test       # a release updates itself to the next, and turns down tampered ones
./Meta/release.py package    # builds Xenon, and writes a signed Xenon.app, its installer (a .dmg), its archive and the appcast to Build/distribution
./Meta/release.py publish    # publishes them, and tags the commit
```

Bump `VERSION.txt` first. Releases are signed with a self-signed certificate and an update key, both in the login
keychain of the Mac they're made on. [Updates.md](Updates.md) covers setting those up, what Xenon checks, and the test.
The build fetches Sparkle's release (`UI/Xenon.cmake`) by itself, on macOS.

`.github/workflows/release.yml` runs `test` and `package` on GitHub's macOS runner instead, by hand, with the
certificate and the update key as the repository's secrets, and keeps the installer, the archive and the appcast as the
run's artifact. With **publish** ticked, on `main`, it runs `publish` too, with a token for ThaRae/xenon-releases.
