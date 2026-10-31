#!/usr/bin/env python3
"""Run the feature-2 canvas readback proofs against a compiled Xenon binary."""

from __future__ import annotations

import argparse
import base64
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
XENON_ROOT = HERE.parents[2]
DEFAULT_XENON = XENON_ROOT / "Build" / "release" / "xenon" / "bin" / "Xenon"
CAPTURED = HERE / "captured"


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(data: bytes) -> tuple[int, int, bytes]:
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a png")
    pos = 8
    width = height = bit_depth = color_type = None
    idat = []
    while pos + 12 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        ctype = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"IDAT":
            idat.append(chunk)
        elif ctype == b"IEND":
            break
    if width is None or bit_depth != 8:
        raise ValueError(f"unsupported png header {width} {bit_depth} {color_type}")
    raw = zlib.decompress(b"".join(idat))
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[color_type]
    stride = width * channels
    out = bytearray(height * stride)
    i = 0
    prev = bytes(stride)
    for y in range(height):
        filt = raw[i]
        i += 1
        row = bytearray(raw[i : i + stride])
        i += stride
        if filt == 1:
            for x in range(stride):
                left = row[x - channels] if x >= channels else 0
                row[x] = (row[x] + left) & 255
        elif filt == 2:
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 255
        elif filt == 3:
            for x in range(stride):
                left = row[x - channels] if x >= channels else 0
                row[x] = (row[x] + ((left + prev[x]) // 2)) & 255
        elif filt == 4:
            for x in range(stride):
                left = row[x - channels] if x >= channels else 0
                up = prev[x]
                ul = prev[x - channels] if x >= channels else 0
                row[x] = (row[x] + paeth(left, up, ul)) & 255
        elif filt != 0:
            raise ValueError(f"png filter {filt}")
        out[y * stride : (y + 1) * stride] = row
        prev = bytes(row)
    if color_type == 6:
        return width, height, bytes(out)
    if color_type == 2:
        rgba = bytearray(width * height * 4)
        for n in range(width * height):
            rgba[n * 4 : n * 4 + 3] = out[n * 3 : n * 3 + 3]
            rgba[n * 4 + 3] = 255
        return width, height, bytes(rgba)
    raise ValueError(f"png color type {color_type}")


def pixel_diff(a: bytes, b: bytes) -> dict:
    n = min(len(a), len(b))
    differing = 0
    max_delta = 0
    for i in range(n):
        delta = abs(a[i] - b[i])
        if delta:
            differing += 1
            if delta > max_delta:
                max_delta = delta
    differing += abs(len(a) - len(b))
    return {
        "differing_bytes": differing,
        "max_delta": max_delta,
        "length_a": len(a),
        "length_b": len(b),
    }


def run_xenon(executable: Path, profile: Path, url: str, extra: list[str], timeout: int = 90) -> None:
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    command = [str(executable), "--headless=manual", *extra, "--profile-path", str(profile), url]
    subprocess.run(command, env=env, capture_output=True, text=True, timeout=timeout, check=False)


def run_xenon_until(executable: Path, profile: Path, url: str, extra: list[str], path: Path, predicate, timeout: int = 70) -> None:
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


def wait_for(path: Path, predicate, timeout: float = 70) -> None:
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
    dest.parent.mkdir(exist_ok=True)
    command = [
        str(executable),
        "--headless=screenshot",
        "--screenshot-delay",
        "2",
        "--screenshot-path",
        str(dest),
        "--window-width",
        "320",
        "--window-height",
        "180",
        "--disable-scrollbar-painting",
        "--force-cpu-painting",
        *extra,
        "--profile-path",
        str(profile),
        url,
    ]
    subprocess.run(command, env=env, capture_output=True, text=True, timeout=40, check=False)


def fnv1a64(data: bytes) -> str:
    hash_ = 14695981039346656037
    for byte in data:
        hash_ ^= byte
        hash_ = (hash_ * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{hash_:016x}"


def write_off_profile(path: Path, sites: list[str] | None = None) -> None:
    config = path / "config"
    config.mkdir(parents=True, exist_ok=True)
    (config / "FarblingSeeds.json").write_text(
        json.dumps({"tokens": {}, "offSites": sites or ["http://site-a.test"]})
    )


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
    profile = Path(tempfile.mkdtemp(prefix="fp2-same-"))
    run_xenon_until(
        args.xenon,
        profile,
        "http://site-a.test:8080/start.html",
        extra,
        same_process,
        lambda data: "a-direct" in data.get("reads", {})
        and "b-direct" in data.get("reads", {})
        and data.get("complete")
        and len((data.get("reads", {}).get("a-direct") or {}).get("reloads") or []) >= 3,
        timeout=120,
    )

    site_a = CAPTURED / "site-a.json"
    site_a.unlink(missing_ok=True)
    run_xenon_until(
        args.xenon,
        Path(tempfile.mkdtemp(prefix="fp2-a-")),
        "http://site-a.test:8080/page.html?run=site-a&pixels=1",
        extra,
        site_a,
        lambda data: "samples" in data and len(data["samples"]) >= 3,
        timeout=90,
    )

    off_a = CAPTURED / "off-a.json"
    off_a.unlink(missing_ok=True)
    poff = Path(tempfile.mkdtemp(prefix="fp2-off-"))
    write_off_profile(poff)
    run_xenon_until(
        args.xenon,
        poff,
        "http://site-a.test:8080/page.html?run=off-a&pixels=1",
        extra,
        off_a,
        lambda data: "samples" in data and len(data["samples"]) >= 3,
        timeout=90,
    )

    off_both = CAPTURED / "off-both.json"
    off_both.unlink(missing_ok=True)
    poff_both = Path(tempfile.mkdtemp(prefix="fp2-off-both-"))
    write_off_profile(poff_both, ["http://site-a.test", "http://site-b.test"])
    run_xenon_until(
        args.xenon,
        poff_both,
        "http://site-a.test:8080/start.html?run=off-both",
        extra,
        off_both,
        lambda data: "a-direct" in data.get("reads", {}) and "b-direct" in data.get("reads", {}) and data.get("complete"),
        timeout=120,
    )

    screenshot(args.xenon, Path(tempfile.mkdtemp(prefix="fp2-shot-a-")), "http://site-a.test:8080/shot.html", CAPTURED / "a-standard.png", extra)
    poff_shot = Path(tempfile.mkdtemp(prefix="fp2-shot-off-"))
    write_off_profile(poff_shot)
    screenshot(args.xenon, poff_shot, "http://site-a.test:8080/shot.html", CAPTURED / "a-off.png", extra)

    summary = analyze()
    (CAPTURED / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary.get("ok") else 1


def first_measure(path: Path) -> dict:
    data = json.loads(path.read_text())
    if "samples" in data:
        return data["samples"][0]["measure"]
    return {}


def analyze() -> dict:
    same = json.loads((CAPTURED / "same-process.json").read_text())
    site_a = json.loads((CAPTURED / "site-a.json").read_text())
    off_a = json.loads((CAPTURED / "off-a.json").read_text())
    off_both = json.loads((CAPTURED / "off-both.json").read_text())
    a_reads = same["reads"]["a-direct"]
    b_reads = same["reads"]["b-direct"]
    a_sha = a_reads["measure"]["getImageDataSha"]
    b_sha = b_reads["measure"]["getImageDataSha"]
    iframe_sha = (a_reads.get("crossIframe") or {}).get("getImageDataSha")
    same_reloads = [item["getImageDataSha"] for item in a_reads.get("reloads") or []]
    a_lower = (a_reads.get("lower") or {}).get("lowerSha")
    b_lower = (b_reads.get("lower") or {}).get("lowerSha")
    samples = site_a["samples"]
    reload_shas = [s["measure"]["getImageDataSha"] for s in samples]
    off_measure = off_a["samples"][0]["measure"]
    a_measure = samples[0]["measure"]
    off_a_sha = off_both["reads"]["a-direct"]["measure"]["getImageDataSha"]
    off_b_sha = off_both["reads"]["b-direct"]["measure"]["getImageDataSha"]
    off_a_seed = off_both["reads"]["a-direct"].get("seed")
    off_b_seed = off_both["reads"]["b-direct"].get("seed")

    png_vs_pixels = None
    if a_measure.get("pixelsB64") and a_measure.get("pngB64"):
        pixels = base64.b64decode(a_measure["pixelsB64"])
        png = base64.b64decode(a_measure["pngB64"])
        _w, _h, decoded = decode_png(png)
        png_vs_pixels = pixel_diff(pixels, decoded)

    off_vs_a = None
    if a_measure.get("pixelsB64") and off_measure.get("pixelsB64"):
        off_vs_a = pixel_diff(base64.b64decode(a_measure["pixelsB64"]), base64.b64decode(off_measure["pixelsB64"]))

    shot_diff = None
    std_shot = CAPTURED / "a-standard.png"
    off_shot = CAPTURED / "a-off.png"
    if std_shot.exists() and off_shot.exists() and std_shot.stat().st_size and off_shot.stat().st_size:
        shot_diff = pixel_diff(decode_png(std_shot.read_bytes())[2], decode_png(off_shot.read_bytes())[2])

    worker_sha = (samples[0].get("worker") or {}).get("sha")
    screenshot_sha = None
    off_shot = CAPTURED / "a-off.png"
    if off_shot.exists() and off_shot.stat().st_size:
        screenshot_sha = fnv1a64(decode_png(off_shot.read_bytes())[2])
    off_pixels_sha = None
    off_both_a_measure = off_both["reads"]["a-direct"]["measure"]
    if off_both_a_measure.get("pixelsB64"):
        off_pixels_sha = fnv1a64(base64.b64decode(off_both_a_measure["pixelsB64"]))
    ok = (
        a_sha != b_sha
        and len(same_reloads) == 3
        and len(set(same_reloads)) == 1
        and same_reloads[0] == a_sha
        and iframe_sha == a_sha
        and a_lower != b_lower
        and a_measure.get("pngBytesEqual") is True
        and a_measure.get("blankZero") is True
        and off_measure.get("getImageDataSha") != a_sha
        and off_a_sha == off_b_sha
        and off_a_seed == "off"
        and off_b_seed == "off"
        and screenshot_sha is not None
        and off_a_sha == screenshot_sha
        and (png_vs_pixels or {}).get("differing_bytes") == 0
        and (off_vs_a or {}).get("max_delta") == 1
        and (shot_diff or {}).get("differing_bytes") == 0
        and worker_sha == a_measure.get("convertToBlobMainSha")
    )
    return {
        "ok": ok,
        "a_sha": a_sha,
        "b_sha": b_sha,
        "a_ne_b": a_sha != b_sha,
        "iframe_sha": iframe_sha,
        "iframe_matches_a": iframe_sha == a_sha,
        "same_process_reloads": same_reloads,
        "same_process_reloads_match_a_sha": len(set(same_reloads)) == 1 and same_reloads and same_reloads[0] == a_sha,
        "a_lower_sha": a_lower,
        "b_lower_sha": b_lower,
        "lower_a_ne_b": a_lower != b_lower,
        "reload_shas": reload_shas,
        "reloads_identical": len(set(reload_shas)) == 1,
        "png_bytes_equal": a_measure.get("pngBytesEqual"),
        "blank_zero": a_measure.get("blankZero"),
        "createImageBitmapOk": a_measure.get("createImageBitmapOk"),
        "transferToImageBitmapOk": a_measure.get("transferToImageBitmapOk"),
        "worker_sha": worker_sha,
        "convertToBlobMainSha": a_measure.get("convertToBlobMainSha"),
        "toBlobSha": a_measure.get("toBlobSha"),
        "toDataURLSha": a_measure.get("toDataURLSha"),
        "png_vs_getImageData": png_vs_pixels,
        "off_sha": off_measure.get("getImageDataSha"),
        "off_vs_standard": off_vs_a,
        "off_a_sha": off_a_sha,
        "off_b_sha": off_b_sha,
        "off_a_equals_off_b": off_a_sha == off_b_sha,
        "off_a_seed": off_a_seed,
        "off_b_seed": off_b_seed,
        "screenshot_sha": screenshot_sha,
        "off_pixels_sha": off_pixels_sha,
        "off_equals_screenshot": off_a_sha == screenshot_sha,
        "screenshot_diff": shot_diff,
        "a_seed": a_reads.get("seed"),
        "b_seed": b_reads.get("seed"),
        "off_seed": off_a["samples"][0].get("seed"),
    }


if __name__ == "__main__":
    sys.exit(main())
