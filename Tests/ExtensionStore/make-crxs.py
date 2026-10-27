#!/usr/bin/env python3

# Writes the CRX files that Meta/xenon.py test serves from a local stand-in for the Chrome Web Store to check Xenon's
# password manager installs (UI/PasswordManagers.h), and that TestXenonCRX checks verify_crx() and unpack_zip() with.
# Each run makes new test keys: two developers' and a store's, whose IDs and hash go in keys.json.
#
#   bitwarden-1.0.crx       a stand-in for Bitwarden, signed by its developer and the store
#   bitwarden-2.0.crx       its next version
#   bitwarden-3.0-tampered.crx
#                           a newer version still, whose archive was changed after it was signed
#   bitwarden-3.0-unpublished.crx
#                           a newer version signed by its developer only
#   proton-pass-1.0.crx     a stand-in for Proton Pass, from the other developer
#   escape.crx              correctly signed for the first developer, with an archive entry named ../escape.txt
#
# The stand-ins' content script marks every page with the extension's name and version. Needs the openssl command:
# Tests/ExtensionStore/make-crxs.py [--openssl PATH]

from __future__ import annotations

import argparse
import hashlib
import io
import json
import subprocess
import tempfile
import zipfile

from pathlib import Path

DIRECTORY = Path(__file__).resolve().parent

CONTENT_SCRIPT = """document.documentElement.dataset.xenonPasswordManager =
    `${chrome.runtime.getManifest().name} ${chrome.runtime.getManifest().version}`;
"""


def main():
    parser = argparse.ArgumentParser(description="Write the test CRX files for Xenon's password manager installs")
    parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as scratch:

        def make_key(name: str) -> tuple[Path, bytes]:
            key = Path(scratch) / f"{name}.pem"
            subprocess.check_call([args.openssl, "genrsa", "-out", key, "2048"], stderr=subprocess.DEVNULL)
            public_key = subprocess.check_output([args.openssl, "rsa", "-in", key, "-pubout", "-outform", "DER"], stderr=subprocess.DEVNULL)
            return key, public_key

        def sign(key: Path, message: bytes) -> bytes:
            path = Path(scratch) / "message"
            path.write_bytes(message)
            return subprocess.check_output([args.openssl, "dgst", "-sha256", "-sign", key, path])

        bitwarden = make_key("bitwarden")
        proton_pass = make_key("proton-pass")
        store = make_key("store")

        def write_crx(name: str, developer, archive: bytes, signers: list, tamper: bool = False):
            crx_id = hashlib.sha256(developer[1]).digest()[:16]
            signed_header_data = field(1, crx_id)
            message = b"CRX3 SignedData\x00" + len(signed_header_data).to_bytes(4, "little") + signed_header_data + archive
            header = b"".join(field(2, field(1, public_key) + field(2, sign(key, message))) for key, public_key in signers)
            header += field(10000, signed_header_data)
            if tamper:
                # The first file's modification time, which leaves the archive itself intact.
                archive = archive[:10] + bytes([archive[10] ^ 1]) + archive[11:]
            (DIRECTORY / name).write_bytes(b"Cr24" + (3).to_bytes(4, "little") + len(header).to_bytes(4, "little") + header + archive)

        write_crx("bitwarden-1.0.crx", bitwarden, extension_zip("Test Bitwarden", "1.0"), [bitwarden, store])
        write_crx("bitwarden-2.0.crx", bitwarden, extension_zip("Test Bitwarden", "2.0"), [bitwarden, store])
        write_crx("bitwarden-3.0-tampered.crx", bitwarden, extension_zip("Test Bitwarden", "3.0"), [bitwarden, store], tamper=True)
        write_crx("bitwarden-3.0-unpublished.crx", bitwarden, extension_zip("Test Bitwarden", "3.0"), [bitwarden])
        write_crx("proton-pass-1.0.crx", proton_pass, extension_zip("Test Proton Pass", "1.0"), [proton_pass, store])
        write_crx("escape.crx", bitwarden, extension_zip("Test Bitwarden", "1.0", {"../escape.txt": "escaped"}), [bitwarden, store])

        keys = {
            "bitwarden": extension_id(bitwarden[1]),
            "proton-pass": extension_id(proton_pass[1]),
            "publisherKeyHash": hashlib.sha256(store[1]).hexdigest(),
        }
        (DIRECTORY / "keys.json").write_text(json.dumps(keys, indent=2) + "\n")


def extension_zip(name: str, version: str, extra_files: dict | None = None) -> bytes:
    manifest = {
        "manifest_version": 3,
        "name": name,
        "version": version,
        "content_scripts": [{"matches": ["http://127.0.0.1/*"], "js": ["content.js"], "run_at": "document_end"}],
    }
    files = {"manifest.json": json.dumps(manifest, indent=2) + "\n", "content.js": CONTENT_SCRIPT, **(extra_files or {})}
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zip:
        for path, text in files.items():
            # A fixed date keeps the files the same from one run to the next, but for their keys.
            zip.writestr(zipfile.ZipInfo(path, date_time=(2026, 1, 1, 0, 0, 0)), text, zipfile.ZIP_DEFLATED)
    return archive.getvalue()


def extension_id(public_key: bytes) -> str:
    return "".join(chr(ord("a") + int(digit, 16)) for digit in hashlib.sha256(public_key).hexdigest()[:32])


def varint(value: int) -> bytes:
    encoded = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            encoded.append(byte | 0x80)
        else:
            encoded.append(byte)
            return bytes(encoded)


# A length-delimited protocol buffer field.
def field(number: int, value: bytes) -> bytes:
    return varint(number << 3 | 2) + varint(len(value)) + value


if __name__ == "__main__":
    main()
