# Live compiled-Xenon results

Binary: `Build/release/xenon/bin/Xenon` (exit 0). Pages served at `http://site-a.test:8080` and `http://site-b.test:8080`.

1. Site A hash `standard:695ebf6315d3bcc6`, site B `standard:373321c7317907c6` — different.
2. Site A is the same on three reloads in one process. Same-site iframe, cross-site iframe, and dedicated worker all report site A's hash.
3. Dedicated worker reports site A's hash (`standard:695ebf6315d3bcc6`).
4. Restart changes A from `standard:06961c14927158d6` to `standard:e456ac458b7b24d9`, then stays stable.
5. With `offSites: ["http://site-a.test"]`, site A reports `off` (document, iframes, worker). Site B still farbles (`standard:dd1729cf6a6a4a87`).
6. Without `--expose-internals-object`, `internalsPresent` is false and every readout is `no-internals`.

Raw JSON: `site-a.json`, `site-b.json`, `restart-1.json`, `restart-2.json`, `off-a.json`, `off-b.json`, `no-internals.json`.
