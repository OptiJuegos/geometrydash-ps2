#!/usr/bin/env python3
"""Convert a GD3DS .t3s manifest into PS2 native T8 sprite pages.

P2H1 little-endian format:
  char[4] magic, u32 image_count, u32 page_count
  each page: u16 width, u16 height, u8 psm (=2), u8 reserved[3],
             256 x RGBA8 CLUT (GS CSM1 order, alpha 0..128),
             width*height unswizzled T8 texels
  each image: u16 page, x, y, width, height, reserved

Keeping the subimage array in manifest order lets the Citro2D-compatible
renderer reuse the game's C2D_SpriteSheetGetImage indices. Uses Pillow's
FASTOCTREE, which supports transparency and does not require libimagequant.
"""
import argparse
import os
import pathlib
import struct
import sys

from PIL import Image


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "psp" / "tools"))
from mkatlas import blit, pack, parse_t3s  # noqa: E402


CLEAR_INDEX = 255


def pack_page(canvas):
    # Fully clear pixels get an entry of their own (CLEAR_INDEX). Left in the
    # octree, their leftover RGB (a lossy GD copy's grey background) merged
    # with solid colors into half-clear entries: boxes around sprites. So they
    # are painted a color the page already has before quantizing, which adds
    # no cluster, and pointed at the clear entry after.
    clear = canvas.getchannel("A").point(lambda a: 255 if a == 0 else 0)
    solid = canvas
    if clear.getbbox():
        filler = next((px for px in canvas.get_flattened_data() if px[3] == 255), (0, 0, 0, 0))
        solid = canvas.copy()
        solid.paste(filler, mask=clear)
    indexed = solid.quantize(colors=CLEAR_INDEX, method=Image.Quantize.FASTOCTREE,
                             dither=Image.Dither.NONE)
    indexed.paste(CLEAR_INDEX, mask=clear)
    palette = indexed.getpalette("RGBA")
    if palette is None:
        raise ValueError("Pillow returned a palette without RGBA channels")
    entries = [tuple(palette[i:i + 4]) for i in range(0, len(palette), 4)][:CLEAR_INDEX]
    entries += [(0, 0, 0, 0)] * (256 - len(entries))
    assert len(entries) == 256 and entries[CLEAR_INDEX] == (0, 0, 0, 0)

    # CSM1 permutes the second and third groups of 8 entries in every 32.
    clut = bytearray(256 * 4)
    for index, (r, g, b, a) in enumerate(entries):
        dest = (index & ~0x18) | ((index & 0x08) << 1) | ((index & 0x10) >> 1)
        clut[dest * 4:dest * 4 + 4] = bytes((r, g, b, (a * 128 + 127) // 255))
    return bytes(clut), indexed.tobytes()


def convert(manifest, output, only=None):
    # The PSP override logic resolves the psp/gfx/ replacement relative to
    # the project root, even when this script receives absolute paths.
    manifest = manifest.resolve()
    output = output.resolve()
    os.chdir(ROOT)
    fmt, border, paths = parse_t3s(os.path.relpath(manifest, ROOT))
    selected = list(range(len(paths))) if only is None else sorted(set(only))
    if not selected or any(i < 0 or i >= len(paths) for i in selected):
        raise ValueError("--only contains an invalid image index")
    images = [Image.open(paths[i]).convert("RGBA") for i in selected]
    placements, sizes = pack(images, border)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as handle:
        # Sparse sheets retain the original index space. Unselected entries
        # have zero dimensions and are never passed to C2D_SpriteFromSheet.
        handle.write(struct.pack("<4sII", b"P2H1", len(paths), len(sizes)))
        for page_index, (width, height) in enumerate(sizes):
            canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
            for i, original in enumerate(images):
                pidx, x, y, padding = placements[i]
                if pidx == page_index:
                    blit(canvas, original, x, y, padding, border)
            clut, texels = pack_page(canvas)
            handle.write(struct.pack("<HHB3x", width, height, 2))
            handle.write(clut)
            handle.write(texels)
        sprites = [struct.pack("<6H", 0, 0, 0, 0, 0, 0)] * len(paths)
        for index, im in enumerate(images):
            page, x, y, _pad = placements[index]
            sprites[selected[index]] = struct.pack("<6H", page, x, y, im.width, im.height, 0)
        for sprite in sprites:
            handle.write(sprite)
    print(f"{output}: {len(images)}/{len(paths)} sprites, {len(sizes)} T8 pages, "
          f"{output.stat().st_size} bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--only", help="Comma-separated original sprite indices to retain")
    args = parser.parse_args()
    indices = None if args.only is None else [int(s) for s in args.only.split(",")]
    convert(args.manifest, args.output, indices)


if __name__ == "__main__":
    main()
