#!/usr/bin/env python3

# Reproducible memory and BrowserBench measurements for issue #28.
#
#   Meta/measure.py content-blocker              # filter-list engine, no browser build
#   Meta/measure.py processes --pid PID          # RSS/PSS of a running process tree
#   Meta/measure.py browsers --xenon PATH [...]  # headless Xenon (and Chrome) on sample pages
#   Meta/measure.py bench --xenon PATH [...]     # Speedometer / JetStream / MotionMark, if reachable
#   Meta/measure.py web-benchmarks --xenon PATH  # Ladybird's web-benchmarks suites, in headless Xenon
#   Meta/measure.py js-benchmarks                # Ladybird's js-benchmarks suites, in the js and wasm REPLs
#   Meta/measure.py report LOG [--baseline LOG]  # a benchmark log as markdown, compared with an earlier one
#   Meta/measure.py self-test                    # Linux smaps parsing, no Xenon needed
#
# Writes JSON to --log (default Meta/measure-results/<stamp>.json) and a markdown table to stdout.
# Documentation/Memory.md says how to rerun this and what each experiment decided.

from __future__ import annotations

import argparse
import json
import math
import os
import platform
import re
import signal
import statistics
import subprocess
import sys
import tempfile
import time
import venv

from datetime import datetime
from datetime import timezone
from pathlib import Path

import patches

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
MEASURE_CRATE_DIR = XENON_SOURCE_DIR / "Meta" / "ci" / "memory-measurement"
FILTER_LISTS_DIR = XENON_SOURCE_DIR / "UI" / "Resources" / "filter-lists"
RESULTS_DIR = XENON_SOURCE_DIR / "Meta" / "measure-results"

PROCESS_TYPES = (
    "WebContent",
    "WebWorker",
    "RequestServer",
    "ImageDecoder",
    "Compositor",
    "MediaServer",
    "AudioServer",
    "Xenon",
    "ladybird",
    "Ladybird",
    "chrome",
    "Chrome",
    "chromium",
)

REMOTE_PAGES = [
    "https://github.com/LadybirdBrowser/ladybird",
    "https://www.youtube.com/",
    "https://x.com/",
]

# MotionMark 1.3.1 starts only from its Run Benchmark button, and that button stays disabled until the suite's
# frame-rate probe finishes, so neither a query string nor an onload handler can start it. This copy of the suite's
# index.html polls for the enabled button and presses it; --resource-map serves it as the suite's document, and every
# other resource still comes from browserbench.org.
MOTIONMARK_PAGE = "https://browserbench.org/MotionMark1.3.1/"
MOTIONMARK_START_PAGE = XENON_SOURCE_DIR / "Meta" / "motionmark-autostart.html"

BROWSER_BENCH_PAGES = [
    ("Speedometer 3.1", "https://browserbench.org/Speedometer3.1/?startAutomatically"),
    # JetStreamDriver.js reads ?report=true only (it starts 4 s after its resource prefetch); it has no
    # ?startAutomatically parameter, so that URL always left the suite on its Start Test page.
    ("JetStream 2.2", "https://browserbench.org/JetStream2.2/?report=true"),
    ("MotionMark 1.3.1", MOTIONMARK_PAGE),
]
BENCH_SUITE_KEYS = [name.split()[0].lower() for name, _ in BROWSER_BENCH_PAGES]
BENCH_SUBSTITUTIONS = {
    "MotionMark 1.3.1": [
        {"url": MOTIONMARK_PAGE, "file": str(MOTIONMARK_START_PAGE), "content_type": "text/html"},
    ],
}
# Headless Linux under Xvfb has no working GPU backend, so MotionMark is measured with CPU painting.
BENCH_EXTRA_FLAGS = {"MotionMark 1.3.1": ["--force-cpu-painting"]}

SETTLE_SECONDS = 25
# Default Speedometer 3.1 is 10 iterations and often needs well over 3 minutes on Ladybird.
BENCH_SECONDS = 900
# JetStream 2.2 runs every plan's iterations to completion; 20 minutes was still 55 of 64 tests.
# MotionMark's full run, every test at three seconds plus warm-up, finished inside 600 s on the software rasterizer.
BENCH_SECONDS_BY_SUITE = {"JetStream 2.2": 1800, "MotionMark 1.3.1": 600}
# How long Xenon gets to write its dump and quit after the delay, which a page as big as MotionMark's results takes.
BENCH_DUMP_GRACE = 180
BENCH_WINDOW = (1280, 900)

# Ladybird's own benchmark suites, which its CI runs on every push to master (ladybird/.github/workflows/
# web-benchmarks.yml and js-and-wasm-benchmarks.yml). web-benchmarks drives a browser, headless, through a local server
# its runner starts; js-benchmarks runs the js and wasm REPLs, which Xenon's build makes from the same patched engine.
# Each is checked out, with a venv for its runner, under Build/benchmarks.
LADYBIRD_BENCHMARKS = {
    "web-benchmarks": "https://github.com/LadybirdBrowser/web-benchmarks.git",
    "js-benchmarks": "https://github.com/LadybirdBrowser/js-benchmarks.git",
}
LADYBIRD_BENCHMARKS_DIR = XENON_SOURCE_DIR / "Build" / "benchmarks"
# Ladybird's CI runs each test five times.
LADYBIRD_BENCHMARK_ITERATIONS = 5
# report counts a difference as a change only when it's bigger than this many standard errors of the difference, and
# than this share of the earlier value. GitHub's runners are shared machines, where a few percent either way is noise.
SIGNIFICANT_STANDARD_ERRORS = 2
SIGNIFICANT_SHARE = 0.03


