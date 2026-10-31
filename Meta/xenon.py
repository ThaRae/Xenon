#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import hashlib
import html
import http.server
import io
import json
import math
import os
import re
import shutil
import signal
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import wave

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

import patches  # noqa: E402

PRESETS = {
    "Release": ("release", "vcpkg-release"),
    "Debug": ("debug", "vcpkg-debug"),
    "Sanitizer": ("sanitizer", "vcpkg-sanitizer"),
}

APP_TARGET = "xenon"
TESTS_TARGET = "xenon-tests"

LADYBIRD_TESTS_EXCLUDED = "^TestXenon|LibWebAccessibilityBridge|AccessibilityBridgeMac"
LADYBIRD_TEST_TIMEOUT_SECONDS = 1800
LADYBIRD_TEST_PYTHON_PACKAGES = ["pyyaml", "requests", "six", "cryptography"]

SMOKE_TEST_MIN_SCREENSHOT_BYTES = 20_000

WELCOME_PAGE_TEXT = "Choose for myself"

IMAGE_DECODER_TEST_PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
)
IMAGE_DECODER_TEST_MARKER = "XENON-IMAGE-DECODER-TEST "

WELCOME_SCREENSHOT_DELAY_SECONDS = 4

SKIPPED_CHECKS = "the checks that load pages (skipped: about:welcome didn't load)"

PRINTED_CRASH_REPORTS = 5
PRINTED_CRASH_REPORT_CHARACTERS = 6000

BLOCKED_REQUESTS = {"/jquery.popunder.js": "script", "/site-tracking-pixel.gif": "img"}
ALLOWED_REQUESTS = {"/app.js": "script", "/logo.gif": "img"}

FILTERED_PAGE_URL = "https://citationmachine.net/"
FILTERED_PAGE_MARKER = "XENON-CONTENT-BLOCKING "
FILTERED_PAGE = f"""<!doctype html><title>Xenon content blocking</title>
<div class="adds-wrapper">ad</div><div class="article">article</div>
<script>
    const found = {{
        "canRunAds": String(window.canRunAds),
        ".adds-wrapper": getComputedStyle(document.querySelector(".adds-wrapper")).display,
        ".article": getComputedStyle(document.querySelector(".article")).display,
    }};
    document.body.append("{FILTERED_PAGE_MARKER}" + JSON.stringify(found));
</script>"""
FILTERED_PAGE_EXPECTED = {
    False: {"canRunAds": "undefined", ".adds-wrapper": "block", ".article": "block"},
    True: {"canRunAds": "true", ".adds-wrapper": "none", ".article": "block"},
}

BLOCKING_DASHBOARD_URL = "xenon://blocking/"
FILTER_LIST_MANIFEST = XENON_SOURCE_DIR / "UI" / "Resources" / "filter-lists" / "manifest.json"

FILTER_LIST_UPDATES_DIR = XENON_SOURCE_DIR / "Tests" / "FilterListUpdates"
FILTER_LIST_UPDATES_MARKER = "XENON-FILTER-LIST-UPDATES-TEST "

TEST_EXTENSION_DIR = XENON_SOURCE_DIR / "Tests" / "Extensions" / "basic"
EXTENSION_TEST_MARKER = "XENON-EXTENSION-TEST "
NATIVE_MESSAGING_ECHO_HOST = XENON_SOURCE_DIR / "Tests" / "NativeMessaging" / "echo_host.py"

CERTIFICATE_ERRORS_DIR = XENON_SOURCE_DIR / "Tests" / "CertificateErrors"
CERTIFICATE_ERROR_TEST_MARKER = "XENON-CERTIFICATE-ERROR-TEST "

WEB_AUTHN_TEST_MARKER = "XENON-WEBAUTHN-TEST "

UPDATE_STAND_IN_VERSION = "99.0.0"
UPDATE_BUTTON_SHOWN = "The toolbar offers to relaunch to update to Xenon "

SESSION_SAVE_WAIT_SECONDS = 3

EXTENSION_STORE_DIR = XENON_SOURCE_DIR / "Tests" / "ExtensionStore"
PASSWORD_MANAGER_TEST_MARKER = "XENON-PASSWORD-MANAGER-TEST "


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
    ladybird_test_parser = subparsers.add_parser(
        "ladybird-test", help="Build Ladybird's own tests and run them against the patched engine"
    )
    ladybird_test_parser.add_argument("--no-build", action="store_true")
    ladybird_test_parser.add_argument(
        "--timeout",
        type=int,
        default=LADYBIRD_TEST_TIMEOUT_SECONDS,
        help=f"How long one test may run, in seconds (default {LADYBIRD_TEST_TIMEOUT_SECONDS})",
    )
    subparsers.add_parser("clean", help="Delete the build directory for the preset")
    subparsers.add_parser("self-test", help="Check the filter list bundle server's path handling")

    args = parser.parse_args()
    platform = Platform()

    if args.command == "clean":
        build_dir, _ = configure_build_env(args.preset, args.jobs)
        shutil.rmtree(build_dir, ignore_errors=True)
        return

    if args.command == "self-test":
        sys.exit(self_test())

    patches.ensure_applied()
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
    elif args.command == "ladybird-test":
        enable_ladybird_tests(build_dir)
        if not args.no_build:
            build_main(build_dir, args.jobs)
        ladybird_test_main(build_dir, args.timeout)


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
        if cmake_cache_value(build_dir, "ENABLE_CI_BASELINE_CPU") != "ON":
            run_command(["cmake", "-S", LADYBIRD_SOURCE_DIR, "-B", build_dir, "-DENABLE_CI_BASELINE_CPU=ON"])
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
            "-DBUILD_TESTING=OFF",
            "-DENABLE_CI_BASELINE_CPU=ON",
        ]
    )

    return build_dir


def enable_ladybird_tests(build_dir: Path):
    if cmake_cache_value(build_dir, "BUILD_TESTING") == "ON":
        return
    run_command(["cmake", "-S", LADYBIRD_SOURCE_DIR, "-B", build_dir, "-DBUILD_TESTING=ON"])


def cmake_cache_value(build_dir: Path, name: str) -> str | None:
    for line in (build_dir / "CMakeCache.txt").read_text(errors="replace").splitlines():
        key, _, value = line.partition("=")
        if key.split(":", 1)[0] == name:
            return value
    return None


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
        executable = build_dir / "xenon" / "bin" / "Xenon"

    if not executable.exists():
        print(f"Executable not found: {executable}. Build Xenon before running it.", file=sys.stderr)
        sys.exit(1)
    return executable


def run_main(platform: Platform, build_dir: Path, args: list[str]):
    run_command([app_executable(platform, build_dir), *args])


def test_main(platform: Platform, build_dir: Path):
    failures = []

    smoke_dir = build_dir / "smoke"
    smoke_dir.mkdir(parents=True, exist_ok=True)
    test_env = {"XENON_SCREENSHOT_DIR": str(smoke_dir), **os.environ}

    for test in sorted((build_dir / "bin").glob("TestXenon*")):
        if test.is_dir() or not os.access(test, os.X_OK):
            continue
        print(f"==> {test.name}", flush=True)
        if subprocess.run([test], env=test_env).returncode != 0:
            failures.append(test.name)

    failures += check_app(platform, app_executable(platform, build_dir), smoke_dir)

    if failures:
        print(f"Failed: {', '.join(failures)}", file=sys.stderr)
        sys.exit(1)
    print("All tests passed.")


def ladybird_test_main(build_dir: Path, timeout: int):
    os.environ["TESTS_ONLY"] = "1"
    paths = [ladybird_test_python_packages(build_dir), os.environ.get("PYTHONPATH")]
    os.environ["PYTHONPATH"] = os.pathsep.join(path for path in paths if path)
    run_command(
        [
            "ctest",
            "--test-dir",
            build_dir,
            "--output-on-failure",
            "--timeout",
            str(timeout),
            "--exclude-regex",
            LADYBIRD_TESTS_EXCLUDED,
        ]
    )


def ladybird_test_python_packages(build_dir: Path) -> str:
    environment = build_dir / "ladybird-test-python"
    python = environment / "bin" / "python3"
    if not python.exists():
        run_command([cmake_cache_value(build_dir, "Python3_EXECUTABLE") or sys.executable, "-m", "venv", environment])
    run_command(
        [python, "-m", "pip", "install", "--quiet", "--disable-pip-version-check", *LADYBIRD_TEST_PYTHON_PACKAGES]
    )
    return subprocess.check_output(
        [python, "-c", "import sysconfig; print(sysconfig.get_path('purelib'))"], text=True
    ).strip()


def check_app(platform: Platform, executable: Path, smoke_dir: Path) -> list[str]:
    reports_dir = crash_report_directory(platform)
    earlier_reports = saved_crash_reports(reports_dir)
    failures = run_app_checks(platform, executable, smoke_dir)
    if failures:
        reports = saved_crash_reports(reports_dir)
        new_reports = sorted(reports.keys() - earlier_reports.keys())
        for name in new_reports[:PRINTED_CRASH_REPORTS]:
            print(f"==> crash report {name}", file=sys.stderr)
            print(reports[name].read_text(errors="replace")[:PRINTED_CRASH_REPORT_CHARACTERS], file=sys.stderr)
        if len(new_reports) > PRINTED_CRASH_REPORTS:
            print(f"    and {len(new_reports) - PRINTED_CRASH_REPORTS} more in {reports_dir}", file=sys.stderr)
    return failures


def crash_report_directory(platform: Platform) -> Path:
    if data := os.environ.get("XDG_DATA_HOME"):
        return Path(data) / "Xenon" / "CrashReports"
    if platform.host_system == HostSystem.macOS:
        return Path.home() / "Library" / "Application Support" / "Xenon" / "CrashReports"
    return Path.home() / ".local" / "share" / "Xenon" / "CrashReports"


def saved_crash_reports(reports_dir: Path) -> dict[str, Path]:
    if not reports_dir.is_dir():
        return {}
    return {path.name: path for path in reports_dir.rglob("*.txt")}


