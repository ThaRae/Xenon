#!/usr/bin/env python3
"""Run the feature-5 font enumeration proofs against a compiled Xenon binary."""

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
WHITELIST = {
    "serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui",
    "SerenitySans", "Noto Emoji",
    "DejaVu Sans Mono", "Liberation Mono", "Liberation Sans", "Liberation Serif",
    "Arial", "Helvetica", "Times New Roman", "Times", "Courier New", "Courier",
    "Georgia", "Verdana", "Trebuchet MS", "Comic Sans MS", "Impact",
    "Palatino", "Bookman",
}


def installed_families() -> list[str]:
    out = subprocess.check_output(["fc-list", ":", "family"], text=True)
    names = set()
    for line in out.splitlines():
        for part in line.split(","):
            name = part.strip()
            if name:
                names.add(name)
    return sorted(names)


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


def screenshot(executable: Path, profile: Path, url: str, dest: Path, extra: list[str]) -> None:
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    dest.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(executable), "--headless=screenshot", "--screenshot-delay", "2",
        "--screenshot-path", str(dest), "--window-width", "640", "--window-height", "400",
        "--disable-scrollbar-painting", "--force-cpu-painting", *extra,
        "--profile-path", str(profile), url,
    ]
    subprocess.run(command, env=env, capture_output=True, text=True, timeout=40, check=False)


def write_off_profile(path: Path, sites: list[str]) -> None:
    config = path / "config"
    config.mkdir(parents=True, exist_ok=True)
    (config / "FarblingSeeds.json").write_text(json.dumps({"tokens": {}, "offSites": sites}))


def disagreements(detections: list[dict]) -> list[dict]:
    leaks = []
    for item in detections:
        width = item.get("width")
        canvas = item.get("canvas")
        check = item.get("check")
        if width != canvas:
            leaks.append({**item, "why": "width-canvas"})
        if width is True and check is False:
            leaks.append({**item, "why": "width-true-check-false"})
    return leaks


