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

# Speedometer 3.1 / JetStream 2.2 / MotionMark 1.3.1. Speedometer starts from its URL and JetStream from
# ?report=true; MotionMark has no URL-driven start, so bench serves Meta/motionmark-autostart.html in place of the
# suite's document through --resource-map and forces CPU painting. Headless text dumps after --seconds (default
# 900) so the JSON includes a numeric suite score; --suite narrows the run and --dump-dir keeps the full text.
./Meta/measure.py bench --xenon Build/release/bin/Xenon.app
./Meta/measure.py bench --xenon Build/release/xenon/bin/Xenon --seconds 900
./Meta/measure.py bench --xenon Build/release/xenon/bin/Xenon --suite motionmark --seconds 600

# Ladybird's own benchmark suites (see Benchmarks below). web-benchmarks runs in headless Xenon; js-benchmarks in the
# js and wasm REPLs, which Xenon's build makes from the same patched engine (./Meta/xenon.py build js, then wasm).
./Meta/measure.py web-benchmarks --xenon Build/release/bin/Xenon.app --benchmarks Speedometer3,WebKitDOM
./Meta/measure.py js-benchmarks --suites SunSpider,Octane --iterations 3
./Meta/measure.py report Meta/measure-results/<stamp>-js-benchmarks.json --baseline <an earlier log>

