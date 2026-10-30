# Feature 2 proof: canvas script readback farbling

What was proven against the compiled Xenon binary (`Build/release/xenon/bin/Xenon`), not a mock.

HMAC-SHA256 of the 16-byte farbling seed over packed unpremultiplied RGBA8888, then 16 XORs of one RGB low bit. Only the copy handed to script is noised. The displayed bitmap stays untouched.

## One profile, one process

`run-proof.py` loads A then B in a single temp profile and a single browser process (`start.html` → `location.href` to B).

- Site A and site B report different `getImageData` hashes (`8200e031e4fcbfc7` vs `4d751c3876b93dcf`).
- A cross-site iframe of B inside A reports A's hash (nested documents inherit the top-level seed from feature 1).
- `getImageData`, decoded `toDataURL("image/png")`, `toBlob`, and `OffscreenCanvas.convertToBlob` (main and dedicated worker) are the same pixels.

Raw JSON: `captured/same-process.json`, `captured/site-a.json`. Summary: `captured/summary.json`.

## Reloads, Off, blank, noise size

1. Site A is identical across three reloads (`dd1edad8f0c5a863`).
2. With `offSites: ["http://site-a.test"]`, site A reports `off` and a different pixel hash. Versus Standard, 16 bytes differ and the max per-channel delta is 1.
3. A blank canvas stays all zeros.
4. Python-decoded PNG bytes match `getImageData` exactly (0 differing bytes).

## Screenshots (live, displayed canvas)

`captured/a-standard.png` and `captured/a-off.png` are headless screenshots of `shot.html` (320×180 canvas, no UI chrome) from the compiled Xenon. Pixel-diff count is 0: the compositor's copy is not farbled.

## How to rerun

Map the test hosts, then from the Xenon checkout after `./Meta/xenon.py build`:

```bash
echo '127.0.0.1 site-a.test site-b.test' | sudo tee -a /etc/hosts
python3 docs/fingerprinting-proof/02-canvas-readback/serve.py 8080
# other terminal:
python3 docs/fingerprinting-proof/02-canvas-readback/run-proof.py
```

`run-proof.py` launches Xenon `--headless=manual --expose-internals-object --force-cpu-painting --disable-sandbox` against `start.html` and `page.html` in one profile each, then Off and the two screenshots.

## Tests on this binary

- `TestCanvasPerturb` 7/7, `TestFarblingRNG` 6/6, `TestXenonFarblingSeedStore` 7/7
- `python3 Meta/xenon.py test --no-build` passed
- `test-web --filter '*canvas*'` 551/0 (Text, Ref, Offscreen, WPT, Screenshot). Feature 1 did not run this canvas subset; none of these tests failed here.
- `HTML/*Worker*` 2/0, same as feature 1.