def main() -> int:
    parser = argparse.ArgumentParser(description="Measure Xenon's memory and BrowserBench scores (issue #28)")
    parser.add_argument("--log", type=Path, help="JSON file to append a result object to")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("content-blocker", help="Build and run the content-blocker crate measurement")

    processes = subparsers.add_parser("processes", help="Sum RSS/PSS of a process tree")
    processes.add_argument("--pid", type=int, required=True)
    processes.add_argument("--label", default="processes")

    browsers = subparsers.add_parser("browsers", help="Launch headless Xenon (and Chrome) on sample pages")
    browsers.add_argument("--xenon", type=Path, required=True)
    browsers.add_argument("--chrome", type=Path)
    browsers.add_argument("--settle", type=float, default=SETTLE_SECONDS)
    browsers.add_argument("--no-remote", action="store_true", help="only the local cross-site-frame page")

    bench = subparsers.add_parser("bench", help="Open BrowserBench suites in headless Xenon")
    bench.add_argument("--xenon", type=Path, required=True)
    bench.add_argument("--seconds", type=float, default=BENCH_SECONDS)
    bench.add_argument(
        "--suite",
        action="append",
        choices=BENCH_SUITE_KEYS,
        help="run only this suite; repeat to pick more than one",
    )
    bench.add_argument("--dump-dir", type=Path, help="write each suite's full headless text dump here")

    web_benchmarks = subparsers.add_parser("web-benchmarks", help="Run Ladybird's web-benchmarks in headless Xenon")
    web_benchmarks.add_argument("--xenon", type=Path, required=True)
    web_benchmarks.add_argument("--benchmarks", default="all", help="comma-separated, as run.py takes them")
    web_benchmarks.add_argument("--iterations", type=int, default=LADYBIRD_BENCHMARK_ITERATIONS)
    web_benchmarks.add_argument("--ref", default="master", help="the suite's branch or commit")

    js_benchmarks = subparsers.add_parser("js-benchmarks", help="Run Ladybird's js-benchmarks in the js and wasm REPLs")
    js_benchmarks.add_argument("--build-dir", type=Path, default=XENON_SOURCE_DIR / "Build" / "release")
    js_benchmarks.add_argument("--suites", default="all", help="comma-separated, as run.py takes them")
    js_benchmarks.add_argument("--iterations", type=int, default=LADYBIRD_BENCHMARK_ITERATIONS)
    js_benchmarks.add_argument("--ref", default="master", help="the suite's branch or commit")

    report = subparsers.add_parser("report", help="Print a benchmark log as markdown")
    report.add_argument("result", type=Path)
    report.add_argument("--baseline", type=Path, help="an earlier log of the same command to compare with")

    subparsers.add_parser("self-test", help="Check Linux smaps parsing against this process")

    args = parser.parse_args()
    if args.command == "content-blocker":
        result = measure_content_blocker()
    elif args.command == "processes":
        result = {"label": args.label, **sample_tree(args.pid)}
        print_tree(result)
    elif args.command == "browsers":
        result = measure_browsers(args.xenon, args.chrome, args.settle, not args.no_remote)
    elif args.command == "bench":
        result = measure_bench(args.xenon, args.seconds, args.suite, args.dump_dir)
    elif args.command == "web-benchmarks":
        result = measure_web_benchmarks(args.xenon, args.benchmarks, args.iterations, args.ref)
    elif args.command == "js-benchmarks":
        result = measure_js_benchmarks(args.build_dir, args.suites, args.iterations, args.ref)
    elif args.command == "report":
        baseline = json.loads(args.baseline.read_text()) if args.baseline and args.baseline.exists() else None
        print(report_log(json.loads(args.result.read_text()), baseline))
        return 0
    elif args.command == "self-test":
        return self_test()
    else:
        parser.error(f"unknown command {args.command}")

    log_path = args.log or RESULTS_DIR / f"{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}-{args.command}.json"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "command": args.command,
        "recorded_at": datetime.now(timezone.utc).isoformat(),
        "result": result,
    }
    log_path.write_text(json.dumps(payload, indent=2) + "\n")
    print(f"wrote {log_path}", flush=True)
    # As Ladybird's runners do, a run that lost a test still counts, with the test named, and fails only when nothing was
    # measured.
    if result.get("failures") and os.environ.get("CI"):
        print(f"::warning title={args.command}::Left out, as they didn't finish: {', '.join(result['failures'])}")
    if result.get("returncode") or result.get("results") == {}:
        return 1
    return 0


def measure_content_blocker() -> dict:
    if not (
        XENON_SOURCE_DIR / "ladybird" / "Libraries" / "LibWeb" / "ContentBlocker" / "Rust" / "src" / "lib.rs"
    ).exists():
        print("The ladybird submodule is missing. Run: git submodule update --init --depth 1", file=sys.stderr)
        sys.exit(1)

    patches = XENON_SOURCE_DIR / "Meta" / "patches.py"
    status = subprocess.run(
        [sys.executable, str(patches), "status"], cwd=XENON_SOURCE_DIR, capture_output=True, text=True
    )
    if "without the series" in status.stdout:
        print("==> applying Patches/ladybird so the crate matches WebContent", flush=True)
        if subprocess.run([sys.executable, str(patches), "apply"], cwd=XENON_SOURCE_DIR).returncode != 0:
            sys.exit(1)

    print("==> cargo build --release (content-blocker measurement)", flush=True)
    build = subprocess.run(
        ["cargo", "build", "--release", "--manifest-path", str(MEASURE_CRATE_DIR / "Cargo.toml")],
        cwd=XENON_SOURCE_DIR,
    )
    if build.returncode != 0:
        sys.exit(build.returncode)

    executable = MEASURE_CRATE_DIR / "target" / "release" / "measure"
    print("==> measure", FILTER_LISTS_DIR, flush=True)
    output = subprocess.check_output([str(executable), str(FILTER_LISTS_DIR)], text=True)
    print(output, end="" if output.endswith("\n") else "\n", flush=True)
    return {
        "output": output,
        "filter_lists_bytes": sum(path.stat().st_size for path in FILTER_LISTS_DIR.rglob("*") if path.is_file()),
    }


def xenon_executable(bundle_or_bin: Path) -> Path:
    mac = bundle_or_bin / "Contents" / "MacOS" / "Xenon"
    if mac.exists():
        return mac
    if bundle_or_bin.is_file():
        return bundle_or_bin
    linux = bundle_or_bin / "bin" / "Xenon"
    if linux.exists():
        return linux
    print(f"Xenon executable not found under {bundle_or_bin}", file=sys.stderr)
    sys.exit(1)


def chrome_executable(bundle_or_bin: Path) -> Path:
    mac = bundle_or_bin / "Contents" / "MacOS" / "Google Chrome"
    if mac.exists():
        return mac
    if bundle_or_bin.is_file():
        return bundle_or_bin
    print(f"Chrome executable not found under {bundle_or_bin}", file=sys.stderr)
    sys.exit(1)


