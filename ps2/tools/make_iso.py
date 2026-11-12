#!/usr/bin/env python3
"""Build a PS2 ISO with ROMFS or an MCPK assets.pak.

Layout: SYSTEM.CNF, the ELF under its disc id (e.g. SLUS_666.01), and either
ASSETS.PAK;1 at the disc root or romfs/ with names from disc_names.py.

Usage:
  python ps2/tools/make_iso.py
  python ps2/tools/make_iso.py --elf ps2/out/pak-release/GeometryDashPS2.elf --pak ps2/out/pak-release/assets.pak --out ps2/out/pak-release/GeometryDashPS2.iso
"""
import argparse
import io
import os
import re
import struct
import sys
from pathlib import Path

import pycdlib

sys.path.insert(0, str(Path(__file__).parent))
import disc_names  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RELEASE = ROOT / "ps2" / "out" / "release"

def check_pak(path: Path) -> int:
    """Reject invalid/truncated MCPK, rather than producing a broken disc."""
    size = path.stat().st_size
    if size < 32 or size > 0x7FFFFFFF:
        raise ValueError(f"invalid MCPK length: {path}")
    with path.open("rb") as fh:
        header = fh.read(32)
        magic, version, count, table, names_start, names_bytes, _, _ = struct.unpack(">4s7I", header)
        if magic != b"MCPK" or version != 1 or not 0 < count <= 100000:
            raise ValueError(f"not MCPK v1: {path}")
        if table < 32 or table + count * 16 > names_start or names_start + names_bytes > size:
            raise ValueError(f"invalid MCPK index bounds: {path}")
        fh.seek(names_start)
        names = fh.read(names_bytes)
        fh.seek(table)
        has_menu = False
        for _ in range(count):
            _, name, offset, length = struct.unpack(">4I", fh.read(16))
            if name >= names_bytes or offset + length > size:
                raise ValueError(f"invalid MCPK entry bounds: {path}")
            end = names.find(b"\0", name)
            if end < 0:
                raise ValueError(f"invalid MCPK filename: {path}")
            if names[name:end].lower() == b"menus/main_menu.txt":
                has_menu = True
        if not has_menu:
            raise ValueError(f"MCPK missing menus/main_menu.txt: {path}")
    return count


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", type=Path, default=RELEASE / "GeometryDashPS2.elf")
    ap.add_argument("--romfs", type=Path, default=RELEASE / "romfs")
    ap.add_argument("--pak", type=Path,
                    help="MCPK assets.pak; ISO will contain ASSETS.PAK;1 instead of romfs/")
    ap.add_argument("--id", default="SLUS_666.01", help="disc id, form SLUS_123.45")
    ap.add_argument("--out", type=Path, default=RELEASE / "GeometryDashPS2.iso")
    ap.add_argument("--vmode", default="NTSC", choices=["NTSC", "PAL"])
    opts = ap.parse_args()

    if not re.fullmatch(r"[A-Z]{4}_\d{3}\.\d{2}", opts.id):
        sys.exit(f"--id must look like SLUS_123.45, got {opts.id}")
    for need in (opts.elf, opts.pak if opts.pak else opts.romfs / "menus" / "main_menu.txt"):
        if not need.exists():
            sys.exit(f"missing {need} (run build.bat first)")
    count = check_pak(opts.pak) if opts.pak else 0

    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=2, vol_ident="GEOMETRYDASHPS2")

    def add_file(src: Path, iso_name: str):
        iso.add_file(str(src), "/" + iso_name + ";1")

    cnf = (f"BOOT2 = cdrom0:\\{opts.id};1\r\nVER = 1.00\r\nVMODE = {opts.vmode}\r\n").encode("ascii")
    iso.add_fp(io.BytesIO(cnf), len(cnf), "/SYSTEM.CNF;1")
    add_file(opts.elf, opts.id)

    made = set()
    size = 0
    if opts.pak:
        add_file(opts.pak, "ASSETS.PAK")
        size = opts.pak.stat().st_size
    else:
        for path in sorted(opts.romfs.rglob("*")):
            rel = "romfs/" + path.relative_to(opts.romfs).as_posix()
            mangled = disc_names.path(rel)
            if path.is_dir():
                continue
            parts = mangled.split("/")
            for i in range(1, len(parts)):
                d = "/" + "/".join(parts[:i])
                if d not in made:
                    iso.add_directory(d)
                    made.add(d)
            add_file(path, mangled)
            count += 1
            size += path.stat().st_size
    opts.out.parent.mkdir(parents=True, exist_ok=True)
    temporary = opts.out.with_name(opts.out.name + ".tmp")
    try:
        iso.write(str(temporary))
        iso.close()
        # Verify ISO9660 directory entries before replacing any prior release.
        check = pycdlib.PyCdlib()
        try:
            check.open(str(temporary))
            header_out = io.BytesIO()
            check.get_file_from_iso_fp(header_out, iso_path="/SYSTEM.CNF;1")
            if header_out.getvalue() != cnf:
                raise IOError("ISO SYSTEM.CNF verification failed")
            if opts.pak:
                record = check.get_record(iso_path="/ASSETS.PAK;1")
                if record.data_length > 0 and record.data_length != size:
                    raise IOError("ISO assets.pak length does not match input")
            else:
                check.get_record(iso_path="/ROMFS/MENUS/MAIN_MENU.TXT;1")
            check.get_record(iso_path="/" + opts.id + ";1")
        finally:
            check.close()
        os.replace(temporary, opts.out)
    finally:
        if temporary.exists():
            temporary.unlink()
    print(f"ISO: {opts.out} ({opts.out.stat().st_size / 1048576:.1f} MiB), "
          f"{count} {'MCPK entries' if opts.pak else 'romfs files'} "
          f"({size / 1048576:.1f} MiB), id {opts.id}")


if __name__ == "__main__":
    main()