def analyze(installed: list[str]) -> dict:
    same = json.loads((CAPTURED / "same-process.json").read_text())
    site_a = json.loads((CAPTURED / "site-a.json").read_text())
    off_both = json.loads((CAPTURED / "off-both.json").read_text())
    a = same["reads"]["a-direct"]
    b = same["reads"]["b-direct"]
    a_m = a["measure"]
    b_m = b["measure"]
    off_a = off_both["reads"]["a-direct"]["measure"]
    off_b = off_both["reads"]["b-direct"]["measure"]
    a_present = a_m.get("present") or []
    b_present = b_m.get("present") or []
    off_present = off_a.get("present") or []
    tested_installed = [name for name in installed if name in {item["family"] for item in a_m.get("detections") or []}]
    only_a = sorted(set(a_present) - set(b_present))
    only_b = sorted(set(b_present) - set(a_present))
    reloads = [item.get("hash") for item in a.get("reloads") or []]
    iframe = a.get("crossIframe") or {}
    worker = a.get("worker") or {}
    worker_present = worker.get("present") or []
    a_leaks = disagreements(a_m.get("detections") or [])
    b_leaks = disagreements(b_m.get("detections") or [])
    whitelist_on_a = [name for name in WHITELIST if name in {item["family"] for item in a_m.get("detections") or []}]
    whitelist_detected = [name for name in whitelist_on_a if name in a_present and name in b_present]
    whitelist_installed = [name for name in whitelist_on_a if name in installed]
    bundled = {"SerenitySans", "Noto Emoji"}
    absent_detected = [name for name in a_present + b_present if name not in installed and name not in bundled]
    off_text = [name for name in off_present if name not in bundled]
    extras_data = {}
    extras_path = CAPTURED / "extras.json"
    if extras_path.exists():
        extras_data = (json.loads(extras_path.read_text()).get("samples") or [{}])[0]
    web = extras_data.get("webFont") or a_m.get("webFont") or {}
    local_probes = extras_data.get("localProbes") or a_m.get("localProbes") or []
    extras_present = extras_data.get("present") or []
    rejected = [item["family"] for item in a_m.get("detections") or [] if item.get("width") is False and item["family"] in installed]
    local_rejected_fails = [
        probe for probe in local_probes
        if probe.get("used") is False and probe.get("family") not in extras_present
    ]
    samples = site_a.get("samples") or []
    summary = {
        "installed_fc_list": installed,
        "installed_count": len(installed),
        "a_present": a_present,
        "b_present": b_present,
        "a_hash": a_m.get("hash"),
        "b_hash": b_m.get("hash"),
        "a_ne_b": a_m.get("hash") != b_m.get("hash"),
        "only_a": only_a,
        "only_b": only_b,
        "reloads": reloads,
        "reloads_match_a": len(reloads) == 3 and len(set(reloads)) == 1 and reloads[0] == a_m.get("hash"),
        "iframe_hash": iframe.get("hash"),
        "iframe_matches_a": iframe.get("hash") == a_m.get("hash"),
        "a_width_canvas_agree": all(item.get("width") == item.get("canvas") for item in a_m.get("detections") or []),
        "b_width_canvas_agree": all(item.get("width") == item.get("canvas") for item in b_m.get("detections") or []),
        "a_leaks": a_leaks,
        "b_leaks": b_leaks,
        "off_a_present": off_present,
        "off_b_present": off_b.get("present") or [],
        "off_a_equals_off_b": off_present == (off_b.get("present") or []),
        "off_equals_installed_tested": set(off_text) >= {"DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono", "Lato", "Liberation Mono", "Liberation Sans", "Liberation Serif"}
        and "FontAwesome" not in off_present,
        "tested_installed": tested_installed,
        "whitelist_installed": whitelist_installed,
        "whitelist_detected_on_a_and_b": whitelist_detected,
        "whitelist_always": all(name in a_present and name in b_present for name in whitelist_installed),
        "absent_detected": sorted(set(absent_detected)),
        "absent_never_detected": not absent_detected,
        "web_font": web,
        "web_font_renders": web.get("renders") is True,
        "local_probes": local_probes,
        "rejected_installed_on_a": rejected,
        "local_rejected_fails": local_rejected_fails,
        "worker_present": worker_present,
        "worker_ok": worker.get("ok") is True,
        "worker_agrees_a": worker.get("ok") is True and worker_present == a_present,
        "page_reload_hashes": [item.get("hash") for item in samples],
        "a_seed": a.get("seed") or a_m.get("seed"),
        "b_seed": b.get("seed") or b_m.get("seed"),
        "screenshot_a": (CAPTURED / "a-standard.png").exists(),
        "screenshot_off": (CAPTURED / "a-off.png").exists(),
    }
    summary["ok"] = (
        summary["a_ne_b"]
        and summary["reloads_match_a"]
        and summary["iframe_matches_a"]
        and summary["a_width_canvas_agree"]
        and summary["b_width_canvas_agree"]
        and summary["off_a_equals_off_b"]
        and summary["off_equals_installed_tested"]
        and summary["whitelist_always"]
        and summary["absent_never_detected"]
        and summary["web_font_renders"]
        and (not rejected or bool(local_rejected_fails))
        and (summary["worker_agrees_a"] or not summary["worker_ok"])
        and summary["screenshot_a"]
        and summary["screenshot_off"]
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
    installed = installed_families()
    extra = ["--expose-internals-object", "--force-cpu-painting", "--disable-sandbox"]

    same_process = CAPTURED / "same-process.json"
    same_process.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp5-same-")),
        "http://site-a.test:8080/start.html",
        extra,
        same_process,
        lambda data: "a-direct" in data.get("reads", {})
        and "b-direct" in data.get("reads", {})
        and data.get("complete")
        and len((data.get("reads", {}).get("a-direct") or {}).get("reloads") or []) >= 3,
        timeout=180,
    )

    site_a = CAPTURED / "site-a.json"
    site_a.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp5-a-")),
        "http://site-a.test:8080/page.html?run=site-a",
        extra,
        site_a,
        lambda data: "samples" in data and len(data["samples"]) >= 3,
        timeout=120,
    )

    off_both = CAPTURED / "off-both.json"
    off_both.unlink(missing_ok=True)
    poff = Path(tempfile.mkdtemp(prefix="fp5-off-"))
    write_off_profile(poff, ["http://site-a.test", "http://site-b.test"])
    run_xenon_until(
        args.xenon,
        poff,
        "http://site-a.test:8080/start.html?run=off-both",
        extra,
        off_both,
        lambda data: "a-direct" in data.get("reads", {}) and "b-direct" in data.get("reads", {}) and data.get("complete"),
        timeout=180,
    )

    extras = CAPTURED / "extras.json"
    extras.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp5-extras-")),
        "http://site-a.test:8080/extras.html",
        extra,
        extras,
        lambda data: "samples" in data and data["samples"],
        timeout=40,
    )

    screenshot(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp5-shot-a-")),
        "http://site-a.test:8080/shot.html",
        CAPTURED / "a-standard.png",
        extra,
    )
    poff_shot = Path(tempfile.mkdtemp(prefix="fp5-shot-off-"))
    write_off_profile(poff_shot, ["http://site-a.test"])
    screenshot(
        args.xenon,
        poff_shot,
        "http://site-a.test:8080/shot.html",
        CAPTURED / "a-off.png",
        extra,
    )

    summary = analyze(installed)
    (CAPTURED / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