def measure_browsers(xenon_path: Path, chrome_path: Path | None, settle: float, remote: bool) -> dict:
    import http.server
    import threading

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/":
                # localhost vs 127.0.0.1 is a different origin, so each frame gets its own WebContent process.
                port = self.server.server_address[1]
                frames = "".join(f'<iframe src="http://localhost:{port}/frame-{index}"></iframe>' for index in range(3))
                body = f"<!doctype html><title>Frames</title>Three cross-site frames{frames}".encode()
            else:
                body = b"<!doctype html><title>frame</title>frame"
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    local_page = f"http://127.0.0.1:{server.server_address[1]}/"
    pages = [local_page] + (REMOTE_PAGES if remote else [])
    xenon = xenon_executable(xenon_path)
    results = []
    try:
        for page in pages:
            print(f"### {page}", flush=True)
            for label, blocking in (("Xenon, blocking on", True), ("Xenon, blocking off", False)):
                results.append(run_browser(label, xenon_command(xenon, page, blocking), settle))
                time.sleep(2)
            if chrome_path is not None:
                chrome = chrome_executable(chrome_path)
                results.append(
                    run_browser(
                        "Chrome (headless)",
                        [
                            str(chrome),
                            "--headless=new",
                            "--no-first-run",
                            "--no-default-browser-check",
                            "--window-size=1280,900",
                            f"--user-data-dir={tempfile.mkdtemp(prefix='xenon-measure-chrome-')}",
                            page,
                        ],
                        settle,
                    )
                )
            print(flush=True)
    finally:
        server.shutdown()
    return {"pages": pages, "runs": results}


def xenon_command(
    executable: Path,
    page: str,
    blocking: bool,
    extra: list[str] | None = None,
    substitutions: list[dict] | None = None,
) -> list[str]:
    profile = Path(tempfile.mkdtemp(prefix="xenon-measure-"))
    config = profile / "config"
    config.mkdir(parents=True, exist_ok=True)
    (config / "Settings.json").write_text(json.dumps({"contentBlockers": {"enabled": blocking}}))
    flags = list(extra) if extra else ["--headless=manual"]
    if substitutions:
        # RequestServer loads the map before it enters its sandbox and is allowed to read each substituted file.
        resource_map = profile / "resource-map.json"
        resource_map.write_text(json.dumps({"substitutions": substitutions}))
        flags.append(f"--resource-map={resource_map}")
    return [str(executable), *flags, "--profile-path", str(profile), page]


ACCESSIBILITY_LEAF = re.compile(r'text leaf name="([^"]*)"')
ACCESSIBILITY_NAME = re.compile(r'(?:heading|link|button|image) name="([^"]*)"')
SCORE_LINE = re.compile(r"^([0-9]+(?:\.[0-9]+)?)\s*(?:±.*)?$")
QUOTED_SCORE = re.compile(r'^"([0-9]+(?:\.[0-9]+)?)"$')


def scan_quoted_score(lines: list[str], header_index: int) -> list[float]:
    """Decimal values in the quoted fragments that follow a layout-dump `"Score"` fragment line."""
    out = []
    for candidate in lines[header_index + 1 : header_index + 40]:
        match = QUOTED_SCORE.match(candidate)
        if match is None:
            match = SCORE_LINE.match(candidate)
        if match is not None:
            value = float(match.group(1))
            if value > 0:
                out.append(value)
    return out


def visible_page_lines(text: str) -> list[str]:
    """Split a suite dump into the strings a user could read.

    Xenon's text mode prints the page's inner text. Builds from before engine patch 0041 captured the dump's type
    by reference, so their delayed dumps could print the layout tree and the GC graph after the text, or the
    accessibility tree instead. The first two can be tens of megabytes and never hold a score, so cut them off; in an
    accessibility tree the visible strings are the name="..." attributes of text leaves and headings/links/buttons.
    """
    text = text.split("\nViewport <#document> at [", 1)[0]
    if 'text leaf name="' not in text:
        return [line.strip() for line in text.splitlines()]
    lines = []
    for raw in text.splitlines():
        match = ACCESSIBILITY_LEAF.search(raw) or ACCESSIBILITY_NAME.search(raw)
        if match:
            lines.append(match.group(1))
    return lines


def parse_browserbench_score(suite: str, text: str) -> float | None:
    """Pull a numeric suite score out of headless page text. Returns None if the suite never finished."""
    if not text:
        return None
    if 'text leaf name="' in text:
        # JetStream's summary is <div class="score">96.629</div><label>Score</label>: in an accessibility dump
        # the total is the cluster's text leaf just before the "Score" label. Scanning forward from "Score"
        # instead reaches the first benchmark's heading a few leaves later (113.639 on a 10-test run).
        match = re.search(r'text leaf name="([0-9]+(?:\.[0-9]+)?)"\s*\n\s*text leaf name="Score"', text)
        if match is not None:
            value = float(match.group(1))
            if value > 0:
                return value
        if suite.startswith("JetStream") and "level=3" in text:
            # Benchmark rows keep their per-benchmark score after the suite stops, but a dump without the
            # summary leaf has no suite total; the fallbacks below would return the first row's number.
            return None
    lines = visible_page_lines(text)
    quoted_header_index = None
    if 'text leaf name="' not in text:
        # Layout-tree dumps quote their text fragments, so the <h1>Score</h1> fragment is a bare
        # `"Score"` line and the result cell lands later as a bare `"5.51"` fragment line; the ±
        # confidence has its own fragment, which QUOTED_SCORE only accepts on a bare decimal line.
        for index, line in enumerate(lines):
            inline = re.match(r'^"score:\s*([0-9]+(?:\.[0-9]+)?)"$', line.lower())
            if inline is not None:
                value = float(inline.group(1))
                if value > 0:
                    return value
            if line.lower().rstrip(":") == '"score"':
                quoted_header_index = index
                break
        if quoted_header_index is not None:
            for candidate in scan_quoted_score(lines, quoted_header_index):
                return candidate
    for index, line in enumerate(lines):
        lowered = line.lower()
        if lowered in {"error", "invalid"} and index > 0 and lines[index - 1].lower().rstrip(":") == "score":
            return None
        score_header = lowered == "score" or lowered.startswith("score:")
        if not score_header:
            continue
        candidates: list[str] = []
        if ":" in line:
            candidates.append(line.split(":", 1)[1].strip())
        candidates.extend(lines[index + 1 : index + 8])
        for candidate in candidates:
            if candidate.lower() in {"error", "invalid"}:
                return None
            match = re.match(r"^([0-9]+(?:\.[0-9]+)?)\s*(?:±.*)?$", candidate)
            if match is None:
                match = QUOTED_SCORE.match(candidate)
            if match:
                value = float(match.group(1))
                if value > 0:
                    return value
    # The DOM order can be "<div class=score>142.7</div><label>Score</label>" (JetStream's summary).
    for index, line in enumerate(lines[:-1]):
        if lines[index + 1].lower().rstrip(":") == "score" and SCORE_LINE.match(line):
            return float(SCORE_LINE.match(line).group(1))
    # MotionMark has no "Score" label next to its total, only the score and its "±" confidence.
    for index, line in enumerate(lines[:-1]):
        if SCORE_LINE.match(line) and lines[index + 1].startswith(("±", "Â±", "+/-")):
            return float(SCORE_LINE.match(line).group(1))
    # Xenon's text mode also dumps the layout tree, and on some pages body.innerText comes back empty while the
    # rendered score is still there: the suite puts its total in <div class="score">.
    match = re.search(r'<div\.score[ >\n][^\n]*\n.{0,1600}?"([0-9]+(?:\.[0-9]+)?)"', text, re.DOTALL)
    if match:
        value = float(match.group(1))
        if value > 0:
            return value
    # JetStream sometimes prints "JetStream 2.2 Score: 142.7" on one line.
    match = re.search(r"(?i)" + re.escape(suite.split()[0]) + r".{0,40}score[:\s]+([0-9]+(?:\.[0-9]+)?)", text)
    if match:
        value = float(match.group(1))
        if value > 0:
            return value
    return None


