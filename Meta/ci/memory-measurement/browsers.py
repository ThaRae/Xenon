#!/usr/bin/env python3

# Temporary (issue #28): the memory headless Xenon and headless Chrome use on a few pages, summed over each browser's
# processes. macOS only: it reads each process's physical footprint (Activity Monitor's Memory column) with footprint(1).
#
#   browsers.py --xenon Build/release/bin/Xenon.app [--chrome "/Applications/Google Chrome.app"]

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

SETTLE_SECONDS = 25
REMOTE_PAGES = [
    "https://github.com/LadybirdBrowser/ladybird",
    "https://www.youtube.com/",
    "https://x.com/",
]


def local_server() -> tuple[str, http.server.ThreadingHTTPServer]:
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            port = self.server.server_address[1]
            if self.path == "/":
                # Each frame is another site (localhost rather than 127.0.0.1), so it gets a process of its own.
                frames = "".join(f'<iframe src="http://localhost:{port}/frame?{index}"></iframe>' for index in range(3))
                body = f"<!doctype html><title>Frames</title><p>Three cross-site frames</p>{frames}".encode()
            else:
                body = b"<!doctype html><p>frame</p>"
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
SUMMARY_LINE = re.compile(r"^Summary Footprint: (\d+) B")


def footprints(pids: list[int]) -> tuple[list[tuple[str, int, int]], int]:
    command = ["footprint", "-f", "bytes"]
    for pid in pids:
        command += ["-p", str(pid)]
    output = subprocess.run(command, capture_output=True, text=True).stdout
    processes: list[tuple[str, int, int]] = []
    total = 0
    for line in output.splitlines():
        if match := FOOTPRINT_LINE.match(line):
            processes.append((match[1], int(match[3]), 0))
        elif (match := PEAK_LINE.match(line)) and processes:
            name, current, _ = processes[-1]
            processes[-1] = (name, current, int(match[1]))
        elif match := SUMMARY_LINE.match(line):
            total = int(match[1])
    if not total:
        total = sum(current for _, current, _ in processes)
    return processes, total


def mib(value: int) -> str:
    return f"{value / (1024 * 1024):.0f}"


def measure(label: str, command: list[str]) -> dict:
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    try:
        time.sleep(SETTLE_SECONDS)
        if process.poll() is not None:
            print(f"{label}: exited early with {process.returncode}", flush=True)
            return {}
        processes, total = footprints(descendants(process.pid))
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

    by_name: dict[str, list[int]] = {}
    for name, current, _ in processes:
        by_name.setdefault(name, []).append(current)
    breakdown = ", ".join(
        f"{len(values)}x {name} {'+'.join(mib(value) for value in sorted(values, reverse=True))}"
        for name, values in sorted(by_name.items(), key=lambda item: -sum(item[1]))
    )
    peaks = ", ".join(f"{name} {mib(peak)}" for name, _, peak in processes if "WebContent" in name)
    print(f"{label}: {mib(total)} MiB in {len(processes)} processes ({breakdown})", flush=True)
    if peaks:
        print(f"    WebContent peaks: {peaks}", flush=True)
    return {"label": label, "total": total, "processes": processes}


def xenon_profile(directory: str, blocking: bool):
    config = Path(directory) / "config"
    config.mkdir(parents=True, exist_ok=True)
    (config / "Settings.json").write_text(json.dumps({"contentBlockers": {"enabled": blocking}}))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--xenon", required=True, type=Path)
    parser.add_argument("--chrome", type=Path)
    arguments = parser.parse_args()

    xenon = arguments.xenon / "Contents" / "MacOS" / "Xenon"
    lists = arguments.xenon / "Contents" / "Resources" / "xenon" / "filter-lists"
    local_page, server = local_server()

    try:
        for page in [local_page, *REMOTE_PAGES]:
            print(f"### {page}", flush=True)
            for label, blocking, with_lists in (
                ("Xenon, blocking on", True, True),
                ("Xenon, blocking off", False, True),
                ("Xenon without its lists (as Ladybird ships)", False, False),
            ):
                hidden_lists = lists.with_name("filter-lists.hidden")
                if not with_lists:
                    lists.rename(hidden_lists)
                try:
                    with tempfile.TemporaryDirectory() as profile:
                        xenon_profile(profile, blocking)
                        measure(label, [str(xenon), "--headless=manual", "--profile-path", profile, page])
                finally:
                    if not with_lists:
                        hidden_lists.rename(lists)

            if arguments.chrome:
                chrome = arguments.chrome / "Contents" / "MacOS" / "Google Chrome"
                with tempfile.TemporaryDirectory() as profile:
                    measure(
                        "Chrome (headless, no extensions)",
                        [
                            str(chrome),
                            "--headless=new",
                            f"--user-data-dir={profile}",
                            "--no-first-run",
                            "--no-default-browser-check",
                            "--window-size=1280,900",
                            page,
                        ],
                    )
            print(flush=True)
    finally:
        server.shutdown()


if __name__ == "__main__":
    sys.exit(main())