def run_app_checks(platform: Platform, executable: Path, smoke_dir: Path) -> list[str]:
    failures = []

    smoke_dir.mkdir(parents=True, exist_ok=True)
    screenshot = smoke_dir / "welcome.png"
    screenshot.unlink(missing_ok=True)

    env = dict(os.environ)
    if platform.host_system != HostSystem.macOS and not env.get("DISPLAY") and not env.get("WAYLAND_DISPLAY"):
        env["QT_QPA_PLATFORM"] = "offscreen"

    print("==> headless render of about:welcome", flush=True)
    with tempfile.TemporaryDirectory() as profile:
        loaded, output = run_headless(executable, env, ["--headless=text", "--profile-path", profile, "about:welcome"])
        if loaded and WELCOME_PAGE_TEXT in output:
            print(f'    loaded, with "{WELCOME_PAGE_TEXT}"')
        else:
            print(output[-4000:], file=sys.stderr)
            failures.append("loading about:welcome")

        rendered, output = run_headless(
            executable,
            env,
            [
                "--headless=screenshot",
                f"--screenshot-delay={WELCOME_SCREENSHOT_DELAY_SECONDS}",
                f"--screenshot-path={screenshot}",
                "--window-width=1100",
                "--window-height=900",
                "--profile-path",
                profile,
                "about:welcome",
            ],
        )
        if rendered and screenshot.exists() and screenshot.stat().st_size >= SMOKE_TEST_MIN_SCREENSHOT_BYTES:
            print(f"    rendered {screenshot} ({screenshot.stat().st_size} bytes)")
        else:
            print(output[-4000:], file=sys.stderr)
            failures.append("headless render of about:welcome")

    if not loaded:
        failures.append(SKIPPED_CHECKS)
        return failures

    print("==> a fresh start keeps to Xenon's directories", flush=True)
    if not check_profile_directory(platform, executable, env):
        failures.append("profile directory")

    print("==> content blocking with the bundled filter lists", flush=True)
    if not check_content_blocking(executable, env):
        failures.append("content blocking")

    print("==> daily filter list updates from a local server", flush=True)
    if not check_filter_list_updates(executable, env):
        failures.append("filter list updates")

    print("==> an ImageDecoder starts with a page's first image, and goes once idle", flush=True)
    if not check_image_decoder_lifecycle(executable, env):
        failures.append("image decoder lifecycle")

    print("==> media servers start when a page needs one, stay while it is paused, and go once idle", flush=True)
    if not check_media_server_lifecycle(executable, env):
        failures.append("media server lifecycle")

    print("==> the compositor gives back the rasters of what stopped painting", flush=True)
    if not check_compositor_idle_memory_release(executable, env):
        failures.append("compositor idle memory release")

    print("==> the blocking dashboard", flush=True)
    if not check_blocking_dashboard(executable, env):
        failures.append("blocking dashboard")

    print("==> the certificate warning, for a local HTTPS server with a self-signed certificate", flush=True)
    if not check_certificate_errors(executable, env):
        failures.append("certificate warning")

    print("==> extension pages and their chrome.* APIs", flush=True)
    if not check_extensions(executable, env):
        failures.append("extensions")

    print("==> tabs come back with their pages after a crash and after a quit", flush=True)
    if not check_session_restore(executable, env):
        failures.append("session restore")

    print("==> the toolbar offers an update that's ready", flush=True)
    if not check_update_button(executable, env):
        failures.append("update button")

    print("==> WebAuthn requests reach the UI process", flush=True)
    if not check_web_authn(executable, env):
        failures.append("WebAuthn")

    print("==> password managers from a stand-in for the Chrome Web Store", flush=True)
    if not check_password_managers(executable, env):
        failures.append("password managers")

    return failures


def check_profile_directory(platform: Platform, executable: Path, env: dict) -> bool:
    with tempfile.TemporaryDirectory() as temporary_home:
        home = Path(temporary_home).resolve()
        fresh_env = dict(env, HOME=str(home))
        for name in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"):
            fresh_env.pop(name, None)

        if platform.host_system == HostSystem.macOS:
            config, data, cache = "Library/Preferences", "Library/Application Support", "Library/Caches"
            roots = [config, data, cache]
        else:
            config, data, cache = ".config", ".local/share", ".cache"
            (home / "runtime").mkdir(mode=0o700)
            fresh_env["XDG_RUNTIME_DIR"] = str(home / "runtime")
            fresh_env["QT_QPA_PLATFORM"] = "offscreen"
            roots = [config, data, cache, "runtime"]

        loaded, output = run_headless(executable, fresh_env, ["--headless=text", "about:blank"])
        expected = [home / root / "Xenon" / "Profiles" / "default" for root in roots]
        expected.append(home / data / "Xenon" / "CrashReports")
        missing = [path for path in expected if not path.is_dir()]
        ladybird = [path for path in home.rglob("Ladybird") if path.is_dir()]

        if not loaded:
            print(output[-4000:], file=sys.stderr)
        for path in missing:
            print(f"    missing {path.relative_to(home)}", file=sys.stderr)
        for path in ladybird:
            print(f"    created {path.relative_to(home)}", file=sys.stderr)
        if not loaded or missing or ladybird:
            return False

        print(f"    {', '.join(f'~/{root}/Xenon' for root in roots)}, and no Ladybird directory")
        return True


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
        with tempfile.TemporaryDirectory() as pages:
            filtered_page = Path(pages) / "filtered.html"
            filtered_page.write_text(FILTERED_PAGE)
            substitution = {"url": FILTERED_PAGE_URL, "file": str(filtered_page), "content_type": "text/html"}
            resource_map = Path(pages) / "resource-map.json"
            resource_map.write_text(json.dumps({"substitutions": [substitution]}))

            for blocking in (False, True):
                requested.clear()
                with tempfile.TemporaryDirectory() as profile:
                    (Path(profile) / "config").mkdir()
                    (Path(profile) / "config" / "Settings.json").write_text(
                        json.dumps({"contentBlockers": {"enabled": blocking}})
                    )
                    command = [str(executable), "--headless=text", "--profile-path", profile]
                    try:
                        subprocess.run([*command, url], env=env, capture_output=True, timeout=120)
                        filtered = subprocess.run(
                            [*command, "--resource-map", str(resource_map), FILTERED_PAGE_URL],
                            env=env,
                            capture_output=True,
                            text=True,
                            timeout=120,
                        )
                    except subprocess.TimeoutExpired:
                        print("    timed out loading a test page", file=sys.stderr)
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

                found = None
                for line in (filtered.stdout + filtered.stderr).splitlines():
                    if FILTERED_PAGE_MARKER in line:
                        found = json.loads(line.split(FILTERED_PAGE_MARKER, 1)[1])
                if found != FILTERED_PAGE_EXPECTED[blocking]:
                    print(f"    {label}: {FILTERED_PAGE_URL} found {found}", file=sys.stderr)
                    if found is None:
                        print((filtered.stdout + filtered.stderr)[-4000:], file=sys.stderr)
                    passed = False
                else:
                    print(f"    {label}: {FILTERED_PAGE_URL} found {found}")
    finally:
        server.shutdown()
    return passed


class LifecyclePages:
    SCRIPT = """
    function report(name) {
        return fetch("/event?name=" + name).catch(() => {});
    }
    function waitForStep(step) {
        return new Promise(resolve => {
            const poll = () => fetch("/go?step=" + step)
                .then(response => response.text())
                .then(go => go === "1" ? resolve() : setTimeout(poll, 200), () => setTimeout(poll, 200));
            poll();
        });
    }
"""

    def __init__(self, pages: dict[str, tuple[str, bytes]]):
        self.events: set[str] = set()
        self.released: set[str] = set()
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                url = urllib.parse.urlsplit(self.path)
                query = urllib.parse.parse_qs(url.query)
                if url.path == "/event":
                    owner.events.add(query.get("name", [""])[0])
                    content_type, body = "text/plain", b"ok"
                elif url.path == "/go":
                    content_type, body = "text/plain", b"1" if query.get("step", [""])[0] in owner.released else b"0"
                elif url.path in pages:
                    content_type, body = pages[url.path]
                else:
                    self.send_response(404)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):
                pass

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.origin = f"http://127.0.0.1:{self.server.server_address[1]}"

    def release(self, step: int):
        self.released.add(str(step))

    def close(self):
        self.server.shutdown()


def start_lifecycle_page(executable: Path, env: dict, url: str) -> tuple[subprocess.Popen, str]:
    profile = tempfile.mkdtemp(prefix="xenon-lifecycle-")
    process = subprocess.Popen(
        [str(executable), "--headless=manual", "--profile-path", profile, url],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )
    return process, profile


def stop_lifecycle_page(process: subprocess.Popen, profile: str) -> str:
    try:
        output, _ = process.communicate(timeout=1)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            output, _ = process.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output, _ = process.communicate()
    shutil.rmtree(profile, ignore_errors=True)
    return output


def wait_until(condition, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.2)
    return bool(condition())


def lifecycle_results(output: str, marker: str):
    results = None
    for line in output.splitlines():
        if marker in line:
            results = json.loads(line.split(marker, 1)[1].rstrip().removesuffix('"'))
    return results


