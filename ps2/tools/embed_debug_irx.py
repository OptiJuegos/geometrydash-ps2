#!/usr/bin/env python3
"""Embed existing local PS2SDK USB IRX into the debug ELF (not release).

The generated assembly remains under ps2/out/debug; SDK binaries are neither
modified nor published as separate downloaded files.
"""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    paths = [("usbd", args.sdk / "iop/irx/usbd.irx"),
             ("mass", args.sdk / "iop/irx/usbhdfsd.irx")]
    lines = ["/* Generated debug-only USB modules; do not edit. */",
             '.section .rodata', '.balign 16']
    for label, source in paths:
        source = source.resolve(strict=True)
        if source.stat().st_size < 1000:
            raise ValueError(f"Invalid IRX size: {source}")
        asm_path = source.as_posix().replace('"', '\\"')
        lines.extend([f'.global gdps2_debug_{label}_start',
                      f'.global gdps2_debug_{label}_end',
                      f'gdps2_debug_{label}_start:',
                      f'  .incbin "{asm_path}"',
                      f'gdps2_debug_{label}_end:', '.balign 16'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines) + '\n', encoding='ascii')
    print(f"PASS: generated debug USB IRX embedding: {args.output}")


if __name__ == "__main__":
    main()
