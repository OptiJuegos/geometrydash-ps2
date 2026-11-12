#!/usr/bin/env python3
"""Pack PS2 romfs/ in Minecraft/OptiCraft MCPK v1 format (no compression).

Usage: python ps2/tools/make_assets_pak.py [romfs_directory] [output.pak]
The archive contains paths relative to romfs/: menus/main_menu.txt, gfx/..., etc.
Files are streamed, never all held in RAM. The output replaces atomically.
The hash-sorted MCPK index is independent of the physical data order.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import struct

MAGIC = b"MCPK"
ALIGN = 64
CD_SECTOR = 2048
HEADER = struct.Struct(">4s7I")
RECORD = struct.Struct(">4I")

# The resource order used by PS2 startup (source/main.c, ui_screen.c).
# Keep the MCPK table hash-sorted; only the data extents use this ordering.
BOOT_SEQUENCE = (
    "gfx/startup_logo.p2sheet",
    "gfx/ui.p2sheet", "gfx/ui_2.p2sheet", "gfx/goldFont.p2sheet",
    "gfx/bars.p2sheet", "gfx/bg_sheet_01.p2sheet",
    "gfx/windows.p2sheet", "gfx/bigFont.p2sheet",
    "gfx/chatFont.p2sheet", "gfx/bg_gradient.p2sheet",
    "gfx/bg_sheet_02.p2sheet", "gfx/grounds.p2sheet",
    "gfx/sprites.p2sheet", "gfx/portals.p2sheet",
    "gfx/glow.p2sheet", "gfx/icons.p2sheet",
    "gfx/trails.p2sheet", "gfx/particles.p2sheet",
    "sfx/playSound_01.wav", "sfx/quitSound_01.wav",
    "sfx/explode_11.wav", "sfx/endStart_02.wav",
    "sfx/honk.wav", "sfx/highscoreGet02.wav",
)
BOOT_PRIORITY = {name.casefold(): index for index, name in enumerate(BOOT_SEQUENCE)}


def physical_order(key: str) -> tuple[int, int, str]:
    folded = key.casefold()
    if folded in BOOT_PRIORITY:
        return (0, BOOT_PRIORITY[folded], folded)
    # Songs are needed only when playing a level; keep them after boot data.
    group = (1 if folded.startswith(("menus/", "gfx/")) else
             3 if folded.startswith("songs/") else 2)
    return (group, 0, folded)


def hash_key(key: str) -> int:
    value = 0x811C9DC5
    for byte in key.lower().encode("utf-8"):
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return value


def align_to(value: int, alignment: int = ALIGN) -> int:
    return (value + alignment - 1) // alignment * alignment


def gather(root: Path) -> list[tuple[str, Path, int]]:
    if not root.is_dir():
        raise FileNotFoundError(f"ROMFS directory does not exist: {root}")
    result = []
    seen = set()
    for file in root.rglob("*"):
        if not file.is_file():
            continue
        if file.is_symlink():
            raise ValueError(f"Symlink not allowed in package: {file}")
        key = file.relative_to(root).as_posix()
        folded = key.casefold()
        if folded in seen:
            raise ValueError(f"Duplicate case-insensitive key: {key}")
        seen.add(folded)
        if not key or "\x00" in key or any(x in (".", "..") for x in key.split("/")):
            raise ValueError(f"Invalid pak key: {key!r}")
        result.append((key, file, file.stat().st_size))
    result.sort(key=lambda item: (hash_key(item[0]), item[0].lower()))
    if not result:
        raise ValueError("No files to package")
    return result


def build(root: Path, dest: Path, verify: bool = True,
          layout: str = "hash", alignment: int = ALIGN) -> None:
    if layout not in ("hash", "boot"):
        raise ValueError(f"Unsupported MCPK layout: {layout}")
    if alignment < 64 or alignment & (alignment - 1):
        raise ValueError("MCPK alignment must be a power of two >= 64")
    if dest.resolve().is_relative_to(root.resolve()):
        raise ValueError("Output pak cannot be placed inside the input ROMFS directory")
    files = gather(root)
    table_offset = HEADER.size
    names_offset = table_offset + RECORD.size * len(files)
    names = bytearray()
    offsets = []
    for key, _, _ in files:
        offsets.append(len(names))
        names.extend(key.encode("utf-8") + b"\x00")
    first_data = align_to(names_offset + len(names), alignment)
    cursor = first_data
    # Hash order for directory entries, but sequential read order for
    # on-disc extents. Readers use absolute offsets from the MCPK table.
    physical_files = sorted(files, key=lambda x: physical_order(x[0])) if layout == "boot" else files
    data_offsets: dict[str, int] = {}
    for key, _, size in physical_files:
        if size >= 2**32 or cursor + size > 0x7FFFFFFF:
            raise ValueError("Pak entry too large for PS2 32-bit file offsets")
        data_offsets[key] = cursor
        cursor = align_to(cursor + size, alignment)
    table = bytearray()
    for (key, _, size), name_offset in zip(files, offsets):
        table.extend(RECORD.pack(hash_key(key), name_offset, data_offsets[key], size))
    dest.parent.mkdir(parents=True, exist_ok=True)
    temp = dest.with_name(dest.name + ".tmp")
    try:
        with temp.open("wb") as out:
            out.write(HEADER.pack(MAGIC, 1, len(files), table_offset, names_offset, len(names), alignment, 0))
            out.write(table)
            out.write(names)
            for key, file, size in physical_files:
                offset = data_offsets[key]
                out.write(b"\x00" * (offset - out.tell()))
                with file.open("rb") as src:
                    while chunk := src.read(1024 * 1024):
                        out.write(chunk)
                if out.tell() != offset + size:
                    raise IOError(f"File changed while packaging: {file}")
        if verify:
            with temp.open("rb") as f:
                header = f.read(HEADER.size)
                assert HEADER.unpack(header)[:3] == (MAGIC, 1, len(files))
                for i, (_, path, size) in enumerate(files):
                    _, _, offset, packed_size = RECORD.unpack_from(table, i * RECORD.size)
                    if packed_size != size:
                        raise IOError(f"Incorrect size: {path}")
                    f.seek(offset)
                    digest = hashlib.sha256()
                    left = size
                    while left:
                        chunk = f.read(min(left, 1024 * 1024))
                        if not chunk:
                            raise IOError(f"Truncated pak: {path}")
                        digest.update(chunk)
                        left -= len(chunk)
                    original = hashlib.sha256()
                    with path.open("rb") as src:
                        while chunk := src.read(1024 * 1024):
                            original.update(chunk)
                    if digest.digest() != original.digest():
                        raise IOError(f"Corrupt packed resource: {path}")
        os.replace(temp, dest)
    finally:
        if temp.exists():
            temp.unlink()
    print(f"MCPK v1: {len(files)} files, {dest.stat().st_size:,} bytes, "
          f"layout={layout}, align={alignment}: {dest}")


if __name__ == "__main__":
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("romfs", nargs="?", type=Path, default=repo / "ps2/out/package/romfs")
    parser.add_argument("output", nargs="?", type=Path, default=repo / "ps2/out/package/assets.pak")
    parser.add_argument("--no-verify", action="store_true")
    parser.add_argument("--layout", choices=("hash", "boot"), default="hash",
                        help="physical data order (boot groups PS2 startup assets)")
    parser.add_argument("--align", type=int, default=ALIGN,
                        help="power-of-two byte alignment (2048 for CD sectors)")
    args = parser.parse_args()
    build(args.romfs, args.output, not args.no_verify, args.layout, args.align)
