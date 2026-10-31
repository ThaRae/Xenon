#!/usr/bin/env python3

from __future__ import annotations

import argparse
import difflib
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

from pathlib import Path

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
PACKAGES_DIR = XENON_SOURCE_DIR / "Packaging" / "aur"

APPIMAGE_PACKAGE = "xenon-bin"
APPIMAGE_URL = "https://github.com/ThaRae/xenon-releases/releases/download/v{0}/Xenon-{0}-x86_64.AppImage"

AUR_REPOSITORY = "ssh://aur@aur.archlinux.org/{}.git"
AUR_HOST_KEY = "aur.archlinux.org ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIEuBKrPzbawxA/k2g6NcyV5jmqwJ2s+zpgZGZ7tpLIcN"

ARCH_IMAGE = "archlinux:latest"


def main():
    parser = argparse.ArgumentParser(description="Update Xenon's AUR packages, and push them to the AUR")
    subparsers = parser.add_subparsers(dest="command", required=True)

    update_parser = subparsers.add_parser("update", help="bump the packages to a release")
    update_parser.add_argument("--version", help="the release to bump them to, rather than VERSION.txt's")
    update_parser.add_argument("--appimage", type=Path, help="the release's AppImage, rather than the published one")

    check_parser = subparsers.add_parser("check", help="check that each .SRCINFO matches its PKGBUILD")
    check_parser.add_argument("packages", nargs="*", help="the packages to check, rather than all of them")

    push_parser = subparsers.add_parser("push", help="push the packages to the AUR")
    push_parser.add_argument("packages", nargs="*", help="the packages to push, rather than all of them")
    push_parser.add_argument("--ssh-key", type=Path, help="the AUR account's private key, rather than ssh's own setup")

    args = parser.parse_args()
    if args.command == "update":
        update_main(args)
    elif args.command == "check":
        check_main(args)
    elif args.command == "push":
        push_main(args)


def relative(path: Path) -> str:
    return str(path.relative_to(XENON_SOURCE_DIR))


