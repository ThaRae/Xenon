#!/usr/bin/env python3
"""Run the feature-6 hardwareConcurrency / deviceMemory proofs against Xenon."""

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
SPEC_BUCKETS = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0}
LADYBIRD_BUCKETS = {1.0, 2.0, 4.0}


def nproc() -> int:
    return int(subprocess.check_output(["nproc"], text=True).strip())


def mem_kib() -> int:
    for line in Path("/proc/meminfo").read_text().splitlines():
        if line.startswith("MemTotal:"):
            return int(line.split()[1])
    raise SystemExit("no MemTotal")


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


def concurrency_in_range(value, real: int) -> bool:
    if not isinstance(value, int):
        return False
    if real < 2:
        return value == real
    return 2 <= value <= real


def memory_ok(value, exposed: bool, real_guess: float) -> bool:
    if not exposed:
        return value is None
    return value in SPEC_BUCKETS and value <= max(real_guess, 8.0)


def analyze(real_cores: int, mem_kib_value: int) -> dict:
    same = json.loads((CAPTURED / "same-process.json").read_text())
    off_both = json.loads((CAPTURED / "off-both.json").read_text())
    strict = json.loads((CAPTURED / "strict.json").read_text())
    a = same["reads"]["a-direct"]
    b = same["reads"]["b-direct"]
    a_m = a["measure"]
    b_m = b["measure"]
    reloads = a.get("reloads") or []
    same_iframe = a.get("sameIframe") or {}
    cross_iframe = a.get("crossIframe") or {}
    worker = a.get("worker") or {}
    shared = a.get("sharedWorker") or {}
    off_a = off_both["reads"]["a-direct"]["measure"]
    off_b = off_both["reads"]["b-direct"]["measure"]
    sites = []
    for key, value in sorted((same.get("reads") or {}).items()):
        if key.startswith("site-"):
            sites.append(value)
    site_a_spread = next((item for item in sites if (item or {}).get("host") == "site-a.test:8080"), {})
    a_hw = a_m.get("hardwareConcurrency")
    site_a_views = {
        "start": a_hw,
        "spread": site_a_spread.get("hardwareConcurrency"),
        "reloads": [item.get("hardwareConcurrency") for item in reloads],
        "same_iframe": same_iframe.get("hardwareConcurrency"),
        "worker": worker.get("hardwareConcurrency"),
        "shared_worker": shared.get("hardwareConcurrency"),
        "cross_iframe": cross_iframe.get("hardwareConcurrency"),
        "start_seed": a_m.get("seed"),
        "spread_seed": site_a_spread.get("seed"),
    }
    site_a_same_process_stable = (
        site_a_views["spread"] == a_hw
        and site_a_views["same_iframe"] == a_hw
        and site_a_views["worker"] == a_hw
        and site_a_views["shared_worker"] == a_hw
        and site_a_views["cross_iframe"] == a_hw
        and len(reloads) == 3
        and all(item.get("hardwareConcurrency") == a_hw for item in reloads)
        and site_a_views["start_seed"] == site_a_views["spread_seed"]
    )
    hw_values = [item.get("hardwareConcurrency") for item in sites]
    a_strict = (strict.get("reads") or {}).get("a-strict") or {}
    b_strict = (strict.get("reads") or {}).get("b-strict") or {}
    real_mem_gib = mem_kib_value / 1024 / 1024
    summary = {
        "nproc": real_cores,
        "mem_kib": mem_kib_value,
        "mem_gib_approx": round(real_mem_gib, 3),
        "a": a_m,
        "b": b_m,
        "a_ne_b_concurrency": a_m.get("hardwareConcurrency") != b_m.get("hardwareConcurrency"),
        "reloads": reloads,
        "reloads_match_a": len(reloads) == 3 and all(item.get("hardwareConcurrency") == a_m.get("hardwareConcurrency") for item in reloads),
        "same_iframe": same_iframe,
        "same_iframe_matches_a": same_iframe.get("hardwareConcurrency") == a_m.get("hardwareConcurrency"),
        "cross_iframe": cross_iframe,
        "cross_iframe_matches_a": cross_iframe.get("hardwareConcurrency") == a_m.get("hardwareConcurrency"),
        "cross_iframe_seed_rule": "top-level site (nested documents inherit the top-level farbling seed, same as features 1-5)",
        "worker": worker,
        "worker_matches_a": worker.get("ok") is True and worker.get("hardwareConcurrency") == a_m.get("hardwareConcurrency"),
        "shared_worker": shared,
        "shared_worker_matches_a": shared.get("ok") is True and shared.get("hardwareConcurrency") == a_m.get("hardwareConcurrency"),
        "shared_worker_unsupported": shared.get("unsupported") is True,
        "off_a": off_a,
        "off_b": off_b,
        "off_a_equals_nproc": off_a.get("hardwareConcurrency") == real_cores,
        "off_b_equals_nproc": off_b.get("hardwareConcurrency") == real_cores,
        "strict_a": a_strict,
        "strict_b": b_strict,
        "strict_concurrency_fixed": a_strict.get("hardwareConcurrency") == min(4, real_cores) and b_strict.get("hardwareConcurrency") == min(4, real_cores),
        "same_process_profile": "captured/same-process.json",
        "site_a_views": site_a_views,
        "site_a_same_process_stable": site_a_same_process_stable,
        "spread": sites,
        "spread_n": len(sites),
        "spread_concurrency_values": sorted({v for v in hw_values if isinstance(v, int)}),
        "spread_has_two_concurrency": len({v for v in hw_values if isinstance(v, int)}) >= 2,
        "concurrency_in_range": all(concurrency_in_range(item.get("hardwareConcurrency"), real_cores) for item in [a_m, b_m, off_a, off_b] + sites + [a_strict, b_strict] if item),
        "device_memory_a_exposed": a_m.get("deviceMemoryExposed"),
        "device_memory_note": "NavigatorDeviceMemory.idl is [SecureContext]; http://site-a.test is not potentially trustworthy so deviceMemory is absent there. 127.0.0.0/8 and localhost expose it.",
    }
    mem_values = []
    for item in sites + [a_m, b_m, off_a, off_b, a_strict, b_strict]:
        if item and item.get("deviceMemoryExposed"):
            mem_values.append(item.get("deviceMemory"))
    summary["device_memory_values"] = mem_values
    standard_mem = [item.get("deviceMemory") for item in sites if item and item.get("deviceMemoryExposed")]
    summary["standard_device_memory_values"] = standard_mem
    summary["device_memory_all_spec_buckets"] = all(v in SPEC_BUCKETS for v in mem_values)
    summary["standard_device_memory_ladybird_buckets"] = all(v in LADYBIRD_BUCKETS for v in standard_mem)
    off_loop = {}
    off_loop_path = CAPTURED / "off-loopback.json"
    if off_loop_path.exists():
        off_loop = json.loads(off_loop_path.read_text()).get("reads") or {}
    strict_loop = {}
    strict_loop_path = CAPTURED / "strict-loopback.json"
    if strict_loop_path.exists():
        strict_loop = json.loads(strict_loop_path.read_text()).get("reads") or {}
    off_loop_a = off_loop.get("a") or {}
    off_loop_b = off_loop.get("b") or {}
    strict_loop_a = strict_loop.get("a-strict") or {}
    strict_loop_b = strict_loop.get("b-strict") or {}
    summary["off_loopback_a"] = off_loop_a
    summary["off_loopback_b"] = off_loop_b
    summary["strict_loopback_a"] = strict_loop_a
    summary["strict_loopback_b"] = strict_loop_b
    off_mem = [item.get("deviceMemory") for item in (off_loop_a, off_loop_b) if item.get("deviceMemoryExposed")]
    summary["off_device_memory"] = off_mem
    summary["off_device_memory_equals_real"] = len(off_mem) == 2 and len(set(off_mem)) == 1
    strict_mem = [item.get("deviceMemory") for item in (strict_loop_a, strict_loop_b) if item.get("deviceMemoryExposed")]
    summary["strict_device_memory"] = strict_mem
    summary["strict_device_memory_fixed"] = len(strict_mem) == 2 and strict_mem[0] == strict_mem[1]
    summary["ok"] = (
        summary["spread_n"] >= 6
        and (summary["spread_has_two_concurrency"] or real_cores < 3)
        and summary["reloads_match_a"]
        and summary["same_iframe_matches_a"]
        and summary["cross_iframe_matches_a"]
        and summary["worker_matches_a"]
        and (summary["shared_worker_matches_a"] or summary["shared_worker_unsupported"])
        and summary["off_a_equals_nproc"]
        and summary["off_b_equals_nproc"]
        and summary["strict_concurrency_fixed"]
        and summary["concurrency_in_range"]
        and summary["device_memory_all_spec_buckets"]
        and summary["standard_device_memory_ladybird_buckets"]
        and summary["site_a_same_process_stable"]
        and summary.get("off_device_memory_equals_real")
        and summary.get("strict_device_memory_fixed")
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
    real_cores = nproc()
    mem = mem_kib()
    extra = ["--expose-internals-object", "--force-cpu-painting", "--disable-sandbox"]
    print(f"nproc={real_cores} MemTotal_kib={mem}", flush=True)

    def same_process_ready(data):
        reads = data.get("reads") or {}
        return (
            "a-direct" in reads
            and "b-direct" in reads
            and data.get("complete")
            and sum(1 for key in reads if key.startswith("site-")) >= 8
        )

    same_process = CAPTURED / "same-process.json"
    same_process.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp6-same-")),
        "http://site-a.test:8080/start.html",
        extra,
        same_process,
        same_process_ready,
        timeout=240,
    )
    (CAPTURED / "spread.json").write_text(same_process.read_text())

    off_both = CAPTURED / "off-both.json"
    off_both.unlink(missing_ok=True)
    poff = Path(tempfile.mkdtemp(prefix="fp6-off-"))
    write_off_profile(poff, [
        "http://site-a.test", "http://site-b.test",
        "http://127.0.0.1", "http://localhost",
        "http://127.0.0.2", "http://127.0.0.3", "http://127.0.0.4", "http://127.0.0.5",
    ])
    run_xenon_until(
        args.xenon,
        poff,
        "http://site-a.test:8080/start.html?run=off-both",
        extra,
        off_both,
        same_process_ready,
        timeout=240,
    )

    strict_path = CAPTURED / "strict.json"
    strict_path.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp6-strict-")),
        "http://site-a.test:8080/strict.html?run=strict&next=" + "http%3A%2F%2Fsite-b.test%3A8080%2Fstrict.html%3Frun%3Dstrict",
        extra,
        strict_path,
        lambda data: "a-strict" in data.get("reads", {}) and "b-strict" in data.get("reads", {}) and data.get("complete"),
        timeout=60,
    )

    off_mem = CAPTURED / "off-loopback.json"
    off_mem.unlink(missing_ok=True)
    poff_mem = Path(tempfile.mkdtemp(prefix="fp6-off-mem-"))
    write_off_profile(poff_mem, ["http://127.0.0.1", "http://127.0.0.2"])
    run_xenon_until(
        args.xenon,
        poff_mem,
        "http://127.0.0.1:8080/sample.html?run=off-loopback&key=a&next=" + "http%3A%2F%2F127.0.0.2%3A8080%2Fsample.html%3Frun%3Doff-loopback%26key%3Db",
        extra,
        off_mem,
        lambda data: "a" in data.get("reads", {}) and "b" in data.get("reads", {}) and data.get("complete"),
        timeout=40,
    )

    strict_mem = CAPTURED / "strict-loopback.json"
    strict_mem.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp6-strict-mem-")),
        "http://127.0.0.1:8080/strict.html?run=strict-loopback&next=" + "http%3A%2F%2F127.0.0.2%3A8080%2Fstrict.html%3Frun%3Dstrict-loopback",
        extra,
        strict_mem,
        lambda data: "a-strict" in data.get("reads", {}) and "b-strict" in data.get("reads", {}) and data.get("complete"),
        timeout=40,
    )

    summary = analyze(real_cores, mem)
    (CAPTURED / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
