#!/usr/bin/env python3
"""Run the feature-1 farbling seed proofs against a compiled Xenon binary."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
XENON_ROOT = HERE.parents[2]
DEFAULT_XENON = XENON_ROOT / "Build" / "release" / "xenon" / "bin" / "Xenon"
CAPTURED = HERE / "captured"


def run_xenon(executable: Path, profile: Path, url: str, extra: list[str], timeout: int = 60) -> None:
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    command = [str(executable), "--headless=manual", *extra, "--profile-path", str(profile), url]
    subprocess.run(command, env=env, capture_output=True, text=True, timeout=timeout, check=False)


def run_xenon_until(executable: Path, profile: Path, url: str, extra: list[str], path: Path, predicate, timeout: int = 50) -> None:
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    command = [str(executable), "--headless=manual", *extra, "--profile-path", str(profile), url]
    proc = subprocess.Popen(command, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_for(path, predicate, timeout=timeout)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def wait_for(path: Path, predicate, timeout: float = 50) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.exists():
            try:
                data = json.loads(path.read_text())
            except json.JSONDecodeError:
                data = None
            if data is not None and predicate(data):
                return
        time.sleep(0.2)
    raise SystemExit(f"timed out waiting for {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xenon", type=Path, default=DEFAULT_XENON)
    parser.add_argument("--label", default="")
    args = parser.parse_args()
    if not args.xenon.exists():
        print(f"missing binary: {args.xenon}", file=sys.stderr)
        return 1
    CAPTURED.mkdir(exist_ok=True)
    extra = ["--expose-internals-object"]
    same_process = CAPTURED / "same-process.json"
    same_process.unlink(missing_ok=True)
    pending = CAPTURED / "nav-pending-after.json"
    pending.unlink(missing_ok=True)

    profile = Path(tempfile.mkdtemp(prefix="fp-same-"))
    run_xenon_until(
        args.xenon,
        profile,
        "http://site-a.test:8080/start.html",
        extra,
        same_process,
        lambda data: "a-direct" in data.get("reads", {})
        and "b-location" in data.get("reads", {})
        and "complete" in data,
    )
    pending_profile = Path(tempfile.mkdtemp(prefix="fp-pending-"))
    run_xenon_until(
        args.xenon,
        pending_profile,
        "http://site-a.test:8080/pending.html?run=nav-pending-after",
        extra,
        pending,
        lambda data: "delay-same-iframe" in data.get("reads", {})
        and "b-first" in data.get("reads", {})
        and "complete" in data,
        timeout=50,
    )

    pr1 = tempfile.mkdtemp(prefix="fp-rs1-")
    run_xenon(args.xenon, Path(pr1), "http://site-a.test:8080/page.html?run=restart-1", extra)
    pr2 = Path(tempfile.mkdtemp(prefix="fp-rs2-"))
    src = Path(pr1) / "config" / "FarblingSeeds.json"
    if src.exists():
        (pr2 / "config").mkdir(parents=True, exist_ok=True)
        shutil.copy(src, pr2 / "config" / "FarblingSeeds.json")
    run_xenon(args.xenon, pr2, "http://site-a.test:8080/page.html?run=restart-2", extra)

    poff = Path(tempfile.mkdtemp(prefix="fp-off-"))
    (poff / "config").mkdir()
    (poff / "config" / "FarblingSeeds.json").write_text(
        json.dumps({"tokens": {}, "offSites": ["http://site-a.test"]})
    )
    run_xenon(args.xenon, poff, "http://site-a.test:8080/page.html?run=off-a", extra)
    run_xenon(args.xenon, poff, "http://site-b.test:8080/page.html?run=off-b", extra)

    run_xenon(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp-plain-")),
        "http://site-a.test:8080/page.html?run=no-internals",
        [],
    )

    names = ["same-process", "nav-pending-after", "restart-1", "restart-2", "off-a", "off-b", "no-internals"]
    for name in names:
        path = CAPTURED / f"{name}.json"
        print("====", name, args.label, "====")
        print(path.read_text() if path.exists() else "MISSING")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.TimeoutExpired as error:
        print(f"timed out: {error}", file=sys.stderr)
        raise SystemExit(1)
