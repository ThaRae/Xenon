# Feature 4 proof: WebAudio farbling

What was proven against the compiled Xenon binary (`Build/release/xenon/bin/Xenon`), not a mock. OfflineAudioContext rendering and AnalyserNode readouts both run headless in this guest.

One page-wide fudge in `[0.999, 1.000)` is derived from the ESO seed (`Farbling::audio_fudge_factor`, key `"webaudio"`) and cached on the environment. Strict uses the same fudge, not Brave's LFSR sample replacement. Off is `1.0`. The audio graph and AudioDestinationNode output are not touched.

## Once-only mechanism

`getChannelData` returns a live `Float32Array` over the buffer's internal storage. The fudge is multiplied into that storage on first script readout (`AudioBuffer::ensure_script_fudge`), then a per-buffer flag blocks a second multiply. `copyFromChannel` runs the same helper first, so it matches the live view. `copyToChannel` writes script values and sets the flag without multiplying, so a write-then-read does not drift.

Engine fill of an OfflineAudioContext result used to call `getChannelData` and then overwrite the view, which armed the flag on zeros and left real samples unfarbled. Fill now copies through `channel_data()` so the first script readout still sees unfarbled samples and can apply the fudge once.

Limits: if script reads a buffer and then plays it through `AudioBufferSourceNode`, `acquire_contents` snapshots the already-farbled storage, so playback is about `20*log10(fudge)` ≈ 0.0026 dB quieter. That is inaudible. Destination output of a live graph is still clean. `copyFromChannel` has no `relevant_global_object`, so it takes the fudge from `incumbent_settings_object()`; `getChannelData` uses the relevant global.

Frequency readouts apply the fudge in the linear domain: `getFloatFrequencyData` / `getByteFrequencyData` add `20*log10(fudge)` to each dB bin (Brave's FarbleConvertFloatToDb), then the byte conversion runs on those corrected dB values. Time-domain readouts still multiply samples.

## Standard, one profile, one process

Triangle `OscillatorNode` at 10000 Hz into `DynamicsCompressorNode`, `OfflineAudioContext` 44100×1. Hash is FNV-1a 64 of the full `getChannelData` (`crypto.subtle` is missing, same as features 2–3).

- A `fnv1a64:a09ea98792bd6007` ≠ B `fnv1a64:1b1e670c16384835`.
- Three reloads of A in the same process keep A's hash. A cross-site iframe of B inside A matches A (nested documents inherit the top-level seed).
- `getChannelData` twice is the same live view and equals `copyFromChannel`.
- Off A equals Off B equals test-web (no seed store, farbling Off) at `fnv1a64:641ed91ed5e6a17d`.
- Full-buffer ratio vs Off: n=44099 nonzero samples of 44100, max relative diff 0.0611% (under 0.1%), ratio span 1.07e-7 around 0.9993886.
- On A, `getFloatFrequencyData` minus Off is a constant `20*log10(fudge)` in every finite bin: min -0.005313873291015625, max -0.00531005859375, expected -0.005312379493187838, n_finite=1024.
- `copyToChannel` then `getChannelData` twice matches the written source and does not drift.
- Dedicated-worker `OfflineAudioContext` is missing (`Error: OfflineAudioContext missing`). Unsupported, not faked.

Byte frequency hash `fnv1a64:2db7621718bafe10` is identical on A, B, and Off — the fudge does not move quantized byte frequency data for this signal, and no extra noise was added.

## Strict (test-only `internals.setFarblingLevel("strict")`, no UI)

Same fudge formula as Standard. A fresh profile gets a new seed, so the Strict hash is not compared to Standard A from another profile.

## How to rerun

```bash
echo '127.0.0.1 site-a.test site-b.test' | sudo tee -a /etc/hosts
python3 docs/fingerprinting-proof/04-webaudio/serve.py 8080
python3 docs/fingerprinting-proof/04-webaudio/run-proof.py
```

## Tests on this binary

- `TestAudioFudge` 9/9
- `TestWebGLFarbling` 8/8, `TestCanvasPerturb` 8/8, `TestFarblingRNG` 6/6
- `python3 Meta/xenon.py test --no-build` passed
- `test-web --filter '*WebAudio*'` 34/0 (includes unpatched fingerprint hash)
- `test-web --filter '*webgl*'` 21/0 and `*WebGL*` 2/0
