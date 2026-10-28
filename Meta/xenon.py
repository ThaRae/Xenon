#!/usr/bin/env python3

# Builds and runs Xenon: Ladybird's engine (the `ladybird` submodule) with Xenon's own frontend in `UI/`.
#
# Ladybird is configured from its own source tree, and Meta/CMake/XenonHook.cmake is injected after its
# project() call so the Xenon frontend is added once every engine library and helper process exists. Xenon's patches
# to the engine (Patches/ladybird) are applied first, if ladybird/ doesn't have them yet; see Meta/patches.py.

from __future__ import annotations

import argparse
import hashlib
import http.server
import json
import os
import shutil
import signal
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

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

# A page rendered with nothing on it compresses to a few kilobytes; the welcome page is well over this.
SMOKE_TEST_MIN_SCREENSHOT_BYTES = 20_000

# Text on Xenon's welcome page that Ladybird's doesn't have.
WELCOME_PAGE_TEXT = "Use Xenon's picks"

# The headless screenshot is taken this long after the load starts, not after it finishes.
WELCOME_SCREENSHOT_DELAY_SECONDS = 4

# Requests the bundled filter lists block (EasyList: /jquery.popunder.js; EasyPrivacy: -tracking-pixel.), and ordinary
# ones nothing should block.
BLOCKED_REQUESTS = {"/jquery.popunder.js": "script", "/site-tracking-pixel.gif": "img"}
ALLOWED_REQUESTS = {"/app.js": "script", "/logo.gif": "img"}

