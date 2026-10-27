# Xenon

A browser frontend for the Ladybird engine, with Helium browser's feature set. See README.md for the architecture
and the feature checklist.

## Rules

- `ladybird/` is a submodule pinned to an upstream commit. Xenon's engine changes are a patch series in
  `Patches/ladybird`, applied at build time. Make them with `Meta/patches.py` (apply, commit in `ladybird/`, export),
  never by committing the submodule at a local commit. Keep each patch generic and upstreamable, with an upstream-style
  subject (`LibWeb: ...`), and keep the engine change as small as the feature allows.
- Port Helium's features, not its look. Xenon has its own palette with a **#ff0050** accent: a rose scale in
  `UI/BrandColors.h` for the Qt chrome, and `--xenon-rose*` variables in `UI/Resources/about-pages/webui.css` for the
  internal pages. Use those rather than Ladybird's violet/indigo, and don't retheme anything to match Helium.
- To change one of Ladybird's internal pages, put Xenon's version in `UI/Resources/about-pages/` and add it to
  `XENON_ABOUT_PAGE_OVERRIDES` in `UI/Xenon.cmake`; files upstream doesn't have go in `XENON_ABOUT_PAGE_ADDITIONS`.
  Never edit the copy in `ladybird/Base/res`. Xenon's own pages share `xenon.css` (the `--xenon-*` tokens, the glow,
  the periodic-table mark).
- `UI/` started as a copy of `ladybird/UI/Qt`. Keep upstream's structure, naming, comment style, and the `Ladybird`
  namespace, so upstream changes still merge cleanly. Put Xenon-only features in their own files (`Bangs.cpp`,
  `XenonSettings.cpp`) where possible, and keep edits to copied files small.
- Includes are `<UI/Foo.h>`, never `<UI/Qt/Foo.h>`, which would resolve to Ladybird's copy of the header.
- Settings that Ladybird already has stay in `WebView::Application::settings()`. Xenon-only settings go through
  `UI/XenonSettings`, which is backed by QSettings.
- New source files go in `UI/Xenon.cmake`.
- The app icon's PNGs (`UI/Icons/xenon.png` and `UI/Icons/macos/app_icon.iconset`) are generated. Change
  `UI/Icons/xenon.svg` (or `xenon-small.svg`, used at 32px and below) and rerun `Meta/generate-app-icon.py`.
- The filter lists in `UI/Resources/filter-lists` are generated. Refresh them with `Meta/update-filter-lists.py`
  (from xenon-ublock), never by hand. Xenon passes them to Ladybird as `--content-blocker-list` arguments
  (`UI/FilterLists.cpp`). Lists added in `create_platform_options()` instead are dropped the first time a blocking
  setting changes.

## Commands

- `./Meta/xenon.py build`: configure (the first run builds the vcpkg deps, which takes a long time) and build
  the `xenon` target.
- `./Meta/xenon.py run`: build and launch `Build/release/bin/Xenon.app`.
- `ninja -C Build/release xenon`: incremental rebuild once configured.
- `./Meta/check.py` (`--fix` to format): run before committing. CI runs it too, along with `./Meta/xenon.py test`
  (unit tests in `Tests/`, plus a headless render of about:welcome).
- New unit tests are `Tests/Test*.cpp`, registered in `UI/Xenon.cmake` as `TestXenon*` targets under `xenon-tests`.
- `./Meta/patches.py apply` / `export` / `pin` / `status`: the engine patch series (see the top of the script).
