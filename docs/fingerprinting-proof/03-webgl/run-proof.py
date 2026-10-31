#!/usr/bin/env python3
"""Run the feature-3 WebGL farbling proofs against a compiled Xenon binary."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
XENON_ROOT = HERE.parents[2]
DEFAULT_XENON = XENON_ROOT / "Build" / "release" / "xenon" / "bin" / "Xenon"
CAPTURED = HERE / "captured"
FARBLED = [
    "MAX_VERTEX_UNIFORM_COMPONENTS",
    "MAX_VERTEX_UNIFORM_BLOCKS",
    "MAX_VERTEX_OUTPUT_COMPONENTS",
    "MAX_VARYING_COMPONENTS",
    "MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS",
    "MAX_FRAGMENT_UNIFORM_COMPONENTS",
    "MAX_FRAGMENT_UNIFORM_BLOCKS",
    "MAX_FRAGMENT_INPUT_COMPONENTS",
    "MAX_UNIFORM_BUFFER_BINDINGS",
    "MAX_COMBINED_UNIFORM_BLOCKS",
    "MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS",
    "MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS",
]


def run_xenon_until(executable: Path, profile: Path, url: str, extra: list[str], path: Path, predicate, timeout: int = 90) -> None:
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


def wait_for(path: Path, predicate, timeout: float = 90) -> None:
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


def write_off_profile(path: Path, sites: list[str]) -> None:
    config = path / "config"
    config.mkdir(parents=True, exist_ok=True)
    (config / "FarblingSeeds.json").write_text(json.dumps({"tokens": {}, "offSites": sites}))


def differing_maxes(a: dict, b: dict) -> list[str]:
    names = []
    for name in FARBLED:
        if a.get(name) != b.get(name):
            names.append(name)
    return names


def values_are_real_or_minus_one(farbled: dict, real: dict) -> bool:
    for name in FARBLED:
        got = farbled.get(name)
        expected = real.get(name)
        if got is None or expected is None:
            continue
        if got not in (expected, expected - 1):
            return False
    return True


def analyze() -> dict:
    same = json.loads((CAPTURED / "same-process.json").read_text())
    site_a = json.loads((CAPTURED / "site-a.json").read_text())
    off_both = json.loads((CAPTURED / "off-both.json").read_text())
    a = same["reads"]["a-direct"]
    b = same["reads"]["b-direct"]
    a_gl2 = a["measure"]["webgl2"]
    b_gl2 = b["measure"]["webgl2"]
    a_gl1 = a["measure"]["webgl"]
    off_a = off_both["reads"]["a-direct"]["measure"]
    off_b = off_both["reads"]["b-direct"]["measure"]
    reloads = [item["webgl2Hash"] for item in a.get("reloads") or []]
    iframe = a.get("crossIframe") or {}
    samples = site_a["samples"]
    worker = samples[0].get("worker") or {}
    differ = differing_maxes(a_gl2.get("maxes") or {}, b_gl2.get("maxes") or {})
    summary = {
        "webgl_ok": a_gl1.get("ok") is True,
        "webgl2_ok": a_gl2.get("ok") is True,
        "a_webgl2_hash": a_gl2.get("hash"),
        "b_webgl2_hash": b_gl2.get("hash"),
        "differing_maxes": differ,
        "a_ne_b": a_gl2.get("hash") != b_gl2.get("hash"),
        "same_process_reloads": reloads,
        "reloads_match_a": len(reloads) == 3 and len(set(reloads)) == 1 and reloads[0] == a_gl2.get("hash"),
        "iframe_webgl2_hash": (iframe.get("webgl2") or {}).get("hash"),
        "iframe_matches_a": (iframe.get("webgl2") or {}).get("hash") == a_gl2.get("hash"),
        "values_real_or_minus_one": values_are_real_or_minus_one(a_gl2.get("maxes") or {}, (off_a.get("webgl2") or {}).get("maxes") or {}),
        "alloc_ok": a_gl2.get("allocOk"),
        "a_vendor": a_gl2.get("vendor"),
        "a_renderer": a_gl2.get("renderer"),
        "off_vendor": (off_a.get("webgl2") or {}).get("vendor"),
        "off_renderer": (off_a.get("webgl2") or {}).get("renderer"),
        "generic_on_standard": a_gl2.get("vendor") == "Xenon" and a_gl2.get("renderer") == "Xenon Generic Renderer",
        "real_on_off": (off_a.get("webgl2") or {}).get("vendor") not in (None, "Xenon")
        and (off_a.get("webgl2") or {}).get("renderer") not in (None, "Xenon Generic Renderer"),
        "extensions_a": a_gl2.get("extensions"),
        "extensions_off": (off_a.get("webgl2") or {}).get("extensions"),
        "extensions_match_off": a_gl2.get("extensions") == (off_a.get("webgl2") or {}).get("extensions"),
        "extension_ok": a_gl2.get("extensionOk") is True and (off_a.get("webgl2") or {}).get("extensionOk") is True,
        "worker_webgl2_hash": (worker.get("webgl2") or {}).get("hash"),
        "worker_matches_main": (worker.get("webgl2") or {}).get("hash") == samples[0]["webgl2"]["hash"],
        "readpixels_a": a_gl2.get("readPixels"),
        "readpixels_b": b_gl2.get("readPixels"),
        "readpixels_off": (off_a.get("webgl2") or {}).get("readPixels"),
        "readpixels_equal": a_gl2.get("readPixels") == b_gl2.get("readPixels") == (off_a.get("webgl2") or {}).get("readPixels"),
        "a_seed": a.get("seed"),
        "b_seed": b.get("seed"),
        "off_a_seed": off_both["reads"]["a-direct"].get("seed") or off_a.get("seed"),
        "off_b_seed": off_both["reads"]["b-direct"].get("seed") or off_b.get("seed"),
    }
    summary["ok"] = (
        summary["webgl_ok"]
        and summary["webgl2_ok"]
        and summary["a_ne_b"]
        and bool(summary["differing_maxes"])
        and summary["reloads_match_a"]
        and summary["iframe_matches_a"]
        and summary["values_real_or_minus_one"]
        and summary["alloc_ok"] is True
        and summary["generic_on_standard"]
        and summary["real_on_off"]
        and summary["extensions_match_off"]
        and summary["extension_ok"]
        and summary["worker_matches_main"]
        and summary["readpixels_equal"]
    )
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xenon", type=Path, default=DEFAULT_XENON)
    args = parser.parse_args()
    if not args.xenon.exists():
        print(f"missing binary: {args.xenon}", file=sys.stderr)
        return 1
    CAPTURED.mkdir(exist_ok=True)
    extra = ["--expose-internals-object", "--force-cpu-painting", "--disable-sandbox"]

    same_process = CAPTURED / "same-process.json"
    same_process.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp3-same-")),
        "http://site-a.test:8080/start.html",
        extra,
        same_process,
        lambda data: "a-direct" in data.get("reads", {})
        and "b-direct" in data.get("reads", {})
        and data.get("complete")
        and len((data.get("reads", {}).get("a-direct") or {}).get("reloads") or []) >= 3,
        timeout=120,
    )

    first = json.loads(same_process.read_text())["reads"]["a-direct"]["measure"]
    if not first.get("webgl", {}).get("ok") and not first.get("webgl2", {}).get("ok"):
        print(json.dumps({"ok": False, "error": "WebGL contexts did not work", "measure": first}, indent=2))
        return 1

    site_a = CAPTURED / "site-a.json"
    site_a.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp3-a-")),
        "http://site-a.test:8080/page.html?run=site-a",
        extra,
        site_a,
        lambda data: "samples" in data and len(data["samples"]) >= 3,
        timeout=90,
    )

    off_both = CAPTURED / "off-both.json"
    off_both.unlink(missing_ok=True)
    poff = Path(tempfile.mkdtemp(prefix="fp3-off-"))
    write_off_profile(poff, ["http://site-a.test", "http://site-b.test"])
    run_xenon_until(
        args.xenon,
        poff,
        "http://site-a.test:8080/start.html?run=off-both",
        extra,
        off_both,
        lambda data: "a-direct" in data.get("reads", {}) and "b-direct" in data.get("reads", {}) and data.get("complete"),
        timeout=120,
    )

    summary = analyze()
    (CAPTURED / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
