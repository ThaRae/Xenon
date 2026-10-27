#!/usr/bin/env python3

# Refreshes the !bangs Xenon ships (UI/Resources/bangs) from Kagi's community-maintained bang list, which Helium's own
# list is generated from. It has most of DuckDuckGo's bangs under the same triggers, and it's MIT-licensed. DuckDuckGo
# publishes its bang.js without a license, so Xenon doesn't ship that.
#
#   Meta/update-bangs.py                    use Kagi's latest release
#   Meta/update-bangs.py --tag 202610261529 use that release
#   Meta/update-bangs.py --kagi-bangs PATH  use a local checkout of https://github.com/kagisearch/bangs
#
# It writes bangs.json, one bang per line, sorted by trigger, in Kagi's format (s: the site's name, t and ts: its
# triggers, u: the query URL with {{{s}}} for the query, ad: the site's own domain when u searches it through another
# site, fmt: format flags, all on when it's absent), and NOTICE.txt, which credits the source. The build turns
# bangs.json into a lookup table (Meta/generate-bang-table.py). Meta/check.py checks it.

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import urllib.request

from pathlib import Path
from urllib.parse import urlsplit

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
OUTPUT_DIR = XENON_SOURCE_DIR / "UI" / "Resources" / "bangs"
REPOSITORY = "kagisearch/bangs"

# Bangs Xenon adds to Kagi's list, as Helium adds the same bang in its own extras. Kagi's list has no bang for these
# triggers.
XENON_BANGS = [
    {"s": "Kagi", "t": "k", "ts": ["kagi"], "u": "https://kagi.com/search?q={{{s}}}"},
]

KEYS = ("s", "t", "ts", "u", "ad", "fmt")
FORMAT_FLAGS = {"open_base_path", "open_snap_domain", "url_encode_placeholder", "url_encode_space_to_plus"}


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "Xenon Meta/update-bangs.py"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def latest_release() -> str:
    release = json.loads(fetch(f"https://api.github.com/repos/{REPOSITORY}/releases/latest"))
    return release["tag_name"]


def load_from_checkout(checkout: Path) -> tuple[bytes, str, str]:
    if not (checkout / "data" / "bangs.json").exists():
        sys.exit(f"{checkout} isn't a checkout of {REPOSITORY}")
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=checkout, capture_output=True, text=True).stdout.strip()
    data = (checkout / "data" / "bangs.json").read_bytes()
    return data, (checkout / "LICENSE").read_text(), commit[:12] or "unknown commit"


def load_release(tag: str) -> tuple[bytes, str, str]:
    base = f"https://raw.githubusercontent.com/{REPOSITORY}/refs/tags/{tag}"
    return fetch(f"{base}/data/bangs.json"), fetch(f"{base}/LICENSE").decode(), f"release {tag}"


def reason_to_leave_out(bang: dict) -> str | None:
    url = bang["u"]
    if url.startswith("/"):
        return "Kagi searches restricted to one site, which only work on Kagi"
    if "x" in bang:
        return "queries rewritten by a regular expression, which only Kagi does"
    host = urlsplit(url).hostname or ""
    if host == "kagi.com" or host.endswith(".kagi.com"):
        return "Kagi's own services, under generic triggers like !help and !api (Helium leaves them out too)"
    if "{{{s}}}" not in url:
        return "no place in the URL for the query"
    if urlsplit(url).scheme != "https":
        return "not https"
    return None


def normalized(bang: dict) -> dict:
    triggers = [bang["t"].lower(), *(trigger.lower() for trigger in bang.get("ts", []))]
    result = {"s": bang["s"], "t": triggers[0]}
    if len(triggers) > 1:
        result["ts"] = triggers[1:]
    result["u"] = bang["u"]
    if "ad" in bang:
        result["ad"] = bang["ad"]
    if "fmt" in bang:
        unknown = set(bang["fmt"]) - FORMAT_FLAGS
        if unknown:
            sys.exit(f"!{bang['t']} has format flags Xenon doesn't know: {', '.join(sorted(unknown))}")
        result["fmt"] = sorted(set(bang["fmt"]))
    return result


def main():
    parser = argparse.ArgumentParser(description="Refresh Xenon's !bangs from Kagi's bang list")
    parser.add_argument("--tag", help="the release of kagisearch/bangs to use (default: the latest)")
    parser.add_argument("--kagi-bangs", type=Path, help="use a local checkout instead of downloading a release")
    args = parser.parse_args()

    if args.kagi_bangs:
        data, license_text, version = load_from_checkout(args.kagi_bangs.resolve())
    else:
        data, license_text, version = load_release(args.tag or latest_release())

    upstream = json.loads(data)
    left_out: dict[str, list[str]] = {}
    bangs = []
    for bang in upstream:
        reason = reason_to_leave_out(bang)
        if reason:
            left_out.setdefault(reason, []).append(bang["t"])
        else:
            bangs.append(normalized(bang))

    seen = {trigger for bang in bangs for trigger in (bang["t"], *bang.get("ts", []))}
    for bang in XENON_BANGS:
        taken = [trigger for trigger in (bang["t"], *bang.get("ts", [])) if trigger in seen]
        if taken:
            sys.exit(f"Kagi's list now has !{', !'.join(taken)}; drop it from XENON_BANGS")
        bangs.append(normalized(bang))

    bangs.sort(key=lambda bang: bang["t"].encode())
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    lines = [json.dumps({key: bang[key] for key in KEYS if key in bang}, ensure_ascii=False) for bang in bangs]
    (OUTPUT_DIR / "bangs.json").write_text("[\n" + ",\n".join(lines) + "\n]\n", encoding="utf-8")

    source = f"https://github.com/{REPOSITORY}"
    checksum = hashlib.sha256(data).hexdigest()
    (OUTPUT_DIR / "NOTICE.txt").write_text(notice(source, version, checksum, license_text, left_out), encoding="utf-8")

    triggers = sum(1 + len(bang.get("ts", [])) for bang in bangs)
    print(f"Wrote {len(bangs)} bangs ({triggers} triggers) from Kagi's {version} to {relative(OUTPUT_DIR)}")
    for reason, left_out_triggers in left_out.items():
        print(f"  left out {len(left_out_triggers)}: {reason}")


def notice(source: str, version: str, checksum: str, license_text: str, left_out: dict[str, list[str]]) -> str:
    lines = [
        "Xenon's !bangs",
        "",
        "bangs.json is generated by Meta/update-bangs.py; don't edit it by hand. It holds Kagi's community-maintained "
        "bang list, which has most of DuckDuckGo's bangs under the same triggers, plus the few bangs Xenon adds (XENON_BANGS "
        "in Meta/update-bangs.py). The list remains under its MIT license, below.",
        "",
        "Kagi Bangs",
        f"  Source:   {source} ({version}, data/bangs.json)",
        f"  SHA-256:  {checksum}",
        "  License:  MIT (below)",
        "",
        "Left out of Kagi's list:",
    ]
    for reason, triggers in left_out.items():
        examples = ", ".join(f"!{trigger}" for trigger in sorted(triggers)[:6]) + ", ..." * (len(triggers) > 6)
        lines.append(f"  {reason} ({len(triggers)}): {examples}")
    lines += ["", "-" * 80, "", license_text.strip(), ""]
    return "\n".join(lines)


def relative(path: Path) -> str:
    return str(path.relative_to(XENON_SOURCE_DIR))


if __name__ == "__main__":
    main()
