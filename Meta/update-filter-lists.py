#!/usr/bin/env python3

# Refreshes the filter lists Xenon ships (UI/Resources/filter-lists) from xenon-ublock, which converts uBlock Origin's
# filter lists into lists Ladybird's content blocker applies correctly.
#
#   Meta/update-filter-lists.py                       rebuild the lists in ../xenon-ublock, then copy them here
#   Meta/update-filter-lists.py --no-build            copy what ../xenon-ublock last built
#   Meta/update-filter-lists.py --xenon-ublock PATH   use another checkout
#   Meta/update-filter-lists.py --output DIR          write the bundle elsewhere, as xenon-filter-lists' daily build does
#
# Xenon ships the lists Helium's uBlock Origin fork turns on (xenon-ublock's "helium" preset), except the ones in
# EXCLUDED. Every shipped list's file records its source, its license, and what the conversion kept.
#
# The lists are converted for Ladybird with the content blocker patches in Xenon's patch series (Patches/ladybird, from
# xenon-ublock): they keep uBlock Origin's scriptlets, procedural filters, popup and CNAME filters. uBlock Origin's own
# lists go in trusted-lists/, which may use the scriptlets uBO reserves for its lists, and uBO's scriptlets and
# redirect surrogates in resources/.

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys

from pathlib import Path

from tempfile import TemporaryDirectory

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
OUTPUT_DIR = XENON_SOURCE_DIR / "UI" / "Resources" / "filter-lists"
PRESET = "helium"
ENGINE = "xenon"

# The bundle's layout, and the engine features its lists rely on, as Xenon reads them (BUNDLE_FORMAT in
# UI/FilterListBundle.h). Bump both together whenever a Xenon built before the change couldn't use the new output: a new
# kind of file, or lists that need engine patches it lacks. Xenon only downloads bundles in the format it reads.
FORMAT = 1

# Lists in the preset that Xenon doesn't ship, and why.
EXCLUDED = {
    "plowe-0": "Peter Lowe's list states no license, so it needs its author's permission to redistribute",
    "helium-annoyances": "AGPL-3.0, and almost none of it survives conversion for Ladybird",
    "helium-unbreak": "AGPL-3.0, and almost none of it survives conversion for Ladybird",
}


def checked_list_id(list_id: str) -> str:
    """A list id out of the converter's manifest, which names the file the list ships as. It must be a plain name, so
    that neither it nor the manifest can reach a file outside the bundle's lists/ or trusted-lists/."""
    if (
        not list_id
        or list_id in (".", "..")
        or "\\" in list_id
        or Path(list_id).name != list_id
    ):
        sys.exit(
            f"the manifest's list id '{list_id}' isn't a plain name, so its list can't be shipped"
        )
    return list_id


def checked_dist_file(dist: Path, file: str, what: str) -> Path:
    """A file name out of the converter's manifest, which may only name a file the converter built into dist/."""
    source = (dist / file).resolve()
    if not source.is_relative_to(dist.resolve()):
        sys.exit(
            f"the manifest's {what} '{file}' is outside {dist}, so it isn't a converted file"
        )
    return source


