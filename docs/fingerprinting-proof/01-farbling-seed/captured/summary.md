# Live compiled-Xenon results

Binary: `Build/release/xenon/bin/Xenon` (ninja xenon exit 0). Pages served at `http://site-a.test:8080` and `http://site-b.test:8080`.

## One-profile navigation (check 2)

Before the navigation-seed fix (`nav-before.json`), B's first inline script reported A's hash:

- a-direct / b-location / b-click: `standard:7d5c25dae1d15ec9`
- a-back / aAgain: `standard:d2f25d6442f11028`

After (`nav-after.json` / `same-process.json`):

- a-direct / a-back / aAgain / iframeNavFirst / iframeNavDest: `standard:76a7da9b2d07d4fb`
- b-location / b-click: `standard:1d81cfe5db8482a8`

A ≠ B, A is stable across back and reload, and a self-navigating cross-site iframe keeps the top site's hash.

## Other proofs

1. Site A hash `standard:695ebf6315d3bcc6`, site B `standard:373321c7317907c6` — different (separate loads; superseded as the A≠B proof by the one-profile run above).
2. Site A is the same on three reloads in one process. Same-site iframe, cross-site iframe, and dedicated worker all report site A's hash.
3. Dedicated worker reports site A's hash (`standard:695ebf6315d3bcc6`).
4. Restart changes A from `standard:7c30fca05ff24d34` to `standard:018d37439018eeb8`, then stays stable.
5. With `offSites: ["http://site-a.test"]`, site A reports `off` (document, iframes, worker). Site B still farbles (`standard:242fe15402c9fef8`).
6. Without `--expose-internals-object`, `internalsPresent` is false and every readout is `no-internals`.

## Tests

See `unit-tests.txt` and `test-web.txt`. TestFarblingRNG 6/6. The one `Text/input/HTML/*window*` fail is `media-source-append-window-trims-audio.html` (`NotSupportedError: Unsupported MIME type`).

## Screenshots

Live: `settings-fingerprinting.png`. Unit-test: `blocking-popover-light.png`, `blocking-popover-site-off.png`. See README.
