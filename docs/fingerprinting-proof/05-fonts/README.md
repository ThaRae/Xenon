# Feature 5 proof: font enumeration farbling

Proven against `Build/release/xenon/bin/Xenon` after the review fixes (check() leak removed, FontCascadeMemo re-lock, Ahem whitelist). This guest has **15** fontconfig families (`fc-list`). The allow-function whitelist always permits CSS generics, bundled `SerenitySans` / `Noto Emoji` / `Ahem`, and Ladybird's generic-fallback names (Liberation *, DejaVu Sans Mono, Arial, Times, …). Everything else is a stable 1-in-20 extra from the site seed.

## What is live

`allow_font_family(seed, family, language)` in `Libraries/LibWeb/Farbling/FontAllow.cpp`. Off allows all. Standard/Strict share the whitelist plus 1-in-20. Glyph outlines and metrics of an allowed font are not touched. URL `@font-face` is not filtered (snapshot faces win before the system-font walk). `document.fonts.check()` does not special-case rejected installed names: an empty matching list returns true, same as a name that is not installed.

## Proofs (one profile, one process for A vs B)

- A `fnv1a64:d822feb548658876` ≠ B `fnv1a64:5a0964c3cf1f01d4`. Difference: A also has **Lato**. No seed picking this run.
- Three reloads of A and a cross-site iframe of B inside A match A. Worker OffscreenCanvas `measureText` matches A.
- Width and canvas agree on every tested family.
- `check()` is **true** for absent names and for rejected installed names. Width and canvas stay **false** on both. That disagreement is the css-font-loading empty-list rule, not a leak: check(rejected) === check(absent) for every input. Allowed, whitelist, and URL web fonts are true on all three methods.
- Off A equals Off B and includes the installed text families from fc-list (DejaVu Sans/Serif/Mono, Lato, Liberation *). FontAwesome does not change span width even Off (icon font). Bundled SerenitySans / Noto Emoji appear though they are not in fc-list.
- Whitelist families that exist here are detected on A and B. Absent Windows/macOS names are never detected.
- URL `@font-face` of Ahem.ttf / HashSans.woff renders. `local("Lato")` of a rejected family does not.
- Screenshots: `captured/a-standard.png` and `captured/a-off.png`.
- Eight sites in one process (`spread.html`, one fresh profile, no retries): see the table in this README. `127.0.0.1` drew **DejaVu Sans**; the other seven hosts had the empty extra set.

## Retries

None this run. An earlier review looped profiles until B kept DejaVu Sans so A≠B would show; that loop is not repeated here. This run used one same-process profile (A got Lato), one spread profile, and one Off profile.

## check() table

`check` / `width` / `canvas` on A, B, and A Off. Rejected installed names match absent names on `check()` (all true). Off has no farbling rejects; the Off "rejected_installed" rows are installed faces whose metrics never beat the fallback (FontAwesome icon font, Lato weight faces).

### A (Standard)

| class | family | check | width | canvas |
| --- | --- | --- | --- | --- |
| absent | Arial | true | false | false |
| absent | Comic Sans MS | true | false | false |
| absent | Papyrus | true | false | false |
| rejected_installed | DejaVu Sans | true | false | false |
| rejected_installed | DejaVu Serif | true | false | false |
| rejected_installed | FontAwesome | true | false | false |
| allowed_installed | DejaVu Sans Mono | true | true | true |
| allowed_installed | Lato | true | true | true |
| allowed_installed | Liberation Mono | true | true | true |
| whitelist | Liberation Sans | true | true | true |
| whitelist | DejaVu Sans Mono | true | true | true |
| url_web_font | ProofWebFont | true | true | true |

### B (Standard)