def run(*command, **kwargs) -> str:
    result = subprocess.run([str(part) for part in command], capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        sys.exit(f"{' '.join(str(part) for part in command)} failed:\n{result.stdout}{result.stderr}")
    return result.stdout


def read_version() -> str:
    return (XENON_SOURCE_DIR / "VERSION.txt").read_text().strip()


def packages(names: list[str]) -> list[Path]:
    available = sorted(path.parent for path in PACKAGES_DIR.glob("*/PKGBUILD"))
    for name in names:
        if PACKAGES_DIR / name not in available:
            sys.exit(f"{relative(PACKAGES_DIR)} has no package {name}")
    return [PACKAGES_DIR / name for name in names] if names else available


def variable(pkgbuild: str, name: str) -> str:
    match = re.search(rf"^{name}=(\S+)$", pkgbuild, re.MULTILINE)
    if not match:
        sys.exit(f"A PKGBUILD doesn't set {name}")
    return match[1]


def set_variable(pkgbuild: str, name: str, value: str) -> str:
    variable(pkgbuild, name)
    return re.sub(rf"^{name}=\S+$", f"{name}={value}", pkgbuild, count=1, flags=re.MULTILINE)


def appimage_sha256(version: str, appimage: Path | None) -> str:
    digest = hashlib.sha256()
    if appimage:
        with appimage.open("rb") as file:
            while chunk := file.read(1 << 20):
                digest.update(chunk)
        return digest.hexdigest()

    url = APPIMAGE_URL.format(version)
    print(f"Downloading {url}", flush=True)
    try:
        with urllib.request.urlopen(url, timeout=120) as response:
            while chunk := response.read(1 << 20):
                digest.update(chunk)
    except OSError as error:
        sys.exit(f"Couldn't download {url} ({error}). Is Xenon {version} published?")
    return digest.hexdigest()


def makepkg(package: Path, *arguments: str) -> str:
    destinations = ("BUILDDIR", "PKGDEST", "SRCDEST", "SRCPKGDEST", "LOGDEST")
    if shutil.which("makepkg"):
        with tempfile.TemporaryDirectory() as directory:
            environment = dict(os.environ, **dict.fromkeys(destinations, directory))
            return run("makepkg", *arguments, cwd=package, env=environment)
    if not shutil.which("docker"):
        sys.exit("makepkg isn't installed, and neither is docker, to run it in Arch Linux's image")
    command = [
        "docker", "run", "--rm", "--platform", "linux/amd64", "--user", f"{os.getuid()}:{os.getgid()}",
        "--volume", f"{package}:/package:ro", "--workdir", "/package",
        *(argument for name in destinations for argument in ("--env", f"{name}=/tmp")),
        ARCH_IMAGE, "makepkg", *arguments,
    ]  # fmt: skip
    return run(*command, cwd=package)


def update_main(args):
    version = args.version or read_version()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        sys.exit(f"A release's version is major.minor.patch: {version}")

    for package in packages([]):
        path = package / "PKGBUILD"
        pkgbuild = path.read_text()
        if variable(pkgbuild, "pkgver") != version:
            pkgbuild = set_variable(set_variable(pkgbuild, "pkgver", version), "pkgrel", "1")
        if package.name == APPIMAGE_PACKAGE:
            checksum = f"sha256sums=('{appimage_sha256(version, args.appimage)}')"
            pkgbuild, count = re.subn(r"^sha256sums=\('[0-9a-f]{64}'\)$", checksum, pkgbuild, flags=re.MULTILINE)
            if count != 1:
                sys.exit(f"{relative(path)} has no line of sha256sums=('...') for the AppImage")
        path.write_text(pkgbuild)
        (package / ".SRCINFO").write_text(makepkg(package, "--printsrcinfo"))
        print(f"{package.name} {version}-{variable(pkgbuild, 'pkgrel')}")


def check_main(args):
    stale = []
    for package in packages(args.packages):
        srcinfo = package / ".SRCINFO"
        written = srcinfo.read_text() if srcinfo.is_file() else ""
        expected = makepkg(package, "--printsrcinfo")
        if written != expected:
            diff = difflib.unified_diff(
                written.splitlines(True), expected.splitlines(True), relative(srcinfo), "makepkg --printsrcinfo"
            )
            sys.stdout.writelines(diff)
            stale.append(package.name)
    if stale:
        sys.exit(f"The .SRCINFO of {', '.join(stale)} isn't its PKGBUILD's. Run Meta/aur.py update.")
    print("Every .SRCINFO matches its PKGBUILD.")


def push_main(args):
    environment = dict(os.environ)
    with tempfile.TemporaryDirectory() as directory:
        if args.ssh_key:
            known_hosts = Path(directory) / "known_hosts"
            known_hosts.write_text(f"{AUR_HOST_KEY}\n")
            environment["GIT_SSH_COMMAND"] = (
                f"ssh -i '{args.ssh_key.resolve()}' -o IdentitiesOnly=yes -o UserKnownHostsFile='{known_hosts}' "
                "-o StrictHostKeyChecking=yes"
            )

        for package in packages(args.packages):
            pkgbuild = (package / "PKGBUILD").read_text()
            release = f"{variable(pkgbuild, 'pkgver')}-{variable(pkgbuild, 'pkgrel')}"
            files = run("git", "-C", XENON_SOURCE_DIR, "ls-files", "--", relative(package)).splitlines()
            if not any(Path(file).name == ".SRCINFO" for file in files):
                sys.exit(f"{relative(package)} has no .SRCINFO in git, which the AUR needs")

            clone = Path(directory) / package.name
            run("git", "clone", "--quiet", AUR_REPOSITORY.format(package.name), clone, env=environment)
            head = subprocess.run(["git", "-C", clone, "rev-parse", "--verify", "--quiet", "HEAD"], capture_output=True)
            new = head.returncode != 0
            for path in clone.iterdir():
                if path.name != ".git":
                    shutil.rmtree(path) if path.is_dir() else path.unlink()
            for file in files:
                shutil.copy2(XENON_SOURCE_DIR / file, clone / Path(file).name)
            run("git", "-C", clone, "add", "--all")
            if not run("git", "-C", clone, "status", "--porcelain").strip():
                print(f"{package.name}: the AUR has {release} already")
                continue
            message = f"Add {package.name} {release}" if new else f"Update to {release}"
            run("git", "-C", clone, "commit", "--quiet", "--message", message)
            run("git", "-C", clone, "push", "--quiet", "origin", "HEAD:master", env=environment)
            print(f"{package.name}: pushed {release} to https://aur.archlinux.org/packages/{package.name}")


if __name__ == "__main__":
    main()