def check_image_decoder_lifecycle(executable: Path, env: dict) -> bool:
    image_page = f"""<!doctype html><title>image decoder</title>
<p>images</p>
<script>
    {LifecyclePages.SCRIPT}
    const results = {{}};
    function load(name) {{
        return new Promise(resolve => {{
            const image = new Image();
            image.onload = () => {{ results[name] = image.naturalWidth; resolve(); }};
            image.onerror = () => {{ results[name] = "failed"; resolve(); }};
            image.src = "/pixel.png?" + name;
        }});
    }}
    (async () => {{
        await load("first");
        await report("first");
        await waitForStep(1);
        await load("second");
        await report("second");
        await waitForStep(2);
        await load("third");
        console.log("{IMAGE_DECODER_TEST_MARKER}" + JSON.stringify(results));
        window.close();
    }})();
</script>"""
    pages = LifecyclePages(
        {
            "/": ("text/html", b"<!doctype html><title>no images</title><p>no images</p>"),
            "/image": ("text/html", image_page.encode()),
            "/pixel.png": ("image/png", IMAGE_DECODER_TEST_PNG),
        }
    )

    passed = True
    try:
        browser, profile = start_lifecycle_page(executable, env, f"{pages.origin}/")
        try:
            time.sleep(3)
            started = helper_pids(browser.pid, "ImageDecoder")
        finally:
            stop_lifecycle_page(browser, profile)
        if started:
            print(f"    a page without images started {len(started)} ImageDecoder process(es)", file=sys.stderr)
            passed = False
        else:
            print("    a page without images started no ImageDecoder")

        browser, profile = start_lifecycle_page(executable, env, f"{pages.origin}/image")
        problems = []
        try:
            first = []
            if not wait_until(lambda: "first" in pages.events, 20):
                problems.append("the first image did not load")
            elif not (first := helper_pids(browser.pid, "ImageDecoder")):
                problems.append("the first image did not start an ImageDecoder")
            else:
                os.kill(first[0], signal.SIGKILL)
                if not wait_until(lambda: first[0] not in helper_pids(browser.pid, "ImageDecoder"), 10):
                    problems.append("the first ImageDecoder did not stop")
            pages.release(1)

            if not wait_until(lambda: "second" in pages.events, 20):
                problems.append("the second image did not load")
            elif not [pid for pid in helper_pids(browser.pid, "ImageDecoder") if pid not in first]:
                problems.append("the second image did not start a new ImageDecoder")
            elif not wait_until(lambda: not helper_pids(browser.pid, "ImageDecoder"), 30):
                problems.append("an ImageDecoder with nothing to decode for 30 s is still running")
            pages.release(2)

            try:
                browser.wait(timeout=30)
            except subprocess.TimeoutExpired:
                pass
        finally:
            output = stop_lifecycle_page(browser, profile)

        results = lifecycle_results(output, IMAGE_DECODER_TEST_MARKER)
        if results != {"first": 1, "second": 1, "third": 1}:
            problems.append(f"the images decoded to {results}")
        if problems:
            for problem in problems:
                print(f"    {problem}", file=sys.stderr)
            print(output[-4000:], file=sys.stderr)
            passed = False
        else:
            print(
                "    an image started a decoder, a new one replaced it when it was killed, it went once idle, and the "
                "next image started another"
            )
    finally:
        pages.close()
    return passed


MEDIA_SERVER_TEST_MARKER = "XENON-MEDIA-SERVER-TEST "
MEDIA_SERVER_TEST_TONE_SECONDS = 1
MEDIA_SERVER_TEST_PAUSE_SECONDS = 35


def media_test_tone() -> bytes:
    frames = b"".join(
        struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * index / 22_050)))
        for index in range(22_050 * MEDIA_SERVER_TEST_TONE_SECONDS)
    )
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(22_050)
        wav.writeframes(frames)
    return buffer.getvalue()


def check_media_server_lifecycle(executable: Path, env: dict) -> bool:
    probe_page = f"""<!doctype html><title>media probe</title>
<p>no media</p>
<script>
    const supported = document.createElement("video").canPlayType("video/mp4");
    console.log("{MEDIA_SERVER_TEST_MARKER}" + JSON.stringify({{ supported }}));
</script>"""

    playback_page = f"""<!doctype html><title>media playback</title>
<audio src="/tone.wav" loop muted></audio>
<script>
    {LifecyclePages.SCRIPT}
    const audio = document.querySelector("audio");
    const results = {{}};
    audio.addEventListener("error", () => {{ results.error = audio.error ? audio.error.code : "unknown"; }});
    (async () => {{
        results.played = await audio.play().then(() => true, error => error.name);
        audio.pause();
        await report("paused");
        await waitForStep(1);
        results.resumed = await audio.play().then(() => true, error => error.name);
        audio.pause();
        audio.removeAttribute("src");
        audio.load();
        audio.remove();
        console.log("{MEDIA_SERVER_TEST_MARKER}" + JSON.stringify(results));
        await report("stopped");
    }})();
</script>"""

    pages = LifecyclePages(
        {
            "/": ("text/html", b"<!doctype html><title>no media</title><p>no media</p>"),
            "/probe": ("text/html", probe_page.encode()),
            "/playback": ("text/html", playback_page.encode()),
            "/tone.wav": ("audio/wav", media_test_tone()),
        }
    )

    def media_pids(root: int) -> list[int]:
        return helper_pids(root, "MediaServer", "AudioServer", "VideoDecoderServer")

    passed = True
    try:
        browser, profile = start_lifecycle_page(executable, env, f"{pages.origin}/")
        try:
            time.sleep(3)
            started = media_pids(browser.pid)
        finally:
            stop_lifecycle_page(browser, profile)
        if started:
            print("    a page without media started a media server", file=sys.stderr)
            passed = False
        else:
            print("    a page without media started no MediaServer and no AudioServer")

        browser, profile = start_lifecycle_page(executable, env, f"{pages.origin}/probe")
        try:
            started = wait_until(lambda: helper_pids(browser.pid, "MediaServer"), 10)
            retired = started and wait_until(lambda: not helper_pids(browser.pid, "MediaServer"), 25)
        finally:
            output = stop_lifecycle_page(browser, profile)
        if not started:
            print("    a media feature test did not start a MediaServer", file=sys.stderr)
            print(output[-4000:], file=sys.stderr)
            passed = False
        elif not retired:
            print("    a MediaServer idle for 25 s is still running", file=sys.stderr)
            passed = False
        else:
            print("    a media feature test started a MediaServer, and it went away once the page stopped using it")

        browser, profile = start_lifecycle_page(executable, env, f"{pages.origin}/playback")
        problems = []
        try:
            if not wait_until(lambda: "paused" in pages.events, 20):
                problems.append("the page did not get to pause its audio")
            elif not wait_until(lambda: helper_pids(browser.pid, "AudioServer"), 10):
                problems.append("playback did not start an AudioServer")
            else:
                time.sleep(MEDIA_SERVER_TEST_PAUSE_SECONDS)
                if not helper_pids(browser.pid, "MediaServer") or not helper_pids(browser.pid, "AudioServer"):
                    problems.append(
                        f"the media servers of an element paused for {MEDIA_SERVER_TEST_PAUSE_SECONDS} s went away"
                    )
            pages.release(1)

            if not wait_until(lambda: "stopped" in pages.events, 20):
                problems.append("the page did not get to play on and stop")
            elif not wait_until(lambda: not media_pids(browser.pid), 45):
                problems.append("the media servers are still running 45 s after the page stopped playing")
        finally:
            output = stop_lifecycle_page(browser, profile)

        results = lifecycle_results(output, MEDIA_SERVER_TEST_MARKER)
        if not results or results.get("played") is not True or results.get("resumed") is not True or "error" in results:
            problems.append(f"playback reported {results}")
        if problems:
            for problem in problems:
                print(f"    {problem}", file=sys.stderr)
            print(output[-4000:], file=sys.stderr)
            passed = False
        else:
            print(
                "    playback started an AudioServer, a paused element kept its servers and played on, and they went "
                "away once the page was done"
            )
    finally:
        pages.close()
    return passed


COMPOSITOR_VISIBLE_RELEASE_DELAY = 30
COMPOSITOR_RELEASE_PATTERN = re.compile(
    r"COMPOSITOR-IDLE-RELEASE context=(?P<context>\d+) nested=(?P<nested>true|false) visible=(?:true|false) "
    r"gpu=(?P<gpu>true|false) idle_ms=\d+ tiles=(?P<tiles>\d+) nested_lists=(?P<nested_lists>\d+) text=\d+"
)
COMPOSITOR_KEEP_PATTERN = re.compile(
    r"COMPOSITOR-IDLE-KEEP context=(?P<context>\d+) nested=(?P<nested>true|false) visible=(?:true|false) "
    r"idle_ms=(?P<idle_ms>\d+)"
)
COMPOSITOR_SCREENSHOT_PATTERN = re.compile(r"COMPOSITOR-SCREENSHOT context=(?P<context>\d+) after_release=true")
COMPOSITOR_IDLE_TEST_SVG_URL = "data:image/svg+xml," + urllib.parse.quote(
    "<svg xmlns='http://www.w3.org/2000/svg' width='160' height='100' viewBox='0 0 160 100'>"
    "<defs><linearGradient id='g' x1='0' y1='0' x2='1' y2='1'>"
    "<stop offset='0' stop-color='#ff0050'/><stop offset='1' stop-color='#08f'/></linearGradient></defs>"
    "<rect x='4' y='4' width='152' height='92' rx='16' fill='url(#g)'/>"
    "<circle cx='80' cy='50' r='28' fill='#fff' opacity='.7'/></svg>"
)
COMPOSITOR_IDLE_TEST_PAGE = """<!doctype html><meta charset="utf-8"><title>idle</title>
<style>
    body { margin: 0; font: 20px sans-serif; background: #fff; color: #123; }
    .bar { padding: 10px 16px; background: #ff0050; color: #fff; font-size: 28px; font-weight: bold; }
    .card { margin: 14px; padding: 14px; border: 3px solid #1c6; border-radius: 12px; width: 520px; }
    .blur { filter: blur(1.5px); }
    .tiles { margin: 14px; width: 560px; height: 100px; background: url("SVG_URL") 0 0 / 80px 50px repeat; }
    .turned { margin: 14px; padding: 10px; width: 560px; transform: rotate(-4deg); opacity: .9; background: #eef; }
</style>
<div class="bar">idle compositor</div>
<div class="card blur">Blurred text in a filtered card, so the layer needs a raster of its own.</div>
<div class="card"><img src="SVG_URL" width="240" height="150" alt=""><p>An SVG image, drawn from a raster the
    compositor keeps.</p></div>
<div class="tiles"></div>
<div class="turned">This block has its own transform and opacity, so it is a composited layer.</div>
<canvas id="c" width="260" height="100" style="margin:14px;border:2px solid #333"></canvas>
<script>
    const context = document.getElementById("c").getContext("2d");
    const gradient = context.createLinearGradient(0, 0, 260, 0);
    gradient.addColorStop(0, "#0a0");
    gradient.addColorStop(1, "#00f");
    context.fillStyle = gradient;
    context.fillRect(0, 0, 260, 100);
    context.fillStyle = "#fff";
    context.font = "24px sans-serif";
    context.fillText("2D canvas", 40, 60);
</script>
""".replace("SVG_URL", COMPOSITOR_IDLE_TEST_SVG_URL)
COMPOSITOR_BUSY_TEST_PAGE = """<!doctype html><meta charset="utf-8"><title>busy</title>
<style>
    body { margin: 0; font: 20px sans-serif; }
    .spinner { margin: 14px; width: 60px; height: 60px; background: #ff0050; animation: spin 1s linear infinite; }
    @keyframes spin { to { transform: rotate(360deg); } }
    .tiles { margin: 14px; width: 560px; height: 100px; background: url("SVG_URL") 0 0 / 80px 50px repeat; }
    iframe { margin: 14px; width: 600px; height: 300px; border: 0; }
</style>
<div class="spinner"></div>
<img src="SVG_URL" width="240" height="150" alt="">
<div class="tiles"></div>
<iframe srcdoc="FRAME"></iframe>
""".replace("SVG_URL", COMPOSITOR_IDLE_TEST_SVG_URL).replace(
    "FRAME",
    html.escape(
        '<!doctype html><meta charset="utf-8"><body style="margin: 0; font: 20px sans-serif">'
        f'<img src="{COMPOSITOR_IDLE_TEST_SVG_URL}" width="240" height="150" alt="">'
        '<div style="width: 560px; height: 100px; '
        f'background: url({COMPOSITOR_IDLE_TEST_SVG_URL}) 0 0 / 80px 50px repeat"></div>'
        "<p>A still frame beside a page that keeps painting.</p>"
    ),
)