def dump_excerpt(output: str) -> str:
    start = output.find("Dumping page text")
    if start != -1:
        # Keep the helper process lines just before the dump: they say whether the resource substitution was applied.
        return output[max(0, start - 1000) : start + 4000]
    return output[-4000:]


def measure_bench(
    xenon_path: Path, seconds: float, suites: list[str] | None = None, dump_dir: Path | None = None
) -> dict:
    xenon = xenon_executable(xenon_path)
    results = []
    for name, url in BROWSER_BENCH_PAGES:
        if suites and name.split()[0].lower() not in suites:
            continue
        suite_seconds = max(seconds, BENCH_SECONDS_BY_SUITE.get(name, 0))
        extra = [
            "--headless=text",
            f"--screenshot-delay={max(2, int(suite_seconds))}",
            f"--window-width={BENCH_WINDOW[0]}",
            f"--window-height={BENCH_WINDOW[1]}",
            *BENCH_EXTRA_FLAGS.get(name, []),
        ]
        print(f"### {name} ({url})", flush=True)
        command = xenon_command(xenon, url, True, extra, BENCH_SUBSTITUTIONS.get(name))
        started = time.time()
        process = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True, text=True
        )
        output = ""
        sample: dict = {}
        try:
            try:
                output, _ = process.communicate(timeout=suite_seconds + BENCH_DUMP_GRACE)
            except subprocess.TimeoutExpired:
                sample = (
                    sample_tree(process.pid) if process.poll() is None else {"error": f"exited {process.returncode}"}
                )
                try:
                    process.send_signal(signal.SIGTERM)
                except ProcessLookupError:
                    pass
                leftover = process.communicate(timeout=15)[0] if process.stdout is not None else ""
                output = (output or "") + (leftover or "")
            else:
                sample = {"note": "process exited after text dump", "returncode": process.returncode}
        finally:
            stop_process_group(process)
        elapsed = time.time() - started
        score = parse_browserbench_score(name, output or "")
        if score is None:
            reason = "suite did not print a numeric score (Start never ran, dump too early, or headless text failed)"
            print(f"{name}: no score after {elapsed:.0f}s. {reason}", flush=True)
        else:
            print(f"{name}: score {score} after {elapsed:.0f}s", flush=True)
        if sample and "rss" in sample:
            print(f"{name}: {process_summary(sample)}", flush=True)
        if dump_dir is not None:
            dump_dir.mkdir(parents=True, exist_ok=True)
            (dump_dir / f"{name.split()[0].lower()}-{int(suite_seconds)}s.txt").write_text(output or "")
        results.append(
            {
                "suite": name,
                "url": url,
                "seconds": elapsed,
                "score": score,
                "sample": sample,
                # The page text is at the start of the dump; the layout tree and GC graph that follow add tens of
                # megabytes and hold no score.
                "output_excerpt": dump_excerpt(output or ""),
            }
        )
    return {"suites": results}


def measure_web_benchmarks(xenon_path: Path, benchmarks: str, iterations: int, ref: str) -> dict:
    # Absolute, as run.py runs from its checkout.
    xenon = xenon_executable(xenon_path).resolve()
    # run.py gives the browser a fresh profile of its own (--profile-path), so Xenon runs with its defaults.
    arguments = ["--executable", str(xenon), "--benchmarks", benchmarks, "--iterations", str(iterations)]
    result = run_ladybird_benchmarks("web-benchmarks", ref, arguments)
    # run.py drops a benchmark that didn't finish from its results, and names it last.
    failed = re.search(r"The following benchmarks failed: (.*)", result.pop("output"))
    result["failures"] = failed.group(1).strip().split(", ") if failed else []
    return {"executable": str(xenon), "selection": benchmarks, "iterations": iterations, **result}


def measure_js_benchmarks(build_dir: Path, suites: str, iterations: int, ref: str) -> dict:
    # Absolute, as run.py runs from its checkout.
    js = build_dir.resolve() / "bin" / "js"
    wasm = build_dir.resolve() / "bin" / "wasm"
    if not js.exists() or not wasm.exists():
        print(f"{js} and {wasm} are built by: Meta/xenon.py build js && Meta/xenon.py build wasm", file=sys.stderr)
        sys.exit(1)
    # Without --continue-on-failure, the first test that fails stops the run and loses every result before it. With it,
    # run.py leaves that test out and says so, and the run fails here once the rest are measured.
    arguments = [
        "--executable",
        str(js),
        "--wasm-executable",
        str(wasm),
        "--suites",
        suites,
        "--iterations",
        str(iterations),
        "--continue-on-failure",
    ]
    result = run_ladybird_benchmarks("js-benchmarks", ref, arguments)
    # The command ends with the test, as a path: ... PosixPath('Octane/box2d.js')]'
    result["failures"] = re.findall(r"Test execution failure: Command '\[.*'([^']+)'\)?\]'", result.pop("output"))
    return {"executable": str(js), "selection": suites, "iterations": iterations, **result}


