# Live compiled-Xenon results

Binary: `Build/release/xenon/bin/Xenon` (ninja xenon exit 0). Pages served at `http://site-a.test:8080` and `http://site-b.test:8080`.

## One-profile navigation (check 2)

Before the navigation-seed fix (`nav-before.json`), B's first inline script reported A's hash:

- a-direct / b-location / b-click: `standard:7d5c25dae1d15ec9`
- a-back / aAgain: `standard:d2f25d6442f11028`

After (`same-process.json` on this binary):

- a-direct / a-back / aAgain / iframeNavFirst / iframeNavDest: `standard:99c3180f5ac934bb`
- b-location / b-click: `standard:f0c2df6804de2306`

A ≠ B, A is stable across back and reload, and a self-navigating cross-site iframe keeps the top site's hash.

## Pending navigation

Before nested inherit (`nav-pending-before.json`): A's document `standard:c243c318ead4593d`, same-site iframe `standard:8aab0122e6bf2d74` (B), about:blank iframe stayed on A, then B's first read `standard:8aab0122e6bf2d74`.

After (`nav-pending-after.json`): delay / 204 / attachment document and iframes `standard:6a3700dd2e82ce96`, B first read `standard:7b7c1626519c362e`.

## Other proofs

1. Restart changes A from `standard:c840180c5f654ca5` to `standard:79b8c80f93c524ab`, then stays stable.
2. With `offSites: ["http://site-a.test"]`, site A reports `off` (document, iframes, worker). Site B still farbles (`standard:f2539a0a12ccfdf5`).
3. Without `--expose-internals-object`, `internalsPresent` is false and every readout is `no-internals`.

## Tests

See `unit-tests.txt` and `test-web.txt`. TestFarblingRNG 6/6. TestXenonFarblingSeedStore 7/7. The one `Text/input/HTML/*window*` fail is `media-source-append-window-trims-audio.html` (`NotSupportedError: Unsupported MIME type`).

## Screenshots

Live: `settings-fingerprinting.png`. Unit-test: `blocking-popover-light.png`, `blocking-popover-site-off.png`. See README.
