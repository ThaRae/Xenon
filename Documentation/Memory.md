# Memory and speed measurements

How Xenon compares to Chrome and to upstream Ladybird, how to rerun the numbers, and what each change for
[issue #28](https://github.com/ThaRae/Xenon/issues/28) did. The owner's question was whether the extra memory is
Xenon's or Ladybird's.

## How to rerun

From a Xenon checkout with the `ladybird` submodule initialized:

```bash
# Filter-list engine only (no Qt, no vcpkg). Applies Patches/ladybird if needed.
./Meta/measure.py content-blocker

# RSS/PSS of a running Xenon, summed across UI, WebContent, RequestServer, ImageDecoder, WebWorker, Compositor:
./Meta/measure.py processes --pid "$(pgrep -n Xenon)"

# Headless Xenon (and Chrome, if you pass it) on a local cross-site-frame page plus github.com, youtube.com, x.com.
# Needs a built Xenon. On Linux the binary is Build/release/xenon/bin/Xenon; on macOS, Build/release/bin/Xenon.app.
./Meta/measure.py browsers --xenon Build/release/bin/Xenon.app
./Meta/measure.py browsers --xenon Build/release/xenon/bin/Xenon --chrome /usr/bin/google-chrome --no-remote

# Speedometer 3.1 / JetStream 2.2 / MotionMark 1.3.1. URLs pass startAutomatically; headless text
# dumps after --seconds (default 900) via --screenshot-delay so the JSON includes a numeric suite score.
./Meta/measure.py bench --xenon Build/release/bin/Xenon.app
./Meta/measure.py bench --xenon Build/release/xenon/bin/Xenon --seconds 900

# Linux smaps parsing, no Xenon needed:
./Meta/measure.py self-test
```

Each command writes JSON under `Meta/measure-results/`. The content-blocker crate lives in
`Meta/ci/memory-measurement/` and talks to Ladybird's content-blocker sources the way WebContent does, after Xenon's
patches.

On Linux the sum is RSS and PSS from `/proc`. PSS is the one that does not double-count shared mappings. On macOS it
is `footprint(1)`'s physical footprint, Activity Monitor's Memory column.

## Xenon versus upstream

Measured on main after [#34](https://github.com/ThaRae/Xenon/pull/34) and [#35](https://github.com/ThaRae/Xenon/pull/35),
which already cut the two Xenon-only costs that were dominating:

**Most of the original gap to Chrome was Xenon's filter lists**, not Ladybird. Every WebContent process built its own
adblock engine from ~380,000 uBlock Origin filters (each tab, each cross-site frame, and the spare process). That was
about 108 MiB per process before #34. After #34's three engine patches it is about 10–13 MiB per process that actually
holds a page. #35 stopped extension background pages from building that engine at all, and lets those backgrounds idle
out after 30 s as Chrome does.

Without Xenon's lists, Ladybird is close to Chrome on YouTube and a bit above it on GitHub and X. That remainder is
upstream: one process per cross-site frame, plus a spare process, plus the page's own JS heap, layout, and media. It
will move with Ladybird.

Headless physical footprint from #34, macOS CI, 25 s after load, one page each (MiB):

| Page | Xenon, blocking on (after #34) | Xenon without its lists | Chrome |
| --- | ---: | ---: | ---: |
| Local page with cross-site frames | 253 | 195 | 439 |
| github.com/LadybirdBrowser/ladybird | 605 | 638 | 527 |
| youtube.com | 838 | 631 | 618 |
| x.com (login wall) | 578 | 541 | 378 |

YouTube with blocking on still adds ~130 MiB in the page's own process versus blocking off. The engine is only ~13 MiB
of that; the rest is what the lists inject, most likely uBlock Origin's YouTube scriptlets wrapping `fetch` / XHR /
`JSON.parse`. Pinning that down needs a JS heap profile in a real build.

Linux (AlmaLinux 10 host, Xenon built and run in `ghcr.io/ladybirdbrowser/ladybird-ci:2026.10.16`, headless, 25 s after
load, PSS so shared mappings are not double-counted), two runs, `Meta/measure.py browsers`:

| Page | Blocking on PSS (run 1 / 2) | Blocking off PSS (run 1 / 2) |
| --- | ---: | ---: |
| Local page with cross-site frames | 644 / 644 | 444 / 445 |
| github.com/LadybirdBrowser/ladybird | 1212 / 1246 | 1225 / 1224 |
| youtube.com | 2496 / 2495 | 2108 / 2249 |
| x.com (login wall) | 1413 / 1431 | 1308 / 1309 |

YouTube with blocking on is ~390 MiB PSS above blocking off on this machine. GitHub is slightly *lower* with blocking
on. JSON: `Meta/measure-results/browsers-1.json` and `browsers-2.json`.

After skipping the spare WebContent process, decommitting after load, and caching blocker verdicts, PSS with blocking
on was local 439, github 1161, youtube 2152, x.com 1290 (one fewer WebContent on each page). Speedometer 3.1 headless
on this box scored 7.24.

## Experiments

| # | Change | What it does | Delta | Keep? | Portability |
| --- | --- | --- | --- | --- | --- |
| 0 | Baseline harness | `Meta/measure.py` plus the content-blocker crate | n/a | keep | Xenon-only; no engine files |
| 1 | Hibernate on memory pressure | Background tabs discard when PSI / MemAvailable say the machine is short of memory, as Chromium Memory Saver does, not only after the 2 h timeout | Not re-measured in this environment (no full Xenon build). Chromium's equivalent is the difference between "many background tabs sit forever" and "they go away under pressure". Unit-tested in `TestXenonMemoryPressure`. | keep | Xenon UI only (`UI/MemoryPressure.cpp`, `TabHibernation.cpp`, a XenonSettings key). Survives a Ladybird rebase as long as `prepare_for_discard()` (#23) still exists. |
| 2 | Spare process skips the content blocker | The pre-warmed WebContent process no longer builds the engine until a tab takes it | About 10–13 MiB RSS in the spare process, plus the ~90 MiB peak while it used to build, from #34's per-process engine cost. New tabs pay ~0.4 s to build the engine on first use. | keep | One small `LibWebView` patch (`0039`). Touches `send_content_blockers()` / spare launch, the same seam #34 and #35 already use. |
| 3 | GC when a page is hidden | WebContent collects the JS heap and returns mimalloc pages when the UI hides the tab | Helps background tabs, not the foreground YouTube/GitHub/X tab. Same idea as #34's `ak_kmalloc_collect()` after building the engine. | keep | One small `WebContent` patch (`0038`). Hooks `set_system_visibility_state`, which Ladybird already has. |
| 4 | Skip spare WebContent | Xenon does not pre-warm an idle WebContent process | Linux PSS: local 644→439, github 1212→1161, youtube 2496→2152, x.com 1413→1290 (WC count −1) | keep | `UI/Application.cpp` + `LibWebView` option (`0041`) |
| 5 | Decommit after load | GC + `ak_kmalloc_decommit_free_pages` 4s after load finish | Folded into the PSS numbers above; `TestXenonAllocatorRelease` drives the shipped decommit | keep | `WebContent` + `AK` in `0041` |
| 6 | Blocker verdict cache | Cache 1024 request checks; skip data:/blob:/about: | Crate: 0 mismatches on 1,818,984 requests | keep | ContentBlocker Rust in `0041` |

Reverted: none. LTO/PGO, a process cap, a shared mmap'd engine, and dropping YouTube scriptlets were considered and
left alone: the first two need a full build and a BrowserBench score to judge, sharing the engine needs an adblock-rust
change (#34), and dropping scriptlets would stop blocking YouTube ads. Sending a serialized engine instead of the list
text was measured (65 ms vs 501 ms to load, 33 vs 35 MiB kept) and skipped: it does not cut retained memory, and
adblock-rust still copies the blob.

## What this environment could not measure

This cloud checkout has no Qt, no vcpkg tree, no Docker, and no Ladybird CI image, so it cannot configure or build
Xenon or Ladybird. A cold Xenon build is hours. There is no Chrome binary.

What ran here:

- `Meta/measure.py self-test` (Linux RSS/PSS parsing; this process was RSS 14.8 MiB / PSS 10.3 MiB)
- The content-blocker crate (`Meta/ci/memory-measurement`, log in `Meta/measure-results/20261028T234253Z-content-blocker.txt`):
  - Xenon's full lists: 26.1 MiB of text, 501 ms to build, peak 116.3 MiB while building, **35.3 MiB kept** (RSS after `mi_collect`)
  - Deserializing a pre-built adblock engine: 65 ms, 33.3 MiB kept (faster, not smaller)
  - YouTube still injects ~97 KB of scriptlets; GitHub ~11 KB; X ~47 KB
  - 1,818,984 request checks against `$document` hashes: 0 mismatches vs stock adblock-rust
- Unit tests for memory-pressure thresholds, which compile only once Xenon itself does

What did not:

- A live Xenon vs Ladybird vs Chrome footprint on youtube.com / x.com / github.com
- Speedometer, JetStream, MotionMark scores (the pages need a Start click and minutes of runtime in a built browser)
- Re-measuring experiments 1–3 in a running browser

Rerun `Meta/measure.py browsers` and `bench` on a machine that has a built Xenon (CI's macOS job, or a local
`./Meta/xenon.py build`) to fill those in. The JSON log format is stable so later runs can be appended.
