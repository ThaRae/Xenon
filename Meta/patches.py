#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import subprocess
import sys

from enum import Enum
from pathlib import Path

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
LADYBIRD_SOURCE_DIR = XENON_SOURCE_DIR / "ladybird"
PATCHES_DIR = XENON_SOURCE_DIR / "Patches" / "ladybird"
BRANCH = "xenon-patches"
BASE_REF = "refs/xenon-patches/base"

COMMITTER_ENV = {
    "GIT_COMMITTER_NAME": "Xenon",
    "GIT_COMMITTER_EMAIL": "xenon@localhost",
}


class State(Enum):
    PINNED = "the pinned commit, without the series"
    PATCHED = "the pinned commit plus the series"
    OTHER = "neither the pinned commit nor the pinned commit plus the series"


def main():
    parser = argparse.ArgumentParser(description="Apply or export Xenon's patches to Ladybird")
    subparsers = parser.add_subparsers(dest="command", required=True)
    apply_parser = subparsers.add_parser("apply", help="Apply the series to the pinned commit in ladybird/")
    apply_parser.add_argument("--base", help="apply onto this commit instead of the pinned one")
    apply_parser.add_argument("--force", action="store_true", help="discard uncommitted changes in ladybird/")
    subparsers.add_parser("export", help="Write the commits on xenon-patches to Patches/ladybird")
    subparsers.add_parser("pin", help="Stage the commit the series was applied onto as the pinned commit")
    subparsers.add_parser("status", help="Say what ladybird/ is checked out at")
    args = parser.parse_args()

    if not (LADYBIRD_SOURCE_DIR / ".git").exists():
        print("The ladybird submodule is missing. Run: git submodule update --init --depth 1", file=sys.stderr)
        sys.exit(1)

    if args.command == "apply":
        apply_series(args.base or pinned_commit(), force=args.force)
    elif args.command == "export":
        export_series()
    elif args.command == "pin":
        pin_base()
    elif args.command == "status":
        print(f"ladybird/ is {state().value} ({len(patch_files())} patches in {relative(PATCHES_DIR)})")


def relative(path: Path) -> str:
    return str(path.relative_to(XENON_SOURCE_DIR))


def git(*args: str, cwd: Path = LADYBIRD_SOURCE_DIR, input: str | None = None, env: dict | None = None) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=cwd,
        input=input,
        capture_output=True,
        text=True,
        env={**os.environ, **(env or {})},
    )
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed:\n{result.stderr.strip()}")
    return result.stdout.strip()


def pinned_commit() -> str:
    return git("ls-files", "--stage", "ladybird", cwd=XENON_SOURCE_DIR).split()[1]


def applied_base() -> str:
    try:
        return git("rev-parse", "--verify", "--quiet", BASE_REF)
    except RuntimeError:
        return pinned_commit()


def patch_files() -> list[Path]:
    return sorted(PATCHES_DIR.glob("*.patch")) if PATCHES_DIR.exists() else []


def patch_ids(patches: str) -> list[str]:
    output = git("patch-id", "--stable", input=patches)
    return [line.split()[0] for line in output.splitlines()]


def state(base: str | None = None) -> State:
    base = base or pinned_commit()
    head = git("rev-parse", "HEAD")
    patches = patch_files()

    if head == base:
        return State.PINNED if patches else State.PATCHED

    commits = git("rev-list", "--count", f"{base}..HEAD") if is_ancestor(base, head) else None
    if commits is None or int(commits) != len(patches):
        return State.OTHER

    applied = patch_ids(git("log", "--reverse", "-p", "--format=From %H", f"{base}..HEAD"))
    expected = patch_ids("".join(path.read_text() for path in patches))
    return State.PATCHED if applied == expected else State.OTHER


def is_ancestor(ancestor: str, commit: str) -> bool:
    try:
        git("merge-base", "--is-ancestor", ancestor, commit)
        return True
    except RuntimeError:
        return False


def has_local_changes() -> bool:
    return bool(git("status", "--porcelain", "--untracked-files=no"))


def apply_series(base: str, force: bool = False):
    if has_local_changes() and not force:
        print(
            "ladybird/ has uncommitted changes. Commit and export them, or pass --force to discard them.",
            file=sys.stderr,
        )
        sys.exit(1)

    patches = patch_files()
    base = git("rev-parse", "--verify", f"{base}^{{commit}}")
    git("checkout", "--quiet", "--force", "-B", BRANCH, base)
    git("update-ref", BASE_REF, base)
    if not patches:
        print(f"No patches in {relative(PATCHES_DIR)}; ladybird/ is at {base[:10]}")
        return

    for path in patches:
        try:
            git("am", "--quiet", "--3way", "--keep-cr", "--committer-date-is-author-date", str(path), env=COMMITTER_ENV)
        except RuntimeError as error:
            print(f"{relative(path)} doesn't apply to {base[:10]}:\n{error}", file=sys.stderr)
            print(
                "Resolve it in ladybird/ and run `git am --continue` there, then Meta/patches.py export; or run "
                "`git am --abort` there to give up.",
                file=sys.stderr,
            )
            sys.exit(1)

    print(f"Applied {len(patches)} patches to {base[:10]} on ladybird/'s {BRANCH} branch")


def ensure_applied():
    current = state()
    if current == State.PINNED:
        apply_series(pinned_commit())
    elif current == State.OTHER:
        print(
            f"warning: ladybird/ is {current.value}, so it builds as it is. Meta/patches.py export saves engine changes "
            "to the series.",
            file=sys.stderr,
        )


def export_series():
    base = applied_base()
    if git("rev-parse", "--abbrev-ref", "HEAD") != BRANCH:
        print(f"ladybird/ isn't on the {BRANCH} branch; run Meta/patches.py apply first", file=sys.stderr)
        sys.exit(1)
    if not is_ancestor(base, "HEAD"):
        print(f"{BRANCH} isn't based on {base[:10]}, which the series was applied onto", file=sys.stderr)
        sys.exit(1)
    if has_local_changes():
        print("ladybird/ has uncommitted changes; commit them first", file=sys.stderr)
        sys.exit(1)

    for path in patch_files():
        path.unlink()
    PATCHES_DIR.mkdir(parents=True, exist_ok=True)

    git(
        "format-patch",
        "--quiet",
        "--zero-commit",
        "--no-numbered",
        "--no-signature",
        "--full-index",
        "--output-directory",
        str(PATCHES_DIR),
        f"{base}..HEAD",
    )
    print(f"Exported {len(patch_files())} patches to {relative(PATCHES_DIR)}")
    if base != pinned_commit():
        print(f"The series is based on {base[:10]}, not the pinned commit; Meta/patches.py pin pins {base[:10]}.")


def pin_base():
    base = applied_base()
    git("update-index", "--cacheinfo", f"160000,{base},ladybird", cwd=XENON_SOURCE_DIR)
    print(f"Staged ladybird at {base[:10]} as the pinned commit; commit it along with the exported patches")


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
