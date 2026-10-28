# Fingerprinting protection

Xenon's fingerprinting protection is an independent reimplementation of the idea behind Brave's "farbling"
([Brave's write-up](https://brave.com/privacy-updates/4-fingerprinting-defenses-2.0/)). No Brave code is used: Brave's
implementation lives in Chromium and is MPL-2.0, while this one is written for Ladybird's LibWeb under BSD-2-Clause.
What's shared is the design: small, deterministic randomization keyed per session and per site.

## Seeds

`Web::Fingerprinting::session_secret()` is 64 random bits from the system's random source, made once per process.
`seed_for_host()` mixes it with the host of the origin asking, so one site always sees the same values in a session,
two sites see unrelated values, and a restart changes all of them. Opaque origins use the session secret alone.
Each API derives its own stream from the seed (`seed_for_key()`), so learning one value tells nothing about another.

The secret is per WebContent or WebWorker process. With site isolation each site has its own process anyway; a
dedicated worker may see different values from its page.

## What's farbled

| API | What happens |
| --- | --- |
| `HTMLCanvasElement.toDataURL()` / `toBlob()`, `OffscreenCanvas.convertToBlob()` | The serialized copy has the low bit of one color channel flipped on roughly one pixel in eight (fully transparent pixels and premultiplied limits respected) |
| `CanvasRenderingContext2D.getImageData()` | The same, on the returned `ImageData` |
| `AudioBuffer.getChannelData()` | The channel is scaled once, in place, by a factor in [0.99, 1), since the returned array is live |
| `AudioBuffer.copyFromChannel()` | The copy is scaled, unless `getChannelData()` already scaled the channel |
| `AudioBuffer.copyToChannel()` | If a live view was already farbled, newly written samples are scaled too so the view stays consistent |
| `AnalyserNode.get{Float,Byte}{Frequency,TimeDomain}Data()` | The data is scaled before it's written |
| `navigator.hardwareConcurrency` | Between 4 and min(8, real count); between 2 and the real count below 4 |
| `navigator.deviceMemory` | One of 1, 2, 4, never above the real value |

The default User-Agent is Chrome's, without a Ladybird token, and the `Sec-CH-UA` brand list for it is GREASE,
Chromium, and Google Chrome at the same major version, as Chrome sends. A User-Agent that does have a `Ladybird/`
token still gets the Ladybird brand. `navigator.webdriver` stays false unless WebDriver is active, which Ladybird
already did.

Not farbled (follow-ups): WebGL `UNMASKED_VENDOR` / `UNMASKED_RENDERER`, font enumeration, screen coordinates, plugin
names. Seeds are keyed by the origin's host, not eTLD+1.

## The setting

`WebView::Settings::fingerprinting_protection()` (key `fingerprintingProtection`) is on by default. It reaches pages
through `set_enable_fingerprinting_protection` on WebContent and workers through the same message on WebWorker, and
lands in `LoaderConfig::enable_fingerprinting_protection`. In test mode it's forced off, since test output has to be
deterministic. `internals.setFingerprintingProtection()` turns it back on for the LibWeb text tests of farbling.

Engine patches `0030`–`0033` carry the LibWeb farbling, the setting, the Chrome User-Agent, and the AudioBuffer
write-path fix plus text tests.