def run_ladybird_benchmarks(name: str, ref: str, arguments: list[str]) -> dict:
    directory = LADYBIRD_BENCHMARKS_DIR / name
    directory.mkdir(parents=True, exist_ok=True)
    if not (directory / ".git").exists():
        subprocess.run(["git", "init", "--quiet"], cwd=directory, check=True)
    subprocess.run(
        ["git", "fetch", "--quiet", "--depth", "1", LADYBIRD_BENCHMARKS[name], ref], cwd=directory, check=True
    )
    subprocess.run(["git", "checkout", "--quiet", "--force", "FETCH_HEAD"], cwd=directory, check=True)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=directory, text=True).strip()

    # The runners' own packages (scipy, tabulate, ...), in a venv beside each, as Ladybird's CI installs them.
    environment = directory / ".venv"
    python = environment / "bin" / "python3"
    if not python.exists():
        venv.create(environment, with_pip=True)
    pip = [python, "-m", "pip", "install", "--quiet", "--disable-pip-version-check", "-r", "requirements.txt"]
    subprocess.run(pip, cwd=directory, check=True)

    output_path = directory / "results.json"
    output_path.unlink(missing_ok=True)
    print(f"### {name} {commit[:10]}", flush=True)
    started = time.time()
    process = subprocess.Popen(
        [python, "run.py", *arguments, "--output", str(output_path)],
        cwd=directory,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    output = []
    for line in process.stdout:
        print(line, end="", flush=True)
        output.append(line)
    process.wait()

    return {
        "repository": LADYBIRD_BENCHMARKS[name],
        "commit": commit,
        "xenon_commit": source_commit(XENON_SOURCE_DIR),
        # The commit the patches were applied to; ladybird/'s HEAD is the patched branch.
        "ladybird_commit": patches.applied_base(),
        "host": host_description(),
        "seconds": time.time() - started,
        "returncode": process.returncode,
        "results": json.loads(output_path.read_text()) if output_path.exists() else {},
        "output": "".join(output),
    }


def source_commit(directory: Path) -> str:
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=directory, capture_output=True, text=True)
    return result.stdout.strip()


