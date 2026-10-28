#!/usr/bin/env python3

# Xenon's changes to the Ladybird engine, carried as a patch series in Patches/ladybird and applied to the pinned
# submodule commit at build time (as Helium patches Chromium). The submodule itself always points at upstream.
#
#   Meta/patches.py apply           check out the pinned commit in ladybird/ and apply the series on a local
#                                   xenon-patches branch, one commit per patch
#   Meta/patches.py apply --base REV
#                                   apply onto another commit instead (the weekly build against upstream master)
#   Meta/patches.py export          write the commits on xenon-patches back out to Patches/ladybird
#   Meta/patches.py pin             make the commit the series was last applied onto the pinned commit
#   Meta/patches.py status          say whether ladybird/ is the pinned commit, the series, or something else
#
# To change the engine: apply, then edit, amend, or add commits in ladybird/ (git rebase -i works as usual), then
# export and commit the patch files. Meta/xenon.py build applies the series by itself when ladybird/ is still at the
# pinned commit, and leaves ladybird/ alone otherwise, so a build mid-edit builds the edit.
#
# To move to a newer Ladybird: fetch it in ladybird/, `apply --base FETCH_HEAD`, fix whichever patch stops applying,
# then export and pin, and commit both. .gitmodules sets the submodule to ignore=all, so that the patched checkout never
# shows as a change in Xenon and `git commit -a` can't pin a commit that only exists locally; pin is how it moves.

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile

from enum import Enum
from pathlib import Path

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
LADYBIRD_SOURCE_DIR = XENON_SOURCE_DIR / "ladybird"
PATCHES_DIR = XENON_SOURCE_DIR / "Patches" / "ladybird"
BRANCH = "xenon-patches"
# Where apply records the commit it applied the series onto, in ladybird/'s repository.
BASE_REF = "refs/xenon-patches/base"

# Applied commits get a fixed committer and the author's date, so the same series on the same base always gives the
# same commits, on any machine and without a git identity configured (as in CI).
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
    # The index rather than HEAD, so that a pin that's staged but not yet committed counts.
    return git("ls-files", "--stage", "ladybird", cwd=XENON_SOURCE_DIR).split()[1]


def applied_base() -> str:
    try:
        return git("rev-parse", "--verify", "--quiet", BASE_REF)
    except RuntimeError:
        return pinned_commit()


def patch_files() -> list[Path]:
    return sorted(PATCHES_DIR.glob("*.patch")) if PATCHES_DIR.exists() else []


def patch_ids(patches: str) -> list[str]:
    # A patch-id is a hash of a diff with whitespace and line numbers left out, so it identifies a change whatever
    # commit it sits on and however its file was written.
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


def has_commit(commit: str) -> bool:
    try:
        git("cat-file", "-e", f"{commit}^{{commit}}")
        return True
    except RuntimeError:
        return False


def has_local_changes() -> bool:
    return bool(git("status", "--porcelain", "--untracked-files=no"))


def write_series_blobs(series_base: str, patches: list[Path]):
    # When a patch doesn't apply as is, git am --3way merges it from the blobs it was made from, which are the series
    # base's files as the patches before it left them. A shallow ladybird/ has the base's own blobs but none of those,
    # so every patch after the first would fail with "lacks necessary blobs". Applying the series to its base in a
    # scratch index writes them to the object store, and touches nothing else: apply makes the same commits either way.
    with tempfile.TemporaryDirectory() as directory:
        env = {"GIT_INDEX_FILE": str(Path(directory) / "index")}
        git("read-tree", series_base, env=env)
        for path in patches:
            try:
                git("apply", "--cached", str(path), env=env)
            except RuntimeError:
                # The series isn't based on series_base after all (exported but not yet pinned), so there's nothing
                # more to write.
                return


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

    pinned = pinned_commit()
    if base != pinned and has_commit(pinned):
        write_series_blobs(pinned, patches)

    for path in patches:
        try:
            git("am", "--quiet", "--3way", "--keep-cr", "--committer-date-is-author-date", str(path), env=COMMITTER_ENV)
        except RuntimeError as error:
            print(f"{relative(path)} doesn't apply to {base[:10]}:\n{error}", file=sys.stderr)
            if "lacks necessary blobs" in str(error) and not has_commit(pinned):
                print(
                    "git am can't fall back to a merge without the pinned commit. Run `git am --abort` and "
                    f"`git fetch --depth 1 origin {pinned}` in ladybird/, then apply again.",
                    file=sys.stderr,
                )
            print(
                "Resolve it in ladybird/ and run `git am --continue` there, then Meta/patches.py export; or run "
                "`git am --abort` there to give up.",
                file=sys.stderr,
            )
            sys.exit(1)

    print(f"Applied {len(patches)} patches to {base[:10]} on ladybird/'s {BRANCH} branch")


def ensure_applied():
    # Called by Meta/xenon.py before a build. Anything other than a fresh checkout is someone's work in progress.
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

    # The options keep the files stable from one export to the next: no commit hashes, no patch count in the subject,
    # no git version in the signature. Full blob ids let `git am --3way` fall back to a merge when the base moves.
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
