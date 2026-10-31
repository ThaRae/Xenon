#!/usr/bin/env python3

from __future__ import annotations

import argparse
import subprocess
import tempfile

from pathlib import Path

DIRECTORY = Path(__file__).resolve().parent

VALID_FROM = "20260101000000Z"
VALID_UNTIL = "21251231235959Z"


def main():
    parser = argparse.ArgumentParser(description="Write the certificates that test Xenon's certificate warning")
    parser.add_argument("--openssl", default="openssl", help="the OpenSSL command to use")
    args = parser.parse_args()

    def openssl(*arguments: str | Path):
        subprocess.run([args.openssl, *map(str, arguments)], check=True, capture_output=True)

    def key(path: Path):
        openssl("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", path)

    def der(pem: Path, name: str):
        openssl("x509", "-in", pem, "-outform", "DER", "-out", DIRECTORY / name)

    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)

        server_key = DIRECTORY / "self-signed-key.pem"
        key(server_key)
        openssl(
            "req", "-x509", "-key", server_key, "-out", DIRECTORY / "self-signed.pem",
            "-subj", "/CN=127.0.0.1",
            "-addext", "subjectAltName=IP:127.0.0.1,IP:::1,DNS:localhost",
            "-not_before", VALID_FROM, "-not_after", VALID_UNTIL,
        )  # fmt: skip
        der(DIRECTORY / "self-signed.pem", "self-signed.der")

        server_only = scratch / "self-signed-server.pem"
        openssl(
            "req", "-x509", "-key", server_key, "-out", server_only,
            "-subj", "/CN=127.0.0.1",
            "-addext", "subjectAltName=IP:127.0.0.1",
            "-addext", "keyUsage=critical,digitalSignature",
            "-addext", "extendedKeyUsage=serverAuth",
            "-not_before", VALID_FROM, "-not_after", VALID_UNTIL,
        )  # fmt: skip
        der(server_only, "self-signed-server.der")

        authority_key = scratch / "authority-key.pem"
        authority = scratch / "authority.pem"
        key(authority_key)
        openssl(
            "req", "-x509", "-key", authority_key, "-out", authority,
            "-subj", "/O=Xenon tests/CN=Xenon Test Authority",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign",
            "-not_before", VALID_FROM, "-not_after", VALID_UNTIL,
        )  # fmt: skip

        issued = {
            "untrusted.der": ("127.0.0.1", "IP:127.0.0.1", VALID_FROM, VALID_UNTIL),
            "expired.der": ("127.0.0.1", "IP:127.0.0.1", "20200101000000Z", "20210101000000Z"),
            "not-yet-valid.der": ("127.0.0.1", "IP:127.0.0.1", "20990101000000Z", "21000101000000Z"),
            "other-host.der": ("example.com", "DNS:example.com", VALID_FROM, VALID_UNTIL),
        }
        for name, (common_name, alternative_names, valid_from, valid_until) in issued.items():
            leaf_key = scratch / "leaf-key.pem"
            request = scratch / "leaf.csr"
            leaf = scratch / "leaf.pem"
            key(leaf_key)
            openssl("req", "-new", "-key", leaf_key, "-out", request, "-subj", f"/CN={common_name}")
            extensions = scratch / "extensions.cnf"
            extensions.write_text(f"subjectAltName={alternative_names}\nbasicConstraints=CA:FALSE\n")
            openssl(
                "x509", "-req", "-in", request, "-out", leaf,
                "-CA", authority, "-CAkey", authority_key, "-extfile", extensions,
                "-not_before", valid_from, "-not_after", valid_until,
            )  # fmt: skip
            der(leaf, name)


if __name__ == "__main__":
    main()
