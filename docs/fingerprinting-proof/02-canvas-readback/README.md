# Feature 2 proof: canvas script readback farbling

What was proven against the compiled Xenon binary (`Build/release/xenon/bin/Xenon`), not a mock.

HMAC-SHA256 of the 16-byte farbling seed over packed unpremultiplied RGBA8888. Each of 16 flips takes a 64-bit pixel index and a channel from `HMAC(seed, digest || counter)`, so canvases larger than 65536 pixels are covered. Only the copy handed to script is noised. The displayed bitmap stays untouched.

## One profile, one process

`run-proof.py` loads A (three reloads) then B in a single temp profile and a single browser process (`start.html` → `location.href` to B).

- Site A `cd41fa1e798004d3` ≠ site B `bffa52f5730e23f1`.
- Three reloads of A in that same process are `cd41fa1e798004d3` (equals that run's `a_sha`).
- A cross-site iframe of B inside A reports A's hash.
- A 1000×200 canvas with the scene only in rows 150–199: `getImageData` of that subrect is `dec5587cd4b569c3` on A and `98659a587ce31ff5` on B.
- `getImageData`, decoded `toDataURL("image/png")`, `toBlob`, and `OffscreenCanvas.convertToBlob` (main and dedicated worker) are the same pixels.

Raw JSON: `captured/same-process.json`. Summary: `captured/summary.json`.

## Off returns real pixels

One process, both sites Off (`offSites: ["http://site-a.test", "http://site-b.test"]`, `captured/off-both.json`):

- A Off `2e8c89ea26526f05`
- B Off `2e8c89ea26526f05`
- Decoded live screenshot of the displayed canvas (`captured/a-off.png`) `2e8c89ea26526f05`

Those three match. The display path is never farbled, so the screenshot is the unpatched reference.

Versus Standard, 16 bytes differ and the max per-channel delta is 1. A blank canvas stays all zeros.

## Screenshots (live, displayed canvas)

`captured/a-standard.png` and `captured/a-off.png` are headless screenshots of `shot.html` (320×180 canvas, no UI chrome). Pixel-diff count is 0.

## How to rerun

```bash
echo '127.0.0.1 site-a.test site-b.test' | sudo tee -a /etc/hosts
python3 docs/fingerprinting-proof/02-canvas-readback/serve.py 8080
python3 docs/fingerprinting-proof/02-canvas-readback/run-proof.py
```

## Tests on this binary

- `TestCanvasPerturb` 8/8 (includes flips past pixel 65536 on a 1000×200 buffer)
- `TestFarblingRNG` 6/6
- `python3 Meta/xenon.py test --no-build` passed
- `test-web --filter '*canvas*'` 551/0. One crowded run reported 549/2 (`createImageBitmap-bounds`, `2d.gradient.interpolate.zerosize.stroke.worker`); both passed in isolation and the next full run was 551/0.
