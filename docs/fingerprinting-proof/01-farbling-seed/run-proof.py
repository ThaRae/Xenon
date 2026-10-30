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
from pathlib import Path

HERE = Path(__file__).resolve().parent
XENON_ROOT = HERE.parents[2]
DEFAULT_XENON = XENON_ROOT / "Build" / "release" / "xenon" / "bin" / "Xenon"
CAPTURED = HERE / "captured"


def run_xenon(executable: Path, profile: Path, url: str, extra: list[str], timeout: int = 40) -> None:
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    command = [str(executable), "--headless=manual", *extra, "--profile-path", str(profile), url]
    subprocess.run(command, env=env, capture_output=True, text=True, timeout=timeout, check=False)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xenon", type=Path, default=DEFAULT_XENON)
    args = parser.parse_args()
    if not args.xenon.exists():
        print(f"missing binary: {args.xenon}", file=sys.stderr)
        return 1
    CAPTURED.mkdir(exist_ok=True)
    extra = ["--expose-internals-object"]

    pa = tempfile.mkdtemp(prefix="fp-a-")
    run_xenon(args.xenon, Path(pa), "http://site-a.test:8080/page.html?run=site-a", extra)
    pb = tempfile.mkdtemp(prefix="fp-b-")
    run_xenon(args.xenon, Path(pb), "http://site-b.test:8080/page.html?run=site-b", extra)

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
    (poff / "config" / "FarblingSeeds.json").write_text(json.dumps({"tokens": {}, "offSites": ["http://site-a.test"]}))
    run_xenon(args.xenon, poff, "http://site-a.test:8080/page.html?run=off-a", extra)
    run_xenon(args.xenon, poff, "http://site-b.test:8080/page.html?run=off-b", extra)

    run_xenon(args.xenon, Path(tempfile.mkdtemp(prefix="fp-plain-")), "http://site-a.test:8080/page.html?run=no-internals", [])

    for name in ("site-a", "site-b", "restart-1", "restart-2", "off-a", "off-b", "no-internals"):
        path = CAPTURED / f"{name}.json"
        print(name, path.read_text() if path.exists() else "MISSING")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.TimeoutExpired as error:
        print(f"timed out: {error}", file=sys.stderr)
        raise SystemExit(1)
