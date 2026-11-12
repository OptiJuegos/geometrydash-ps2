#!/usr/bin/env python3
"""Verify that a PS2 MCPK ISO boots the expected ELF and contains the intact pak.

Usage: python ps2/tests/pak_iso_smoke.py
Requires pycdlib (also required by ps2/tools/make_iso.py).
"""
import argparse
import hashlib
import io
import re
import tempfile
from pathlib import Path

import pycdlib

ROOT = Path(__file__).resolve().parents[2]
PAK_RELEASE = ROOT / "ps2" / "out" / "pak-release"


def digest(path: Path) -> bytes:
    value = hashlib.sha256()
    with path.open("rb") as src:
        for chunk in iter(lambda: src.read(1024 * 1024), b""):
            value.update(chunk)
    return value.digest()


def iso_digest(iso: pycdlib.PyCdlib, identifier: str) -> bytes:
    with tempfile.TemporaryFile() as temp:
        iso.get_file_from_iso_fp(temp, iso_path=identifier)
        temp.seek(0)
        value = hashlib.sha256()
        for chunk in iter(lambda: temp.read(1024 * 1024), b""):
            value.update(chunk)
        return value.digest()


def verify(image: Path, elf: Path, pak: Path, expected_id: str) -> None:
    if not re.fullmatch(r"[A-Z]{4}_\d{3}\.\d{2}", expected_id):
        raise ValueError("Invalid disc ID")
    iso = pycdlib.PyCdlib()
    iso.open(str(image))
    try:
        config = io.BytesIO()
        iso.get_file_from_iso_fp(config, iso_path="/SYSTEM.CNF;1")
        if f"BOOT2 = cdrom0:\\{expected_id};1".encode() not in config.getvalue():
            raise AssertionError("SYSTEM.CNF does not boot this ELF")
        if iso_digest(iso, f"/{expected_id};1") != digest(elf):
            raise AssertionError("ELF bytes differ inside ISO")
        if iso_digest(iso, "/ASSETS.PAK;1") != digest(pak):
            raise AssertionError("MCPK bytes differ inside ISO")
        contents = [
            entry.file_identifier().decode("ascii", "replace")
            for entry in iso.list_children(iso_path="/")
        ]
        if any(entry.startswith("ROMFS") for entry in contents):
            raise AssertionError("Unexpected duplicated ROMFS in MCPK ISO")
    finally:
        iso.close()
    print(f"PASS: ISO SYSTEM.CNF, {expected_id}, ASSETS.PAK SHA-256 and no duplicated ROMFS")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=PAK_RELEASE / "GeometryDashPS2-PAK.iso")
    ap.add_argument("--elf", type=Path, default=PAK_RELEASE / "GeometryDashPS2.elf")
    ap.add_argument("--pak", type=Path, default=PAK_RELEASE / "assets.pak")
    ap.add_argument("--id", default="SLUS_666.01")
    args = ap.parse_args()
    verify(args.iso, args.elf, args.pak, args.id)
