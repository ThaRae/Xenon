# Feature 1 proof: farbling seed store

What was proven against the compiled Xenon binary (`Build/release/xenon/bin/Xenon`), not a mock.

## One profile, one process

`run-proof.py` loads A then B in a single temp profile and a single browser process (`start.html` → `location.href` to B → `history.back()` to A → `<a>` click to B → reload A).

- Site A and site B report different seed hashes on their first script read.
- Reloading A afterwards in that same process restores A's original hash.
- A cross-site iframe that navigates itself keeps the **top** site's hash.

Raw JSON: `captured/same-process.json` (after the navigation-seed fix). `captured/nav-before.json` is the same sequence on the unfixed binary, where B's first script reported A's hash.

## Other checks (same binary)

1. Site A is stable across three reloads. A same-site iframe, a cross-site iframe, and a dedicated worker all report site A's hash (`captured/site-a.json`).
2. After restarting the browser process (same persisted tokens), site A's hash changes and is stable again (`captured/restart-1.json`, `captured/restart-2.json`).
3. With the per-site switch Off for site A (`offSites: ["http://site-a.test"]`), site A reports `off` and no seed; site B still farbles (`captured/off-a.json`, `captured/off-b.json`).
4. A normal page (no `--expose-internals-object`) cannot reach `internals` (`captured/no-internals.json`).

## Screenshots

Live, from the compiled Xenon (`--headless=screenshot about:settings` with `offSites: ["http://site-a.test"]`):

- `captured/settings-fingerprinting.png` — Fingerprinting protection row (Standard) and Sites without fingerprinting protection listing `http://site-a.test`.

Unit-test renders (`TestXenonBlockingPopover`):

- `captured/blocking-popover-light.png`
- `captured/blocking-popover-site-off.png`

A live shield popover was not captured. Headless screenshot loads a URL; it cannot click the address-bar shield. Xvfb is installed, but there is no GUI driver here to open the popover on a real window. The site-off unit-test render is the Off-for-site-a.test popover.

`blocking-popover-dark.png` was removed: it was byte-identical to the light render.

## How to rerun

Map the test hosts, then from the Xenon checkout after `./Meta/xenon.py build`:

```bash
echo '127.0.0.1 site-a.test site-b.test' | sudo tee -a /etc/hosts
python3 docs/fingerprinting-proof/01-farbling-seed/serve.py 8080
# other terminal:
python3 docs/fingerprinting-proof/01-farbling-seed/run-proof.py
```

`run-proof.py` launches Xenon `--headless=manual --expose-internals-object` against `http://site-a.test:8080/start.html` in one profile, then the restart / Off / no-internals pages. Each page POSTs JSON to `/report`.
