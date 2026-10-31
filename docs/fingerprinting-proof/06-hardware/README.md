# Feature 6 proof: hardwareConcurrency and deviceMemory

Proven against `Build/release/xenon/bin/Xenon`. Guest: **nproc=12**, **MemTotal=32853076 kB** (~31.3 GiB). Ladybird's unfarbled `deviceMemory` is the spec power-of-two GiB clamped to **[1, 4]**, so the real bucket here is **4**.

## What is live

- `farbled_hardware_concurrency(seed, real)` in `Libraries/LibWeb/Farbling/HardwareConcurrency.cpp`. Off returns real. Standard draws a stable value in `[2, real]` from RNG(seed, `"hardwareConcurrency"`); if real < 2, real. Strict returns **4**, or real if real < 4.
- `farbled_device_memory(seed, real)` in `Libraries/LibWeb/Farbling/DeviceMemory.cpp`. Off returns real. Standard picks from `{0.25, 0.5, 1, 2, 4, 8}` capped at the real bucket. Strict returns **8**, or the real bucket if lower (here 4).
- Hooks: `NavigatorConcurrentHardwareMixin::hardware_concurrency(JS::Realm&)` and `NavigatorDeviceMemoryMixin::device_memory(JS::Realm&)`, used by both `Navigator` and `WorkerNavigator`. Seed is `principal_realm_settings_object(realm).farbling_seed()`.
- Nested documents inherit the **top-level** farbling seed (features 1–5). A cross-site iframe inside A reports A's values. Dedicated and shared workers match the creating page.
- The process thread pool is unchanged; only the script-visible values are replaced.
- `deviceMemory` is `[SecureContext]` (`NavigatorDeviceMemory.idl`). `http://site-a.test` is not potentially trustworthy, so the attribute is absent there. `127.0.0.0/8` and `localhost` expose it.
- Layout tests (`is_test_mode`) get an Off seed so `navigator-compatibility-mode-exposure.html` still sees the real count. Proofs and the browser farble.

## Proofs (one profile, one process for A vs B)

- nproc=12. A Standard **5**, B Standard **3**. Three reloads of A stay 5. Same-site iframe 5. Cross-site iframe of B inside A is 5 (top-level seed). Dedicated worker 5. Shared worker 5.
- Off A and Off B both **12**. Loopback Off `deviceMemory` **4** on 127.0.0.1 and 127.0.0.2 (Ladybird real).
- Strict A and Strict B both **hardwareConcurrency 4**. Loopback Strict `deviceMemory` **4** (real bucket < 8).
- Eight-site spread, one process, no retries: concurrency values `{2, 3, 8, 9, 11}` all in `[2, 12]`. Loopback `deviceMemory` values `{0.25, 1, 2, 4}` are spec buckets ≤ 4.

## How to rerun

```bash
python3 docs/fingerprinting-proof/06-hardware/serve.py 8080
python3 docs/fingerprinting-proof/06-hardware/run-proof.py
```

## Tests

- `TestHardwareFarbling` 10/10
- `TestFontAllow` 5/5, `TestAudioFudge` 9/9, `TestFarblingRNG` 6/6, `TestCanvasPerturb` 8/8, `TestWebGLFarbling` 8/8
- `python3 Meta/xenon.py test --no-build` passed
- `test-web --filter '*avigator*'` Pass: 10 (including `navigator-compatibility-mode-exposure.html`)
- `test-web --filter '*Worker*'` Pass: 56