| class | family | check | width | canvas |
| --- | --- | --- | --- | --- |
| absent | Arial | true | false | false |
| absent | Comic Sans MS | true | false | false |
| absent | Papyrus | true | false | false |
| rejected_installed | DejaVu Sans | true | false | false |
| rejected_installed | DejaVu Serif | true | false | false |
| rejected_installed | FontAwesome | true | false | false |
| allowed_installed | DejaVu Sans Mono | true | true | true |
| allowed_installed | Liberation Mono | true | true | true |
| allowed_installed | Liberation Sans | true | true | true |
| whitelist | Liberation Sans | true | true | true |
| whitelist | DejaVu Sans Mono | true | true | true |
| url_web_font | ProofWebFont | true | true | true |

### A Off

| class | family | check | width | canvas |
| --- | --- | --- | --- | --- |
| absent | Arial | true | false | false |
| absent | Comic Sans MS | true | false | false |
| absent | Papyrus | true | false | false |
| rejected_installed | FontAwesome | true | false | false |
| rejected_installed | Lato Black | true | false | false |
| rejected_installed | Lato Hairline | true | false | false |
| allowed_installed | DejaVu Sans | true | true | true |
| allowed_installed | DejaVu Sans Mono | true | true | true |
| allowed_installed | DejaVu Serif | true | true | true |
| whitelist | Liberation Sans | true | true | true |
| whitelist | DejaVu Sans Mono | true | true | true |
| url_web_font | ProofWebFont | true | true | true |

## (b)3

Width and canvas agree on every family. `check()` is true whenever the matching `@font-face` list is empty, so it is true for both absent and rejected installed names while width/canvas stay false. That is not a leak: the two classes are indistinguishable. Allowed, whitelist, and URL web fonts are true on all three methods.

## 8-site spread (one process, one profile, no retries)

| host | extras | hash |
| --- | --- | --- |
| site-a.test:8080 | (none) | fnv1a64:5a0964c3cf1f01d4 |
| site-b.test:8080 | (none) | fnv1a64:5a0964c3cf1f01d4 |
| 127.0.0.1:8080 | DejaVu Sans | fnv1a64:3a3fb64c2c92a432 |
| localhost:8080 | (none) | fnv1a64:5a0964c3cf1f01d4 |
| 127.0.0.2:8080 | (none) | fnv1a64:5a0964c3cf1f01d4 |
| 127.0.0.3:8080 | (none) | fnv1a64:5a0964c3cf1f01d4 |
| 127.0.0.4:8080 | (none) | fnv1a64:5a0964c3cf1f01d4 |
| 127.0.0.5:8080 | (none) | fnv1a64:5a0964c3cf1f01d4 |

## Feature 4 base comparison

Same `test-web --test-path ladybird/Tests/LibWeb --disable-sandbox --force-cpu-painting --per-test-timeout 30` command.

| test | F4 `da46d069b8` / xenon `f669e95` | this F5 after the review fix |
| --- | --- | --- |
| FontFace-arraybuffer-matching.html | Pass: 1 | Pass: 1 |
| local-font-sources.html | Pass: 1 | Pass: 1 |
| FontFaceSet-check.html | Pass: 1 | Pass: 1 |

F5 first crashed FontFace-arraybuffer-matching (non-recursive FontCascadeMemo mutex: `forget_matching` held the lock, then `font_farbling_query()` tried `set_farbling_query`). local-font-sources failed with `NetworkError: Failed to load font` because test-web file:// uses site key `file://` at Standard and rejected `local('Ahem')`. Both match F4 after the fixes. Crowded `*font*` on this F5: Pass: 452, Fail: 0.

## How to rerun

```bash
python3 docs/fingerprinting-proof/05-fonts/serve.py 8080
python3 docs/fingerprinting-proof/05-fonts/run-proof.py
```

`serve.py` binds `0.0.0.0` so the spread hosts `127.0.0.2`–`127.0.0.5` can connect.

## Tests

- `TestFontAllow` 5/5
- `TestAudioFudge` 9/9, `TestFarblingRNG` 6/6, `TestCanvasPerturb` 8/8, `TestWebGLFarbling` 8/8
- `python3 Meta/xenon.py test --no-build` passed
- `test-web --filter '*FontFace*'` Pass: 11; `--filter '*font*'` Pass: 452
