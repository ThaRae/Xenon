# Feature 3 proof: WebGL / WebGL2 farbling

What was proven against the compiled Xenon binary (`Build/release/xenon/bin/Xenon`), not a mock. Software GL (ANGLE + llvmpipe) works in this guest.

Farbling happens in WebContent, after RemoteWebGLTransport returns real values and before script sees them. The GPU process never gets the seed.

## Standard

Brave's 12 WebGL2 `MAX_*` pnames: real, or real minus 1 with probability 1/2 from a per-pname PRNG stream. WebGL1 has none of those pnames. `UNMASKED_VENDOR_WEBGL` / `UNMASKED_RENDERER_WEBGL` are the fixed strings `Xenon` / `Xenon Generic Renderer` for every user. `getSupportedExtensions` / `getExtension` are honest. `readPixels` is honest.

## One profile, one process

- WebGL and WebGL2 contexts both work.
- A and B differ on `MAX_VERTEX_UNIFORM_BLOCKS`, `MAX_VERTEX_OUTPUT_COMPONENTS`, `MAX_VARYING_COMPONENTS`, `MAX_FRAGMENT_UNIFORM_COMPONENTS`, `MAX_FRAGMENT_UNIFORM_BLOCKS`, `MAX_UNIFORM_BUFFER_BINDINGS`, `MAX_COMBINED_UNIFORM_BLOCKS`.
- Three reloads of A in that process keep the same WebGL2 hash. A cross-site iframe of B inside A matches A.
- Every farbled value is the Off (real) value or that minus 1. Binding a uniform buffer at the reported `MAX_UNIFORM_BUFFER_BINDINGS - 1` succeeds.
- Standard vendor/renderer: `Xenon` / `Xenon Generic Renderer`. Off: `Google Inc. (Mesa)` / `ANGLE (Mesa, llvmpipe ...)`.
- Extension lists match On vs Off; every listed name returns non-null from `getExtension`.
- Dedicated-worker OffscreenCanvas WebGL2 matches the main thread on that site.
- `readPixels` of a cleared 8×8 canvas is `[64, 128, 191, 255]` on A, B, and Off.

Raw JSON: `captured/same-process.json`, `captured/site-a.json`, `captured/off-both.json`. Summary: `captured/summary.json`.

## How to rerun

```bash
echo '127.0.0.1 site-a.test site-b.test' | sudo tee -a /etc/hosts
python3 docs/fingerprinting-proof/03-webgl/serve.py 8080
python3 docs/fingerprinting-proof/03-webgl/run-proof.py
```

## Tests on this binary

- `TestWebGLFarbling` 8/8
- `TestCanvasPerturb` 8/8, `TestFarblingRNG` 6/6
- `python3 Meta/xenon.py test --no-build` passed
- `test-web --filter '*webgl*'` 21/0 and `*WebGL*` 2/0
- `test-web --filter '*canvas*'` crowded run 549/2 (`createImageBitmap-bounds`, `zerosize.stroke.worker`); both passed in isolation, same flakes as feature 2