def main():
    global OUTPUT_DIR
    parser = argparse.ArgumentParser(
        description="Refresh Xenon's bundled filter lists from xenon-ublock"
    )
    parser.add_argument(
        "--xenon-ublock", type=Path, default=XENON_SOURCE_DIR.parent / "xenon-ublock"
    )
    parser.add_argument(
        "--no-build", action="store_true", help="copy the lists xenon-ublock last built"
    )
    parser.add_argument(
        "--output", type=Path, default=OUTPUT_DIR, help="where to write the bundle"
    )
    parser.add_argument(
        "command",
        nargs="?",
        help='"self-test" feeds hostile manifests to the path checks',
    )
    args = parser.parse_args()
    if args.command == "self-test":
        sys.exit(self_test())
    OUTPUT_DIR = args.output.resolve()

    converter = args.xenon_ublock.resolve()
    if not (converter / "tools" / "build.py").exists():
        sys.exit(f"{converter} isn't a xenon-ublock checkout")

    if not args.no_build:
        subprocess.check_call(
            [sys.executable, "tools/build.py", "--preset", PRESET, "--engine", ENGINE],
            cwd=converter,
        )

    dist = converter / f"dist-{ENGINE}"
    manifest = json.loads((dist / "manifest.json").read_text())
    preset = manifest["presets"][PRESET]
    shipped = [entry for entry in preset["lists"] if entry["id"] not in EXCLUDED]

    unlicensed = [entry["id"] for entry in shipped if entry["license_needs_review"]]
    if unlicensed:
        sys.exit(
            f"these lists' licenses need review before Xenon can ship them: {', '.join(unlicensed)}"
        )

    for name in ("lists", "trusted-lists", "resources"):
        shutil.rmtree(OUTPUT_DIR / name, ignore_errors=True)
        (OUTPUT_DIR / name).mkdir(parents=True)
    for entry in shipped:
        checked_list_id(entry["id"])
        source = checked_dist_file(
            dist, entry["file"], f"file for the list '{entry['id']}'"
        )
        entry["file"] = (
            f"{'trusted-lists' if entry['trusted'] else 'lists'}/{entry['id']}.txt"
        )
        shutil.copyfile(source, OUTPUT_DIR / entry["file"])

    resources = manifest["resources"]
    shutil.copyfile(
        checked_dist_file(dist, resources["file"], "resources file"),
        OUTPUT_DIR / "resources" / "ublock-resources.json",
    )

    commit = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        cwd=converter,
        capture_output=True,
        text=True,
    ).stdout.strip()
    bundle_manifest = {
        "format": FORMAT,
        "generated_at": manifest["generated_at"],
        "engine": manifest["engine"],
        "resources": {**resources, "file": "resources/ublock-resources.json"},
        "converter": {"name": "xenon-ublock", "commit": commit or "unknown"},
        "preset": PRESET,
        "excluded": EXCLUDED,
        "lists": shipped,
    }
    (OUTPUT_DIR / "manifest.json").write_text(
        json.dumps(bundle_manifest, indent=2) + "\n"
    )
    shutil.copyfile(converter / "LICENSE", OUTPUT_DIR / "LICENSE-GPL-3.0.txt")
    (OUTPUT_DIR / "NOTICE.txt").write_text(
        notice(shipped, resources, manifest["generated_at"], commit)
    )

    total = sum((OUTPUT_DIR / entry["file"]).stat().st_size for entry in shipped)
    print(f"Copied {len(shipped)} lists ({total / 1e6:.1f} MB) into {OUTPUT_DIR}")
    for list_id, reason in EXCLUDED.items():
        print(f"  left out {list_id}: {reason}")


def notice(shipped: list[dict], resources: dict, generated_at: str, commit: str) -> str:
    lines = [
        "Xenon's content blocking filter lists",
        "",
        "These are third-party filter lists, converted by xenon-ublock for Ladybird's content blocker on "
        f"{generated_at} (xenon-ublock {commit or 'unknown commit'}). Converting dropped or rewrote the filters "
        "Ladybird can't apply; each list's header says how many it kept. Each list remains under its own license, as "
        "below. The GPL-3.0 text is in LICENSE-GPL-3.0.txt; the lists offered under the GPL or CC BY-SA are "
        "distributed here under the GPL.",
        "",
    ]
    lines += [
        f"uBlock Origin {resources['ublock_version']}: scriptlets and redirect resources",
        f"  Source:   {resources['source']}",
        "  License:  GPL-3.0 (LICENSE-GPL-3.0.txt)",
        "",
    ]
    for entry in shipped:
        upstream = entry["upstream"]
        lines += [
            entry["title"],
            f"  Source:   {upstream['url']}",
            f"  Homepage: {upstream.get('homepage') or upstream['url']}",
            f"  License:  {entry['license']} ({entry['license_url']})",
            "",
        ]
    return "\n".join(lines)