def host_description() -> dict:
    cpu = platform.processor()
    if sys.platform == "linux":
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    elif sys.platform == "darwin":
        cpu = subprocess.run(
            ["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True, text=True
        ).stdout.strip()
    return {"system": platform.system(), "machine": platform.machine(), "cpu": cpu, "cpus": os.cpu_count()}


# One measured quantity: its runs, and whether more is better (a score) or less (a time).
class Measure:
    def __init__(self, runs: list[float], higher_is_better: bool):
        self.runs = [float(run) for run in runs]
        self.higher_is_better = higher_is_better

    @property
    def mean(self) -> float:
        return statistics.mean(self.runs)

    @property
    def stdev(self) -> float:
        return statistics.stdev(self.runs) if len(self.runs) > 1 else 0.0

    def text(self) -> str:
        unit = "" if self.higher_is_better else " s"
        spread = f" ± {number_text(self.stdev, 3)}" if len(self.runs) > 1 else ""
        return f"{number_text(self.mean, 4)}{spread}{unit}"


def number_text(value: float, digits: int) -> str:
    return f"{value:,.0f}" if abs(value) >= 10 ** (digits - 1) else f"{value:.{digits}g}"


def ladybird_benchmark_measures(command: str, results: dict) -> tuple[dict, dict]:
    """A Ladybird suite's results.json as two maps of Measure: one per suite (its total) and one per test."""
    totals = {}
    tests = {}
    for group, entries in results.items():
        group_tests = {}
        for name, entry in entries.items():
            if name == "_total":
                continue
            # Octane and WasmCoremark print a score; everything else is timed (js-benchmarks' compare.py picks the same).
            if "reported_score" in entry:
                group_tests[f"{group}/{name}"] = Measure(entry["reported_score"]["runs"], True)
            elif "time" in entry:
                group_tests[f"{group}/{name}"] = Measure(entry["time"]["runs"], False)
        tests.update(group_tests)

        total = entries.get("_total", {})
        if "reported_score" in total:
            totals[group] = Measure(total["reported_score"]["runs"], True)
        elif "time" in total:
            totals[group] = Measure(total["time"]["runs"], False)
        elif group_tests:
            # js-benchmarks has no totals of its own: a suite's total is the sum of its tests, run by run.
            measures = list(group_tests.values())
            counts = {len(measure.runs) for measure in measures}
            if len(counts) == 1 and len({measure.higher_is_better for measure in measures}) == 1:
                runs = [sum(measure.runs[index] for measure in measures) for index in range(counts.pop())]
                totals[group] = Measure(runs, measures[0].higher_is_better)
    return totals, tests


def change_text(new: Measure, old: Measure | None) -> tuple[str, bool]:
    """How new compares with old, in words, and whether the difference is bigger than the noise."""
    if old is None or old.mean <= 0 or new.mean <= 0:
        return "", False
    ratio = new.mean / old.mean if new.higher_is_better else old.mean / new.mean
    words = f"{abs(ratio - 1) * 100:.1f}% {'better' if ratio >= 1 else 'worse'}"
    standard_error = math.sqrt(new.stdev**2 / len(new.runs) + old.stdev**2 / len(old.runs))
    difference = abs(new.mean - old.mean)
    significant = (
        len(new.runs) > 1
        and len(old.runs) > 1
        and difference > SIGNIFICANT_STANDARD_ERRORS * standard_error
        and difference > SIGNIFICANT_SHARE * old.mean
    )
    return (f"**{words}**" if significant else f"{words} (noise)"), significant


def markdown_table(header: list[str], rows: list[list[str]]) -> str:
    # The name and the change read as text; the numbers between them line up on the right.
    alignment = ["---"] + ["---:"] * (len(header) - 2) + ["---"]
    lines = ["| " + " | ".join(header) + " |", "| " + " | ".join(alignment) + " |"]
    lines += ["| " + " | ".join(row) + " |" for row in rows]
    return "\n".join(lines)


def report_log(payload: dict, baseline: dict | None) -> str:
    command = payload.get("command")
    if baseline is not None and baseline.get("command") != command:
        baseline = None
    if command in LADYBIRD_BENCHMARKS:
        return report_ladybird_benchmarks(command, payload, baseline)
    if command == "bench":
        return report_browserbench(payload, baseline)
    return f"`{command}` logs have no report."


def report_ladybird_benchmarks(command: str, payload: dict, baseline: dict | None) -> str:
    result = payload["result"]
    host = result["host"]
    lines = [
        f"#### {command}",
        "",
        f"[{command}]({result['repository'].removesuffix('.git')}) `{result['commit'][:10]}`, "
        f"{result['iterations']} runs of each test, in Xenon `{result['xenon_commit'][:10]}` "
        f"(Ladybird `{result['ladybird_commit'][:10]}` and Xenon's patches), on {host['system']} {host['machine']}, "
        f"{host['cpu']} ({host['cpus']} CPUs). It took {result['seconds'] / 60:.0f} minutes.",
        "",
    ]
    if result["failures"]:
        lines += [f"**Failed, and left out:** {', '.join(result['failures'])}", ""]

    totals, tests = ladybird_benchmark_measures(command, result["results"])
    old_totals, old_tests = {}, {}
    if baseline is None:
        lines += ["No earlier run of main to compare with.", ""]
    else:
        earlier = baseline["result"]
        old_totals, old_tests = ladybird_benchmark_measures(command, earlier["results"])
        lines += [
            f"Compared with main's run of {baseline['recorded_at'][:10]}: Xenon `{earlier['xenon_commit'][:10]}`, "
            f"{earlier['iterations']} runs of each test, on {earlier['host']['cpu']}. A change counts when it's more "
            f"than {SIGNIFICANT_SHARE:.0%} and {SIGNIFICANT_STANDARD_ERRORS} standard errors; anything less is marked "
            "noise.",
            "",
        ]
        if earlier["commit"] != result["commit"]:
            lines += [f"The suite has changed since (`{earlier['commit'][:10]}`), which can account for a change.", ""]
        if earlier["host"]["cpu"] != host["cpu"]:
            lines += ["That run had another CPU, which can account for a change.", ""]

    rows = []
    for group, measure in totals.items():
        old = old_totals.get(group)
        label = "score" if measure.higher_is_better else "time"
        rows.append([group, f"{label} {measure.text()}", old.text() if old else "", change_text(measure, old)[0]])
    # Without an earlier run, the columns for it would be empty.
    columns = 4 if baseline is not None else 2
    header = ["Suite", "Xenon", "Main", "Change"][:columns]
    lines += [markdown_table(header, [row[:columns] for row in rows]), ""]

    test_rows = []
    changed_rows = []
    for name, measure in tests.items():
        old = old_tests.get(name)
        words, significant = change_text(measure, old)
        row = [name, measure.text(), old.text() if old else "", words]
        test_rows.append(row)
        if significant:
            changed_rows.append(row)
    if baseline is not None:
        if changed_rows:
            lines += [f"{len(changed_rows)} tests changed more than the noise:", ""]
            lines += [markdown_table(["Test", "Xenon", "Main", "Change"], changed_rows), ""]
        else:
            lines += ["No test changed more than the noise.", ""]
    lines += [
        f"<details><summary>Every test ({len(test_rows)})</summary>",
        "",
        markdown_table(["Test", *header[1:]], [row[:columns] for row in test_rows]),
        "",
        "</details>",
    ]
    return "\n".join(lines)


def report_browserbench(payload: dict, baseline: dict | None) -> str:
    old_scores = {}
    if baseline is not None:
        old_scores = {suite["suite"]: suite.get("score") for suite in baseline["result"]["suites"]}
    rows = []
    for suite in payload["result"]["suites"]:
        score = suite.get("score")
        old = old_scores.get(suite["suite"])
        change = f"{(score / old - 1) * 100:+.1f}%" if score and old else ""
        rows.append(
            [suite["suite"], number_text(score, 4) if score else "no score", number_text(old, 4) if old else "", change]
        )
    lines = [
        "#### BrowserBench",
        "",
        "One run of each suite, so there's no telling noise from change here; higher is better.",
        "",
    ]
    columns = 2
    if baseline is not None:
        lines += [f"The Main column is main's run of {baseline['recorded_at'][:10]}.", ""]
        columns = 4
    header = ["Suite", "Xenon", "Main", "Change"][:columns]
    return "\n".join(lines + [markdown_table(header, [row[:columns] for row in rows])])


def run_browser(label: str, command: list[str], settle: float) -> dict:
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    try:
        time.sleep(settle)
        if process.poll() is not None:
            print(f"{label}: exited early with {process.returncode}", flush=True)
            return {"label": label, "error": f"exited {process.returncode}"}
        sample = sample_tree(process.pid)
    finally:
        stop_process_group(process)
    sample["label"] = label
    print(f"{label}: {process_summary(sample)}", flush=True)
    return sample


def stop_process_group(process: subprocess.Popen) -> None:
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=10)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()


def descendants(root: int) -> list[int]:
    children: dict[int, list[int]] = {}
    proc = Path("/proc")
    if proc.is_dir():
        for entry in proc.iterdir():
            if not entry.name.isdigit():
                continue
            try:
                status = (entry / "status").read_text()
            except (FileNotFoundError, PermissionError, ProcessLookupError):
                continue
            pid = int(entry.name)
            ppid = 0
            for line in status.splitlines():
                if line.startswith("PPid:"):
                    ppid = int(line.split()[1])
                    break
            children.setdefault(ppid, []).append(pid)
    else:
        output = subprocess.run(["ps", "-A", "-o", "pid=,ppid="], capture_output=True, text=True).stdout
        for line in output.splitlines():
            pid, ppid = (int(field) for field in line.split())
            children.setdefault(ppid, []).append(pid)
    found = []
    stack = [root]
    while stack:
        pid = stack.pop()
        found.append(pid)
        stack.extend(children.get(pid, []))
    return found


def sample_tree(root: int) -> dict:
    pids = descendants(root)
    processes = [sample_pid(pid) for pid in pids]
    processes = [process for process in processes if process is not None]
    rss = sum(process["rss"] for process in processes)
    pss = sum(process["pss"] for process in processes if process["pss"] is not None)
    by_type: dict[str, list[int]] = {}
    for process in processes:
        by_type.setdefault(classify(process["name"]), []).append(process["rss"])
    return {
        "root": root,
        "rss": rss,
        "pss": pss,
        "count": len(processes),
        "by_type": {name: {"count": len(values), "rss": sum(values)} for name, values in sorted(by_type.items())},
        "processes": processes,
    }