# A page at a site the bundled lists have a scriptlet and a hiding rule for (AdGuard's list:
# citationmachine.net##+js(set-constant, canRunAds, true); AdGuard's and EasyList: citationmachine.net##.adds-wrapper).
# Xenon serves it from a local file at the site's own address (--resource-map), so nothing goes to the network. The
# page writes what it found into its text, on a line starting with FILTERED_PAGE_MARKER, which a text-mode run prints.
FILTERED_PAGE_URL = "https://citationmachine.net/"
FILTERED_PAGE_MARKER = "XENON-CONTENT-BLOCKING "
FILTERED_PAGE = f"""<!doctype html><title>Xenon content blocking</title>
<div class="adds-wrapper">ad</div><div class="article">article</div>
<script>
    // The site's ad script would set this; its adblock detection reads it.
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

# The blocking dashboard (UI/BlockingDashboard.cpp), whose page shows the bundled lists once Xenon sends them.
BLOCKING_DASHBOARD_URL = "xenon://blocking/"
FILTER_LIST_MANIFEST = XENON_SOURCE_DIR / "UI" / "Resources" / "filter-lists" / "manifest.json"

# Bundles that a local server publishes to test filter list updates, signed with a key of their own
# (Tests/FilterListUpdates/make-bundles.py). Each test page prints what it found on a line starting with the marker.
FILTER_LIST_UPDATES_DIR = XENON_SOURCE_DIR / "Tests" / "FilterListUpdates"
FILTER_LIST_UPDATES_MARKER = "XENON-FILTER-LIST-UPDATES-TEST "

# A test extension whose pages check the chrome.* APIs and print what they found on a line starting with this.
TEST_EXTENSION_DIR = XENON_SOURCE_DIR / "Tests" / "Extensions" / "basic"
EXTENSION_TEST_MARKER = "XENON-EXTENSION-TEST "

# A local HTTPS server's self-signed certificate (Tests/CertificateErrors). Xenon's certificate warning and the server's
# page each print what they found on a line starting with the marker.
CERTIFICATE_ERRORS_DIR = XENON_SOURCE_DIR / "Tests" / "CertificateErrors"
CERTIFICATE_ERROR_TEST_MARKER = "XENON-CERTIFICATE-ERROR-TEST "

# A page that tries WebAuthn and prints what it found on a line starting with this.
WEB_AUTHN_TEST_MARKER = "XENON-WEBAUTHN-TEST "

# Xenon saves the session a second after a change (UI/SessionSaving.cpp). The session restore check waits this long after
# its changes before it kills Xenon.
SESSION_SAVE_WAIT_SECONDS = 3

# Stand-ins for the password managers, served from a stand-in for the Chrome Web Store and signed with keys of their own
# (Tests/ExtensionStore/make-crxs.py). The page that looks for them prints what it found on a line starting with this.
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
    subparsers.add_parser("clean", help="Delete the build directory for the preset")

    args = parser.parse_args()
    platform = Platform()

    if args.command == "clean":
        build_dir, _ = configure_build_env(args.preset, args.jobs)
        shutil.rmtree(build_dir, ignore_errors=True)
        return

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
    # On Linux, Xenon has an install prefix of its own in the build directory (see UI/Xenon.cmake).
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

    for test in sorted((build_dir / "bin").glob("TestXenon*")):
        if test.is_dir() or not os.access(test, os.X_OK):
            continue
        print(f"==> {test.name}", flush=True)
        if subprocess.run([test]).returncode != 0:
            failures.append(test.name)

    failures += check_app(platform, app_executable(platform, build_dir), build_dir / "smoke")

    if failures:
        print(f"Failed: {', '.join(failures)}", file=sys.stderr)
        sys.exit(1)
    print("All tests passed.")


# The checks that run Xenon itself, and return what failed: the build's, or a release's, which Meta/release.py test runs
# them against. The welcome page's render goes in smoke_dir.
def check_app(platform: Platform, executable: Path, smoke_dir: Path) -> list[str]:
    failures = []

    # Rendering the welcome page without a window runs Xenon's bundled resources, the WebUI bridge, and the engine's
    # helper processes together, which unit tests can't.
    smoke_dir.mkdir(parents=True, exist_ok=True)
    screenshot = smoke_dir / "welcome.png"
    screenshot.unlink(missing_ok=True)

    # The session restore check opens Xenon's windows. CI gives Linux a real X server for them (Meta/ci/start-xvfb.sh);
    # without a display, they go to Qt's offscreen platform.
    env = dict(os.environ)
    if platform.host_system != HostSystem.macOS and not env.get("DISPLAY") and not env.get("WAYLAND_DISPLAY"):
        env["QT_QPA_PLATFORM"] = "offscreen"

    print("==> headless render of about:welcome", flush=True)
    with tempfile.TemporaryDirectory() as profile:
        # Text mode waits for the page to finish loading, so this checks the page itself whatever the machine's speed.
        # It also gets the first launch's one-off work, like building the font cache, out of the screenshot's time.
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

    print("==> a fresh start keeps to Xenon's directories", flush=True)
    if not check_profile_directory(platform, executable, env):
        failures.append("profile directory")

    print("==> content blocking with the bundled filter lists", flush=True)
    if not check_content_blocking(executable, env):
        failures.append("content blocking")

    print("==> daily filter list updates from a local server", flush=True)
    if not check_filter_list_updates(executable, env):
        failures.append("filter list updates")

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

    print("==> WebAuthn requests reach the UI process", flush=True)
    if not check_web_authn(executable, env):
        failures.append("WebAuthn")

    print("==> password managers from a stand-in for the Chrome Web Store", flush=True)
    if not check_password_managers(executable, env):
        failures.append("password managers")

    return failures


# Starts Xenon as on a fresh install, with no profile arguments and a home directory of its own, and checks that its
# profile and crash reports go in Xenon's directories and that nothing goes in Ladybird's.
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
            # The runtime directory isn't under the home directory by default. Moving it moves Wayland's socket too.
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


# Loads a local page with blocking on and off, in a throwaway profile, and checks which of its requests reach the server,
# then loads FILTERED_PAGE and checks that a scriptlet and a hiding rule apply only with blocking on. Xenon is given no
# lists here, so this also proves it loads its bundled ones, and uBlock Origin's resources, by itself.
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


# Daily filter list updates (UI/FilterListUpdates.h), from a local server publishing the bundles in
# Tests/FilterListUpdates, through one profile's life:
#   1. it takes v1 while it runs, and blocks what v1 blocks from then on;
#   2. at its next start it uses v1 in place of the bundled lists, and fetches nothing, since a day hasn't passed;
#   3. a day later it takes v2, downloading only the list that changed;
#   4. it turns down v3, whose list isn't the one its manifest describes, and keeps v2.
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
        # Loads a script until the content blocker stops it, which it does once the update is in.
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
        # Loads each script once, then stays open past the time a check would start (FIRST_CHECK_DELAY).
        "/wait": f"""{report}
            (async () => {{
                const results = {{}};
                for (const src of sources)
                    results[src] = await load(src);
                setTimeout(() => report(results), 8000);
            }})();""",
    }

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path = urllib.parse.urlsplit(self.path).path
            requested.append(path)
            status, content_type, body = 200, "application/javascript", b""
            if path in pages:
                content_type, body = "text/html", f"<!doctype html><script>{pages[path]}</script>".encode()
            elif path.startswith("/bundle/"):
                file = FILTER_LIST_UPDATES_DIR / published["version"] / path.removeprefix("/bundle/")
                if ".." in path or not file.is_file():
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
                file = next(Path(profile).rglob("ContentBlocking/Xenon/state.json"))
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


# The WebAuthn engine patches are in the build: the API is there, and requests go to the UI process, which checks them
# against its own records of the page and runs the ceremony. A headless browser has no prompt to ask the user with,
# so it answers as a browser with no authenticators does. The ceremonies themselves are tested in the engine, with
# virtual authenticators (Tests/LibWeb/Text/input/WebAuthn).
def check_web_authn(executable: Path, env: dict) -> bool:
    page = f"""<!doctype html><script>
        const outcome = promise => promise.then(() => "resolved", error => error.name);
        const publicKey = {{
            rp: {{ name: "Xenon" }},
            user: {{ id: new Uint8Array([1]), name: "user", displayName: "User" }},
            challenge: new Uint8Array(32),
            pubKeyCredParams: [{{ type: "public-key", alg: -7 }}],
        }};
        // NB: One at a time, since a window may only have one request for a credential type pending.
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
        # WebAuthn needs a secure context whose host is a domain: localhost is both.
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


