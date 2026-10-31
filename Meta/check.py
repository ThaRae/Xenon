#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import venv

from pathlib import Path

import patches

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
LADYBIRD_SOURCE_DIR = XENON_SOURCE_DIR / "ladybird"
UI_DIR = XENON_SOURCE_DIR / "UI"
TOOLS_DIR = XENON_SOURCE_DIR / "Build" / "tools"
LADYBIRD_UPSTREAM = "https://github.com/LadybirdBrowser/ladybird.git"

PYYAML_VERSION = "6.0.2"

PLATFORM_SOURCES = {
    "AccessibilityInterface.cpp",
    "ExternalURLActivationToken.cpp",
    "ExternalURLHandler.cpp",
    "GeolocationProviderQt.cpp",
    "WebContentViewLinux.cpp",
    "WebContentViewWindows.cpp",
    "X11Window.cpp",
}

LADYBIRD_BRAND_COLOR_ALLOWED_IN = {"BrandColors.h", "PerformanceMonitorWidget.cpp"}

BANG_FORMAT_FLAGS = ("open_base_path", "open_snap_domain", "url_encode_placeholder", "url_encode_space_to_plus")

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
    scripts = sorted((XENON_SOURCE_DIR / "Meta").rglob("*.sh")) + [XENON_SOURCE_DIR / "UI" / "Linux" / "AppRun"]
    hooks = XENON_SOURCE_DIR / ".githooks"
    return scripts + (sorted(path for path in hooks.iterdir() if path.is_file()) if hooks.exists() else [])


def check_includes(checks: Checks):
    for path in ui_sources(".cpp", ".h", ".mm"):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if re.match(r'\s*#\s*(include|import)\s*[<"]UI/Qt/', line):
                checks.fail("includes", f"{relative(path)}:{number} includes from UI/Qt/; use <UI/...>")


CXX_TEXT_TOKEN = re.compile(
    r'R"(?P<delimiter>[^()\\\s]*)\((?P<raw>.*?)\)(?P=delimiter)"|"(?P<plain>(?:[^"\\\n]|\\.)*)"'
    r"|'(?:[^'\\\n]|\\.)*'|(?P<comment>//[^\n]*|/\*.*?\*/)",
    re.DOTALL,
)
CXX_WHITESPACE = re.compile(r"\s*")
CXX_ESCAPE = re.compile(r"\\(?:u([0-9A-Fa-f]{4})|U([0-9A-Fa-f]{8})|x([0-9A-Fa-f]+)|([0-7]{1,3})|.)", re.DOTALL)


def cxx_without_comments(source: str) -> str:
    def blank(token: re.Match) -> str:
        return re.sub(r"[^\n]", " ", token.group()) if token.group("comment") else token.group()

    return CXX_TEXT_TOKEN.sub(blank, source)


def cxx_string_literal_text(source: str, position: int) -> str | None:
    def unescape(escape: re.Match) -> str:
        if number := escape.group(1) or escape.group(2) or escape.group(3):
            return chr(min(int(number, 16), 0x10FFFF))
        if number := escape.group(4):
            return chr(int(number, 8))
        return "?"

    pieces = []
    while literal := CXX_TEXT_TOKEN.match(source, CXX_WHITESPACE.match(source, position).end()):
        if literal.group("raw") is not None:
            pieces.append(literal.group("raw"))
        elif literal.group("plain") is not None:
            pieces.append(CXX_ESCAPE.sub(unescape, literal.group("plain")))
        else:
            break
        position = literal.end()
    return "".join(pieces) if pieces else None


def check_format_strings(checks: Checks):
    call = re.compile(r"\b(?:qformatted|Utf16String::formatted)\s*\(")
    for path in cxx_sources():
        source = cxx_without_comments(path.read_text())
        for match in call.finditer(source):
            number = source.count("\n", 0, match.start()) + 1
            if source[match.end() :].lstrip().startswith("CheckedFormatString"):
                continue
            text = cxx_string_literal_text(source, match.end())
            if text is None:
                checks.fail("format strings", f"{relative(path)}:{number} formats a string that isn't a literal")
            elif not text.isascii():
                checks.fail(
                    "format strings",
                    f'{relative(path)}:{number} formats a string with non-ASCII text; use tr("...%1").arg()',
                )


def check_build_lists_every_source(checks: Checks):
    build_file = (UI_DIR / "Xenon.cmake").read_text()
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
    directory = UI_DIR / "Resources" / "bangs"
    try:
        bangs = json.loads((directory / "bangs.json").read_text())
    except (OSError, ValueError) as error:
        checks.fail("bangs", f"UI/Resources/bangs/bangs.json can't be read ({error}); run Meta/update-bangs.py")
        return
    if not (directory / "NOTICE.txt").exists():
        checks.fail("bangs", "UI/Resources/bangs/NOTICE.txt is missing; the list's license requires it")

    seen: dict[str, str] = {}
    for bang in bangs:
        name, query_url = bang["s"], bang["u"]
        for trigger in (bang["t"], *bang.get("ts", [])):
            if trigger in seen:
                checks.fail("bangs", f"!{trigger} ({name}) duplicates !{trigger} ({seen[trigger]})")
            seen[trigger] = name
            if not trigger or trigger != trigger.lower() or trigger.startswith("!") or re.search(r"\s", trigger):
                checks.fail("bangs", f"!{trigger} should be lowercase, without spaces, and without the !")
        if not query_url.startswith("https://"):
            checks.fail("bangs", f"!{bang['t']} doesn't use https: {query_url}")
        if "{{{s}}}" not in query_url:
            checks.fail("bangs", f"!{bang['t']} has no {{{{{{s}}}}}} for the query in {query_url}")
        unknown_flags = set(bang.get("fmt", [])) - set(BANG_FORMAT_FLAGS)
        if unknown_flags:
            checks.fail(
                "bangs", f"!{bang['t']} has format flags Xenon doesn't know: {', '.join(sorted(unknown_flags))}"
            )

    for path in web_sources():
        for number, line in enumerate(path.read_text().splitlines(), 1):
            for trigger in re.findall(r"<kbd>!([^\s<]+)", line):
                if trigger.lower() not in seen:
                    checks.fail("bangs", f"{relative(path)}:{number} suggests !{trigger}, which isn't a bang")


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