def sample_pid(pid: int) -> dict | None:
    if sys.platform == "linux":
        return sample_pid_linux(pid)
    if sys.platform == "darwin":
        return sample_pid_darwin(pid)
    print(f"process sampling is not implemented on {sys.platform}", file=sys.stderr)
    return None


def sample_pid_linux(pid: int) -> dict | None:
    proc = Path("/proc") / str(pid)
    try:
        comm = (proc / "comm").read_text().strip()
        status = (proc / "status").read_text()
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        return None
    rss = status_field_bytes(status, "VmRSS:")
    peak = status_field_bytes(status, "VmHWM:")
    pss = None
    rollup = proc / "smaps_rollup"
    try:
        pss = pss_from_smaps(rollup.read_text())
    except (FileNotFoundError, PermissionError, ProcessLookupError, OSError):
        smaps = proc / "smaps"
        try:
            pss = pss_from_smaps(smaps.read_text())
        except (FileNotFoundError, PermissionError, ProcessLookupError, OSError):
            pss = None
    return {"pid": pid, "name": comm, "rss": rss, "peak": peak, "pss": pss}


def status_field_bytes(status: str, name: str) -> int:
    for line in status.splitlines():
        if line.startswith(name):
            return int(line.split()[1]) * 1024
    return 0


def pss_from_smaps(text: str) -> int:
    total = 0
    for line in text.splitlines():
        if line.startswith("Pss:"):
            total += int(line.split()[1]) * 1024
    return total


def sample_pid_darwin(pid: int) -> dict | None:
    output = subprocess.run(["footprint", "-f", "bytes", "-p", str(pid)], capture_output=True, text=True)
    if output.returncode != 0:
        return None
    current = 0
    peak = 0
    name = str(pid)
    for line in output.stdout.splitlines():
        if match := re.match(r"^(.*) \[\d+\]: \S+\s+Footprint: (\d+) B", line):
            name, current = match[1].strip(), int(match[2])
        elif match := re.match(r"^\s*phys_footprint_peak: (\d+) B", line):
            peak = int(match[1])
    return {"pid": pid, "name": name, "rss": current, "peak": peak, "pss": current}


def classify(name: str) -> str:
    for process_type in PROCESS_TYPES:
        if process_type.lower() in name.lower():
            return process_type
    return name or "unknown"


def mib(bytes_value: int) -> str:
    return f"{bytes_value / (1024 * 1024):.1f}"


def process_summary(sample: dict) -> str:
    pss = f", PSS {mib(sample['pss'])} MiB" if sample.get("pss") else ""
    types = ", ".join(f"{info['count']}x {name} {mib(info['rss'])}" for name, info in sample.get("by_type", {}).items())
    return f"RSS {mib(sample['rss'])} MiB{pss} in {sample['count']} processes ({types})"


def print_tree(sample: dict) -> None:
    print(process_summary(sample), flush=True)
    for process in sorted(sample["processes"], key=lambda item: -item["rss"]):
        pss = f" PSS {mib(process['pss'])}" if process["pss"] is not None else ""
        print(f"  {process['pid']:6} {process['name']:<16} RSS {mib(process['rss'])}{pss}", flush=True)


