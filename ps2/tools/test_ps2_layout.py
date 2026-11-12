#!/usr/bin/env python3
"""Static regression checks for the PS2 single-screen 4:3 gameplay HUD.

Does not launch an emulator or modify saved games. Actual GS output still
requires visual verification in PCSX2 / on a PS2 display.
"""
from pathlib import Path
import re

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
HUD = ROOT / "romfs/menus/gameplay_ps2.txt"
TOP = ROOT / "romfs/menus/gameplay_top_ps2.txt"
LEVEL_SELECT = ROOT / "romfs/menus/level_select_ps2.txt"
LEVEL_SELECT_TOP = ROOT / "romfs/menus/level_select_top_ps2.txt"
STAGED = ROOT / "ps2/out/release/romfs/menus"


def read_elements(file):
    rows = []
    for line in file.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        element_type, _, attributes = line.partition(" ")
        props = {key: quoted or unquoted for key, quoted, unquoted in
                 re.findall(r'([a-zA-Z_]\w*)=(?:"([^"]*)"|([^\s]+))', attributes)}
        rows.append((element_type, props))
    return rows


def find(rows, tag):
    matches = [r for kind, r in rows if tag in r.get("tag", "").split(",")]
    if not matches:
        raise AssertionError(f"Missing tag {tag}")
    return matches


def hitbox(item, width, height, dx=0):
    x, y = float(item["x"]) + dx, float(item["y"])
    scale = float(item.get("scale", "1"))
    return (x-width*scale/2, y-height*scale/2,
            x+width*scale/2, y+height*scale/2)


