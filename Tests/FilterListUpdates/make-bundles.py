#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import shutil
import subprocess
import tempfile

from pathlib import Path

DIRECTORY = Path(__file__).resolve().parent

LIST = "! Xenon's filter list update test\n/xenon-update-test.js\n"
TRUSTED_LIST = "! Xenon's filter list update test, as a trusted list\nexample.invalid##.xenon-update-test\n"
RESOURCES = "[]\n"


def main():
    parser = argparse.ArgumentParser(description="Write the test bundles for Xenon's filter list updates")
    parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as scratch:
        key = Path(scratch) / "key.pem"
        subprocess.check_call([args.openssl, "genpkey", "-algorithm", "ed25519", "-out", key])
        public_key = subprocess.check_output([args.openssl, "pkey", "-in", key, "-pubout", "-outform", "DER"])[-32:]
        (DIRECTORY / "public-key.b64").write_text(base64.b64encode(public_key).decode() + "\n")

        def sign(path: Path):
            signature = subprocess.check_output(
                [args.openssl, "pkeyutl", "-sign", "-rawin", "-inkey", key, "-in", path.parent / "manifest.json"]
            )
            path.write_text(base64.b64encode(signature).decode() + "\n")

        write_bundle("v1", "2099-01-01 00:00 UTC", LIST, sign)
        write_bundle("v2", "2099-01-02 00:00 UTC", LIST + "/xenon-update-test-2.gif\n", sign)
        write_bundle("v3", "2099-01-03 00:00 UTC", LIST + "/xenon-update-test-3.gif\n", sign)
        (DIRECTORY / "v3" / "lists" / "xenon-test.txt").write_text(LIST + "/xenon-update-test-4.gif\n")


def write_bundle(name: str, generated_at: str, list_text: str, sign):
    bundle = DIRECTORY / name
    shutil.rmtree(bundle, ignore_errors=True)
    files = {
        "lists/xenon-test.txt": list_text,
        "trusted-lists/xenon-test-trusted.txt": TRUSTED_LIST,
        "resources/ublock-resources.json": RESOURCES,
    }
    for path, text in files.items():
        (bundle / path).parent.mkdir(parents=True, exist_ok=True)
        (bundle / path).write_text(text)

    def describe(path: str) -> dict:
        contents = files[path].encode()
        return {"file": path, "bytes": len(contents), "sha256": hashlib.sha256(contents).hexdigest()}

    manifest = {
        "format": 1,
        "generated_at": generated_at,
        "resources": describe("resources/ublock-resources.json"),
        "lists": [
            {"id": "xenon-test", **describe("lists/xenon-test.txt"), "trusted": False},
            {"id": "xenon-test-trusted", **describe("trusted-lists/xenon-test-trusted.txt"), "trusted": True},
        ],
    }
    (bundle / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    sign(bundle / "manifest.json.sig")


if __name__ == "__main__":
    main()