def released_svg_rasters(release: re.Match) -> bool:
    return int(release["tiles"]) > 0 and (release["gpu"] == "false" or int(release["nested_lists"]) > 0)


def check_compositor_idle_memory_release(executable: Path, env: dict) -> bool:
    logging_env = dict(env, LADYBIRD_COMPOSITOR_IDLE_RELEASE_LOG="1")
    with tempfile.TemporaryDirectory() as temporary_directory:
        directory = Path(temporary_directory)
        page = directory / "idle.html"
        page.write_text(COMPOSITOR_IDLE_TEST_PAGE)

        def screenshot_page(label: str, delay: int) -> tuple[bool, str, bytes]:
            profile = directory / f"{label}-profile"
            profile.mkdir()
            screenshot = directory / f"{label}.png"
            rendered, output = run_headless(
                executable,
                logging_env,
                [
                    "--headless=screenshot",
                    f"--screenshot-delay={delay}",
                    f"--screenshot-path={screenshot}",
                    "--window-width=900",
                    "--window-height=700",
                    "--profile-path",
                    str(profile),
                    page.as_uri(),
                ],
            )
            rendered = rendered and screenshot.exists() and screenshot.stat().st_size >= SMOKE_TEST_MIN_SCREENSHOT_BYTES
            return rendered, output, screenshot.read_bytes() if rendered else b""

        before = screenshot_page("before", 5)
        after = {}
        after_thread = threading.Thread(
            target=lambda: after.setdefault("result", screenshot_page("after", COMPOSITOR_VISIBLE_RELEASE_DELAY + 12))
        )
        after_thread.start()
        busy_output = run_compositor_busy_page(executable, logging_env, directory)
        after_thread.join()

    passed = check_compositor_screenshots(before, after["result"])
    return check_compositor_busy_page(busy_output) and passed


def check_compositor_screenshots(before: tuple[bool, str, bytes], after: tuple[bool, str, bytes]) -> bool:
    for (rendered, output, _), label in ((before, "before"), (after, "after")):
        if not rendered:
            print(f"    the page did not render for the screenshot {label} the release", file=sys.stderr)
            print(output[-4000:], file=sys.stderr)
            return False

    if any(COMPOSITOR_RELEASE_PATTERN.finditer(before[1])):
        print("    the compositor released a page that had painted 5 s before", file=sys.stderr)
        print(before[1][-4000:], file=sys.stderr)
        return False

    releases = [match for match in COMPOSITOR_RELEASE_PATTERN.finditer(after[1]) if match["nested"] == "false"]
    if not any(released_svg_rasters(match) for match in releases):
        print(
            "    the compositor did not give back the page's SVG and tile rasters once it stopped painting",
            file=sys.stderr,
        )
        print(after[1][-4000:], file=sys.stderr)
        return False
    released_contexts = {match["context"] for match in releases}
    if not any(match["context"] in released_contexts for match in COMPOSITOR_SCREENSHOT_PATTERN.finditer(after[1])):
        print("    the screenshot after the release did not paint the released page again", file=sys.stderr)
        print(after[1][-4000:], file=sys.stderr)
        return False

    if before[2] != after[2]:
        print("    the page rendered differently after the compositor released its rasters", file=sys.stderr)
        return False
    print("    a page that stopped painting gave its rasters back, and was painted the same again")
    return True


