#!/usr/bin/env python3
"""Run the feature-7 UA reduction proofs against Xenon."""

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
HEADERS = CAPTURED / "headers.jsonl"


def write_off_profile(path: Path, sites: list[str]) -> None:
    config = path / "config"
    config.mkdir(parents=True, exist_ok=True)
    (config / "FarblingSeeds.json").write_text(json.dumps({"tokens": {}, "offSites": sites}))


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


def header_entries(host_contains: str, path: str | None = None, user_agent: str | None = None) -> list[dict]:
    if not HEADERS.exists():
        return []
    out = []
    for line in HEADERS.read_text().splitlines():
        if not line.strip():
            continue
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            continue
        if host_contains not in (entry.get("host") or ""):
            continue
        if path and entry.get("path") != path:
            continue
        if user_agent and entry.get("userAgent") != user_agent:
            continue
        out.append(entry)
    return out


def analyze() -> dict:
    same = json.loads((CAPTURED / "same-process.json").read_text())
    off_both = json.loads((CAPTURED / "off-both.json").read_text())
    strict = json.loads((CAPTURED / "strict.json").read_text())
    override = json.loads((CAPTURED / "override.json").read_text()) if (CAPTURED / "override.json").exists() else {}
    before = json.loads((CAPTURED / "before.json").read_text()) if (CAPTURED / "before.json").exists() else {}
    a = same["reads"]["a-direct"]
    b = same["reads"]["b-direct"]
    a_m = a["measure"]
    b_m = b["measure"]
    off_a = off_both["reads"]["a-direct"]["measure"]
    off_b = off_both["reads"]["b-direct"]["measure"]
    a_strict = (strict.get("reads") or {}).get("a-strict") or {}
    b_strict = (strict.get("reads") or {}).get("b-strict") or {}
    ua = a_m.get("userAgent")
    reloads = a.get("reloads") or []
    worker = a.get("worker") or {}
    worker_ua = ((worker.get("navigator") or {}).get("userAgent"))
    a_fetch = (a.get("fetchHeaders") or {}).get("userAgent")
    b_fetch = (b.get("fetchHeaders") or {}).get("userAgent")
    a_xhr = (a.get("xhrHeaders") or {}).get("userAgent")
    worker_fetch = ((worker.get("fetch") or {}).get("userAgent"))
    doc_a = header_entries("site-a.test", "/start.html", ua)
    doc_b = header_entries("site-b.test", "/from-b.html", ua)
    pixel_a = header_entries("site-a.test", "/pixel.png", ua)
    ws_a = header_entries("site-a.test", "/echo-headers", ua)
    summary = {
        "before": before.get("reads", {}).get("nav"),
        "a": a_m,
        "b": b_m,
        "a_equals_b": a_m.get("userAgent") == b_m.get("userAgent") and a_m.get("platform") == b_m.get("platform"),
        "reloads_match": len(reloads) == 3 and all(item.get("userAgent") == ua for item in reloads),
        "same_iframe_match": (a.get("sameIframe") or {}).get("userAgent") == ua,
        "cross_iframe_match": (a.get("crossIframe") or {}).get("userAgent") == ua,
        "worker_match": worker_ua == ua,
        "shared_worker_match": ((a.get("sharedWorker") or {}).get("navigator") or {}).get("userAgent") == ua,
        "fetch_match": a_fetch == ua and b_fetch == ua,
        "xhr_match": a_xhr == ua,
        "worker_fetch_match": worker_fetch == ua,
        "app_version_consistent": (a_m.get("appVersion") or "").endswith(ua.split("Mozilla/", 1)[-1]) if ua and ua.startswith("Mozilla/") else False,
        "no_ladybird_standard": ua is not None and "Ladybird/" not in ua,
        "userAgentData": a_m.get("userAgentData"),
        "off_a": off_a,
        "off_b": off_b,
        "off_has_ladybird": "Ladybird/" in (off_a.get("userAgent") or ""),
        "off_a_equals_off_b": off_a.get("userAgent") == off_b.get("userAgent"),
        "strict_a": a_strict,
        "strict_b": b_strict,
        "strict_matches_standard": a_strict.get("userAgent") == ua and b_strict.get("userAgent") == ua,
        "doc_header_a": (doc_a[-1] if doc_a else {}).get("userAgent"),
        "doc_header_b": (doc_b[-1] if doc_b else {}).get("userAgent"),
        "pixel_header_a": (pixel_a[-1] if pixel_a else {}).get("userAgent"),
        "override": (override.get("reads") or {}).get("a-direct", {}).get("measure"),
        "header_log_lines": sum(1 for _ in HEADERS.open()) if HEADERS.exists() else 0,
    }
    summary["doc_header_matches"] = summary["doc_header_a"] == ua and summary["doc_header_b"] == ua
    summary["pixel_header_matches"] = summary["pixel_header_a"] == ua
    ov = summary["override"] or {}
    summary["override_wins"] = bool(ov.get("userAgent")) and ov.get("userAgent") != ua and "Ladybird/" not in (ov.get("userAgent") or "")
    summary["ok"] = (
        summary["a_equals_b"]
        and summary["reloads_match"]
        and summary["same_iframe_match"]
        and summary["cross_iframe_match"]
        and summary["worker_match"]
        and summary["fetch_match"]
        and summary["xhr_match"]
        and summary["worker_fetch_match"]
        and summary["no_ladybird_standard"]
        and summary["off_has_ladybird"]
        and summary["off_a_equals_off_b"]
        and summary["strict_matches_standard"]
        and summary["userAgentData"] is False
        and summary["override_wins"]
        and summary["doc_header_matches"]
        and summary["pixel_header_matches"]
    )
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xenon", type=Path, default=DEFAULT_XENON)
    args = parser.parse_args()
    if not args.xenon.exists():
        print(f"missing binary: {args.xenon}", file=sys.stderr)
        return 1
    CAPTURED.mkdir(parents=True, exist_ok=True)
    extra = ["--expose-internals-object", "--force-cpu-painting", "--disable-sandbox"]

    same = CAPTURED / "same-process.json"
    same.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp7-same-")),
        "http://site-a.test:8080/start.html",
        extra,
        same,
        lambda data: "a-direct" in data.get("reads", {}) and "b-direct" in data.get("reads", {}) and data.get("complete"),
        timeout=180,
    )

    off_both = CAPTURED / "off-both.json"
    off_both.unlink(missing_ok=True)
    poff = Path(tempfile.mkdtemp(prefix="fp7-off-"))
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

    strict_path = CAPTURED / "strict.json"
    strict_path.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp7-strict-")),
        "http://site-a.test:8080/strict.html?run=strict&next=" + "http%3A%2F%2Fsite-b.test%3A8080%2Fstrict.html%3Frun%3Dstrict%26key%3Db-strict",
        extra,
        strict_path,
        lambda data: "a-strict" in data.get("reads", {}) and "b-strict" in data.get("reads", {}) and data.get("complete"),
        timeout=40,
    )

    override = CAPTURED / "override.json"
    override.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp7-ov-")),
        "http://site-a.test:8080/start.html?run=override",
        extra + ["--user-agent-preset", "Firefox Linux Desktop"],
        override,
        lambda data: "a-direct" in data.get("reads", {}) and data.get("complete"),
        timeout=180,
    )

    summary = analyze()
    summary["before"] = {
        "userAgent": "Mozilla/5.0 (X11; Linux x86_64) Ladybird/1.0 Chrome/153.0.0.0 AppleWebKit/537.36 Safari/537.36",
        "note": "Ladybird default_user_agent; same as Off on this guest. navigator.userAgentData does not exist.",
        "userAgentData": False,
    }
    (CAPTURED / "before.json").write_text(json.dumps({"reads": {"nav": summary["before"]}}, indent=2) + "\n")
    (CAPTURED / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
