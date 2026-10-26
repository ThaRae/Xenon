#!/usr/bin/env python3

# Renders Xenon's app icon from its sources in UI/Icons into the PNGs the build uses: the macOS iconset that
# UI/Xenon.cmake turns into app_icon.icns, and the window icon (UI/Icons/xenon.png) that xenon.qrc embeds.
#
#   Meta/generate-app-icon.py
#
# xenon.svg is the artwork, full bleed. xenon-small.svg is the same mark with heavier strokes, for 32px and below.
# The script places the artwork on Apple's icon grid (an 824px rounded square on a 1024px canvas, with a drop shadow)
# and renders each size with rsvg-convert (from librsvg: `brew install librsvg`).

import math
import re
import shutil
import subprocess
import sys
import tempfile

from pathlib import Path

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
ICONS_DIR = XENON_SOURCE_DIR / "UI" / "Icons"
ICONSET_DIR = ICONS_DIR / "macos" / "app_icon.iconset"

# The largest pixel size that uses xenon-small.svg.
SMALL_ARTWORK_MAX_SIZE = 32

# Every file in the iconset, and its size in pixels.
ICONSET = {f"icon_{size}x{size}.png": size for size in (16, 32, 128, 256, 512)} | {
    f"icon_{size}x{size}@2x.png": size * 2 for size in (16, 32, 128, 256, 512)
}

WINDOW_ICON_SIZE = 256


def rounded_square(size: float, offset: float) -> str:
    # A superellipse, which is close to the continuous corners of Apple's icon shape.
    exponent, steps = 5, 192
    points = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        x = math.copysign(abs(math.cos(t)) ** (2 / exponent), math.cos(t))
        y = math.copysign(abs(math.sin(t)) ** (2 / exponent), math.sin(t))
        points.append(f"{offset + size / 2 * (1 + x):.2f} {offset + size / 2 * (1 + y):.2f}")
    return "M" + " L".join(points) + " Z"


def framed(artwork: Path) -> str:
    content = re.search(r"<svg[^>]*>(.*)</svg>", artwork.read_text(), re.DOTALL)
    if not content:
        sys.exit(f"{artwork} isn't an SVG document")
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
    <defs>
        <path id="icon-shape" d="{rounded_square(824, 100)}"/>
        <clipPath id="icon-clip"><use href="#icon-shape"/></clipPath>
        <filter id="icon-shadow" x="-10%" y="-10%" width="120%" height="125%"><feGaussianBlur stdDeviation="10"/></filter>
    </defs>
    <use href="#icon-shape" fill="#000" fill-opacity="0.3" filter="url(#icon-shadow)" transform="translate(0 10)"/>
    <g clip-path="url(#icon-clip)">
        <svg x="100" y="100" width="824" height="824" viewBox="0 0 100 100">{content.group(1)}</svg>
        <use href="#icon-shape" fill="none" stroke="#fff" stroke-opacity="0.16" stroke-width="8"/>
    </g>
</svg>
"""


def render(source: Path, size: int, output: Path):
    subprocess.run(
        ["rsvg-convert", "--width", str(size), "--height", str(size), "--output", str(output), str(source)], check=True
    )


def main():
    if not shutil.which("rsvg-convert"):
        sys.exit(
            "rsvg-convert isn't installed. It comes with librsvg (brew install librsvg, or your distribution's librsvg)."
        )

    with tempfile.TemporaryDirectory() as temporary:
        large, small = Path(temporary) / "large.svg", Path(temporary) / "small.svg"
        large.write_text(framed(ICONS_DIR / "xenon.svg"))
        small.write_text(framed(ICONS_DIR / "xenon-small.svg"))

        for name, size in ICONSET.items():
            render(small if size <= SMALL_ARTWORK_MAX_SIZE else large, size, ICONSET_DIR / name)
        render(large, WINDOW_ICON_SIZE, ICONS_DIR / "xenon.png")

    print(f"Rendered {len(ICONSET)} iconset sizes and the {WINDOW_ICON_SIZE}px window icon.")


if __name__ == "__main__":
    main()
