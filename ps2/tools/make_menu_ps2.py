#!/usr/bin/env python3
"""Build PS2 sprites/layout directly from the game's PSP main-menu files.

Sources of truth:
  psp/menus/main_menu_top_psp.txt (top art)
  psp/menus/main_menu_psp.txt     (merged touch UI)
  gfx/ui/ui.t3s + gfx/ui/ui_2/ui_2.t3s (sprite IDs)

Outputs a compact P2H1 T8 atlas and a C header of original layout records.
Other game systems (particle simulation, level selection, audio) are separate.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import struct
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "psp" / "tools"))
from mkatlas import parse_t3s, pack, blit  # noqa: E402
from mkatlas_ps2 import pack_page  # noqa: E402


SOURCES = (
    "psp/menus/main_menu_top_psp.txt",
    "psp/menus/main_menu_psp.txt",
)
MANIFESTS = {0: "gfx/ui/ui.t3s", 1: "gfx/ui/ui_2/ui_2.t3s"}
KEYS = {
    "A": "KEY_A", "B": "KEY_B", "X": "KEY_X",
    "Y": "KEY_Y", "START": "KEY_START", "SELECT": "KEY_SELECT",
    "UP": "KEY_DUP", "DOWN": "KEY_DDOWN", "LEFT": "KEY_DLEFT",
    "RIGHT": "KEY_DRIGHT", "L": "KEY_L", "R": "KEY_R",
}


def load_layout():
    items = []
    for source in SOURCES:
        for line_no, raw in enumerate((ROOT / source).read_text(encoding="utf-8").splitlines(), 1):
            stripped = raw.strip()
            if not stripped or stripped.startswith("#"):
                continue
            if stripped.startswith("@merge "):
                chunks = stripped.split()
                if len(chunks) != 4:
                    raise ValueError(f"{source}:{line_no} invalid merge")
                for number in chunks[1:]:
                    float(number)  # validate: coordinates applied by C2D scene backend
                continue
            terms = shlex.split(stripped, comments=True)
            if not terms or terms[0] not in {"button", "image"}:
                raise ValueError(f"{source}:{line_no} unknown UI item: {stripped}")
            attrs = {}
            for kv in terms[1:]:
                if "=" not in kv:
                    raise ValueError(f"{source}:{line_no} invalid attribute: {kv}")
                key, val = kv.split("=", 1)
                attrs[key] = val
            index = int(attrs["id"])
            sheet = int(attrs.get("sheet", 0))
            if sheet not in MANIFESTS:
                raise ValueError(f"{source}:{line_no} unsupported sprite sheet {sheet}")
            action = attrs.get("action", "")
            if action and not re.fullmatch(r"[a-z][a-z0-9_]*", action):
                raise ValueError(f"{source}:{line_no} invalid action {action}")
            binds = [k for k in attrs.get("keyBinds", "").split(",") if k]
            unknown = [k for k in binds if k not in KEYS]
            if unknown:
                raise ValueError(f"{source}:{line_no} unknown key binds: {unknown}")
            bind_mask = [KEYS[k] for k in binds]
            scale = float(attrs.get("scale", "1"))
            items.append({
                "type": terms[0], "sheet": sheet, "id": index,
                "screen": 0 if source == SOURCES[0] else 1,
                "x": float(attrs["x"]), "y": float(attrs["y"]),
                "sx": float(attrs.get("sx", scale)),
                "sy": float(attrs.get("sy", scale)),
                "action": action, "tag": attrs.get("tag", ""),
                "keys": bind_mask,
            })
    return items


def load_merge():
    merge = re.search(r'^@merge\s+(\S+)\s+(\S+)\s+(\S+)\s*$',
                      (ROOT / SOURCES[1]).read_text(encoding="utf-8"), re.M)
    if not merge:
        raise ValueError(f"Missing @merge directive in {SOURCES[1]}")
    dx, dy, scale = (float(v) for v in merge.groups())
    if not scale > 0:
        raise ValueError("@merge scale must be positive")
    return dx, dy, scale


def generate(output_atlas: Path, output_header: Path):
    os.chdir(ROOT)
    layout = load_layout()
    merge_dx, merge_dy, merge_scale = load_merge()
    actions_in_engine = set(re.findall(
        r'\{\s*"([a-z_]+)"\s*,\s*action_open_',
        (ROOT / "source/menus/main_menu.c").read_text(encoding="utf-8")))
    unknown_actions = sorted({item["action"] for item in layout
                              if item["action"] and item["action"] not in actions_in_engine})
    if unknown_actions:
        raise ValueError(f"Main menu has no original handlers for {unknown_actions}")
    manifests = {sheet: parse_t3s(path) for sheet, path in MANIFESTS.items()}
    required = list(dict.fromkeys((item["sheet"], item["id"]) for item in layout))
    # Alternate title uses exactly the same call as main_menu.c: ui_2 id 3.
    if (1, 0) in required:
        required.append((1, 3))
    sprites = []
    for sheet, index in required:
        sprite_list = manifests[sheet][2]
        if not 0 <= index < len(sprite_list):
            raise ValueError(f"invalid sprite id {sheet}:{index}")
        sprites.append(Image.open(sprite_list[index]).convert("RGBA"))
    # Respect the original transparent/edge border setting per image.
    placements, sizes = pack(sprites, "edge")
    output_atlas.parent.mkdir(parents=True, exist_ok=True)
    with output_atlas.open("wb") as f:
        f.write(struct.pack("<4sII", b"P2H1", len(sprites), len(sizes)))
        for pi, size in enumerate(sizes):
            canvas = Image.new("RGBA", size, (0, 0, 0, 0))
            for idx, image in enumerate(sprites):
                page, x, y, pad = placements[idx]
                if page == pi:
                    blit(canvas, image, x, y, pad, "edge")
            palette, texels = pack_page(canvas)
            f.write(struct.pack("<HHB3x", *size, 2))
            f.write(palette)
            f.write(texels)
        for idx, image in enumerate(sprites):
            page, x, y, _pad = placements[idx]
            f.write(struct.pack("<6H", page, x, y, image.width, image.height, 0))

    def fnum(v):
        return f"{v:.6f}f"

    lines = [
        "/* Auto-generated by ps2/tools/make_menu_ps2.py; DO NOT EDIT. */",
        "/* Positions and actions originate in psp/menus/main_menu*_psp.txt. */",
        "#pragma once",
        "#include <3ds.h>",
        "typedef struct {",
        "    unsigned short sprite;",
        "    float x, y, sx, sy;",
        "    unsigned char screen; /* GFX_TOP or GFX_BOTTOM */",
        "    unsigned char is_button;",
        "    unsigned int shortcut;",
        "    const char *action;",
        "    const char *tag;",
        "} Ps2MenuEntry;",
        "static const Ps2MenuEntry ps2_menu_entries[] = {",
    ]
    for item in layout:
        slot = required.index((item["sheet"], item["id"]))
        key_expr = " | ".join(item["keys"]) or "0"
        lines.append(f'    {{{slot}, {fnum(item["x"])}, {fnum(item["y"])}, '
                     f'{fnum(item["sx"])}, {fnum(item["sy"])}, '
                     f'{item["screen"]}, {int(item["type"] == "button")}, {key_expr}, '
                     f'"{item["action"]}", "{item["tag"]}"}},')
    lines += [
        "};",
        "#define PS2_MENU_ENTRY_COUNT (sizeof(ps2_menu_entries) / sizeof(ps2_menu_entries[0]))",
        f"#define PS2_MENU_ALT_TITLE_SPRITE {required.index((1, 3)) if (1, 3) in required else 0}",
        f"#define PS2_MENU_MERGE_X {fnum(merge_dx)}",
        f"#define PS2_MENU_MERGE_Y {fnum(merge_dy)}",
        f"#define PS2_MENU_MERGE_SCALE {fnum(merge_scale)}",
    ]
    output_header.parent.mkdir(parents=True, exist_ok=True)
    output_header.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"PS2 main menu: {len(layout)} original UI elements, "
          f"{len(required)} sprites, {len(sizes)} T8 pages, "
          f"{output_atlas.stat().st_size} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("atlas", type=Path)
    parser.add_argument("header", type=Path)
    opts = parser.parse_args()
    generate(opts.atlas.resolve(), opts.header.resolve())
