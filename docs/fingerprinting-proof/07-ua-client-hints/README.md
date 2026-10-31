# Feature 7 proof: UA reduction (not per-site farbling)

Proven against `Build/release/xenon/bin/Xenon`. Standard and Strict send one reduced Chrome 153 UA per OS. Off keeps Ladybird's default (`Ladybird/1.0`). There is no `navigator.userAgentData`. Low-entropy `Sec-CH-UA*` headers are derived from whatever User-Agent the request carries. `Accept-CH` / high-entropy hints are not implemented.

## What is live

`Farbling::effective_user_agent` / `effective_platform` in `Libraries/LibWeb/Farbling/UserAgent.cpp`. If farbling is Off, or if `LoaderConfig.user_agent` is not Ladybird's default (devtools / `--user-agent-preset`), the configured string wins. Otherwise every site gets:

```
Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Safari/537.36
```

Hooks: `NavigatorID` (userAgent, appVersion, platform, oscpu), fetch `User-Agent` (Fetching.cpp), WebSocket handshake.

## Proofs

- A Standard equals B Standard (different seeds, same UA). 3 reloads, same-site iframe, cross-site iframe, dedicated worker + worker fetch, shared worker, XHR, document GET, `/pixel.png` all match.
- Off A and Off B keep `Ladybird/1.0` (F6/Ladybird default).
- Strict A and B match Standard.
- `--user-agent-preset "Firefox Linux Desktop"` wins in navigator and workers (`Firefox/156.0`).
- `userAgentData` is false.

## How to rerun

```bash
python3 docs/fingerprinting-proof/07-ua-client-hints/serve.py 8080
python3 docs/fingerprinting-proof/07-ua-client-hints/run-proof.py
```

## Tests

- `TestUserAgentReduction` 6/6
- F1–F6 farbling unit tests still pass
- `test-web --filter '*avigator*'` Pass: 10; `*user-agent*` 7; frozen-platform 1; client-hints 1; `*Worker*` 56
- `python3 Meta/xenon.py test --no-build` passed
