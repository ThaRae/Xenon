#!/usr/bin/env python3
#
# Copyright (c) 2026, the Xenon developers.
#
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import hashlib
import re
import shutil
import subprocess
import sys

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDOR = ROOT / "Build" / "rune"
PATCHES = ROOT / "Patches" / "rune"
MANIFEST = ROOT / "Rune" / "ui" / "Cargo.toml"
MARKER = VENDOR / ".xenon-patched"
UPSTREAM = "https://github.com/anomalyco/rune"


def run(arguments, cwd=None):
    subprocess.run(arguments, cwd=cwd, check=True)


def pinned_revision():
    text = MANIFEST.read_text()
    match = re.search(r'rune = \{[^}]*rev = "([0-9a-f]{40})"', text)
    if not match:
        sys.exit(f"No pinned Rune revision in {MANIFEST}")
    return match.group(1)


def patch_files():
    return sorted(PATCHES.glob("*.patch"))


def fingerprint(revision):
    digest = hashlib.sha256(revision.encode())
    for path in patch_files():
        digest.update(path.name.encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()


def cargo_checkout(revision):
    checkouts = Path.home() / ".cargo" / "git" / "checkouts"
    for candidate in checkouts.glob(f"rune-*/{revision[:7]}"):
        if (candidate / "crates" / "rune" / "Cargo.toml").exists():
            return candidate
    return None


def clone(revision):
    VENDOR.mkdir(parents=True, exist_ok=True)
    run(["git", "init", "-q", str(VENDOR)])
    run(["git", "-C", str(VENDOR), "remote", "add", "origin", UPSTREAM])
    run(["git", "-C", str(VENDOR), "fetch", "--depth", "1", "-q", "origin", revision])
    run(["git", "-C", str(VENDOR), "checkout", "-q", "FETCH_HEAD"])


def populate(revision):
    checkout = cargo_checkout(revision)
    if checkout is not None:
        if VENDOR.exists():
            shutil.rmtree(VENDOR)
        shutil.copytree(checkout, VENDOR)
        return
    if (VENDOR / "crates" / "rune" / "Cargo.toml").exists():
        return
    if VENDOR.exists():
        shutil.rmtree(VENDOR)
    clone(revision)


def prepare(force):
    revision = pinned_revision()
    expected = fingerprint(revision)
    if not force and MARKER.is_file() and MARKER.read_text().strip() == expected:
        print(f"Rune {revision[:12]} is ready in {VENDOR}")
        return
    populate(revision)
    for path in patch_files():
        run(["git", "apply", "-p1", "--whitespace=nowarn", str(path)], cwd=VENDOR)
    MARKER.write_text(expected + "\n")
    print(f"Applied {len(patch_files())} Rune patch(es) in {VENDOR}")


def main():
    parser = argparse.ArgumentParser(description="Prepare Rune's patched source tree")
    parser.add_argument("command", choices=["prepare"])
    parser.add_argument("--force", action="store_true")
    arguments = parser.parse_args()
    if arguments.command == "prepare":
        prepare(arguments.force)


if __name__ == "__main__":
    main()