def run_compositor_busy_page(executable: Path, env: dict, directory: Path) -> str:
    page = directory / "busy.html"
    page.write_text(COMPOSITOR_BUSY_TEST_PAGE)
    profile = directory / "busy-profile"
    profile.mkdir()
    log_path = directory / "busy.log"

    def frame_released() -> bool:
        output = log_path.read_text(errors="replace")
        return any(match["nested"] == "true" for match in COMPOSITOR_RELEASE_PATTERN.finditer(output))

    with open(log_path, "w") as log:
        process = subprocess.Popen(
            [
                str(executable),
                "--headless=manual",
                "--window-width=900",
                "--window-height=900",
                "--profile-path",
                str(profile),
                page.as_uri(),
            ],
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            if wait_until(frame_released, COMPOSITOR_VISIBLE_RELEASE_DELAY + 30):
                time.sleep(2)
        finally:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
    return log_path.read_text(errors="replace")


def check_compositor_busy_page(output: str) -> bool:
    releases = list(COMPOSITOR_RELEASE_PATTERN.finditer(output))
    frame_release = next(
        (match for match in releases if match["nested"] == "true" and released_svg_rasters(match)), None
    )
    if not frame_release:
        print("    the frame that stopped painting kept its rasters while the page around it painted", file=sys.stderr)
        print(output[-4000:], file=sys.stderr)
        return False
    if any(match["nested"] == "false" for match in releases):
        print("    the compositor released the rasters of a page that was still painting", file=sys.stderr)
        print(output[-4000:], file=sys.stderr)
        return False

    painting_pages = {
        match["context"]
        for match in COMPOSITOR_KEEP_PATTERN.finditer(output)
        if match["nested"] == "false" and int(match["idle_ms"]) < 1000
    }
    after_release = output[frame_release.end() :]
    if not any(f"COMPOSITOR-FRAME context={context} " in after_release for context in painting_pages):
        print("    the page did not keep painting, with its rasters, after its frame was released", file=sys.stderr)
        print(output[-4000:], file=sys.stderr)
        return False
    print("    a frame that stopped painting gave its rasters back while the page around it kept painting with its own")
    return True


def bundle_file_for_request(path: str, version_dir: Path) -> Path | None:
    file = (version_dir / path.removeprefix("/bundle/")).resolve()
    if ".." in path or not file.is_relative_to(version_dir.resolve()) or not file.is_file():
        return None
    return file


def start_bundle_server(
    pages: dict, published: dict, requested: list, updates_dir: Path
) -> http.server.ThreadingHTTPServer:

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path = urllib.parse.urlsplit(self.path).path
            requested.append(path)
            status, content_type, body = 200, "application/javascript", b""
            if path in pages:
                content_type, body = (
                    "text/html",
                    f"<!doctype html><script>{pages[path]}</script>".encode(),
                )
            elif path.startswith("/bundle/"):
                file = bundle_file_for_request(path, updates_dir / published["version"])
                if file is None:
                    status = 404
                else:
                    content_type, body = "text/plain", file.read_bytes()
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def self_test() -> int:
    problems = 0

    def check(condition: bool, message: str) -> None:
        nonlocal problems
        if not condition:
            problems += 1
            print(f"self-test: {message}", file=sys.stderr)

    with tempfile.TemporaryDirectory() as directory:
        updates_dir = Path(directory)
        version_dir = updates_dir / "v1"
        version_dir.mkdir()
        (version_dir / "manifest.json").write_text("{}\n")

        published = {"version": "v1"}
        for path, want in (
            ("/bundle/manifest.json", version_dir / "manifest.json"),
            ("/bundle//etc/passwd", None),
            ("/bundle/../../outside.txt", None),
            ("/bundle/v1/../v2/manifest.json", None),
            ("/bundle/missing.json", None),
        ):
            got = bundle_file_for_request(path, version_dir)
            check(got == want, f"{path} resolved to {got}, wanted {want}")

        server = start_bundle_server({"/wait": ""}, published, [], updates_dir)
        origin = f"http://127.0.0.1:{server.server_address[1]}"
        try:
            with urllib.request.urlopen(f"{origin}/bundle/manifest.json", timeout=5) as response:
                check(
                    response.status == 200,
                    "the server didn't serve a bundle file inside the version",
                )
        except OSError as error:
            check(False, f"the server raised {error} for a bundle file inside the version")
        for path in ("/bundle//etc/passwd", "/bundle/../../../etc/passwd"):
            try:
                with urllib.request.urlopen(f"{origin}{path}", timeout=5) as response:
                    check(
                        response.status == 404,
                        f"the server answered {response.status} for {path}",
                    )
            except urllib.error.HTTPError as error:
                check(error.code == 404, f"the server answered {error.code} for {path}")
            except OSError as error:
                check(False, f"the server raised {error} for {path}")
        server.shutdown()

    if problems:
        print(
            f"self-test: {problems} problem{'s' if problems != 1 else ''}",
            file=sys.stderr,
        )
        return 1
    print("self-test: the bundle server served only what it publishes")
    return 0


def check_filter_list_updates(executable: Path, env: dict) -> bool:
    published = {"version": "v1"}
    requested: list[str] = []

    report = f"""function report(results) {{
        console.log("{FILTER_LIST_UPDATES_MARKER}" + JSON.stringify(results));
        window.close();
    }}
    function load(src) {{
        return new Promise(resolve => {{
            const script = document.createElement("script");
            script.src = src;
            script.onload = () => resolve("loaded");
            script.onerror = () => resolve("blocked");
            document.head.append(script);
        }});
    }}
    const sources = new URLSearchParams(location.search).getAll("src");"""
    pages = {
        "/poll": f"""{report}
            const deadline = Date.now() + 60000;
            (async () => {{
                for (let attempt = 0; Date.now() < deadline; ++attempt) {{
                    if (await load(`${{sources[0]}}?attempt=${{attempt}}`) === "blocked")
                        return report({{ blocked: true, attempts: attempt }});
                    await new Promise(resolve => setTimeout(resolve, 250));
                }}
                report({{ blocked: false }});
            }})();""",
        "/wait": f"""{report}
            (async () => {{
                const results = {{}};
                for (const src of sources)
                    results[src] = await load(src);
                setTimeout(() => report(results), 8000);
            }})();""",
    }

    server = start_bundle_server(pages, published, requested, FILTER_LIST_UPDATES_DIR)
    origin = f"http://127.0.0.1:{server.server_address[1]}"
    update_env = {
        **env,
        "XENON_FILTER_LIST_UPDATES_URL": f"{origin}/bundle/",
        "XENON_FILTER_LIST_UPDATES_PUBLIC_KEY": (FILTER_LIST_UPDATES_DIR / "public-key.b64").read_text().strip(),
    }

    def run(profile: str, page: str, *sources: str) -> tuple[dict | None, str]:
        requested.clear()
        query = urllib.parse.urlencode([("src", source) for source in sources])
        command = [str(executable), "--headless=manual", "--profile-path", profile, f"{origin}{page}?{query}"]
        try:
            result = subprocess.run(command, env=update_env, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired:
            return None, "timed out"
        output = result.stdout + result.stderr
        for line in output.splitlines():
            if FILTER_LIST_UPDATES_MARKER in line:
                return json.loads(line.split(FILTER_LIST_UPDATES_MARKER, 1)[1].rstrip().removesuffix('"')), output
        return None, output

    def downloads() -> list[str]:
        return sorted(path.removeprefix("/bundle/") for path in requested if path.startswith("/bundle/"))

    passed = True

    def expect(step: str, ok: bool, output: str, details: str = ""):
        nonlocal passed
        if ok:
            print(f"    {step}")
            return
        print(output[-4000:], file=sys.stderr)
        print(f"    FAIL: {step} {details}", file=sys.stderr)
        passed = False

    try:
        with tempfile.TemporaryDirectory() as profile:
            (Path(profile) / "config").mkdir()
            (Path(profile) / "config" / "Settings.json").write_text(
                json.dumps(
                    {
                        "contentBlockers": {"enabled": True},
                        "backgroundNetworking": {
                            "enabled": True,
                            "features": {"contentBlockerSubscriptionUpdates": True},
                        },
                    }
                )
            )

            def state() -> dict:
                files = list(Path(profile).rglob("ContentBlocking/Xenon/state.json"))
                return json.loads(files[0].read_text()) if files else {}

            def a_day_passes():
                for file in Path(profile).rglob("ContentBlocking/Xenon/state.json"):
                    file.write_text(json.dumps({**state(), "checkedAt": 0}))

            results, output = run(profile, "/poll", "/xenon-update-test.js")
            expect(
                "takes the first update while it runs",
                (results or {}).get("blocked") is True and state().get("active") == "209901010000",
                output,
                f"{results}, {state()}",
            )

            results, output = run(profile, "/wait", "/xenon-update-test.js", "/jquery.popunder.js")
            expect(
                "uses it in place of the bundled lists at the next start, and fetches nothing",
                results == {"/xenon-update-test.js": "blocked", "/jquery.popunder.js": "loaded"} and not downloads(),
                output,
                f"{results}, fetched {downloads()}",
            )

            a_day_passes()
            published["version"] = "v2"
            results, output = run(profile, "/poll", "/xenon-update-test-2.gif")
            expect(
                "takes the next day's update, downloading only the list that changed",
                (results or {}).get("blocked") is True
                and downloads() == ["lists/xenon-test.txt", "manifest.json", "manifest.json.sig"]
                and state().get("active") == "209901020000",
                output,
                f"{results}, fetched {downloads()}, {state()}",
            )

            a_day_passes()
            published["version"] = "v3"
            results, output = run(profile, "/wait", "/xenon-update-test-2.gif", "/xenon-update-test-4.gif")
            expect(
                "turns down a list that doesn't match its manifest, and keeps the lists it has",
                results == {"/xenon-update-test-2.gif": "blocked", "/xenon-update-test-4.gif": "loaded"}
                and "lists/xenon-test.txt" in downloads()
                and state().get("active") == "209901020000",
                output,
                f"{results}, fetched {downloads()}, {state()}",
            )
    finally:
        server.shutdown()
    return passed


def check_web_authn(executable: Path, env: dict) -> bool:
    page = f"""<!doctype html><script>
        const outcome = promise => promise.then(() => "resolved", error => error.name);
        const publicKey = {{
            rp: {{ name: "Xenon" }},
            user: {{ id: new Uint8Array([1]), name: "user", displayName: "User" }},
            challenge: new Uint8Array(32),
            pubKeyCredParams: [{{ type: "public-key", alg: -7 }}],
        }};
        (async () => [
            await outcome(navigator.credentials.create({{ publicKey }})),
            await outcome(navigator.credentials.get({{ publicKey: {{ challenge: new Uint8Array(32), rpId: "example.com" }} }})),
            await PublicKeyCredential.getClientCapabilities(),
        ])().then(([create, otherSite, capabilities]) => {{
            const results = {{
                "PublicKeyCredential": typeof PublicKeyCredential === "function",
                "no authenticators": create === "NotAllowedError",
                "another site's RP ID": otherSite === "SecurityError",
                "appid extension": capabilities["extension:appid"] === true,
            }};
            console.log("{WEB_AUTHN_TEST_MARKER}" + JSON.stringify(results));
            window.close();
        }});
    </script>""".encode()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(page)))
            self.end_headers()
            self.wfile.write(page)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        url = f"http://localhost:{server.server_address[1]}/"
        with tempfile.TemporaryDirectory() as profile:
            command = [str(executable), "--headless=manual", "--profile-path", profile, url]
            try:
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
            except subprocess.TimeoutExpired:
                print("    timed out on the WebAuthn page", file=sys.stderr)
                return False
    finally:
        server.shutdown()

    output = result.stdout + result.stderr
    for line in output.splitlines():
        if WEB_AUTHN_TEST_MARKER in line:
            results = json.loads(line.split(WEB_AUTHN_TEST_MARKER, 1)[1].rstrip().removesuffix('"'))
            for name, passed in results.items():
                print(f"    {'pass' if passed else 'FAIL'}: {name}")
            return all(results.values())
    print(output[-4000:], file=sys.stderr)
    return False


