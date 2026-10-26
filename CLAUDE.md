# Xenon

A browser frontend for the Ladybird engine, with Helium browser's feature set. See README.md for the architecture
and the feature checklist.

## Rules

- Don't edit `ladybird/`. It's a pinned, unmodified submodule. Engine-side changes need a different approach, like a
  patch series applied at build time, so raise that before doing it.
- Port Helium's features, not its look. Xenon has its own palette, so don't retheme `UI/ChromeStyle.cpp` to match
  Helium.
- `UI/` started as a copy of `ladybird/UI/Qt`. Keep upstream's structure, naming, comment style, and the `Ladybird`
  namespace, so upstream changes still merge cleanly. Put Xenon-only features in their own files (`Bangs.cpp`,
  `XenonSettings.cpp`) where possible, and keep edits to copied files small.
- Includes are `<UI/Foo.h>`, never `<UI/Qt/Foo.h>`, which would resolve to Ladybird's copy of the header.
- Settings that Ladybird already has stay in `WebView::Application::settings()`. Xenon-only settings go through
  `UI/XenonSettings`, which is backed by QSettings.
- New source files go in `UI/Xenon.cmake`.

## Commands

- `./Meta/xenon.py build`: configure (the first run builds the vcpkg deps, which takes a long time) and build
  the `xenon` target.
- `./Meta/xenon.py run`: build and launch `Build/release/bin/Xenon.app`.
- `ninja -C Build/release xenon`: incremental rebuild once configured.
