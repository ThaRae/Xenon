#!/usr/bin/env python3

# Xenon's fast checks: formatting, and the invariants that keep Xenon building on upstream Ladybird plus Xenon's patch
# series (Patches/ladybird, see Meta/patches.py).
# The same script runs locally (by hand or from the pre-push hook in .githooks/) and as CI's first job.
#
#   Meta/check.py            check everything
#   Meta/check.py --fix      apply the formatters, then check
#   Meta/check.py --online   also confirm the ladybird submodule's commit exists upstream (CI does this)
#   Meta/check.py --unpinned allow ladybird/ to be off its pinned commit (the weekly build against upstream master)
#
# The formatters are the versions Ladybird's own CI uses, read from the submodule, so bumping Ladybird bumps them too.
# clang-format and ruff are installed into Build/tools on first use; prettier runs through npx.

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import venv

from pathlib import Path

import patches

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
LADYBIRD_SOURCE_DIR = XENON_SOURCE_DIR / "ladybird"
UI_DIR = XENON_SOURCE_DIR / "UI"
TOOLS_DIR = XENON_SOURCE_DIR / "Build" / "tools"
LADYBIRD_UPSTREAM = "https://github.com/LadybirdBrowser/ladybird.git"

# Sources in UI/ that UI/Xenon.cmake builds only on some platforms, or not yet at all.
PLATFORM_SOURCES = {
    "AccessibilityInterface.cpp",
    "ExternalURLActivationToken.cpp",
    "ExternalURLHandler.cpp",
    "GeolocationProviderQt.cpp",
    "WebContentViewLinux.cpp",
    "WebContentViewWindows.cpp",
    "X11Window.cpp",
}

# Upstream's chart colors in the performance monitor; Xenon's chrome itself uses the rose scale.
LADYBIRD_BRAND_COLOR_ALLOWED_IN = {"BrandColors.h", "PerformanceMonitorWidget.cpp"}

# Config shared with Ladybird, linked from the submodule so that tools find it from anywhere in Xenon's tree.
REQUIRED_SYMLINKS = {
    ".clang-format": "ladybird/.clang-format",
    ".prettierrc": "ladybird/.prettierrc",
    "rust-toolchain.toml": "ladybird/rust-toolchain.toml",
}


class Checks:
    def __init__(self, fix: bool):
        self.fix = fix
        self.failures: list[str] = []

    def fail(self, check: str, message: str):
        self.failures.append(f"{check}: {message}")


def relative(path: Path) -> str:
    return str(path.relative_to(XENON_SOURCE_DIR))


def ui_sources(*suffixes: str) -> list[Path]:
    return sorted(path for path in UI_DIR.rglob("*") if path.suffix in suffixes and "OrcaScripts" not in path.parts)


def cxx_sources() -> list[Path]:
    tests = sorted((XENON_SOURCE_DIR / "Tests").rglob("*.cpp")) if (XENON_SOURCE_DIR / "Tests").exists() else []
    return ui_sources(".cpp", ".h", ".mm") + tests


def web_sources() -> list[Path]:
    return sorted(path for path in (UI_DIR / "Resources").rglob("*") if path.suffix in (".html", ".css", ".js"))


def python_sources() -> list[Path]:
    return sorted((XENON_SOURCE_DIR / "Meta").rglob("*.py"))


def shell_sources() -> list[Path]:
    scripts = sorted((XENON_SOURCE_DIR / "Meta").rglob("*.sh"))
    hooks = XENON_SOURCE_DIR / ".githooks"
    return scripts + (sorted(path for path in hooks.iterdir() if path.is_file()) if hooks.exists() else [])


# --- Invariants -------------------------------------------------------------------------------------------------------


def check_includes(checks: Checks):
    # <UI/Qt/Foo.h> resolves to Ladybird's copy of a header, silently compiling against upstream's class instead of ours.
    for path in ui_sources(".cpp", ".h", ".mm"):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if re.match(r'\s*#\s*(include|import)\s*[<"]UI/Qt/', line):
                checks.fail("includes", f"{relative(path)}:{number} includes from UI/Qt/; use <UI/...>")