# Linux smaps parsing, no Xenon needed:
./Meta/measure.py self-test
```

Each command writes JSON under `Meta/measure-results/`. The content-blocker crate lives in
`Meta/ci/memory-measurement/` and talks to Ladybird's content-blocker sources the way WebContent does, after Xenon's
patches.

On Linux the sum is RSS and PSS from `/proc`. PSS is the one that does not double-count shared mappings. On macOS it
is `footprint(1)`'s physical footprint, Activity Monitor's Memory column.

## Benchmarks

Ladybird's CI runs two suites of its own on every push to master, and posts the results to its dashboard.
`Meta/measure.py` runs the same ones against Xenon:

- [web-benchmarks](https://github.com/LadybirdBrowser/web-benchmarks): Speedometer 2 and 3, StyleBench, about 230
  micro-benchmarks of the web platform (MicroWeb), and WebKit's DOM, CSS, bindings, parser and SVG suites. Its runner
  serves them locally and drives the browser headless, with a fresh profile, so Xenon runs as a new install does,
  content blocking included. `web-benchmarks --xenon` takes the app (or the Linux build's `Build/release/xenon`).
- [js-benchmarks](https://github.com/LadybirdBrowser/js-benchmarks): SunSpider, Kraken, Octane, JetStream, ARES-6 and
  others, and WebAssembly suites, each test a fresh `js` or `wasm` process. `js-benchmarks` finds the REPLs in
  `Build/release/bin`; they come from the engine with Xenon's patches, so only those patches that touch LibJS, LibWasm
  or AK show here.

Each command checks out the suite's `master` (`--ref` for another revision) in `Build/benchmarks`, with a venv for its
runner, runs every test `--iterations` times (5 by default, as Ladybird's CI does), and logs the runner's results with
the suite's, Xenon's and Ladybird's commits and the machine's CPU. `--benchmarks` / `--suites` take run.py's names,
comma-separated. As in Ladybird's CI, a benchmark with a test that fails, or that doesn't finish in 30 s, is left out
of the results and named in the log and the job's summary; the command fails only when nothing was measured. WebKitDOM's
`textarea-edit` inserts text into a textarea a million times an iteration, and Ladybird queued a `select` event for
each insertion. Running those after the timed loop took the test past 30 s on every macOS run (Ladybird's own CI leaves
WebKitDOM out on macOS for this) and on some Linux ones. Engine patch `0049` stops edits from firing `select`, which
other browsers don't fire for them either. `report` prints a log as markdown: each suite's total, every test, and
with `--baseline`, how each compares with the earlier log. A difference counts as a change only when it's more than 3% and more than two standard
errors of the difference; anything less is labelled noise.

[`.github/workflows/benchmarks.yml`](../.github/workflows/benchmarks.yml) runs both suites on Linux (in Ladybird's CI
image) and macOS, every Sunday on `main` with BrowserBench as well, and by hand:

```bash
gh workflow run benchmarks.yml --ref <branch>                          # Ladybird's two suites, 3 runs of each test
gh workflow run benchmarks.yml -f suites=all -f platforms=macos        # BrowserBench too, macOS only
gh workflow run benchmarks.yml -f suites=web-benchmarks -f web-benchmarks=Speedometer3 -f iterations=5
```

Each job's summary is its suite's report, compared with `main`'s last run of that suite on that platform, whose results
it downloads. Every job keeps its log as an artifact for 90 days (`benchmarks-<platform>-<suite>`), which
`Meta/measure.py report` reads. GitHub's runners are shared virtual machines: a few percent between two runs is
usually the machine, and a change of CPU model between runs (the summary says so) can move everything. Changes that
hold across several weekly runs, or that are well past the noise, are the ones to look into.

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
on.

After skipping the spare WebContent process and decommitting after load (4 and 5 below, measured together with a
blocker verdict cache that was then dropped), PSS with blocking on was local 439, github 1161, youtube 2152, x.com 1290
(one fewer WebContent on each page). Speedometer 3.1 headless on this box scored 7.24.

## Experiments

| # | Change | What it does | Delta | Keep? | Portability |
| --- | --- | --- | --- | --- | --- |
| 0 | Baseline harness | `Meta/measure.py` plus the content-blocker crate | n/a | keep | Xenon-only; no engine files |
| 1 | Hibernate on memory pressure | Background tabs discard when PSI / MemAvailable say the machine is short of memory, as Chromium Memory Saver does, not only after the 2 h timeout | Not re-measured in this environment (no full Xenon build). Chromium's equivalent is the difference between "many background tabs sit forever" and "they go away under pressure". Unit-tested in `TestXenonMemoryPressure`. | keep | Xenon UI only (`UI/MemoryPressure.cpp`, `TabHibernation.cpp`, a XenonSettings key). Survives a Ladybird rebase as long as `prepare_for_discard()` (#23) still exists. |
| 2 | Spare process skips the content blocker | The pre-warmed WebContent process no longer builds the engine until a tab takes it | About 10–13 MiB RSS in the spare process, plus the ~90 MiB peak while it used to build, from #34's per-process engine cost. New tabs pay ~0.4 s to build the engine on first use. | keep | One small `LibWebView` patch (`0039`). Touches `send_content_blockers()` / spare launch, the same seam #34 and #35 already use. |
| 3 | GC when a page is hidden | WebContent collects the JS heap and returns mimalloc pages when the UI hides the tab | Helps background tabs, not the foreground YouTube/GitHub/X tab. Same idea as #34's `ak_kmalloc_collect()` after building the engine. | keep | One small `WebContent` patch (`0038`). Hooks `set_system_visibility_state`, which Ladybird already has. |
| 4 | Skip spare WebContent | Xenon does not pre-warm an idle WebContent process | Linux PSS: local 644→439, github 1212→1161, youtube 2496→2152, x.com 1413→1290 (WC count −1). A new tab waits for its process to launch; it built the content blocker on first use anyway (2) | keep | `UI/Application.cpp` + a `LibWebView` option (`0043`) |
| 5 | Decommit after load | GC + `ak_kmalloc_decommit_free_pages()` 4 s after a top-level load finishes, and decommit after building the content blocker | Folded into the PSS numbers above. The GC pauses the page's main thread once; that pause was not measured. On macOS, where mimalloc already decommits, the option is left as it was (`TestXenonAllocatorRelease` checks both platforms) | keep | `AK` + `LibWeb` (`0042`), `WebContent` (`0044`) |
| 6 | Blocker verdict cache | Cache 1024 request checks; skip data:/blob:/about: | Crate: 0 mismatches on 1,818,984 requests, but not measured on its own in the browser | dropped | Was ContentBlocker Rust |
| 7 | Idle media servers | A MediaServer is given up 15 s after the renderer last used it (it was 30 s, which the 25 s sample never reached), and an AudioServer exits 15 s after its last client; the Browser relaunches either on demand, and retries once when a server has just gone | x.com keeps no MediaServer at the 25 s sample, where it cost 24 MiB PSS in both baseline arms, and the AudioServer a playback session used goes with it; no page starts one without media, and a paused element keeps both (`check_media_server_lifecycle`). youtube still holds one while its player keeps a session, and its ±300 MiB run-to-run spread hides its 20–100 MiB media stack | keep | `LibMediaClient` + `AudioServer` + `LibWebView` (`0047`) |
| 8 | Lazy ImageDecoder | A WebContent or WebWorker process starts an ImageDecoder on the first decode and drops it after 15 s with nothing to decode; a crashed one is replaced at the next decode | github −66 MiB PSS (decoder heap), x.com −22 MiB, local RSS −46 with two processes gone; youtube variance-limited. The first image after an idle spell waits for a new decoder to start; not measured. `check_image_decoder_lifecycle` | keep | `RemoteImageCodecPlugin` + `HelperProcess` (`0046`), and `0045`, which stops the animation sessions of images used as one frame so that they don't keep a decoder |
| 9 | Delayed headless text dumps | `--screenshot-delay` applies to `--headless=text`, capturing the dump type by value | BrowserBench scores parse from ~700 B of inner text, where a dangling capture had produced 22 MiB of mixed dumps | keep | `LibWebView` `Application.cpp` (`0041`) |
| 10 | Idle compositor rasters | Each compositor context drops its cached SVG and repeated-tile rasters once it has painted nothing for 30 s, or 4 s once hidden, when it drops its text blobs too. An animation in one tab doesn't hold another's. Then Skia's unused GPU textures are purged, and mimalloc's, glibc's (on a thread of its own) and macOS's malloc free pages go back to the system | The first version (one 4 s timer for the whole compositor) had Compositor PSS smaller in 8/8 paired cells on Linux with CPU painting, mean −5.2 MiB (github −10 to −12). The per-context version and the GPU path (macOS) aren't measured yet; see below. `check_compositor_idle_memory_release` | keep | `Services/Compositor` (`0049`) |
| 11 | Weak event timing targets | A `PerformanceEventTiming` entry holds the element its event was dispatched at weakly. Its `target` getter returns only an element still in the page, and the performance timeline keeps up to 150 entries (and the first input) for the life of the page, so an entry used to keep the whole subtree its element was removed with alive | In a GC graph dump of an x.com tab at 2.0 GB, 12k DOM nodes were in the page and 83k more were removed copies of the timeline, retained this way and through JIT code (which a separate change addresses). Holding the target weakly frees 26k of those nodes and 6% of the heap's cells on its own, and 78k nodes and 19% of the cells together with the JIT change. Not yet measured as PSS. `Tests/LibWeb/Text/input/EventTiming/removed-target-is-collectable.html` | keep | `LibWeb` `EventTiming` (`0051`) |

To measure the idle compositor (10), run `Meta/measure.py browsers --settle 60` with and without
`LADYBIRD_COMPOSITOR_IDLE_RELEASE=0`, since a visible page keeps its rasters for 30 s after it last painted.
`LADYBIRD_COMPOSITOR_IDLE_RELEASE_LOG=1` makes the compositor log each release, how long purging Skia and each
allocator took, and every frame's preparation time, with `after_release=true` on the first frame that repaints released
rasters. Compare that frame with the ones before it to see what a release costs the next scroll.

Reverted: the blocker verdict cache (6). LTO/PGO, a process cap, a shared mmap'd engine, and dropping YouTube scriptlets were considered and
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
