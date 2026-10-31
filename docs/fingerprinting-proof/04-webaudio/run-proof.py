#!/usr/bin/env python3
"""Run the feature-4 WebAudio farbling proofs against a compiled Xenon binary."""

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


def ratio_stats(farbled_probes, off_probes):
    if not farbled_probes or not off_probes:
        return None
    ratios = []
    max_rel = 0.0
    for a, b in zip(farbled_probes, off_probes):
        fv = a.get("v")
        ov = b.get("v")
        if fv is None or ov is None or ov == 0:
            continue
        rel = abs(fv - ov) / abs(ov)
        max_rel = max(max_rel, rel)
        ratios.append(fv / ov)
    if not ratios:
        return {"max_rel_diff": None, "under_0_1_percent": False, "constant": False, "error": "no-nonzero-probes"}
    return {
        "max_rel_diff": max_rel,
        "min_ratio": min(ratios),
        "max_ratio": max(ratios),
        "ratio_span": max(ratios) - min(ratios),
        "under_0_1_percent": max_rel <= 0.0010001,
        "constant": (max(ratios) - min(ratios)) < 1e-5,
        "n": len(ratios),
    }


def worker_audio(worker):
    if not worker:
        return None
    return worker.get("audio") or worker


def analyze() -> dict:
    same = json.loads((CAPTURED / "same-process.json").read_text())
    site_a = json.loads((CAPTURED / "site-a.json").read_text())
    off_both = json.loads((CAPTURED / "off-both.json").read_text())
    a = same["reads"]["a-direct"]
    b = same["reads"]["b-direct"]
    a_m = a["measure"]
    b_m = b["measure"]
    off_a = off_both["reads"]["a-direct"]["measure"]
    off_b = off_both["reads"]["b-direct"]["measure"]
    reloads = [item.get("hash") for item in a.get("reloads") or []]
    iframe = a.get("crossIframe") or {}
    samples = site_a.get("samples") or []
    a_worker = worker_audio(a.get("worker"))
    b_worker = worker_audio(b.get("worker"))
    write = a_m.get("writeNoDrift") or {}
    ratios = ratio_stats(a_m.get("probes") or [], off_a.get("probes") or [])
    strict = {}
    strict_path = CAPTURED / "strict.json"
    if strict_path.exists():
        strict = (json.loads(strict_path.read_text()).get("samples") or [{}])[0]

    worker_unsupported = bool((a.get("worker") or {}).get("unsupported")) or not (a_worker or {}).get("ok")
    worker_a_matches = bool(a_worker and a_worker.get("ok") and a_worker.get("getChannelDataHash") == a_m.get("getChannelDataHash"))
    worker_b_matches = bool(b_worker and b_worker.get("ok") and b_worker.get("getChannelDataHash") == b_m.get("getChannelDataHash"))

    analyser_a = a_m.get("analyser") or {}
    analyser_b = b_m.get("analyser") or {}
    analyser_off = off_a.get("analyser") or {}

    summary = {
        "offline_ok": a_m.get("ok") is True and b_m.get("ok") is True,
        "crypto_subtle": a_m.get("cryptoSubtle"),
        "a_hash": a_m.get("getChannelDataHash"),
        "b_hash": b_m.get("getChannelDataHash"),
        "a_sum": a_m.get("sum"),
        "b_sum": b_m.get("sum"),
        "a_ne_b": a_m.get("getChannelDataHash") != b_m.get("getChannelDataHash"),
        "same_process_reloads": reloads,
        "reloads_match_a": len(reloads) == 3 and len(set(reloads)) == 1 and reloads[0] == a_m.get("getChannelDataHash"),
        "iframe_hash": iframe.get("getChannelDataHash"),
        "iframe_matches_a": iframe.get("getChannelDataHash") == a_m.get("getChannelDataHash"),
        "twice_identical": a_m.get("twiceIdentical") is True and a_m.get("sameView") is True,
        "equals_copy_from_channel": a_m.get("equalsCopyFromChannel") is True,
        "off_a_hash": off_a.get("getChannelDataHash"),
        "off_b_hash": off_b.get("getChannelDataHash"),
        "off_a_equals_off_b": off_a.get("getChannelDataHash") == off_b.get("getChannelDataHash") and off_a.get("ok") is True,
        "off_differs_from_standard": off_a.get("getChannelDataHash") != a_m.get("getChannelDataHash"),
        "ratio": ratios,
        "write_no_drift": write.get("equal") is True,
        "write_matches_source": write.get("matchesWrite") is True,
        "worker_unsupported": worker_unsupported,
        "worker_a_hash": (a_worker or {}).get("getChannelDataHash"),
        "worker_b_hash": (b_worker or {}).get("getChannelDataHash"),
        "worker_a_equals_main_a": worker_a_matches,
        "worker_b_equals_main_b": worker_b_matches,
        "analyser_a": analyser_a,
        "analyser_b_float_time_hash": analyser_b.get("floatTimeHash"),
        "analyser_off_float_time_hash": analyser_off.get("floatTimeHash"),
        "analyser_ok": analyser_a.get("ok") is True,
        "byte_freq_changed": analyser_a.get("byteFreqChanged"),
        "byte_time_changed": analyser_a.get("byteTimeChanged"),
        "page_reload_hashes": [item.get("getChannelDataHash") for item in samples],
        "page_reloads_stable": len(samples) >= 3 and len({item.get("getChannelDataHash") for item in samples}) == 1,
        "strict_seed": strict.get("seed"),
        "strict_hash": strict.get("getChannelDataHash"),
        "strict_equals_standard_a": strict.get("getChannelDataHash") == a_m.get("getChannelDataHash"),
        "a_seed": a.get("seed") or a_m.get("seed"),
        "b_seed": b.get("seed") or b_m.get("seed"),
        "off_a_seed": off_both["reads"]["a-direct"].get("seed") or off_a.get("seed"),
        "off_b_seed": off_both["reads"]["b-direct"].get("seed") or off_b.get("seed"),
    }
    summary["ok"] = (
        summary["offline_ok"]
        and summary["a_ne_b"]
        and summary["reloads_match_a"]
        and summary["iframe_matches_a"]
        and summary["twice_identical"]
        and summary["equals_copy_from_channel"]
        and summary["off_a_equals_off_b"]
        and summary["off_differs_from_standard"]
        and bool(ratios)
        and ratios.get("under_0_1_percent") is True
        and ratios.get("constant") is True
        and summary["write_no_drift"]
        and summary["write_matches_source"]
        and (summary["worker_unsupported"] or (summary["worker_a_equals_main_a"] and summary["worker_b_equals_main_b"]))
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
        Path(tempfile.mkdtemp(prefix="fp4-same-")),
        "http://site-a.test:8080/start.html",
        extra,
        same_process,
        lambda data: "a-direct" in data.get("reads", {})
        and "b-direct" in data.get("reads", {})
        and data.get("complete")
        and len((data.get("reads", {}).get("a-direct") or {}).get("reloads") or []) >= 3,
        timeout=180,
    )

    first = json.loads(same_process.read_text())["reads"]["a-direct"]["measure"]
    if not first.get("ok"):
        print(json.dumps({"ok": False, "error": "OfflineAudioContext did not work", "measure": first}, indent=2))
        return 1

    site_a = CAPTURED / "site-a.json"
    site_a.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp4-a-")),
        "http://site-a.test:8080/page.html?run=site-a",
        extra,
        site_a,
        lambda data: "samples" in data and len(data["samples"]) >= 3,
        timeout=180,
    )

    off_both = CAPTURED / "off-both.json"
    off_both.unlink(missing_ok=True)
    poff = Path(tempfile.mkdtemp(prefix="fp4-off-"))
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

    strict = CAPTURED / "strict.json"
    strict.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp4-strict-")),
        "http://site-a.test:8080/strict.html",
        extra,
        strict,
        lambda data: "samples" in data and data["samples"] and data["samples"][0].get("ok"),
        timeout=60,
    )

    summary = analyze()
    (CAPTURED / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