def check_build_lists_every_source(checks: Checks):
    build_file = (UI_DIR / "Xenon.cmake").read_text()
    # Sources are listed relative to UI/ (Bangs.cpp, Extensions/Manifest.cpp), or by a path ending in Tests/ or UI/.
    # Generated sources live in the build directory.
    source_pattern = r"(?<!GENERATED_DIR\})(?<![A-Za-z0-9_/])([A-Za-z0-9_/]+\.(?:cpp|mm))\b"
    listed = {name.removeprefix("/") for name in re.findall(source_pattern, build_file)}
    listed = {name.split("/UI/")[-1].removeprefix("UI/") for name in listed}

    for path in ui_sources(".cpp", ".mm"):
        name = str(path.relative_to(UI_DIR))
        if name not in listed and name not in PLATFORM_SOURCES:
            checks.fail("sources", f"{relative(path)} isn't built; add it to UI/Xenon.cmake")

    for name in sorted(listed):
        test_name = name.split("Tests/")[-1]
        if not (UI_DIR / name).exists() and not (XENON_SOURCE_DIR / "Tests" / test_name).exists():
            checks.fail("sources", f"UI/Xenon.cmake lists {name}, which doesn't exist in UI/ or Tests/")


def check_about_page_overrides(checks: Checks):
    build_file = (UI_DIR / "Xenon.cmake").read_text()
    match = re.search(r"set\(XENON_ABOUT_PAGE_OVERRIDES\s+([^)]*)\)", build_file)
    additions_match = re.search(r"set\(XENON_ABOUT_PAGE_ADDITIONS\s+([^)]*)\)", build_file)
    if not match or not additions_match:
        checks.fail("overrides", "UI/Xenon.cmake needs XENON_ABOUT_PAGE_OVERRIDES and XENON_ABOUT_PAGE_ADDITIONS lists")
        return

    overrides = match.group(1).split()
    additions = additions_match.group(1).split()
    shipped = {path.name for path in (UI_DIR / "Resources" / "about-pages").iterdir()}
    upstream = LADYBIRD_SOURCE_DIR / "Base" / "res" / "ladybird" / "about-pages"

    for name in overrides:
        if name not in shipped:
            checks.fail("overrides", f"{name} is listed as an override but UI/Resources/about-pages/{name} is missing")
        if not (upstream / name).exists():
            checks.fail("overrides", f"{name} overrides a page Ladybird no longer ships; check upstream's changes")
    for name in additions:
        if name not in shipped:
            checks.fail("overrides", f"{name} is listed as an addition but UI/Resources/about-pages/{name} is missing")
        if (upstream / name).exists():
            checks.fail("overrides", f"Ladybird now ships its own {name}; move it to XENON_ABOUT_PAGE_OVERRIDES")
    for name in sorted(shipped - set(overrides) - set(additions)):
        checks.fail("overrides", f"UI/Resources/about-pages/{name} isn't listed in UI/Xenon.cmake, so it never ships")


def check_bangs(checks: Checks):
    source = (UI_DIR / "Bangs.cpp").read_text()
    bangs = re.findall(r'\{\s*"([^"]*)"sv,\s*"([^"]*)"sv,\s*"([^"]*)"sv\s*\}', source)
    if not bangs:
        checks.fail("bangs", "found no bangs in UI/Bangs.cpp")

    seen: dict[str, str] = {}
    for trigger, name, query_url in bangs:
        key = trigger.lower()
        if key in seen:
            checks.fail("bangs", f"!{trigger} ({name}) duplicates !{key} ({seen[key]})")
        seen[key] = name
        if not re.fullmatch(r"[a-z0-9.]+", trigger):
            checks.fail("bangs", f"!{trigger} should be lowercase letters and digits, without the !")
        if not query_url.startswith("https://"):
            checks.fail("bangs", f"!{trigger} doesn't use https: {query_url}")
        if query_url.count("%s") != 1:
            checks.fail("bangs", f"!{trigger} needs exactly one %s in {query_url}")


def check_palette(checks: Checks):
    for path in ui_sources(".cpp", ".h", ".mm"):
        if path.name in LADYBIRD_BRAND_COLOR_ALLOWED_IN:
            continue
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if re.search(r"BrandColors::(Violet|Indigo|SlateBlue)", line):
                checks.fail(
                    "palette", f"{relative(path)}:{number} uses Ladybird's brand colors; use BrandColors::Rose*"
                )

    for path in web_sources():
        if path.name == "webui.css":
            continue
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if re.search(r"var\(--(violet|indigo|slate-blue)", line):
                checks.fail("palette", f"{relative(path)}:{number} uses Ladybird's brand colors; use --xenon-rose")