def check_password_managers(executable: Path, env: dict) -> bool:
    keys = json.loads((EXTENSION_STORE_DIR / "keys.json").read_text())
    bitwarden, proton_pass = keys["bitwarden"], keys["proton-pass"]
    published = {bitwarden: ("1.0", "bitwarden-1.0.crx"), proton_pass: ("1.0", "proton-pass-1.0.crx")}
    requested: list[str] = []

    page = f"""<!doctype html><script>
        const params = new URLSearchParams(location.search);
        const attempt = Number(params.get("attempt") ?? 0);
        addEventListener("load", () => setTimeout(() => {{
            const found = document.documentElement.dataset.xenonPasswordManager ?? null;
            if (!found && attempt < Number(params.get("tries"))) {{
                params.set("attempt", attempt + 1);
                setTimeout(() => (location.search = params.toString()), 250);
                return;
            }}
            setTimeout(() => {{
                console.log("{PASSWORD_MANAGER_TEST_MARKER}" + JSON.stringify(found));
                window.close();
            }}, Number(params.get("linger")));
        }}));
    </script>"""

    def version_numbers(version: str) -> tuple:
        return tuple(int(number) for number in version.split(".")) + (0,) * (4 - len(version.split(".")))

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            url = urllib.parse.urlsplit(self.path)
            requested.append(url.path)
            status, content_type, body = 200, "text/html", b""
            if url.path == "/page":
                body = page.encode()
            elif url.path == "/service/update2/crx":
                extension = urllib.parse.parse_qs(urllib.parse.parse_qs(url.query)["x"][0])
                id, version_had = extension["id"][0], extension["v"][0]
                version, file = published[id]
                crx = (EXTENSION_STORE_DIR / file).read_bytes()
                if version_numbers(version) > version_numbers(version_had):
                    update_check = (
                        f'<updatecheck codebase="{origin}/crx/{file}" hash_sha256="{hashlib.sha256(crx).hexdigest()}"'
                        f' size="{len(crx)}" status="ok" version="{version}"/>'
                    )
                else:
                    update_check = '<updatecheck status="noupdate"/>'
                content_type = "text/xml"
                body = (
                    '<?xml version="1.0" encoding="UTF-8"?><gupdate xmlns="http://www.google.com/update2/response"'
                    f' protocol="2.0" server="prod"><app appid="{id}" status="ok">{update_check}</app></gupdate>'
                ).encode()
            elif url.path.startswith("/crx/") and "/" not in url.path.removeprefix("/crx/"):
                content_type, body = (
                    "application/x-chrome-extension",
                    (EXTENSION_STORE_DIR / url.path.removeprefix("/crx/")).read_bytes(),
                )
            else:
                status = 404
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f"http://127.0.0.1:{server.server_address[1]}"
    store_env = {
        **env,
        "XENON_EXTENSION_STORE_URL": f"{origin}/service/update2/crx",
        "XENON_EXTENSION_STORE_PUBLISHER_KEY_HASH": keys["publisherKeyHash"],
        "XENON_PASSWORD_MANAGER_IDS": f"bitwarden={bitwarden},proton-pass={proton_pass}",
    }
    no_report = "(no report)"

    def run(profile: str, *args: str, tries: int = 0, linger: int = 0) -> tuple[str | None, str]:
        requested.clear()
        command = [
            str(executable),
            "--headless=manual",
            "--profile-path",
            profile,
            *args,
            f"{origin}/page?tries={tries}&linger={linger}",
        ]
        try:
            result = subprocess.run(command, env=store_env, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired:
            return no_report, "timed out"
        output = result.stdout + result.stderr
        for line in output.splitlines():
            if PASSWORD_MANAGER_TEST_MARKER in line:
                return json.loads(line.split(PASSWORD_MANAGER_TEST_MARKER, 1)[1].rstrip().removesuffix('"')), output
        return no_report, output

    passed = True

    def expect(step: str, ok: bool, output: str, details: str = ""):
        nonlocal passed
        if ok:
            print(f"    {step}")
            return
        print(output[-4000:], file=sys.stderr)
        print(f"    FAIL: {step} {details}", file=sys.stderr)
        passed = False

    try:
        with tempfile.TemporaryDirectory() as profile:

            def installed() -> dict:
                return {
                    id_directory.name: sorted(version.name for version in id_directory.iterdir())
                    for directory in Path(profile).rglob("Extensions/Installed")
                    for id_directory in directory.iterdir()
                }

            def a_day_passes():
                for file in Path(profile).rglob("Extensions/password-manager-updates.json"):
                    file.write_text(json.dumps({"checkedAt": 0}))

            def downloads() -> list[str]:
                return [path for path in requested if path.startswith("/crx/")]

            found, output = run(profile, "--password-manager=not-a-manager")
            expect(
                "names the offered password managers, 1Password included",
                "There's no password manager called not-a-manager" in output
                and "1password" in output
                and "bitwarden" in output
                and "proton-pass" in output,
                output,
            )

            found, output = run(profile, "--password-manager=bitwarden", tries=40)
            expect(
                "installs the chosen one from the store, and it runs at once",
                found == "Test Bitwarden 1.0" and installed() == {bitwarden: ["1.0"]},
                output,
                f"{found}, {installed()}",
            )

            found, output = run(profile)
            expect(
                "loads it at the next start, without downloading it again",
                found == "Test Bitwarden 1.0" and not downloads(),
                output,
                f"{found}, fetched {downloads()}",
            )

            for directory in Path(profile).rglob(f"Extensions/Installed/{bitwarden}"):
                shutil.rmtree(directory)
            found, output = run(profile, tries=40)
            expect(
                "installs it again if its files are gone",
                found == "Test Bitwarden 1.0"
                and downloads() == ["/crx/bitwarden-1.0.crx"]
                and installed() == {bitwarden: ["1.0"]},
                output,
                f"{found}, fetched {downloads()}, {installed()}",
            )

            a_day_passes()
            published[bitwarden] = ("2.0", "bitwarden-2.0.crx")
            found, output = run(profile, linger=8000)
            expect(
                "downloads the next day's new version, and keeps the one running",
                found == "Test Bitwarden 1.0" and downloads() == ["/crx/bitwarden-2.0.crx"],
                output,
                f"{found}, fetched {downloads()}",
            )
            found, output = run(profile)
            expect(
                "runs the new version from the next start, and deletes the old one",
                found == "Test Bitwarden 2.0" and installed() == {bitwarden: ["2.0"]},
                output,
                f"{found}, {installed()}",
            )

            a_day_passes()
            published[bitwarden] = ("3.0", "bitwarden-3.0-tampered.crx")
            found, output = run(profile, linger=8000)
            tampered_output = output
            found, output = run(profile)
            expect(
                "turns down a version whose signature doesn't match, and keeps the one it has",
                "signatures isn't valid" in tampered_output
                and found == "Test Bitwarden 2.0"
                and installed() == {bitwarden: ["2.0"]},
                tampered_output + output,
                f"{found}, {installed()}",
            )

            found, output = run(profile, "--password-manager=proton-pass", tries=40)
            expect(
                "switches to another, and removes the first",
                found == "Test Proton Pass 1.0" and installed() == {proton_pass: ["1.0"]},
                output,
                f"{found}, {installed()}",
            )

            published[bitwarden] = ("3.0", "bitwarden-3.0-unpublished.crx")
            found, output = run(profile, "--password-manager=bitwarden", tries=8)
            expect(
                "turns down one the store didn't sign, and keeps the one it has",
                "isn't signed by the Chrome Web Store" in output
                and found == "Test Proton Pass 1.0"
                and installed() == {proton_pass: ["1.0"]},
                output,
                f"{found}, {installed()}",
            )

            found, output = run(profile, "--password-manager=none")
            expect(
                "removes it when none is chosen", found is None and installed() == {}, output, f"{found}, {installed()}"
            )

            if sys.platform != "darwin":
                found, output = run(profile, "--password-manager=icloud-passwords")
                expect(
                    "does not offer iCloud Passwords off macOS",
                    "available on macOS" in output and found is None and installed() == {},
                    output,
                    f"{found}, {installed()}",
                )
    finally:
        server.shutdown()
    return passed


def unpacked_extension_id(directory: Path) -> str:
    digest = hashlib.sha256(str(directory.resolve()).encode()).hexdigest()[:32]
    return "".join(chr(ord("a") + int(digit, 16)) for digit in digest)


def run_extension_test_page(
    executable: Path, env: dict, url: str, profile: str | None = None, arguments: tuple[str, ...] = ()
) -> dict | None:
    with tempfile.TemporaryDirectory() as temporary_profile:
        if not profile:
            turn_on_content_blocking(temporary_profile)
        command = [
            str(executable),
            "--headless=manual",
            "--expose-internals-object",
            "--profile-path",
            profile or temporary_profile,
            f"--load-extension={TEST_EXTENSION_DIR}",
            *arguments,
            url,
        ]
        try:
            result = subprocess.run(
                command, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120
            )
        except subprocess.TimeoutExpired:
            print(f"    timed out on {url}", file=sys.stderr)
            return None

    output = result.stdout + result.stderr
    if result.returncode != 0:
        print(output[-4000:], file=sys.stderr)
        print(f"    the browser exited with {result.returncode} on {url}", file=sys.stderr)
        return None
    for line in output.splitlines():
        if EXTENSION_TEST_MARKER in line:
            return json.loads(line.split(EXTENSION_TEST_MARKER, 1)[1].rstrip().removesuffix('"'))
    print(output[-4000:], file=sys.stderr)
    return None


def turn_on_content_blocking(profile: str):
    (Path(profile) / "config").mkdir(exist_ok=True)
    (Path(profile) / "config" / "Settings.json").write_text(json.dumps({"contentBlockers": {"enabled": True}}))


def write_native_messaging_host(directory: Path, name: str, allowed_origin: str, executable: Path) -> None:
    (directory / f"{name}.json").write_text(
        json.dumps(
            {
                "name": name,
                "description": "Xenon native messaging test host",
                "path": str(executable),
                "type": "stdio",
                "allowed_origins": [allowed_origin],
            }
        )
        + "\n"
    )


def check_extensions(executable: Path, env: dict) -> bool:
    extension_id = unpacked_extension_id(TEST_EXTENSION_DIR)
    passed = True
    hosts_directory = Path(tempfile.mkdtemp(prefix="xenon-native-messaging-"))
    echo_host = NATIVE_MESSAGING_ECHO_HOST.resolve()
    echo_host.chmod(echo_host.stat().st_mode | 0o111)
    origin = f"chrome-extension://{extension_id}/"
    write_native_messaging_host(hosts_directory, "com.xenon.test", origin, echo_host)
    write_native_messaging_host(
        hosts_directory, "com.xenon.forbidden", "chrome-extension://notthisextension/", echo_host
    )
    env = {**env, "XENON_NATIVE_MESSAGING_HOSTS": str(hosts_directory)}

    page = f"""<!doctype html><script>
        const results = {{ "no chrome.runtime": typeof chrome === "undefined" || !chrome.runtime }};
        const text = document.createElement("textarea");
        document.documentElement.append(text);
        text.focus();
        text.value = "web page";
        text.select();
        results["no copy without a user gesture"] = !document.execCommand("copy");
        text.value = "";
        results["no paste"] = !document.execCommand("paste") && text.value === "";
        const base = "chrome-extension://{extension_id}/";
        const image_fails = src => new Promise(resolve => {{
            const img = new Image();
            img.onload = () => resolve(false);
            img.onerror = () => resolve(true);
            img.src = src;
        }});
        Promise.all([
            fetch(base + "manifest.json").then(() => false, () => true),
            image_fails(base + "popup.css"),
            fetch(new URLSearchParams(location.search).get("data")).then(() => false, () => true),
            image_fails("/site-tracking-pixel.gif"),
        ]).then(([fetch_failed, image_failed, data_fetch_failed, tracker_failed]) => {{
            results["fetch blocked"] = fetch_failed;
            results["subresource blocked"] = image_failed;
            results["CORS still applies to web pages"] = data_fetch_failed;
            results["the content blocker still applies to web pages"] = tracker_failed;
            console.log("{EXTENSION_TEST_MARKER}" + JSON.stringify(results));
            window.close();
        }});
    </script>""".encode()
    data = b'{"ok": true}'
    gif = bytes.fromhex("47494638396101000100800000000000ffffff21f90401000000002c00000000010001000002024401003b")
    content_script_page = (XENON_SOURCE_DIR / "Tests" / "Extensions" / "content-scripts.html").read_bytes()
    connectable_page = (XENON_SOURCE_DIR / "Tests" / "Extensions" / "externally-connectable.html").read_bytes()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/data.json":
                body, content_type = data, "application/json"
            elif self.path == "/site-tracking-pixel.gif":
                body, content_type = gif, "image/gif"
            elif self.path.startswith("/content-scripts"):
                body, content_type = content_script_page, "text/html"
            elif self.path.startswith("/connectable"):
                body, content_type = connectable_page, "text/html"
            elif self.path.startswith("/frame"):
                body, content_type = b"<!doctype html><p>A frame of the content script page.</p>", "text/html"
            else:
                body, content_type = page, "text/html"
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    page_server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    data_server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    for server in (page_server, data_server):
        threading.Thread(target=server.serve_forever, daemon=True).start()
    data_url = f"http://127.0.0.1:{data_server.server_address[1]}/data.json"

    try:
        with tempfile.TemporaryDirectory() as profile:
            turn_on_content_blocking(profile)
            for run, expected_event in enumerate(("installed:install", "startup"), 1):
                url = f"chrome-extension://{extension_id}/tests.html?data={urllib.parse.quote(data_url, safe='')}"
                results = run_extension_test_page(executable, env, url, profile)
                if results is None:
                    return False
                failed = sorted(
                    name for name, ok in results.items() if ok is not True and not name.startswith("info: ")
                )
                if expected_event not in (results.get("info: background events") or []):
                    failed.append(f"background got {expected_event}")
                checks = sum(1 for name in results if not name.startswith("info: "))
                if failed:
                    print(f"    extension page, run {run}: failed {failed}", file=sys.stderr)
                    passed = False
                else:
                    print(
                        f"    extension page, run {run}: {checks} checks passed, and the background got {expected_event}"
                    )

        page_url = f"http://127.0.0.1:{page_server.server_address[1]}/?data={urllib.parse.quote(data_url, safe='')}"
        results = run_extension_test_page(executable, env, page_url)

        content_script_url = f"http://127.0.0.1:{page_server.server_address[1]}/content-scripts"
        content_script_results = run_extension_test_page(executable, env, content_script_url)

        connectable_url = f"http://127.0.0.1:{page_server.server_address[1]}/connectable?id={extension_id}"
        connectable_results = run_extension_test_page(executable, env, connectable_url)

        idle_url = f"chrome-extension://{extension_id}/idle.html"
        idle_results = run_extension_test_page(executable, env, idle_url, arguments=("--extension-idle-timeout=2",))

        stopped_results = check_stopped_background(executable, env, extension_id)
    finally:
        for server in (page_server, data_server):
            server.shutdown()
        shutil.rmtree(hosts_directory, ignore_errors=True)

    for name, page_results in (
        ("web page", results),
        ("content scripts", content_script_results),
        ("externally connectable page", connectable_results),
        ("idle background", idle_results),
        ("stopped background", stopped_results),
    ):
        if page_results is None:
            return False
        checks = {check: ok for check, ok in page_results.items() if not check.startswith("info: ")}
        failed = sorted(check for check, ok in checks.items() if ok is not True)
        if failed:
            print(f"    {name}: failed {failed}", file=sys.stderr)
            passed = False
        else:
            print(f"    {name}: {len(checks)} checks passed")
    return passed


def check_stopped_background(executable: Path, env: dict, extension_id: str) -> dict | None:
    site_argument = f"--site chrome-extension://{extension_id}"
    with tempfile.TemporaryDirectory() as profile:
        turn_on_content_blocking(profile)
        process = subprocess.Popen(
            [
                str(executable),
                "--headless=manual",
                "--profile-path",
                profile,
                f"--load-extension={TEST_EXTENSION_DIR}",
                "--extension-idle-timeout=2",
                "about:blank",
            ],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )

        def request_server_running() -> bool:
            return any(site_argument in command for command in descendant_command_lines(process.pid))

        def wait_for(condition, seconds: int) -> bool:
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline and process.poll() is None:
                if condition():
                    return True
                time.sleep(0.1)
            return False

        try:
            started = wait_for(request_server_running, 60)
            stopped = started and wait_for(lambda: not request_server_running(), 30)
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()

    return {
        "its extension's site has a RequestServer while it runs": started,
        "that RequestServer goes with it": stopped,
    }


def descendant_command_lines(root: int) -> list[str]:
    return [command for _, command in descendant_processes(root)]


def helper_pids(root: int, *names: str) -> list[int]:
    executable = re.compile(rf"^(?:.*/)?(?:{'|'.join(re.escape(name) for name in names)})(?:\s|$)")
    return [pid for pid, command in descendant_processes(root) if executable.match(command)]


def descendant_processes(root: int) -> list[tuple[int, str]]:
    processes: list[tuple[int, int, str]] = []
    if Path("/proc/self/stat").exists():
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                parent = int((entry / "stat").read_text().rsplit(")", 1)[1].split()[1])
                command = (entry / "cmdline").read_bytes().replace(b"\0", b" ").decode(errors="replace")
            except (OSError, IndexError, ValueError):
                continue
            processes.append((int(entry.name), parent, command))
    else:
        output = subprocess.run(["ps", "-A", "-o", "pid=,ppid=,args="], capture_output=True, text=True).stdout
        for line in output.splitlines():
            pid, parent, *command = line.split(None, 2)
            processes.append((int(pid), int(parent), command[0] if command else ""))

    children: dict[int, list[tuple[int, str]]] = {}
    for pid, parent, command in processes:
        children.setdefault(parent, []).append((pid, command))
    descendants = []
    pending = [root]
    while pending:
        for pid, command in children.get(pending.pop(), []):
            descendants.append((pid, command))
            pending.append(pid)
    return descendants


def check_blocking_dashboard(executable: Path, env: dict) -> bool:
    titles = [entry["title"] for entry in json.loads(FILTER_LIST_MANIFEST.read_text())["lists"]]
    with tempfile.TemporaryDirectory() as profile:
        loaded, output = run_headless(
            executable, env, ["--headless=text", "--profile-path", profile, BLOCKING_DASHBOARD_URL]
        )
    missing = [title for title in titles if title not in output]
    if not loaded or missing or f"of {len(titles)} lists in use" not in output:
        print(output[-4000:], file=sys.stderr)
        print(f"    missing: {missing}", file=sys.stderr)
        return False
    print(f"    shows all {len(titles)} bundled lists")
    return True


SESSION_RESTORE_EXTENSION_MANIFEST = {
    "manifest_version": 3,
    "name": "Xenon session restore test",
    "version": "1.0",
    "permissions": ["tabs"],
    "host_permissions": ["http://127.0.0.1/*"],
    "background": {"service_worker": "background.js"},
}
SESSION_RESTORE_EXTENSION_BACKGROUND = """
    async function report() {
        const tabs = (await chrome.tabs.query({})).sort((a, b) => a.windowId - b.windowId || a.index - b.index);
        const summary = tabs.map(tab => ({ url: tab.url, active: tab.active, status: tab.status, discarded: tab.discarded }));
        const response = await fetch("REPORT_URL?tabs=" + encodeURIComponent(JSON.stringify(summary)));
        for (const { call, tab, args } of await response.json())
            await chrome.tabs[call](...(tab === undefined ? [] : [tabs[tab].id]), ...args);
    }
    (async () => {
        for (;;) {
            await report().catch(error => console.log(`Session restore test: ${error}`));
            await new Promise(resolve => setTimeout(resolve, 500));
        }
    })();
"""


def check_session_restore(executable: Path, env: dict) -> bool:
    state = {"tabs": None, "commands": [], "requests": []}
    changed = threading.Condition()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path, _, query = self.path.partition("?")
            with changed:
                if path == "/report":
                    state["tabs"] = json.loads(urllib.parse.parse_qs(query)["tabs"][0])
                    body, content_type = json.dumps(state["commands"]).encode(), "application/json"
                    state["commands"] = []
                else:
                    state["requests"].append(path)
                    body, content_type = f"<!doctype html><title>{path}</title><p>{path}".encode(), "text/html"
                changed.notify_all()
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f"http://127.0.0.1:{server.server_address[1]}"
    pages = [f"{origin}/page-{number}" for number in (1, 2, 3)]

    def urls(tabs) -> list[str | None]:
        return [tab.get("url") for tab in tabs or []]

    def active_index(tabs) -> int | None:
        return next((index for index, tab in enumerate(tabs or []) if tab["active"]), None)

    def loaded(tabs) -> bool:
        return all(tab["status"] == "complete" for tab in tabs or [])

    with tempfile.TemporaryDirectory() as directory:
        profile = Path(directory) / "profile"
        (profile / "config").mkdir(parents=True)
        (profile / "config" / "Settings.json").write_text(
            json.dumps({"restorePreviousSession": True, "firstRunComplete": True})
        )
        extension = Path(directory) / "extension"
        extension.mkdir()
        (extension / "manifest.json").write_text(json.dumps(SESSION_RESTORE_EXTENSION_MANIFEST))
        (extension / "background.js").write_text(
            SESSION_RESTORE_EXTENSION_BACKGROUND.replace("REPORT_URL", f"{origin}/report")
        )
        log_path = Path(directory) / "xenon.log"
        process = None

        def start(*args: str):
            nonlocal process
            with changed:
                state["tabs"] = None
                state["requests"] = []
            with open(log_path, "w") as log:
                process = subprocess.Popen(
                    [str(executable), "--profile-path", str(profile), f"--load-extension={extension}", *args],
                    env=env,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )

        def stop(how: signal.Signals) -> int | None:
            if how == signal.SIGTERM:
                process.terminate()
            try:
                returncode = process.wait(timeout=60) if how == signal.SIGTERM else None
            except subprocess.TimeoutExpired:
                returncode = None
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
            process.wait()
            return returncode

        def wait_for(step: str, condition, commands: list | None = None) -> bool:
            deadline = time.monotonic() + 120
            with changed:
                state["commands"] = commands or []
                while not condition(state["tabs"]):
                    if time.monotonic() > deadline or process.poll() is not None:
                        print(log_path.read_text(errors="replace")[-4000:], file=sys.stderr)
                        print(f"    {step}: no, the tabs were {state['tabs']}", file=sys.stderr)
                        return False
                    changed.wait(1)
            print(f"    {step}")
            return True

        try:
            start(pages[0])
            if not wait_for("the first page loads", lambda tabs: urls(tabs) == pages[:1] and loaded(tabs)):
                return False
            commands = [
                {"call": "create", "args": [{"url": pages[1], "active": False}]},
                {"call": "duplicate", "tab": 0, "args": []},
            ]
            expected = [pages[0], pages[0], pages[1]]
            if not wait_for(
                "a page opens and one is duplicated", lambda tabs: urls(tabs) == expected and loaded(tabs), commands
            ):
                return False
            time.sleep(SESSION_SAVE_WAIT_SECONDS)
            stop(signal.SIGKILL)

            start()
            if not wait_for(
                "after a crash, the tabs come back with their pages",
                lambda tabs: urls(tabs) == expected and active_index(tabs) == 1,
            ):
                return False
            commands = [{"call": "update", "tab": 2, "args": [{"active": True}]}]
            if not wait_for(
                "a restored tab loads its page when it's selected",
                lambda tabs: (
                    urls(tabs) == expected
                    and active_index(tabs) == 2
                    and not tabs[2]["discarded"]
                    and loaded(tabs)
                    and "/page-2" in state["requests"]
                ),
                commands,
            ):
                return False
            commands = [{"call": "create", "args": [{"url": pages[2]}]}]
            expected = [pages[0], pages[0], pages[1], pages[2]]
            if not wait_for("another page opens", lambda tabs: urls(tabs) == expected and loaded(tabs), commands):
                return False
            if (returncode := stop(signal.SIGTERM)) != 0:
                print(log_path.read_text(errors="replace")[-4000:], file=sys.stderr)
                print(f"    Xenon exited with {returncode} when it was asked to quit", file=sys.stderr)
                return False

            start()
            passed = wait_for(
                "after a quit, the tabs come back with their pages",
                lambda tabs: urls(tabs) == expected and active_index(tabs) == 3,
            )
            stop(signal.SIGTERM)
            return passed
        finally:
            if process and process.poll() is None:
                stop(signal.SIGKILL)
            server.shutdown()


def check_update_button(executable: Path, env: dict) -> bool:
    expected = UPDATE_BUTTON_SHOWN + UPDATE_STAND_IN_VERSION
    with tempfile.TemporaryDirectory() as directory:
        log_path = Path(directory) / "xenon.log"
        with open(log_path, "w") as log:
            process = subprocess.Popen(
                [str(executable), "--profile-path", str(Path(directory) / "profile"), "about:blank"],
                env={**env, "XENON_TEST_UPDATE_READY": UPDATE_STAND_IN_VERSION},
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        offered = False
        try:
            deadline = time.monotonic() + 60
            while not offered and time.monotonic() < deadline and process.poll() is None:
                time.sleep(0.5)
                offered = expected in log_path.read_text(errors="replace")
        finally:
            process.terminate()
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                pass
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
            process.wait()
        if not offered:
            print(log_path.read_text(errors="replace")[-4000:], file=sys.stderr)
            print(f'    no "{expected}"', file=sys.stderr)
            return False
    print(
        f"    the Update button shows for Xenon {UPDATE_STAND_IN_VERSION}, which XENON_TEST_UPDATE_READY stands in for"
    )
    return True


def check_certificate_errors(executable: Path, env: dict) -> bool:
    page = f"""<!doctype html><title>Xenon certificate test</title>
    <link rel="stylesheet" href="/style.css">
    <script src="/script.js"></script>
    <script>
        const report = {{ loaded: location.href, script: window.scriptResult ?? null }};
        const failure = error => `failed: ${{String(error).replaceAll('"', "'")}}`;
        async function attempt(name, request) {{
            try {{
                report[name] = await request();
            }} catch (error) {{
                report[name] = failure(error);
            }}
        }}
        addEventListener("load", async () => {{
            report.stylesheet = getComputedStyle(document.body).color;
            await attempt("fetch", async () => (await fetch("/data.json")).json());
            await attempt("xhr", () => new Promise((resolve, reject) => {{
                const request = new XMLHttpRequest();
                request.open("GET", "/data.json?xhr");
                request.onload = () => resolve(JSON.parse(request.responseText));
                request.onerror = () => reject("error");
                request.send();
            }}));
            await attempt("webSocket", () => new Promise((resolve, reject) => {{
                const socket = new WebSocket(`wss://${{location.host}}/socket`);
                socket.onmessage = event => resolve(event.data);
                socket.onclose = event => reject(`closed with ${{event.code}}`);
                setTimeout(() => reject("timed out"), 10000);
            }}));
            console.log("{CERTIFICATE_ERROR_TEST_MARKER}" + JSON.stringify(report));
            window.close();
        }});
    </script>
    <body>Xenon certificate test</body>""".encode()
    script = b"""
        function callOverridden() { return callOverridden.caller.caller.name; }
        function callParent() { return callOverridden.apply(this, arguments); }
        function overridingMethod() { return callParent(); }
        try {
            window.scriptResult = overridingMethod();
        } catch (error) {
            window.scriptResult = `failed: ${String(error).replaceAll('"', "'")}`;
        }
    """
    resources = {
        "/": ("text/html", page),
        "/style.css": ("text/css", b"body { color: rgb(1, 2, 3); }"),
        "/script.js": ("text/javascript", script),
        "/data.json": ("application/json", b'{"ok": true}'),
    }
    web_socket_message = "hello over the accepted certificate"
    requested: list[str] = []

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            requested.append(self.path)
            if self.path == "/socket":
                self.answer_web_socket()
                return
            content_type, body = resources.get(urllib.parse.urlsplit(self.path).path, resources["/"])
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def answer_web_socket(self):
            key = self.headers.get("Sec-WebSocket-Key", "") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", base64.b64encode(hashlib.sha1(key.encode()).digest()).decode())
            self.end_headers()
            message = web_socket_message.encode()
            self.wfile.write(bytes([0x81, len(message)]) + message)
            self.wfile.flush()
            time.sleep(5)
            self.close_connection = True

        def log_message(self, *args):
            pass

    class Server(http.server.ThreadingHTTPServer):
        def handle_error(self, request, client_address):
            pass

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(CERTIFICATE_ERRORS_DIR / "self-signed.pem", CERTIFICATE_ERRORS_DIR / "self-signed-key.pem")
    server = Server(("127.0.0.1", 0), Handler)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"https://127.0.0.1:{server.server_address[1]}/"
    try:
        with tempfile.TemporaryDirectory() as profile:
            command = [str(executable), "--headless=manual", "--profile-path", profile, url]
            test_env = {**env, "XENON_TEST_PROCEED_PAST_CERTIFICATE_ERRORS": "1"}
            try:
                result = subprocess.run(
                    command, env=test_env, capture_output=True, encoding="utf-8", errors="replace", timeout=60
                )
                output = result.stdout + result.stderr
            except subprocess.TimeoutExpired as error:
                output = "".join(
                    part.decode(errors="replace") if isinstance(part, bytes) else part
                    for part in (error.stdout, error.stderr)
                    if part
                )
                output += f"\ntimed out after {error.timeout} s"
    finally:
        server.shutdown()

    reports = certificate_error_reports(output)
    warning = next((report for report in reports if "problems" in report), None)
    loaded = next((report for report in reports if "loaded" in report), None)

    passed = True

    def expect(step: str, ok: bool, details: str):
        nonlocal passed
        if ok:
            print(f"    {step}")
            return
        print(f"    FAIL: {step} ({details})", file=sys.stderr)
        passed = False

    expect(
        "shows the warning, saying the certificate is self-signed",
        warning is not None and warning["problems"] == ["selfSigned"] and warning["canProceed"] is True,
        f"the warning reported {warning}",
    )
    expect(
        "proceeds to the site, which loads",
        loaded is not None and loaded["loaded"] == url and "/" in requested,
        f"the page reported {loaded}, and the server was asked for {requested}",
    )
    loaded = loaded or {}
    expect(
        "the site's stylesheet, fetch(), XMLHttpRequest and WebSocket go over the accepted certificate",
        loaded.get("stylesheet") == "rgb(1, 2, 3)"
        and loaded.get("fetch") == {"ok": True}
        and loaded.get("xhr") == {"ok": True}
        and loaded.get("webSocket") == web_socket_message,
        f"the page reported {loaded}, and the server was asked for {requested}",
    )
    expect(
        "its script runs, and finds the caller of a function called through apply(), as Ext JS does",
        loaded.get("script") == "overridingMethod",
        f"the script reported {loaded.get('script')}",
    )
    with tempfile.TemporaryDirectory() as profile:
        warning_url = "xenon://certificate-error/?url=" + urllib.parse.quote(url, safe="")
        command = [str(executable), "--headless=manual", "--profile-path", profile, warning_url]
        try:
            result = subprocess.run(
                command, env=test_env, capture_output=True, encoding="utf-8", errors="replace", timeout=60
            )
            direct_output = result.stdout + result.stderr
        except subprocess.TimeoutExpired:
            direct_output = "timed out"
    direct = next((report for report in certificate_error_reports(direct_output) if "problems" in report), None)
    expect(
        "opened at its address, the warning has nothing to proceed past",
        direct is not None and direct["canProceed"] is False,
        f"the warning reported {direct}: {direct_output[-2000:]}",
    )

    if not passed:
        for line in output.splitlines():
            if CERTIFICATE_ERROR_TEST_MARKER in line or "VERIFICATION FAILED" in line or "Failed load" in line:
                print(line, file=sys.stderr)
        print(output[-4000:], file=sys.stderr)
    return passed


def certificate_error_reports(output: str) -> list[dict]:
    return [
        json.loads(line.split(CERTIFICATE_ERROR_TEST_MARKER, 1)[1].rstrip().removesuffix('"'))
        for line in output.splitlines()
        if CERTIFICATE_ERROR_TEST_MARKER in line
    ]


def run_headless(executable: Path, env: dict, args: list[str]) -> tuple[bool, str]:
    try:
        result = subprocess.run([str(executable), *args], env=env, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired as error:
        parts = (error.stdout, error.stderr)
        output = "".join(part.decode(errors="replace") if isinstance(part, bytes) else part for part in parts if part)
        return False, f"{output}\ntimed out after {error.timeout} s"
    output = result.stdout + result.stderr
    if result.returncode != 0:
        how = (
            f"was killed by signal {-result.returncode}"
            if result.returncode < 0
            else f"exited with {result.returncode}"
        )
        output += f"\nXenon {how}"
    return result.returncode == 0, output


def run_command(command: list):
    try:
        subprocess.check_call([str(part) for part in command])
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
