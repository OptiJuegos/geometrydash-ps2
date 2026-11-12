#!/usr/bin/env python3
"""Verify hash index compatibility and PS2 CD boot-ordered physical layout."""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from make_assets_pak import BOOT_SEQUENCE, HEADER, RECORD, hash_key


def scan(pak: Path, romfs: Path) -> tuple[dict[str, tuple[int, int]], int]:
    result = {}
    size = pak.stat().st_size
    with pak.open("rb") as f:
        magic, version, count, table_at, names_at, name_size, align, _ = HEADER.unpack(f.read(HEADER.size))
        assert (magic, version) == (b"MCPK", 1)
        assert 0 < count < 100000 and table_at >= HEADER.size
        assert names_at >= table_at + RECORD.size * count and names_at + name_size <= size
        f.seek(names_at)
        names = f.read(name_size)
        f.seek(table_at)
        records = [RECORD.unpack(f.read(RECORD.size)) for _ in range(count)]
        hashes = [record[0] for record in records]
        assert hashes == sorted(hashes), "MCPK index no longer hash-sorted"
        assert align >= 64 and align & (align - 1) == 0
        for h, name_at, offset, length in records:
            end = names.index(b"\0", name_at)
            key = names[name_at:end].decode("utf-8")
            assert h == hash_key(key)
            assert offset % align == 0, f"unaligned {key}: {offset}"
            assert offset + length <= size and key not in result
            source = romfs / key
            assert source.is_file(), f"missing source: {key}"
            assert source.stat().st_size == length, f"size mismatch: {key}"
            result[key] = (offset, length)
            f.seek(offset)
            digest = hashlib.sha256(f.read(length)).digest()
            assert digest == hashlib.sha256(source.read_bytes()).digest(), f"content mismatch: {key}"
            f.seek(table_at + len(result) * RECORD.size)
    assert len(result) == count
    spans = sorted((offset, offset + length, key) for key, (offset, length) in result.items())
    for previous, following in zip(spans, spans[1:]):
        assert previous[1] <= following[0], f"overlap: {previous[2]} / {following[2]}"
    return result, align


def boot_seek_distance(entries: dict[str, tuple[int, int]]) -> int:
    previous_end = None
    total = 0
    for key in BOOT_SEQUENCE:
        if key not in entries:
            continue
        offset, length = entries[key]
        if previous_end is not None:
            total += abs(offset - previous_end)
        previous_end = offset + length
    return total


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--romfs", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--optimized", type=Path, required=True)
    args = parser.parse_args()
    old, old_align = scan(args.baseline, args.romfs)
    new, new_align = scan(args.optimized, args.romfs)
    assert old.keys() == new.keys()
    assert new_align == 2048 and old_align == 64
    found = [key for key in BOOT_SEQUENCE if key in new]
    assert len(found) >= 18, f"too few boot assets found: {len(found)}"
    offsets = [new[key][0] for key in found]
    assert offsets == sorted(offsets), "boot data not in access order"
    assert all(new[key][0] >= offsets[-1] for key in new
               if key.startswith("songs/")), "songs interleaved with boot assets"
    prev = boot_seek_distance(old)
    now = boot_seek_distance(new)
    assert now < prev, "optimized layout did not improve modeled seek distance"
    overhead = args.optimized.stat().st_size - args.baseline.stat().st_size
    print(f"PASS MCPK v1: {len(new)} SHA-256 checked, hash index sorted, "
          f"{new_align}-byte aligned, {len(found)} boot files in access order")
    print(f"Boot access-order seek distance (not measured load time): "
          f"{prev / 1048576:.2f} MiB -> {now / 1048576:.2f} MiB "
          f"({(1-now/prev)*100:.1f}% less); size overhead {overhead:+,} bytes")


if __name__ == "__main__":
    main()