# Chrome's ID for an unpacked extension without a key: its directory's path, hashed (see UI/Extensions/Extension.cpp).
# Installs, updates, switches, and removes password managers, as the welcome page and the Extensions window do, through
# --password-manager. A local server answers update checks the way the Chrome Web Store does, with the test CRX files.
def check_password_managers(executable: Path, env: dict) -> bool:
    keys = json.loads((EXTENSION_STORE_DIR / "keys.json").read_text())
    bitwarden, proton_pass = keys["bitwarden"], keys["proton-pass"]
    # The version of each extension the store offers, and its file.
    published = {bitwarden: ("1.0", "bitwarden-1.0.crx"), proton_pass: ("1.0", "proton-pass-1.0.crx")}
    requested: list[str] = []

    # Reloads until a password manager's content script marks the page, up to the number of tries asked for, then stays
    # open as long as asked (past the time the daily update check starts) and reports the mark.
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
                file = next(Path(profile).rglob("Extensions/password-manager-updates.json"))
                file.write_text(json.dumps({"checkedAt": 0}))

            def downloads() -> list[str]:
                return [path for path in requested if path.startswith("/crx/")]

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
    finally:
        server.shutdown()
    return passed


def unpacked_extension_id(directory: Path) -> str:
    digest = hashlib.sha256(str(directory.resolve()).encode()).hexdigest()[:32]
    return "".join(chr(ord("a") + int(digit, 16)) for digit in digest)


# Runs a page in a headless browser until the page closes itself, and returns the results it printed after
# EXTENSION_TEST_MARKER. The profile is a throwaway one with content blocking on, unless one is given. The page gets
# Ladybird's internals object, whose grantTransientActivation() stands in for the user's gesture that some APIs need.
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
            # The browser prints what it would have asked the user, which isn't ASCII.
            result = subprocess.run(
                command, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120
            )
        except subprocess.TimeoutExpired:
            print(f"    timed out on {url}", file=sys.stderr)
            return None

    output = result.stdout + result.stderr
    # The browser has to exit cleanly too: a crash as it closes is still a crash.
    if result.returncode != 0:
        print(output[-4000:], file=sys.stderr)
        print(f"    the browser exited with {result.returncode} on {url}", file=sys.stderr)
        return None
    for line in output.splitlines():
        if EXTENSION_TEST_MARKER in line:
            # The console prints the logged string in quotes.
            return json.loads(line.split(EXTENSION_TEST_MARKER, 1)[1].rstrip().removesuffix('"'))
    print(output[-4000:], file=sys.stderr)
    return None


def turn_on_content_blocking(profile: str):
    (Path(profile) / "config").mkdir(exist_ok=True)
    (Path(profile) / "config" / "Settings.json").write_text(json.dumps({"contentBlockers": {"enabled": True}}))