def self_test() -> int:
    """Feeds hostile and honest converter manifests to the path checks, through main() itself, and drives the checks
    directly. A hostile manifest's ids and file names must not reach outside the converter's dist/ or the bundle."""
    problems = 0

    def check(condition: bool, message: str) -> None:
        nonlocal problems
        if not condition:
            problems += 1
            print(f"self-test: {message}", file=sys.stderr)

    # The checks themselves, driven directly.
    for bad_id in ("", ".", "..", "a/b", "a\\b", "../x", "a/", "/a"):
        try:
            checked_list_id(bad_id)
            check(False, f"a hostile list id {bad_id!r} was accepted")
        except SystemExit:
            pass
    check(
        checked_list_id("easylist-generic") == "easylist-generic",
        "an honest list id was refused",
    )

    with TemporaryDirectory() as directory:
        root = Path(directory)
        fake = root / "xenon-ublock"
        (fake / "tools").mkdir(parents=True)
        (fake / "tools" / "build.py").touch()
        (fake / "LICENSE").write_text("GPL-3.0")
        dist = fake / "dist-xenon"
        dist.mkdir()
        (dist / "payload.txt").write_text("converted list\n")
        (root / "outside.txt").write_text("a file outside dist-xenon\n")

        def write_manifest(lists: list[dict], resources_file: str) -> None:
            dist.joinpath("manifest.json").write_text(
                json.dumps(
                    {
                        "presets": {"helium": {"lists": lists}},
                        "resources": {
                            "file": resources_file,
                            "ublock_version": "1",
                            "source": "x",
                        },
                        "generated_at": "now",
                        "engine": "xenon",
                    }
                )
            )

        def run(*arguments: str) -> SystemExit | None:
            argv = sys.argv
            sys.argv = ["update-filter-lists.py", *arguments]
            try:
                main()
            except SystemExit as stop:
                return stop
            finally:
                sys.argv = argv

        honest_list = [
            {
                "id": "easylist-generic",
                "file": "payload.txt",
                "trusted": False,
                "license_needs_review": False,
                "license": "MIT",
                "license_url": "x",
                "title": "t",
                "upstream": {"url": "x"},
            }
        ]

        # An honest manifest still copies its lists.
        write_manifest(honest_list, "payload.txt")
        out = root / "bundle"
        check(
            run("--xenon-ublock", str(fake), "--no-build", "--output", str(out))
            is None,
            "an honest manifest was refused",
        )
        check(
            (out / "lists" / "easylist-generic.txt").is_file(),
            "an honest list wasn't copied",
        )

        # A manifest whose list id climbs out of the bundle is refused, and nothing lands outside.
        traversed = root / "traversed.txt"
        write_manifest([{**honest_list[0], "id": "../traversed"}], "payload.txt")
        check(
            run("--xenon-ublock", str(fake), "--no-build", "--output", str(out))
            is not None,
            "a manifest with a climbing list id was accepted",
        )
        check(
            not traversed.exists(), "a climbing list id wrote a file outside the bundle"
        )

        # A manifest whose file names name files outside the converter's dist/ are refused, for a list and for the
        # resources file, which would otherwise carry an outside file inside the shipped bundle.
        for what, lists, resources_file in (
            (
                "list file",
                [{**honest_list[0], "file": "../../outside.txt"}],
                "payload.txt",
            ),
            ("resources file", honest_list, "../../outside.txt"),
        ):
            write_manifest(lists, resources_file)
            check(
                run("--xenon-ublock", str(fake), "--no-build", "--output", str(out))
                is not None,
                f"a manifest with a {what} outside dist-xenon was accepted",
            )
            resources_out = out / "resources" / "ublock-resources.json"
            check(
                not resources_out.exists()
                or "a file outside dist-xenon" not in resources_out.read_text(),
                f"a {what} outside dist-xenon was copied into the bundle",
            )

    if problems:
        print(
            f"self-test: {problems} problem{'s' if problems != 1 else ''}",
            file=sys.stderr,
        )
        return 1
    print("self-test: the manifest path checks held")
    return 0


if __name__ == "__main__":
    main()
