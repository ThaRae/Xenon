#!/usr/bin/env python3

# Builds and runs Xenon: Ladybird's engine (the `ladybird` submodule) with Xenon's own frontend in `UI/`.
#
# Ladybird is configured from its own source tree, and Meta/CMake/XenonHook.cmake is injected after its
# project() call so the Xenon frontend is added once every engine library and helper process exists.

import argparse
import os
import shutil
import subprocess
import sys

from pathlib import Path

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
LADYBIRD_SOURCE_DIR = XENON_SOURCE_DIR / "ladybird"
BUILD_ROOT_DIR = XENON_SOURCE_DIR / "Build"

if not (LADYBIRD_SOURCE_DIR / "Meta" / "ladybird.py").exists():
    print("The ladybird submodule is missing. Run: git submodule update --init --depth 1", file=sys.stderr)
    sys.exit(1)

sys.path.insert(0, str(LADYBIRD_SOURCE_DIR / "Meta"))

from Utils.build_vcpkg import build_vcpkg  # noqa: E402
from Utils.find_compiler import pick_host_compiler  # noqa: E402
from Utils.host_platform import HostSystem  # noqa: E402
from Utils.host_platform import Platform  # noqa: E402

PRESETS = {
    "Release": ("release", "vcpkg-release"),
    "Debug": ("debug", "vcpkg-debug"),
    "Sanitizer": ("sanitizer", "vcpkg-sanitizer"),
}

APP_TARGET = "xenon"


def main():
    parser = argparse.ArgumentParser(description="Build and run Xenon")
    parser.add_argument("--preset", default=os.environ.get("BUILD_PRESET", "Release"), choices=PRESETS.keys())
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--jobs", "-j")

    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("configure", help="Fetch dependencies and generate the build")
    build_parser = subparsers.add_parser("build", help="Build Xenon (or another target)")
    build_parser.add_argument("target", nargs="?", default=APP_TARGET)
    run_parser = subparsers.add_parser("run", help="Build and launch Xenon")
    run_parser.add_argument("--no-build", action="store_true")
    run_parser.add_argument("args", nargs=argparse.REMAINDER)
    subparsers.add_parser("clean", help="Delete the build directory for the preset")

    args = parser.parse_args()
    platform = Platform()

    if args.command == "clean":
        build_dir, _ = configure_build_env(args.preset, args.jobs)
        shutil.rmtree(build_dir, ignore_errors=True)
        return

    build_dir = configure_main(platform, args.preset, args.cc, args.cxx, args.jobs)

    if args.command == "build":
        build_main(build_dir, args.jobs, args.target)
    elif args.command == "run":
        if not args.no_build:
            build_main(build_dir, args.jobs, APP_TARGET)
        run_main(platform, build_dir, args.args)


def configure_build_env(preset: str, jobs: str | None) -> tuple[Path, Path]:
    build_name, vcpkg_name = PRESETS[preset]
    build_dir = BUILD_ROOT_DIR / build_name
    vcpkg_dir = BUILD_ROOT_DIR / vcpkg_name

    os.environ["LADYBIRD_SOURCE_DIR"] = str(LADYBIRD_SOURCE_DIR)
    os.environ["VCPKG_ROOT"] = str(vcpkg_dir)
    os.environ["PATH"] += os.pathsep + str(vcpkg_dir)

    if jobs:
        os.environ["VCPKG_MAX_CONCURRENCY"] = jobs

    return build_dir, vcpkg_dir


def configure_main(platform: Platform, preset: str, cc: str, cxx: str, jobs: str | None) -> Path:
    build_dir, vcpkg_dir = configure_build_env(preset, jobs)
    build_vcpkg(vcpkg_dir)

    if build_dir.joinpath("build.ninja").exists():
        return build_dir

    cc, cxx = pick_host_compiler(platform, cc, cxx)

    run_command(
        [
            "cmake",
            "--preset",
            preset,
            "-S",
            LADYBIRD_SOURCE_DIR,
            "-B",
            build_dir,
            f"-DCMAKE_C_COMPILER={cc}",
            f"-DCMAKE_CXX_COMPILER={cxx}",
            f"-DCMAKE_PROJECT_ladybird_INCLUDE={XENON_SOURCE_DIR / 'Meta' / 'CMake' / 'XenonHook.cmake'}",
            # Tests and utilities are Ladybird's concern; Xenon only needs the engine and helper processes.
            "-DBUILD_TESTING=OFF",
        ]
    )

    return build_dir


def build_main(build_dir: Path, jobs: str | None, target: str):
    build_args = ["ninja", "-C", build_dir]
    if jobs:
        build_args.extend(["-j", jobs])
    build_args.append(target)
    run_command(build_args)


def run_main(platform: Platform, build_dir: Path, args: list[str]):
    if platform.host_system == HostSystem.macOS:
        executable = build_dir / "bin" / "Xenon.app" / "Contents" / "MacOS" / "Xenon"
    else:
        executable = build_dir / "bin" / "Xenon"

    if not executable.exists():
        print(f"Executable not found: {executable}. Build Xenon before running it.", file=sys.stderr)
        sys.exit(1)

    run_command([executable, *args])


def run_command(command: list):
    try:
        subprocess.check_call([str(part) for part in command])
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
