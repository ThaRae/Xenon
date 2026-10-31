# Feature 4 proof: WebAudio farbling

What was proven against the compiled Xenon binary (`Build/release/xenon/bin/Xenon`), not a mock. OfflineAudioContext rendering and AnalyserNode readouts both run headless in this guest.

One page-wide fudge in `[0.999, 1.000)` is derived from the ESO seed (`Farbling::audio_fudge_factor`, key `"webaudio"`) and cached on the environment. Strict uses the same fudge, not Brave's LFSR sample replacement. Off is `1.0`. The audio graph and AudioDestinationNode output are not touched.

## Once-only mechanism

`getChannelData` returns a live `Float32Array` over the buffer's internal storage. The fudge is multiplied into that storage on first script readout (`AudioBuffer::ensure_script_fudge`), then a per-buffer flag blocks a second multiply. `copyFromChannel` runs the same helper first, so it matches the live view. `copyToChannel` writes script values and sets the flag without multiplying, so a write-then-read does not drift.

Engine fill of an OfflineAudioContext result used to call `getChannelData` and then overwrite the view, which armed the flag on zeros and left real samples unfarbled. Fill now copies through `channel_data()` so the first script readout still sees unfarbled samples and can apply the fudge once.

Limit: if script reads a buffer and then plays it, `acquire_contents` snapshots the already-farbled storage. Destination output of a live graph is still clean.

## Standard, one profile, one process

Triangle `OscillatorNode` at 10000 Hz into `DynamicsCompressorNode`, `OfflineAudioContext` 44100×1. Hash is FNV-1a 64 of the full `getChannelData` (`crypto.subtle` is missing, same as features 2–3).

- A `fnv1a64:f20f08518ee958c0` ≠ B `fnv1a64:36996fe2f8aa9162`. Sums 257.969 vs 257.956.
- Three reloads of A in the same process keep A's hash. A cross-site iframe of B inside A matches A (nested documents inherit the top-level seed).
- `getChannelData` twice is the same live view and equals `copyFromChannel`.
- Off A equals Off B at `fnv1a64:641ed91ed5e6a17d` (the unfarbled buffer).
- Max relative difference vs Off is 0.0300% (under 0.1%). Ratio is a constant ≈ 0.999700 across 63 probes.
- `copyToChannel` then `getChannelData` twice matches the written source and does not drift.
- Dedicated-worker `OfflineAudioContext` is missing (`Error: OfflineAudioContext missing`). Unsupported, not faked.

AnalyserNode after the same offline graph: float frequency/time hashes differ A vs B vs Off. Byte frequency hash `fnv1a64:2db7621718bafe10` is identical on A, B, and Off — the fudge does not move quantized byte frequency data for this signal, and no extra noise was added. Byte time-domain hashes do differ.

## Strict (test-only `internals.setFarblingLevel("strict")`, no UI)

Same fudge formula as Standard. A fresh profile gets a new seed, so the Strict hash is not compared to Standard A from another profile.

## How to rerun

```bash
echo '127.0.0.1 site-a.test site-b.test' | sudo tee -a /etc/hosts
python3 docs/fingerprinting-proof/04-webaudio/serve.py 8080
python3 docs/fingerprinting-proof/04-webaudio/run-proof.py
```

## Tests on this binary

- `TestAudioFudge` 7/7
- `TestWebGLFarbling` 8/8, `TestCanvasPerturb` 8/8, `TestFarblingRNG` 6/6
- `python3 Meta/xenon.py test --no-build` passed
- `test-web --filter '*WebAudio*'` 33/0
- `test-web --filter '*webgl*'` 21/0 and `*WebGL*` 2/0
