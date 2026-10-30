# Feature 1 proof: farbling seed store

What was proven against the compiled Xenon binary (`Build/release/xenon/bin/Xenon`), not a mock:

1. Site A and site B report different seed hashes.
2. Site A is stable across three reloads in one process. A same-site iframe and a cross-site iframe in site A both report site A's hash.
3. A dedicated worker started by site A reports site A's hash.
4. After restarting the browser process (same persisted tokens), site A's hash changes and is stable again.
5. With the per-site switch Off for site A, site A reports `off` and no seed; site B still farbles.
6. A normal page (no `--expose-internals-object`) cannot reach `internals`.

Captured JSON and screenshots are in `captured/`. See `captured/summary.md`.

## How to rerun

Map the test hosts, then from the Xenon checkout after `./Meta/xenon.py build`:

```bash
echo '127.0.0.1 site-a.test site-b.test' | sudo tee -a /etc/hosts
python3 docs/fingerprinting-proof/01-farbling-seed/serve.py 8080
# other terminal:
python3 docs/fingerprinting-proof/01-farbling-seed/run-proof.py
```

`run-proof.py` launches Xenon `--headless=manual --expose-internals-object` against `http://site-a.test:8080/page.html?run=…`. The page POSTs JSON to `/report`.
