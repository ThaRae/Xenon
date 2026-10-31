# Feature 5 proof: font enumeration farbling

Proven against `Build/release/xenon/bin/Xenon`. This guest has **15** fontconfig families (`fc-list`). The allow-function whitelist always permits CSS generics, bundled `SerenitySans` / `Noto Emoji`, and Ladybird's generic-fallback names (Liberation *, DejaVu Sans Mono, Arial, Times, …). Everything else is a stable 1-in-20 extra from the site seed.

## What is live

`allow_font_family(seed, family, language)` in `Libraries/LibWeb/Farbling/FontAllow.cpp`. Off allows all. Standard/Strict share the whitelist plus 1-in-20. Glyph outlines and metrics of an allowed font are not touched. URL `@font-face` is not filtered (snapshot faces win before the system-font walk).

## Proofs (one profile, one process for A vs B)

- A `fnv1a64:5a0964c3cf1f01d4` ≠ B `fnv1a64:3a3fb64c2c92a432`. Difference: B also has **DejaVu Sans**.
- Three reloads of A and a cross-site iframe of B inside A match A. Worker OffscreenCanvas `measureText` matches A.
- Width and canvas agree on every tested family. `document.fonts.check` is true for missing names (css-font-loading empty list) and false for rejected installed families (Lato, DejaVu Serif).
- Off A equals Off B and includes the installed text families from fc-list (DejaVu Sans/Serif/Mono, Lato, Liberation *). FontAwesome does not change span width even Off (icon font). Bundled SerenitySans / Noto Emoji appear though they are not in fc-list.
- Whitelist families that exist here are detected on A and B. Absent Windows/macOS names are never detected.
- URL `@font-face` of Ahem.ttf renders (width 1368 vs serif 1059). `local("Lato")` of a rejected family does not.
- Screenshots: `captured/a-standard.png` and `captured/a-off.png`.

## How to rerun

```bash
python3 docs/fingerprinting-proof/05-fonts/serve.py 8080
python3 docs/fingerprinting-proof/05-fonts/run-proof.py
```

## Tests

- `TestFontAllow` 5/5
- `TestAudioFudge` 9/9, `TestFarblingRNG` 6/6, `TestCanvasPerturb` 8/8, `TestWebGLFarbling` 8/8
- `python3 Meta/xenon.py test --no-build` passed
- `test-web --filter '*FontFace*'` 10 pass, `FontFaceSet-check` pass; `FontFace-arraybuffer-matching` crashed in isolation (likely pre-existing / Ahem-local, not skipped)
