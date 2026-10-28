#!/usr/bin/env python3

# Temporary (issue #28): what an installed extension costs headless Xenon in memory, as the physical footprint of
# Xenon's processes (Activity Monitor's Memory column, from footprint(1)), with and without the test extension, while its
# background is new and once it has had time to go idle. macOS only.
#
#   measure.py --xenon Build/release/bin/Xenon.app --extension Tests/Extensions/basic --label after

from __future__ import annotations

import argparse
import http.server
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time

from pathlib import Path

# Seconds after launch: the background has started and run, and then, past Chrome's 30 idle seconds, it may have stopped.
SAMPLES = (15, 45)
RUNS = 2


def local_server() -> tuple[str, http.server.ThreadingHTTPServer]:
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body = b"<!doctype html><title>Page</title><p>A page in a tab.</p>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return f"http://127.0.0.1:{server.server_address[1]}/", server


def descendants(root: int) -> list[int]:
    output = subprocess.run(["ps", "-A", "-o", "pid=,ppid="], capture_output=True, text=True).stdout
    children: dict[int, list[int]] = {}
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


FOOTPRINT_LINE = re.compile(r"^(.*) \[(\d+)\]: \S+\s+Footprint: (\d+) B")
PEAK_LINE = re.compile(r"^\s*phys_footprint_peak: (\d+) B")


def footprints(pids: list[int]) -> list[tuple[str, int, int]]:
    command = ["footprint", "-f", "bytes"]
    for pid in pids:
        command += ["-p", str(pid)]
    output = subprocess.run(command, capture_output=True, text=True).stdout
    processes: list[tuple[str, int, int]] = []
    for line in output.splitlines():
        if match := FOOTPRINT_LINE.match(line):
            processes.append((match[1], int(match[3]), 0))
        elif (match := PEAK_LINE.match(line)) and processes:
            name, current, _ = processes[-1]
            processes[-1] = (name, current, int(match[1]))
    return processes


def mib(value: int) -> str:
    return f"{value / (1024 * 1024):.1f}"


def describe(processes: list[tuple[str, int, int]]) -> str:
    total = sum(current for _, current, _ in processes)
    by_name: dict[str, list[int]] = {}
    for name, current, _ in processes:
        by_name.setdefault(name, []).append(current)
    breakdown = ", ".join(
        f"{len(values)}x {name} {'+'.join(mib(value) for value in sorted(values, reverse=True))}"
        for name, values in sorted(by_name.items(), key=lambda item: -sum(item[1]))
    )
    peaks = ", ".join(mib(peak) for name, _, peak in processes if "WebContent" in name)
    return f"{mib(total)} MiB in {len(processes)} processes ({breakdown}); WebContent peaks {peaks}"


def run(xenon: Path, page: str, extension: Path | None) -> list[list[tuple[str, int, int]]]:
    with tempfile.TemporaryDirectory() as profile:
        # Blocking on, as the welcome flow leaves it.
        (Path(profile) / "config").mkdir()
        (Path(profile) / "config" / "Settings.json").write_text(json.dumps({"contentBlockers": {"enabled": True}}))
        command = [str(xenon), "--headless=manual", "--profile-path", profile]
        if extension:
            command.append(f"--load-extension={extension}")
        command.append(page)

        process = subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
        )
        samples = []
        try:
            start = time.monotonic()
            for at in SAMPLES:
                time.sleep(max(0, start + at - time.monotonic()))
                if process.poll() is not None:
                    print(f"    exited early with {process.returncode}", flush=True)
                    return samples
                samples.append(footprints(descendants(process.pid)))
        finally:
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=10)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
        return samples


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--xenon", required=True, type=Path)
    parser.add_argument("--extension", required=True, type=Path)
    parser.add_argument("--label", required=True)
    arguments = parser.parse_args()

    xenon = arguments.xenon / "Contents" / "MacOS" / "Xenon"
    page, server = local_server()
    try:
        print(f"## {arguments.label}", flush=True)
        for attempt in range(1, RUNS + 1):
            without = run(xenon, page, None)
            with_extension = run(xenon, page, arguments.extension.resolve())
            for at, base, extended in zip(SAMPLES, without, with_extension):
                base_total = sum(current for _, current, _ in base)
                extended_total = sum(current for _, current, _ in extended)
                print(f"run {attempt}, {at} s after launch:", flush=True)
                print(f"    without the extension: {describe(base)}", flush=True)
                print(f"    with the extension:    {describe(extended)}", flush=True)
                print(
                    f"    the extension costs {mib(extended_total - base_total)} MiB"
                    f" and {len(extended) - len(base)} processes",
                    flush=True,
                )
        print(flush=True)
    finally:
        server.shutdown()


if __name__ == "__main__":
    sys.exit(main())
