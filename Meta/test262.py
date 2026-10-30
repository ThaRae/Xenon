#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import venv

from collections import Counter
from pathlib import Path

import patches

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
WORK_DIR = XENON_SOURCE_DIR / "Build" / "test262"

LIBJS_DATA = "https://github.com/LadybirdBrowser/libjs-data.git"
LIBJS_TEST262 = "https://github.com/LadybirdBrowser/libjs-test262.git"
TEST262 = "https://github.com/tc39/test262.git"

TIMEOUT_SECONDS = 30
LISTED_TESTS = 50

RESULT_NAMES = {
    "PASSED": "Passed",
    "FAILED": "Failed",
    "SKIPPED": "Skipped",
    "METADATA_ERROR": "Metadata error",
    "HARNESS_ERROR": "Harness error",
    "TIMEOUT_ERROR": "Timed out",
    "PROCESS_ERROR": "Crashed",
    "RUNNER_EXCEPTION": "Runner exception",
    "TODO_ERROR": "Not implemented (TODO)",
}


def main() -> int:
    parser = argparse.ArgumentParser(description="Run test262 against Xenon's LibJS and compare it with Ladybird's")
    parser.add_argument(
        "--runner", type=Path, default=XENON_SOURCE_DIR / "Build" / "release" / "bin" / "test262-runner"
    )
    args = parser.parse_args()

    if not args.runner.exists():
        print(f"{args.runner} isn't built. Run: ./Meta/xenon.py build test262-runner", file=sys.stderr)
        return 1
    WORK_DIR.mkdir(parents=True, exist_ok=True)

    base = patches.applied_base()
    upstream = ladybird_run(base)
    if upstream is None:
        print(f"warning: libjs-data has no run of Ladybird {base[:10]}; running the latest test262", file=sys.stderr)
        versions = {"test262": "main", "libjs-test262": "master"}
    else:
        versions = upstream["versions"]

    test262 = fetch(TEST262, versions["test262"], WORK_DIR / "test262")
    scripts = fetch(LIBJS_TEST262, versions["libjs-test262"], WORK_DIR / "libjs-test262")

    environment = WORK_DIR / "venv"
    python = environment / "bin" / "python3"
    if not python.exists():
        venv.create(environment, with_pip=True)
    pip = [python, "-m", "pip", "install", "--quiet", "--disable-pip-version-check", "-r", "requirements.txt"]
    subprocess.run(pip, cwd=scripts, check=True)

    ours_path = WORK_DIR / "xenon.json"
    print(f"==> test262 {versions['test262'][:10]} with {args.runner}", flush=True)
    subprocess.run(
        [
            python,
            scripts / "main.py",
            "--libjs-test262-runner",
            args.runner.resolve(),
            "--test262-root",
            test262,
            "--timeout",
            str(TIMEOUT_SECONDS),
            "--silent",
            "--summary",
            "--per-file",
            ours_path,
        ],
        check=True,
    )
    ours = json.loads(ours_path.read_text())

    if upstream is None:
        summary, regressions = describe(base, versions, ours, None), []
    else:
        theirs_path = WORK_DIR / "ladybird.json"
        theirs_path.write_text(json.dumps(upstream["per_file"]))
        diff = subprocess.run(
            [python, scripts / "per_file_result_diff.py", "--old", theirs_path, "--new", ours_path],
            cwd=scripts,
            capture_output=True,
            text=True,
        )
        (WORK_DIR / "diff.txt").write_text(diff.stdout + diff.stderr)
        summary, regressions = describe(base, versions, ours, upstream)

    (WORK_DIR / "summary.md").write_text(summary)
    print(summary)
    return 1 if regressions else 0


def ladybird_run(commit: str) -> dict | None:
    data = WORK_DIR / "libjs-data"
    if not (data / ".git").exists():
        subprocess.run(["git", "clone", "--quiet", "--filter=blob:none", "--no-checkout", LIBJS_DATA, data], check=True)
    else:
        subprocess.run(["git", "fetch", "--quiet", "origin"], cwd=data, check=True)

    found = git(data, "log", "-1", "--format=%H", "--fixed-strings", f"--grep=ladybird@{commit}", "origin/HEAD")
    if not found:
        return None
    runs = json.loads(git(data, "show", f"{found}:test262/results.json"))
    run = next((run for run in runs if run["versions"]["serenity"] == commit), None)
    if run is None:
        return None
    return {
        "libjs_data_commit": found,
        "versions": run["versions"],
        "per_file": json.loads(git(data, "show", f"{found}:test262/per-file-master.json")),
    }


