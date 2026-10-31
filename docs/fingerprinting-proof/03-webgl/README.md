# Feature 3 proof: WebGL / WebGL2 farbling

What was proven against the compiled Xenon binary (`Build/release/xenon/bin/Xenon`), not a mock. Software GL (ANGLE + llvmpipe) works in this guest.

Farbling happens in WebContent, after RemoteWebGLTransport returns real values and before script sees them. The GPU process never gets the seed.

## Standard

Brave's 12 WebGL2 `MAX_*` pnames: real, or real minus 1 with probability 1/2 from a per-pname PRNG stream. WebGL1 has none of those pnames. `UNMASKED_VENDOR_WEBGL` / `UNMASKED_RENDERER_WEBGL` are the fixed strings `Xenon` / `Xenon Generic Renderer` for every user. `getSupportedExtensions` / `getExtension` are honest. `readPixels` of a 32×32 two-triangle gradient is honest.

## One profile, one process

Worker OffscreenCanvas is read in the same process as main, on A and on B.

- WebGL and WebGL2 contexts both work.
- WebGL1 vendor/renderer: `Xenon` / `Xenon Generic Renderer` on A and B. Off A hash equals Off B hash, with real Mesa/llvmpipe strings.
- WebGL2 A ≠ B on several `MAX_*`. Three reloads of A keep the same hash. A cross-site iframe of B inside A matches A.
- Worker A equals main A; worker B equals main B (webgl and webgl2 hashes).
- `readPixels` of the gradient scene: hash `86daf8af5769f960` on A, B, and Off (webgl2); webgl hashes also match across A, B, Off.
- Binding a uniform buffer at the reported `MAX_UNIFORM_BUFFER_BINDINGS - 1` succeeds.

## Strict (test-only `internals.setFarblingLevel("strict")`, no UI)

On site A: seed `strict:…`, vendor/renderer generic, all 12 `MAX_*` are 0, `getShaderPrecisionFormat` / `getActiveAttrib` / `getActiveUniform` return null, `readPixels` into a 0xAA-filled buffer leaves it untouched.

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