def check_extensions(executable: Path, env: dict) -> bool:
    extension_id = unpacked_extension_id(TEST_EXTENSION_DIR)
    passed = True

    # A web page, and JSON with no CORS headers, from two local servers: two origins.
    page = f"""<!doctype html><script>
        const results = {{ "no chrome.runtime": typeof chrome === "undefined" || !chrome.runtime }};
        // Only an extension's pages copy and paste from script without a user gesture.
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
    # A 1x1 GIF, at a path the bundled lists block (BLOCKED_REQUESTS).
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
        # An extension page gets chrome.* and its extension's origin, and its background worker runs. The page runs
        # twice in one profile: the first time, the extension is installed; the second, the browser starts up with it.
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

        # A web page gets neither chrome.* nor the extension's files, which aren't web accessible, nor the extension's
        # exemption from CORS.
        page_url = f"http://127.0.0.1:{page_server.server_address[1]}/?data={urllib.parse.quote(data_url, safe='')}"
        results = run_extension_test_page(executable, env, page_url)

        # A web page that the extension's content scripts match gets them, each in its world, at its time.
        content_script_url = f"http://127.0.0.1:{page_server.server_address[1]}/content-scripts"
        content_script_results = run_extension_test_page(executable, env, content_script_url)

        # A web page that the extension's externally_connectable matches messages it with chrome.runtime.
        connectable_url = f"http://127.0.0.1:{page_server.server_address[1]}/connectable?id={extension_id}"
        connectable_results = run_extension_test_page(executable, env, connectable_url)

        # The background worker stops once it's idle, and starts again for what it listens to.
        idle_url = f"chrome-extension://{extension_id}/idle.html"
        idle_results = run_extension_test_page(executable, env, idle_url, arguments=("--extension-idle-timeout=2",))
    finally:
        for server in (page_server, data_server):
            server.shutdown()

    for name, page_results in (
        ("web page", results),
        ("content scripts", content_script_results),
        ("externally connectable page", connectable_results),
        ("idle background", idle_results),
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


# Loads the blocking dashboard, whose lists only show up once its page and Xenon have talked over the script bridge.
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


# A test extension's background worker sends the test's server the tabs every half second, and makes the calls to
# chrome.tabs that the server answers with, each on the tab at an index or on none.
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


# Session restore runs Xenon with its windows, since a headless run's session is never saved. Xenon is started three
# times on one profile with the setting to restore the last session on: it's killed right after it opens and duplicates
# tabs (a duplicate restores its source's history, as a reopened tab does), then quits normally after changes made to
# the restored session, and each time its tabs have to come back with their pages.
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
            # A crash takes Xenon's helper processes with it, and so does a quit.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
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
            # A tab the session store hasn't written since it opened comes back as about:blank. The store writes every
            # 30 seconds by itself, and Xenon has it write a change within a second, so a crash a few seconds after
            # tabs open has to keep them.
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

            # The tabs come back, the duplicate active, as when Xenon was killed. The others wait to be selected, and
            # then load their pages.
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


# Opens a page on a local HTTPS server whose certificate is self-signed. Xenon refuses the certificate, and shows its
# warning in place of the page, saying why (UI/CertificateErrors.h). Nothing can click in a headless browser, so XENON_TEST_PROCEED_PAST_CERTIFICATE_ERRORS
# has the warning print what it shows and then proceed, as the user would. The page then loads, and closes the browser.
def check_certificate_errors(executable: Path, env: dict) -> bool:
    page = f"""<!doctype html><title>Xenon certificate test</title><script>
        console.log("{CERTIFICATE_ERROR_TEST_MARKER}" + JSON.stringify({{ loaded: location.href }}));
        window.close();
    </script>""".encode()
    requested: list[str] = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requested.append(self.path)
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(page)))
            self.end_headers()
            self.wfile.write(page)

        def log_message(self, *args):
            pass

    class Server(http.server.ThreadingHTTPServer):
        # Xenon hangs up as soon as it has seen the certificate, until it's told to accept it.
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
    # The warning on its own, opened at its address, knows of no certificate to accept for the site.
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
        # A crash's stack trace would push what the pages printed out of the output's tail.
        for line in output.splitlines():
            if CERTIFICATE_ERROR_TEST_MARKER in line or "VERIFICATION FAILED" in line or "Failed load" in line:
                print(line, file=sys.stderr)
        print(output[-4000:], file=sys.stderr)
    return passed


def certificate_error_reports(output: str) -> list[dict]:
    # The console prints the logged string in quotes.
    return [
        json.loads(line.split(CERTIFICATE_ERROR_TEST_MARKER, 1)[1].rstrip().removesuffix('"'))
        for line in output.splitlines()
        if CERTIFICATE_ERROR_TEST_MARKER in line
    ]


def run_headless(executable: Path, env: dict, args: list[str]) -> tuple[bool, str]:
    try:
        result = subprocess.run([str(executable), *args], env=env, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired as error:
        # What it printed before it was stopped, which can be bytes even in text mode.
        parts = (error.stdout, error.stderr)
        output = "".join(part.decode(errors="replace") if isinstance(part, bytes) else part for part in parts if part)
        return False, f"{output}\ntimed out after {error.timeout} s"
    return result.returncode == 0, result.stdout + result.stderr


def run_command(command: list):
    try:
        subprocess.check_call([str(part) for part in command])
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