def check_filter_lists(checks: Checks):
    # The lists Xenon ships (Meta/update-filter-lists.py writes them): every one the manifest names is there, none is
    # there that it doesn't name, and none ships without a license that allows it.
    directory = UI_DIR / "Resources" / "filter-lists"
    manifest_path = directory / "manifest.json"
    if not manifest_path.exists():
        checks.fail(
            "filter lists", "UI/Resources/filter-lists/manifest.json is missing; run Meta/update-filter-lists.py"
        )
        return

    manifest = json.loads(manifest_path.read_text())
    listed = {entry["file"] for entry in manifest["lists"]}
    present = {
        f"{subdirectory}/{path.name}"
        for subdirectory in ("lists", "trusted-lists")
        for path in (directory / subdirectory).glob("*.txt")
    }
    for file in sorted(listed - present):
        checks.fail("filter lists", f"the manifest names {file}, which is missing")
    for file in sorted(present - listed):
        checks.fail("filter lists", f"{file} isn't in the manifest")

    for entry in manifest["lists"]:
        if entry["license_needs_review"]:
            checks.fail("filter lists", f"{entry['id']} ships without a license that allows it")
        if entry["id"] in manifest.get("excluded", {}):
            checks.fail(
                "filter lists", f"{entry['id']} ships although it's excluded: {manifest['excluded'][entry['id']]}"
            )
    if "resources" in manifest and not (directory / manifest["resources"]["file"]).exists():
        checks.fail("filter lists", f"{manifest['resources']['file']} is missing; run Meta/update-filter-lists.py")
    for name in ("NOTICE.txt", "LICENSE-GPL-3.0.txt"):
        if not (directory / name).exists():
            checks.fail("filter lists", f"UI/Resources/filter-lists/{name} is missing; the lists' licenses require it")


def check_symlinks(checks: Checks):
    for name, target in REQUIRED_SYMLINKS.items():
        path = XENON_SOURCE_DIR / name
        if not path.is_symlink() or os.readlink(path) != target:
            checks.fail("symlinks", f"{name} should be a symlink to {target}")


