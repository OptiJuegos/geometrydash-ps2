#!/usr/bin/env python3
"""Static PS2 4:3 creator/icon kit migration checks; never runs PCSX2."""
from pathlib import Path
import re
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ROMFS = ROOT / "romfs/menus"
STAGED = ROOT / "ps2/out/release/romfs/menus"


def read(filename):
    entries = []
    for line in (ROMFS / filename).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line in ("{", "}"):
            continue
        kind, _, body = line.partition(" ")
        attr = {k: quoted if quoted else bare for k, quoted, bare in
                re.findall(r'(\w+)=(?:"([^"]*)"|([^\s]+))', body)}
        entries.append((kind, attr))
    return entries


def tagged(entries, tag):
    return [attrs for _, attrs in entries if tag in attrs.get("tag", "").split(",")]


def assert_staged(name):
    path = STAGED / name
    if path.exists():
        assert path.read_bytes() == (ROMFS / name).read_bytes(), name


def test_creator():
    source = (ROOT / "source/menus/creator_menu.c").read_text(encoding="utf-8")
    assert '#ifdef __PS2__' in source
    assert '"romfs:/menus/creator_menu_ps2.txt"' in source
    assert '"romfs:/menus/creator_menu_top_ps2.txt"' in source
    bottom = read("creator_menu_ps2.txt")
    top = read("creator_menu_top_ps2.txt")
    assert all(e.get("ps2hide") == "1" for _, e in top if "gradient_top" in e.get("tag", ""))
    actions = {a.get("action") for t, a in bottom if t == "button"}
    assert actions == {"exit", "create", "saved", "external", "search"}
    assert tagged(bottom, "create_button")
    title = next(a for t, a in top if t == "image")
    assert 40 <= float(title["x"]) <= 360
    image_paths = [v.strip() for v in (ROOT / "gfx/ui/ui_2/ui_2.t3s").read_text(encoding="utf-8").splitlines()
                   if v.strip() and not v.startswith("--")]
    boxes = []
    for t, a in bottom:
        if t != "button" or a.get("sheet") != "1":
            continue
        w, h = Image.open(ROOT / "gfx/ui/ui_2" / image_paths[int(a["id"])]).size
        x, y, s = float(a["x"]), float(a["y"]), float(a["scale"])
        box = (x-w*s/2, y-h*s/2, x+w*s/2, y+h*s/2)
        assert box[0] >= 0 and box[2] <= 320 and box[1] >= 0 and box[3] <= 240, a
        boxes.append(box)
    assert len(boxes) == 4
    boxes.sort(key=lambda b: b[0])
    assert all(a[2] <= b[0] for a, b in zip(boxes, boxes[1:])), "creator buttons overlap"


def test_icon_kit():
    source = (ROOT / "source/menus/icon_kit.c").read_text(encoding="utf-8")
    assert '#ifdef __PS2__' in source
    for name in ("icon_kit_ps2.txt", "icon_kit_top_ps2.txt"):
        assert '"romfs:/menus/' + name + '"' in source
    bottom, top = read("icon_kit_ps2.txt"), read("icon_kit_top_ps2.txt")
    assert len([1 for kind, _ in bottom if kind == "icon"]) == 21
    assert tagged(bottom, "bg_window")
    assert len(tagged(top, "star_text")) == len(tagged(top, "secretcoins_text")) == 1
    assert all(e.get("ps2hide") == "1" for kind, e in top if kind == "bggradient")
    assert {'exit', 'palette', 'icons_left', 'icons_right', 'icon_selected'}.issubset({e.get('action') for _, e in bottom})
    # No arrow may steal LEFT/RIGHT from the grid. L1/R1 change gamemode;
    # independent L2/R2 buttons switch pages via the PS2 ZL/ZR mapping.
    arrows = {e.get("action"): e for kind, e in bottom if kind == "button" and
              e.get("action") in ("icons_left", "icons_right")}
    assert arrows["icons_left"].get("keyBinds") == "ZL"
    assert arrows["icons_right"].get("keyBinds") == "ZR"
    assert all(e.get("keyBinds", "") not in ("LEFT", "RIGHT") for _, e in bottom)
    assert "#ifdef __PS2__" in source and "btn->keyBinds |= KEY_L;" in source
    assert "btn->keyBinds |= KEY_R;" in source
    for kind, a in bottom:
        if kind != "icon":
            continue
        x, y = int(a["x"]), int(a["y"])
        assert 40 <= x <= 280 and 115 <= y <= 216


def main():
    test_creator()
    test_icon_kit()
    for name in ("creator_menu_ps2.txt", "creator_menu_top_ps2.txt",
                 "icon_kit_ps2.txt", "icon_kit_top_ps2.txt"):
        assert_staged(name)
    print("PASS: PS2 creator/icon menus use centered 4:3, preserve actions/21 icons and dynamic star/coin tags")


if __name__ == "__main__":
    main()
