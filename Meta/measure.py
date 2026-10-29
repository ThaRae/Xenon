#!/usr/bin/env python3

# Reproducible memory and BrowserBench measurements for issue #28.
#
#   Meta/measure.py content-blocker              # filter-list engine, no browser build
#   Meta/measure.py processes --pid PID          # RSS/PSS of a running process tree
#   Meta/measure.py browsers --xenon PATH [...]  # headless Xenon (and Chrome) on sample pages
#   Meta/measure.py bench --xenon PATH           # Speedometer / JetStream / MotionMark, if reachable
#   Meta/measure.py self-test                    # Linux smaps parsing, no Xenon needed
#
# Writes JSON to --log (default Meta/measure-results/<stamp>.json) and a markdown table to stdout.
# Documentation/Memory.md says how to rerun this and what each experiment decided.

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import time

from datetime import datetime
from datetime import timezone
from pathlib import Path

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

BROWSER_BENCH_PAGES = [
    ("Speedometer 3.1", "https://browserbench.org/Speedometer3.1/?startAutomatically"),
    ("JetStream 2.2", "https://browserbench.org/JetStream2.2/?startAutomatically=true"),
    ("MotionMark 1.3.1", "https://browserbench.org/MotionMark1.3.1/?startAutomatically=true"),
]

SETTLE_SECONDS = 25
# Default Speedometer 3.1 is 10 iterations and often needs well over 3 minutes on Ladybird.
BENCH_SECONDS = 900
BENCH_WINDOW = (1280, 900)


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
        result = measure_bench(args.xenon, args.seconds)
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


def xenon_command(executable: Path, page: str, blocking: bool, extra: list[str] | None = None) -> list[str]:
    profile = Path(tempfile.mkdtemp(prefix="xenon-measure-"))
    config = profile / "config"
    config.mkdir(parents=True, exist_ok=True)
    (config / "Settings.json").write_text(json.dumps({"contentBlockers": {"enabled": blocking}}))
    flags = list(extra) if extra else ["--headless=manual"]
    return [str(executable), *flags, "--profile-path", str(profile), page]


def parse_browserbench_score(suite: str, text: str) -> float | None:
    """Pull a numeric suite score out of headless page text. Returns None if the suite never finished."""
    if not text:
        return None
    lines = [line.strip() for line in text.splitlines()]
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


def measure_bench(xenon_path: Path, seconds: float) -> dict:
    xenon = xenon_executable(xenon_path)
    delay = max(2, int(seconds))
    extra = [
        "--headless=text",
        f"--screenshot-delay={delay}",
        f"--window-width={BENCH_WINDOW[0]}",
        f"--window-height={BENCH_WINDOW[1]}",
    ]
    results = []
    for name, url in BROWSER_BENCH_PAGES:
        print(f"### {name} ({url})", flush=True)
        command = xenon_command(xenon, url, True, extra)
        started = time.time()
        process = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True, text=True
        )
        output = ""
        sample: dict = {}
        try:
            try:
                output, _ = process.communicate(timeout=seconds + 30)
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
        results.append(
            {
                "suite": name,
                "url": url,
                "seconds": elapsed,
                "score": score,
                "sample": sample,
                "output_excerpt": (output or "")[-4000:],
            }
        )
    return {"suites": results}


def run_browser(label: str, command: list[str], settle: float) -> dict:
    executable = command[0]
    try:
        os.chmod(executable, 0o755)
    except OSError:
        pass
    try:
        process = subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
        )
    except PermissionError:
        time.sleep(1)
        try:
            os.chmod(executable, 0o755)
        except OSError:
            pass
        process = subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
        )
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
    if parse_browserbench_score("Speedometer 3.1", "Start Test\nYour browser window is too small.") is not None:
        print("self-test: start page should not yield a score", file=sys.stderr)
        return 1
    auto_start = any("startAutomatically" in url for _, url in BROWSER_BENCH_PAGES)
    if not auto_start:
        print("self-test: BrowserBench URLs must start the suite without a click", file=sys.stderr)
        return 1
    print("self-test: ok", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