def check_patch_messages(checks: Checks):
    lint = LADYBIRD_SOURCE_DIR / "Meta" / "lint-commit.sh"
    if not lint.exists():
        return

    with tempfile.TemporaryDirectory() as temporary:
        body_path = Path(temporary) / "body"
        message_path = Path(temporary) / "message"
        for path in patches.patch_files():
            with path.open("rb") as patch:
                info = subprocess.run(
                    ["git", "mailinfo", str(body_path), os.devnull], stdin=patch, capture_output=True, text=True
                )
            subject = re.search(r"^Subject: (.*)$", info.stdout, re.MULTILINE)
            if info.returncode != 0 or not subject:
                checks.fail("patches", f"{relative(path)} has no commit message git can read")
                continue

            body = body_path.read_text()
            message_path.write_text(f"{subject.group(1)}\n\n{body}" if body else f"{subject.group(1)}\n")
            result = subprocess.run(["bash", str(lint), str(message_path)], capture_output=True, text=True)
            if result.returncode != 0:
                output = re.sub(r"\x1b\[[0-9;]*m", "", result.stdout).splitlines() or [result.stderr.strip()]
                checks.fail("patches", f"{relative(path)}: {output[0].removesuffix(':')}")


def ladybird_ci_versions() -> dict[str, str]:
    dockerfile = (LADYBIRD_SOURCE_DIR / "Meta" / "Docker" / "ci" / "Dockerfile").read_text()
    requirements = (LADYBIRD_SOURCE_DIR / "Meta" / "Linters" / "requirements.txt").read_text()
    return {
        "llvm": re.search(r"ARG LLVM_VERSION=(\d+)", dockerfile).group(1),
        "prettier": re.search(r"prettier@([\d.]+)", dockerfile).group(1),
        "ruff": re.search(r"ruff==([\d.]+)", requirements).group(1),
    }


def venv_python_runs(bin_dir: Path) -> bool:
    try:
        return subprocess.run([bin_dir / "python", "-c", ""], capture_output=True).returncode == 0
    except OSError:
        return False


def tools_venv(versions: dict[str, str]) -> Path:
    stamp = f"clang-format~={versions['llvm']}.1 ruff=={versions['ruff']} pyyaml=={PYYAML_VERSION}"
    bin_dir = TOOLS_DIR / "venv" / "bin"
    stamp_file = TOOLS_DIR / "venv" / "xenon-tools"

    if not stamp_file.exists() or stamp_file.read_text() != stamp or not venv_python_runs(bin_dir):
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
    ruff_config = ["--config", str(LADYBIRD_SOURCE_DIR / "pyproject.toml"), "--config", 'src = ["Meta"]']
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

    check_workflow_steps(checks, bin_dir / "python")


WORKFLOW_STEPS_PROGRAM = """
import json, sys, yaml
steps = []
for path in sys.argv[1:]:
    for job_name, job in yaml.safe_load(open(path)).get("jobs", {}).items():
        for number, step in enumerate(job.get("steps", []), 1):
            if "run" in step:
                steps.append([f"{path}, job {job_name}, step '{step.get('name', number)}'", step["run"]])
print(json.dumps(steps))
"""


def check_workflow_steps(checks: Checks, python: Path):
    workflows = [relative(path) for path in sorted((XENON_SOURCE_DIR / ".github" / "workflows").glob("*.yml"))]
    result = subprocess.run(
        [str(python), "-c", WORKFLOW_STEPS_PROGRAM, *workflows], cwd=XENON_SOURCE_DIR, capture_output=True, text=True
    )
    if result.returncode != 0:
        checks.fail("workflows", f"couldn't read the workflows: {result.stderr.strip()[-600:]}")
        return

    with tempfile.TemporaryDirectory() as temporary:
        script_path = Path(temporary) / "step.sh"
        for where, script in json.loads(result.stdout):
            script_path.write_text(re.sub(r"\$\{\{.*?\}\}", "placeholder", script))
            parsed = subprocess.run(["bash", "-n", str(script_path)], capture_output=True, text=True)
            if parsed.returncode != 0:
                error = parsed.stderr.strip().replace(str(script_path), "step")
                checks.fail("workflows", f"{where} doesn't parse: {error}")
            elif shutil.which("shellcheck"):
                linted = subprocess.run(
                    ["shellcheck", "--shell=bash", "--severity=warning", "--format=gcc", str(script_path)],
                    capture_output=True,
                    text=True,
                )
                if linted.returncode != 0:
                    checks.fail("workflows", f"{where}: {linted.stdout.strip().replace(str(script_path), 'step')}")


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
    check_format_strings(checks)
    check_build_lists_every_source(checks)
    check_about_page_overrides(checks)
    check_bangs(checks)
    check_palette(checks)
    check_filter_lists(checks)
    check_symlinks(checks)
    check_submodule(checks, online=args.online, unpinned=args.unpinned)
    check_patch_messages(checks)
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