def check_submodule(checks: Checks, online: bool, unpinned: bool):
    if not (LADYBIRD_SOURCE_DIR / ".git").exists():
        checks.fail("submodule", "ladybird/ isn't checked out; run git submodule update --init --depth 1")
        return

    # Xenon builds on upstream Ladybird plus the patch series, so an engine change that isn't in the series would build
    # here and nowhere else.
    status = git("-C", str(LADYBIRD_SOURCE_DIR), "status", "--porcelain", "--untracked-files=no")
    if status:
        checks.fail("submodule", "ladybird/ has uncommitted changes; commit them there and run Meta/patches.py export")

    if not unpinned and patches.state() == patches.State.OTHER:
        checks.fail(
            "submodule",
            f"ladybird/ is {patches.State.OTHER.value}; Meta/patches.py export saves engine changes to the series, "
            "and Meta/patches.py apply restores it",
        )

    for path in patches.patch_files():
        if not path.read_text().startswith(f"From {'0' * 40} "):
            checks.fail("patches", f"{relative(path)} wasn't written by Meta/patches.py export")

    pinned = git("ls-tree", "HEAD", "ladybird").split()
    if online and pinned:
        result = subprocess.run(
            ["git", "-C", str(LADYBIRD_SOURCE_DIR), "fetch", "--quiet", "--depth=1", LADYBIRD_UPSTREAM, pinned[2]],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            checks.fail("submodule", f"the pinned commit {pinned[2][:10]} doesn't exist upstream")


# --- Formatters -------------------------------------------------------------------------------------------------------


def ladybird_ci_versions() -> dict[str, str]:
    dockerfile = (LADYBIRD_SOURCE_DIR / "Meta" / "Docker" / "ci" / "Dockerfile").read_text()
    requirements = (LADYBIRD_SOURCE_DIR / "Meta" / "Linters" / "requirements.txt").read_text()
    return {
        "llvm": re.search(r"ARG LLVM_VERSION=(\d+)", dockerfile).group(1),
        "prettier": re.search(r"prettier@([\d.]+)", dockerfile).group(1),
        "ruff": re.search(r"ruff==([\d.]+)", requirements).group(1),
    }


def tools_venv(versions: dict[str, str]) -> Path:
    stamp = f"clang-format~={versions['llvm']}.1 ruff=={versions['ruff']}"
    bin_dir = TOOLS_DIR / "venv" / "bin"
    stamp_file = TOOLS_DIR / "venv" / "xenon-tools"

    if not stamp_file.exists() or stamp_file.read_text() != stamp:
        print(f"Installing {stamp} into {relative(TOOLS_DIR)}", flush=True)
        shutil.rmtree(TOOLS_DIR / "venv", ignore_errors=True)
        venv.create(TOOLS_DIR / "venv", with_pip=True)
        subprocess.check_call([bin_dir / "pip", "install", "--quiet", *stamp.split()])
        stamp_file.write_text(stamp)
    return bin_dir


def run_formatter(checks: Checks, name: str, check_command: list, fix_command: list, files: list[Path]):
    if not files:
        return
    paths = [str(path) for path in files]
    if checks.fix:
        subprocess.run([*fix_command, *paths], cwd=XENON_SOURCE_DIR, capture_output=True)

    result = subprocess.run([*check_command, *paths], cwd=XENON_SOURCE_DIR, capture_output=True, text=True)
    if result.returncode != 0:
        output = result.stdout + result.stderr
        unformatted = [relative(path) for path in files if relative(path) in output or str(path) in output]
        if unformatted:
            checks.fail(name, f"{', '.join(unformatted)} (run Meta/check.py --fix)")
        else:
            checks.fail(name, output.strip()[:600])


def check_formatting(checks: Checks):
    versions = ladybird_ci_versions()
    bin_dir = tools_venv(versions)

    clang_format = str(bin_dir / "clang-format")
    run_formatter(
        checks,
        "clang-format",
        [clang_format, "--dry-run", "--Werror"],
        [clang_format, "-i"],
        cxx_sources(),
    )

    ruff = str(bin_dir / "ruff")
    ruff_config = ["--config", str(LADYBIRD_SOURCE_DIR / "pyproject.toml")]
    run_formatter(
        checks, "ruff", [ruff, "check", *ruff_config], [ruff, "check", "--fix", *ruff_config], python_sources()
    )
    run_formatter(
        checks,
        "ruff format",
        [ruff, "format", "--check", *ruff_config],
        [ruff, "format", *ruff_config],
        python_sources(),
    )

    if shutil.which("npx"):
        prettier = ["npx", "--yes", f"prettier@{versions['prettier']}"]
        run_formatter(checks, "prettier", [*prettier, "--check"], [*prettier, "--write"], web_sources())
    else:
        checks.fail("prettier", "npx isn't installed, so the web pages can't be checked (install Node.js)")

    if shutil.which("shellcheck"):
        run_formatter(checks, "shellcheck", ["shellcheck"], ["shellcheck"], shell_sources())
    else:
        print("note: shellcheck isn't installed; skipping the shell scripts (CI checks them)")


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=XENON_SOURCE_DIR, capture_output=True, text=True).stdout.strip()


def main():
    os.environ.setdefault("PIP_DISABLE_PIP_VERSION_CHECK", "1")
    os.environ.setdefault("npm_config_update_notifier", "false")
    os.environ.setdefault("npm_config_fund", "false")

    parser = argparse.ArgumentParser(description="Xenon's fast checks")
    parser.add_argument("--fix", action="store_true", help="apply the formatters before checking")
    parser.add_argument("--online", action="store_true", help="confirm the submodule's commit exists upstream")
    parser.add_argument("--skip-formatting", action="store_true", help="only check the invariants")
    parser.add_argument("--unpinned", action="store_true", help="allow ladybird/ to be off its pinned commit")
    args = parser.parse_args()

    checks = Checks(fix=args.fix)
    check_includes(checks)
    check_build_lists_every_source(checks)
    check_about_page_overrides(checks)
    check_bangs(checks)
    check_palette(checks)
    check_filter_lists(checks)
    check_symlinks(checks)
    check_submodule(checks, online=args.online, unpinned=args.unpinned)
    if not args.skip_formatting:
        check_formatting(checks)

    if checks.failures:
        print(f"\n{len(checks.failures)} problem(s):", file=sys.stderr)
        for failure in checks.failures:
            print(f"  {failure}", file=sys.stderr)
        sys.exit(1)
    print("All checks passed.")


if __name__ == "__main__":
    main()
