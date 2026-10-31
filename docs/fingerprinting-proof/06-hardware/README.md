# Feature 6 proof: hardwareConcurrency and deviceMemory

Proven against `Build/release/xenon/bin/Xenon`. Guest: **nproc=12**, **MemTotal=32853076 kB** (~31.3 GiB). Ladybird's unfarbled `deviceMemory` is the spec power-of-two GiB clamped to **[1, 4]**, so the real bucket here is **4**. Standard only picks from `{1, 2, 4}` capped at that real bucket.

## What is live

- `farbled_hardware_concurrency(seed, real)` in `Libraries/LibWeb/Farbling/HardwareConcurrency.cpp`. Off returns real. Standard draws a stable value in `[2, real]` from RNG(seed, `"hardwareConcurrency"`); if real < 2, real. Strict returns **4**, or real if real < 4.
- `farbled_device_memory(seed, real)` in `Libraries/LibWeb/Farbling/DeviceMemory.cpp`. Off returns real. Standard picks from Ladybird-returnable buckets `{1, 2, 4}` capped at the real bucket (never 0.25/0.5, which unpatched Ladybird never returns). Strict returns **8**, or the real spec bucket if lower (here 4).
- Hooks: `NavigatorConcurrentHardwareMixin::hardware_concurrency(JS::Realm&)` and `NavigatorDeviceMemoryMixin::device_memory(JS::Realm&)`, used by both `Navigator` and `WorkerNavigator`. Seed is `principal_realm_settings_object(realm).farbling_seed()`.
- Nested documents inherit the **top-level** farbling seed (features 1–5). A cross-site iframe inside A reports A's values. Dedicated and shared workers match the creating page.
- The process thread pool is unchanged; only the script-visible values are replaced.
- `deviceMemory` is `[SecureContext]` (`NavigatorDeviceMemory.idl`). `http://site-a.test` is not potentially trustworthy, so the attribute is absent there. `127.0.0.0/8` and `localhost` expose it.
- Layout tests (`is_test_mode`) get an Off seed so `navigator-compatibility-mode-exposure.html` still sees the real count. Proofs and the shipped Xenon binary farble. Added in feature 6, not earlier.
- Default `NavigatorCompatibilityMode` is **Chrome** (`LibWebCommon/Loader/UserAgent.h`). A page cannot switch it. The debug menu (Chrome/Gecko/WebKit) can, for the session; it is not a per-site setting. WebKit applies `min(farbled, 8)` (or 4 if real < 8) after farbling.

## Same-site stability

The earlier 8 vs 5 mismatch was two profiles, not a bug:

- `captured/same-process.json` A Standard **5**, seed `standard:f25b873d4d13c545`
- a later separate-process `spread.json` site-a.test **8**, seed `standard:94e9bcadd0584b82`

This run uses **one profile and one process** (`captured/same-process.json`, seed `standard:660407c05278c58c`). site-a.test is **10** on the spread page, 3 reloads, same-site iframe, dedicated worker, shared worker, and the cross-site iframe.

## Proofs

- nproc=12. A Standard **10**, B Standard **9**.
- Off A and Off B both **12**. Loopback Off `deviceMemory` **4** / **4**.
- Strict A and B **hardwareConcurrency 4**. Loopback Strict `deviceMemory` **4** / **4**.
- Eight-site spread in that same process: concurrency `{4, 8, 9, 10}` all in `[2, 12]`. Loopback `deviceMemory` `{1, 2, 4}` only.

## How to rerun

```bash
python3 docs/fingerprinting-proof/06-hardware/serve.py 8080
python3 docs/fingerprinting-proof/06-hardware/run-proof.py
```

## Tests

- `TestHardwareFarbling` 10/10
- `test-web --filter '*canvas*'` Pass: 551; `*webgl*` 21; `*WebGL*` 2; `*WebAudio*` 34; `*FontFace*` 11; `*font*` 452
- `navigator-compatibility-mode-exposure.html` Pass: 1 with test-mode Off