def fetch(url: str, revision: str, directory: Path) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    if not (directory / ".git").exists():
        subprocess.run(["git", "init", "--quiet"], cwd=directory, check=True)
    subprocess.run(["git", "fetch", "--quiet", "--depth", "1", url, revision], cwd=directory, check=True)
    subprocess.run(["git", "checkout", "--quiet", "--force", "FETCH_HEAD"], cwd=directory, check=True)
    return directory


def git(directory: Path, *args: str) -> str:
    return subprocess.run(["git", *args], cwd=directory, capture_output=True, text=True, check=True).stdout.strip()


def describe(base: str, versions: dict, ours: dict, upstream: dict | None) -> tuple[str, list[str]]:
    our_results = ours["results"]
    patch_count = len(patches.patch_files())
    lines = ["### test262", ""]
    if upstream is None:
        lines += [
            f"LibJS from Ladybird `{base[:10]}` with Xenon's {patch_count} patches, at test262 `{versions['test262']}`. "
            "LadybirdBrowser/libjs-data has no run of that commit to compare with.",
            "",
        ]
        counts = Counter(our_results.values())
        rows = [f"| {RESULT_NAMES.get(name, name)} | {counts[name]:,} |" for name in RESULT_NAMES if counts[name]]
        lines += ["| Result | Xenon |", "| --- | ---: |", *rows, f"| Total | {len(our_results):,} |"]
        return "\n".join(lines) + "\n", []

    their_results = upstream["per_file"]["results"]
    lines += [
        f"LibJS from Ladybird `{base[:10]}` with Xenon's {patch_count} patches, against Ladybird's own run of that "
        f"commit (LadybirdBrowser/libjs-data `{upstream['libjs_data_commit'][:10]}`), both at test262 "
        f"`{versions['test262'][:10]}`. Each test may take {TIMEOUT_SECONDS} s here, and 10 s in Ladybird's CI.",
        "",
    ]
    ours_counted = Counter(our_results.values())
    theirs_counted = Counter(their_results.values())
    lines += ["| Result | Xenon | Ladybird |", "| --- | ---: | ---: |"]
    for name, label in RESULT_NAMES.items():
        if ours_counted[name] or theirs_counted[name]:
            lines.append(f"| {label} | {ours_counted[name]:,} | {theirs_counted[name]:,} |")
    lines += [f"| Total | {len(our_results):,} | {len(their_results):,} |", ""]

    regressions = []
    timeouts = []
    fixed = []
    for test, theirs in sorted(their_results.items()):
        ours_result = our_results.get(test, "missing")
        if theirs == "PASSED" and ours_result == "TIMEOUT_ERROR":
            timeouts.append(f"{test} (timed out)")
        elif theirs == "PASSED" and ours_result != "PASSED":
            regressions.append(f"{test} ({RESULT_NAMES.get(ours_result, ours_result).lower()})")
        elif theirs != "PASSED" and ours_result == "PASSED":
            fixed.append(f"{test} ({RESULT_NAMES.get(theirs, theirs).lower()} upstream)")

    if regressions:
        lines += [f"**Tests that pass upstream and fail with Xenon's patches: {len(regressions)}**", ""]
        lines += listing(regressions)
    else:
        lines += ["Every test that passes upstream passes with Xenon's patches.", ""]
    if timeouts:
        lines += [f"Tests that pass upstream and timed out here, which doesn't fail the run: {len(timeouts)}", ""]
        lines += listing(timeouts)
    if fixed:
        lines += [f"Tests that pass here and not upstream: {len(fixed)}", ""]
        lines += listing(fixed)
    return "\n".join(lines) + "\n", regressions


def listing(tests: list[str]) -> list[str]:
    lines = [f"- `{test.split(' ', 1)[0]}` {test.split(' ', 1)[1]}" for test in tests[:LISTED_TESTS]]
    if len(tests) > LISTED_TESTS:
        lines.append(f"- and {len(tests) - LISTED_TESTS} more (diff.txt, in the test262 artifact)")
    return lines + [""]


if __name__ == "__main__":
    sys.exit(main())
