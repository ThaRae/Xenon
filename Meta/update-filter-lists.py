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


def main():
    global OUTPUT_DIR
    parser = argparse.ArgumentParser(description="Refresh Xenon's bundled filter lists from xenon-ublock")
    parser.add_argument("--xenon-ublock", type=Path, default=XENON_SOURCE_DIR.parent / "xenon-ublock")
    parser.add_argument("--no-build", action="store_true", help="copy the lists xenon-ublock last built")
    parser.add_argument("--output", type=Path, default=OUTPUT_DIR, help="where to write the bundle")
    args = parser.parse_args()
    OUTPUT_DIR = args.output.resolve()

    converter = args.xenon_ublock.resolve()
    if not (converter / "tools" / "build.py").exists():
        sys.exit(f"{converter} isn't a xenon-ublock checkout")

    if not args.no_build:
        subprocess.check_call([sys.executable, "tools/build.py", "--preset", PRESET, "--engine", ENGINE], cwd=converter)

    dist = converter / f"dist-{ENGINE}"
    manifest = json.loads((dist / "manifest.json").read_text())
    preset = manifest["presets"][PRESET]
    shipped = [entry for entry in preset["lists"] if entry["id"] not in EXCLUDED]

    unlicensed = [entry["id"] for entry in shipped if entry["license_needs_review"]]
    if unlicensed:
        sys.exit(f"these lists' licenses need review before Xenon can ship them: {', '.join(unlicensed)}")

    for name in ("lists", "trusted-lists", "resources"):
        shutil.rmtree(OUTPUT_DIR / name, ignore_errors=True)
        (OUTPUT_DIR / name).mkdir(parents=True)
    for entry in shipped:
        source = dist / entry["file"]
        entry["file"] = f"{'trusted-lists' if entry['trusted'] else 'lists'}/{entry['id']}.txt"
        shutil.copyfile(source, OUTPUT_DIR / entry["file"])

    resources = manifest["resources"]
    shutil.copyfile(dist / resources["file"], OUTPUT_DIR / "resources" / "ublock-resources.json")

    commit = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"], cwd=converter, capture_output=True, text=True
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
    (OUTPUT_DIR / "manifest.json").write_text(json.dumps(bundle_manifest, indent=2) + "\n")
    shutil.copyfile(converter / "LICENSE", OUTPUT_DIR / "LICENSE-GPL-3.0.txt")
    (OUTPUT_DIR / "NOTICE.txt").write_text(notice(shipped, resources, manifest["generated_at"], commit))

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


if __name__ == "__main__":
    main()
