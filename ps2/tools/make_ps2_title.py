#!/usr/bin/env python3
"""Create PS2-only title sprites with the baked-in PSP subtitle removed.

The original PSP/3DS title.png and title_alt.png are NEVER modified. Both
assets remain 428x96 so their existing center/scale behave identically.
Only the isolated cyan 'PSP' region at the bottom right is made transparent;
the Geometry Dash lettering and original edges remain untouched.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "gfx/ui/ui_2"


def generate(source: str, dest: str) -> None:
    with Image.open(ASSETS / source) as original:
        image = original.convert("RGBA")
    if image.size != (428, 96):
        raise ValueError(f"Unexpected source title size: {source}: {image.size}")
    # The original upper Geometry Dash title has colored pixels through y=49.
    # The blue PSP letters and their black/white outline begin at x≈240,
    # below that. Keep all other pixels, including the title's far-right edge.
    image.paste((0, 0, 0, 0), (238, 51, 337, 96))
    image.save(ASSETS / dest, optimize=True)
    print(f"PS2 title: {dest} ({image.width}x{image.height})")


if __name__ == "__main__":
    generate("title.png", "title_ps2.png")
    generate("title_alt.png", "title_alt_ps2.png")
