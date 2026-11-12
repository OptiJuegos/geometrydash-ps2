#!/usr/bin/env python3
"""CPU reconstruction of a PS2 P2H1 T8+CSM1 sheet to isolate GS problems.

Outputs original image IDs sampled out of the *already packaged* atlas.
This is a diagnostic; never changes the game or PSP assets.
"""
import argparse
import struct
from pathlib import Path

from PIL import Image


def read_t8(path):
    raw = Path(path).read_bytes()
    if raw[:4] != b"P2H1":
        raise ValueError("not P2H1")
    count, pages = struct.unpack_from("<II", raw, 4)
    offset = 12
    images = []
    for _ in range(pages):
        w, h, psm = struct.unpack_from("<HHB", raw, offset)
        assert psm == 2, "this tool diagnoses the original T8 converter"
        offset += 8
        clut = raw[offset:offset + 1024]
        offset += 1024
        indices = raw[offset:offset + w * h]
        offset += w * h
        # Inverse of CSM1 conversion in mkatlas_ps2.pack_page().
        lookup = bytearray()
        for i in range(256):
            dst = (i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)
            r, g, b, a = clut[4 * dst:4 * dst + 4]
            lookup.extend((r, g, b, min(255, a * 255 // 128)))
        palette_image = Image.frombytes("P", (w, h), indices)
        palette_image.putpalette(lookup, "RGBA")
        images.append(palette_image.convert("RGBA"))
    sprites = [struct.unpack_from("<6H", raw, offset + i * 12) for i in range(count)]
    return images, sprites


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("atlas", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--sprite", type=int, default=1)
    args = p.parse_args()
    pages, sprites = read_t8(args.atlas)
    page, x, y, w, h, _ = sprites[args.sprite]
    if w == 0 or h == 0:
        raise ValueError("selected sprite has no pixels")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    pages[page].crop((x, y, x + w, y + h)).save(args.output)
    print(f"Atlas sprite {args.sprite}: {w}x{h}, page {page} -> {args.output}")


if __name__ == "__main__":
    main()