def self_test() -> int:
    sample = sample_tree(os.getpid())
    print_tree(sample)
    if sample["rss"] <= 0:
        print("self-test: RSS was 0", file=sys.stderr)
        return 1
    if sys.platform == "linux" and sample["pss"] <= 0:
        print("self-test: PSS was 0 on Linux", file=sys.stderr)
        return 1
    fake = "Pss:                 1234 kB\nPss:                    2 kB\n"
    if pss_from_smaps(fake) != (1234 + 2) * 1024:
        print("self-test: pss_from_smaps failed", file=sys.stderr)
        return 1
    speedometer_dump = "Speedometer 3.1 Results Summary\nScore\n11.9\n± 0.24\nDetails\n"
    parsed = parse_browserbench_score("Speedometer 3.1", speedometer_dump)
    if parsed != 11.9:
        print(f"self-test: Speedometer parser got {parsed!r}, want 11.9", file=sys.stderr)
        return 1
    if parse_browserbench_score("Speedometer 3.1", "Score\nError\n") is not None:
        print("self-test: invalid Speedometer dump should not yield a score", file=sys.stderr)
        return 1
    jetstream_dump = "JetStream 2.2\nScore\n142.7\n"
    parsed = parse_browserbench_score("JetStream 2.2", jetstream_dump)
    if parsed != 142.7:
        print(f"self-test: JetStream parser got {parsed!r}, want 142.7", file=sys.stderr)
        return 1
    motion_dump = "MotionMark 1.3.1\nScore: 203.5\n"
    parsed = parse_browserbench_score("MotionMark 1.3.1", motion_dump)
    if parsed != 203.5:
        print(f"self-test: MotionMark parser got {parsed!r}, want 203.5", file=sys.stderr)
        return 1
    jetstream_summary = "JetStream 2.2\n142.7\nScore\n"
    parsed = parse_browserbench_score("JetStream 2.2", jetstream_summary)
    if parsed != 142.7:
        print(f"self-test: number-before-Score parser got {parsed!r}, want 142.7", file=sys.stderr)
        return 1
    # The shape of a 300 s JetStream accessibility dump: the suite total is a text leaf before the label, and each
    # benchmark heading further down holds its own score.
    jetstream_a11y_dump = (
        "document\n"
        "  generic\n"
        '    text leaf name="JetStream 2.2 is a JavaScript and WebAssembly benchmark suite."\n'
        "  generic\n"
        "    generic\n"
        '      text leaf name="96.629"\n'
        '    text leaf name="Score"\n'
        "  generic\n"
        '    heading name="3d-cube-SP" level=3\n'
        '      text leaf name="3d-cube-SP"\n'
        '    heading name="113.639" level=4\n'
        '      text leaf name="113.639"\n'
    )
    parsed = parse_browserbench_score("JetStream 2.2", jetstream_a11y_dump)
    if parsed != 96.629:
        print(f"self-test: JetStream accessibility total parser got {parsed!r}, want 96.629", file=sys.stderr)
        return 1
    swapped_dump = jetstream_a11y_dump.replace("96.629", "88.401")
    if parse_browserbench_score("JetStream 2.2", swapped_dump) != 88.401:
        print("self-test: JetStream accessibility parser ignored its fixture total", file=sys.stderr)
        return 1
    no_total_dump = jetstream_a11y_dump.replace('text leaf name="96.629"\n', "")
    if parse_browserbench_score("JetStream 2.2", no_total_dump) is not None:
        print("self-test: JetStream run without a total should not yield a score", file=sys.stderr)
        return 1
    a11y_dump = (
        "document\n"
        "  generic\n"
        '    link name="MotionMark 1.3.1" url="#home"\n'
        '      text leaf name="1.3.1"\n'
        "    generic\n"
        '      text leaf name="203.5"\n'
        '      text leaf name="± 1.23%"\n'
    )
    parsed = parse_browserbench_score("MotionMark 1.3.1", a11y_dump)
    if parsed != 203.5:
        print(f"self-test: accessibility dump parser got {parsed!r}, want 203.5", file=sys.stderr)
        return 1
    # A 600 s Speedometer accessibility dump had no page text at all; the score survived only as the first decimal
    # text leaf under the Score heading. The "3.1" version leaf must not be picked up.
    speedometer_a11y_dump = (
        '    link name="Speedometer 3.1" url="#home"\n'
        '      image name="Speedometer"\n'
        "      generic\n"
        '        text leaf name="3.1"\n'
        '    heading name="Score" level=1\n'
        '      text leaf name="Score"\n'
        "    generic\n"
        "      generic\n"
        "        generic\n"
        "    separator\n"
        "    generic\n"
        '      text leaf name="6.31"\n'
        "    generic\n"
        '      text leaf name="± 0.58"\n'
        '    link name="Details" url="#details"\n'
        '      text leaf name="Details"\n'
    )
    parsed = parse_browserbench_score("Speedometer 3.1", speedometer_a11y_dump)
    swap_dump = speedometer_a11y_dump.replace("6.31", "7.44")
    if parse_browserbench_score("Speedometer 3.1", speedometer_a11y_dump) != 6.31:
        print(f"self-test: Speedometer accessibility dump parser got {parsed!r}, want 6.31", file=sys.stderr)
        return 1
    if parse_browserbench_score("Speedometer 3.1", swap_dump) != 7.44:
        print("self-test: Speedometer accessibility dump ignored its fixture number", file=sys.stderr)
        return 1
    # A 120 s Speedometer layout dump put the result in div#result-number as a quoted fragment, followed by the ±
    # confidence fragment.
    speedometer_layout_dump = (
        "        BlockContainer <h1> at [240,180] [0+0+0 800 0+0+0] [30+0+0 44 0+0+26.796875] children: inline\n"
        "          frag 0 from TextNode start: 0, length: 5, rect: [587.734375,180 104.515625x44] baseline: 35.875\n"
        '            "Score"\n'
        "        TextNode <#text> (not painted)\n"
        "      BlockContainer <div.gauge> at [271,250.796875] positioned [31+0+0 738 0+0+31] [0+0+0 78 0+0+0] children: inline\n"
        "        BlockContainer <div.needle> at [634,16.796875] positioned [0+0+0 4 0+0+0] [0+0+0 400 0+0+0] [BFC] children: not-inline\n"
        "          BlockContainer <hr> at [440,369.796875] [199+1+0 400 0+1+199] [40+1+0 0 0+1+40] [BFC] children: not-inline\n"
        "        BlockContainer <div#result-number> at [240,410.796875] [0+0+0 800 0+0+0] [0+0+0 145 0+0+0] children: inline\n"
        "          frag 0 from TextNode start: 0, length: 4, rect: [498.890625,402.296875 282.203125x162] baseline: 131.265625\n"
        '            "4.83"\n'
        "        TextNode <#text> (not painted)\n"
        "        BlockContainer <div#confidence-number> at [240,555.796875] [0+0+0 800 0+0+0] [0+0+0 36 0+0+0] children: inline\n"
        "          frag 0 from TextNode start: 0, length: 6, rect: [590.09375,553.296875 99.8125x41] baseline: 32.984375\n"
        '            "± 0.11"\n'
    )
    parsed = parse_browserbench_score("Speedometer 3.1", speedometer_layout_dump)
    if parsed != 4.83:
        print(f"self-test: Speedometer layout dump parser got {parsed!r}, want 4.83", file=sys.stderr)
        return 1
    if parse_browserbench_score("Speedometer 3.1", speedometer_layout_dump.replace('"4.83"', '"6.02"')) != 6.02:
        print("self-test: Speedometer layout dump ignored its fixture number", file=sys.stderr)
        return 1
    layout_dump = (
        "Dumping page text after 600 seconds\n"
        "Viewport <#document> at [0,0] [0+0+0 1280 0+0+0]\n"
        "  BlockContainer <div.score-container> at [116,234]\n"
        "    BlockContainer <div.score> at [116,234] children: inline\n"
        "      frag 0 from TextNode start: 0, length: 6\n"
        '          "307.56"\n'
    )
    parsed = parse_browserbench_score("MotionMark 1.3.1", layout_dump)
    if parsed != 307.56:
        print(f"self-test: layout tree parser got {parsed!r}, want 307.56", file=sys.stderr)
        return 1
    if parse_browserbench_score("Speedometer 3.1", "Start Test\nYour browser window is too small.") is not None:
        print("self-test: start page should not yield a score", file=sys.stderr)
        return 1
    auto_start = any("startAutomatically" in url for _, url in BROWSER_BENCH_PAGES)
    if not auto_start:
        print("self-test: BrowserBench URLs must start the suite without a click", file=sys.stderr)
        return 1
    # MotionMark has no URL-driven start, so bench must serve its own start page as the suite's document.
    motionmark_url = dict(BROWSER_BENCH_PAGES)["MotionMark 1.3.1"]
    substitution = BENCH_SUBSTITUTIONS.get("MotionMark 1.3.1", [])
    if not substitution or substitution[0]["url"] != motionmark_url:
        print("self-test: MotionMark needs a resource substitution for its own document", file=sys.stderr)
        return 1
    if not MOTIONMARK_START_PAGE.is_file():
        print(f"self-test: missing {MOTIONMARK_START_PAGE}", file=sys.stderr)
        return 1
    start_page = MOTIONMARK_START_PAGE.read_text()
    if "startBenchmark()" not in start_page or "setInterval" not in start_page:
        print("self-test: MotionMark start page does not press Run Benchmark", file=sys.stderr)
        return 1
    print("self-test: ok", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