def intersects(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def main():
    bottom, top = read_elements(HUD), read_elements(TOP)
    select = read_elements(LEVEL_SELECT)
    stats = read_elements(LEVEL_SELECT_TOP)
    level_source = (ROOT / "source/menus/level_select.c").read_text(encoding="utf-8")
    assert '#ifdef __PS2__' in level_source
    assert '"romfs:/menus/level_select_ps2.txt"' in level_source
    assert '"romfs:/menus/level_select_top_ps2.txt"' in level_source

    # PSP-inspired single-screen level selector: PS2's former visible top
    # stats window was drawn over the bottom card and hid the mobile UI.
    assert any(kind == "bggradient" and e.get("ps2hide") != "1" for kind, e in select)
    assert all(e.get("ps2hide") == "1" for kind, e in stats)
    assert any(kind == "window" for kind, _ in stats)  # state placeholders intact
    main_card = find(select, "card_window")[0]
    neighbor = find(select, "card_window_2")[0]
    assert main_card.get("action") == "open_level"
    assert neighbor.get("action") is None
    assert hitbox(main_card, float(main_card["w"]), float(main_card["h"]), dx=40)[0] >= 80
    assert hitbox(main_card, float(main_card["w"]), float(main_card["h"]), dx=40)[2] <= 320
    assert (main_card["x"], main_card["y"]) == (neighbor["x"], neighbor["y"])
    for tag in ("level_card", "level_card_2", "level_title", "level_title_2",
                "card_face", "card_face_2", "level_stars", "level_stars_2",
                "normalprogress", "normalprogress_2", "normalprogressvalue",
                "normalprogressvalue_2", "practiceprogress", "practiceprogress_2",
                "practiceprogressvalue", "practiceprogressvalue_2", "left", "right"):
        find(select, tag)
    assert {e.get("action") for kind, e in select if kind == "button"} >= {"exit", "move_left", "move_right"}
    for kind, item in select:
        if kind == "button" and item.get("action") in {"move_left", "move_right", "exit"}:
            assert item.get("keyBinds"), item
    for tag in ("coin_1", "coin_2", "coin_3", "coin_1_2", "coin_2_2", "coin_3_2"):
        assert all(e.get("ps2hide") == "1" for e in find(select, tag)), tag

    # Original 3DS statistics objects still exist for update_level_top() and
    # update_level_face(), but PS2 must never render them or their background.
    for tag in ("levelname", "totalattempts", "totaljumps", "normalprogress", "practiceprogress", "level_face"):
        assert len(find(stats, tag)) == 1, tag
    for tag in ("coin_1", "coin_2", "coin_3", "coin_1_full", "coin_2_full", "coin_3_full", "coin_circle"):
        assert all(e.get("ps2hide") == "1" for e in find(bottom, tag)), tag
    for tag in ("coin_1", "coin_2", "coin_3"):
        assert all(e.get("ps2hide") == "1" for e in find(top, tag)), tag

    # Both 3DS screens render onto the one GS framebuffer. The bottom screen
    # must never draw a second pause backdrop over the top progress widgets.
    assert sum(t == "window" and e.get("ps2hide") != "1" and "paused" in e.get("tag", "")
               for t, e in bottom) == 0
    assert sum(t == "window" and e.get("ps2hide") != "1" and "pause_menu" in e.get("tag", "")
               for t, e in top) == 1
    pause_window = [e for t, e in top if t == "window" and
                    "pause_menu" in e.get("tag", "")][0]
    assert float(pause_window["x"]) - float(pause_window["w"])/2 >= 40
    assert float(pause_window["x"]) + float(pause_window["w"])/2 <= 360

    for tag in ("pause_btn", "practice_mode", "music_slider", "sound_slider"):
        find(bottom, tag)
    for tag in ("normalprogress", "normalprogressvalue", "practiceprogress", "practiceprogressvalue", "level_title"):
        find(top, tag)

    # Validate actual sprite sizes and spacing, not just center positions.
    manifest = ROOT / "gfx/ui/ui.t3s"
    names = [row.strip() for row in manifest.read_text(encoding="utf-8").splitlines()
             if row.strip() and not row.startswith("--")]
    button_bounds = []
    for kind, e in bottom:
        if kind != "button" or "paused" not in e.get("tag", "").split(","):
            continue
        img = ROOT / "gfx/ui" / names[int(e["id"])]
        w, h = Image.open(img).size
        r = hitbox(e, w, h, dx=40)
        assert 6 <= r[0] < r[2] <= 394 and 4 <= r[1] < r[3] <= 236, e
        if e.get("action") in ("practice", "unpause", "restart", "exit"):
            button_bounds.append((e["action"], r))
    assert len(button_bounds) == 4
    for i, (name, box) in enumerate(button_bounds):
        for other_name, other_box in button_bounds[i+1:]:
            assert not intersects(box, other_box), (name, other_name)

    # Bars above button row; volume sliders below. No 3DS overlapping layout.
    bar = find(top, "practiceprogress")[0]
    bar_bottom = float(bar["y"]) + (20 * float(bar["scale"]) / 2)
    assert bar_bottom < min(b[1] for _, b in button_bounds)
    for tag in ("music_slider", "sound_slider"):
        slider = find(bottom, tag)[0]
        # GS position includes bottom-screen-to-top merge offset x=40.
        slider_bounds = hitbox(slider, 210, 38, dx=40)
        assert slider_bounds[3] < 236
        assert slider_bounds[1] > max(b[3] for _, b in button_bounds)

    # This verifies assets actually staged by build.bat for PCSX2.
    for name in (HUD.name, TOP.name, LEVEL_SELECT.name, LEVEL_SELECT_TOP.name):
        staged = STAGED / name
        if staged.exists():
            original = {HUD.name: HUD, TOP.name: TOP, LEVEL_SELECT.name: LEVEL_SELECT,
                        LEVEL_SELECT_TOP.name: LEVEL_SELECT_TOP}[name]
            assert staged.read_bytes() == original.read_bytes()

    print("PASS: PS2 classic 4:3 level selector, hidden 3DS Level Stats, unified pause, coins hidden, buttons/sliders clear")


if __name__ == "__main__":
    main()
