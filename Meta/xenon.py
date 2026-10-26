#!/usr/bin/env python3

# Builds and runs Xenon: Ladybird's engine (the `ladybird` submodule) with Xenon's own frontend in `UI/`.
#
# Ladybird is configured from its own source tree, and Meta/CMake/XenonHook.cmake is injected after its
# project() call so the Xenon frontend is added once every engine library and helper process exists.

import argparse
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading

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
TESTS_TARGET = "xenon-tests"

# A page rendered with nothing on it compresses to a few kilobytes; the welcome page is well over this.
SMOKE_TEST_MIN_SCREENSHOT_BYTES = 20_000

# Requests the bundled filter lists block (EasyList: /jquery.popunder.js; EasyPrivacy: -tracking-pixel.), and ordinary
# ones nothing should block.
BLOCKED_REQUESTS = {"/jquery.popunder.js": "script", "/site-tracking-pixel.gif": "img"}
ALLOWED_REQUESTS = {"/app.js": "script", "/logo.gif": "img"}


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
    test_parser = subparsers.add_parser("test", help="Build and run the unit tests and the headless smoke test")
    test_parser.add_argument("--no-build", action="store_true")
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
    elif args.command == "test":
        if not args.no_build:
            build_main(build_dir, args.jobs, APP_TARGET, TESTS_TARGET)
        test_main(platform, build_dir)


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


def build_main(build_dir: Path, jobs: str | None, *targets: str):
    build_args = ["ninja", "-C", build_dir]
    if jobs:
        build_args.extend(["-j", jobs])
    build_args.extend(targets)
    run_command(build_args)


def app_executable(platform: Platform, build_dir: Path) -> Path:
    if platform.host_system == HostSystem.macOS:
        executable = build_dir / "bin" / "Xenon.app" / "Contents" / "MacOS" / "Xenon"
    else:
        executable = build_dir / "bin" / "Xenon"

    if not executable.exists():
        print(f"Executable not found: {executable}. Build Xenon before running it.", file=sys.stderr)
        sys.exit(1)
    return executable


def run_main(platform: Platform, build_dir: Path, args: list[str]):
    run_command([app_executable(platform, build_dir), *args])


def test_main(platform: Platform, build_dir: Path):
    failures = []

    for test in sorted((build_dir / "bin").glob("TestXenon*")):
        if test.is_dir() or not os.access(test, os.X_OK):
            continue
        print(f"==> {test.name}", flush=True)
        if subprocess.run([test]).returncode != 0:
            failures.append(test.name)

    # Rendering the welcome page without a window runs Xenon's bundled resources, the WebUI bridge, and the engine's
    # helper processes together, which unit tests can't.
    smoke_dir = build_dir / "smoke"
    smoke_dir.mkdir(exist_ok=True)
    screenshot = smoke_dir / "welcome.png"
    screenshot.unlink(missing_ok=True)

    env = dict(os.environ)
    if platform.host_system != HostSystem.macOS and not env.get("DISPLAY") and not env.get("WAYLAND_DISPLAY"):
        env["QT_QPA_PLATFORM"] = "offscreen"

    print("==> headless render of about:welcome", flush=True)
    command = [
        app_executable(platform, build_dir),
        "--headless=screenshot",
        "--screenshot-delay=2",
        f"--screenshot-path={screenshot}",
        "--window-width=1100",
        "--window-height=900",
        "about:welcome",
    ]
    try:
        result = subprocess.run([str(part) for part in command], env=env, capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        rendered = result.returncode == 0 and screenshot.exists()
    except subprocess.TimeoutExpired as error:
        output = f"timed out after {error.timeout} s"
        rendered = False

    if rendered and screenshot.stat().st_size >= SMOKE_TEST_MIN_SCREENSHOT_BYTES:
        print(f"    rendered {screenshot} ({screenshot.stat().st_size} bytes)")
    else:
        print(output[-4000:], file=sys.stderr)
        failures.append("headless render of about:welcome")

    print("==> content blocking with the bundled filter lists", flush=True)
    if not check_content_blocking(app_executable(platform, build_dir), env):
        failures.append("content blocking")

    if failures:
        print(f"Failed: {', '.join(failures)}", file=sys.stderr)
        sys.exit(1)
    print("All tests passed.")


# Loads a local page with blocking on and off, in a throwaway profile, and checks which of its requests reach the server.
# Xenon is given no lists here, so this also proves it loads its bundled ones by itself.
def check_content_blocking(executable: Path, env: dict) -> bool:
    requested: set[str] = set()
    tags = "".join(f'<{tag} src="{path}"></{tag}>' for path, tag in {**BLOCKED_REQUESTS, **ALLOWED_REQUESTS}.items())
    page = f"<!doctype html><title>Xenon content blocking</title><p>loaded</p>{tags}".encode()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requested.add(self.path)
            body = page if self.path == "/" else b""
            self.send_response(200)
            self.send_header("Content-Type", "text/html" if self.path == "/" else "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/"

    passed = True
    try:
        for blocking in (False, True):
            requested.clear()
            with tempfile.TemporaryDirectory() as profile:
                (Path(profile) / "config").mkdir()
                (Path(profile) / "config" / "Settings.json").write_text(
                    json.dumps({"contentBlockers": {"enabled": blocking}})
                )
                command = [str(executable), "--headless=text", "--profile-path", profile, url]
                try:
                    subprocess.run(command, env=env, capture_output=True, timeout=120)
                except subprocess.TimeoutExpired:
                    print("    timed out loading the test page", file=sys.stderr)
                    return False

            expected_blocked = set(BLOCKED_REQUESTS) if blocking else set()
            reached = {path for path in (*BLOCKED_REQUESTS, *ALLOWED_REQUESTS) if path in requested}
            wrongly_blocked = set(ALLOWED_REQUESTS) - reached
            wrongly_allowed = (set(BLOCKED_REQUESTS) - reached) ^ expected_blocked
            label = "blocking on " if blocking else "blocking off"
            if "/" not in requested or wrongly_blocked or wrongly_allowed:
                print(f"    {label}: page loaded={'/' in requested}, reached={sorted(reached)}", file=sys.stderr)
                passed = False
            else:
                print(f"    {label}: reached {sorted(reached)}")
    finally:
        server.shutdown()
    return passed


def run_command(command: list):
    try:
        subprocess.check_call([str(part) for part in command])
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
