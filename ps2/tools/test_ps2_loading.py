#!/usr/bin/env python3
"""Static PS2 4:3 loading/menu logo regression checks (no emulator or saves)."""
from pathlib import Path
import re

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
LOADING = ROOT / "romfs/menus/loading_screen_top_ps2.txt"
MENU = ROOT / "romfs/menus/main_menu_psp.txt"
STAGED = ROOT / "ps2/out/release/romfs/menus"


def elements(path):
    result = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.lstrip().startswith(("#", "@")):
            continue
        kind, _, body = line.partition(" ")
        props = {name: quoted if quoted else unquoted for name, quoted, unquoted in
                 re.findall(r'(\w+)=(?:"([^"]*)"|([^\s]+))', body)}
        result.append((kind, props))
    return result


def main():
    loading = elements(LOADING)
    menu = elements(MENU)
    images = [props for kind, props in loading if kind == "image"]
    assert len(images) == 1, "PS2 loading should keep only the Geometry Dash title"
    assert images[0].get("tag") == "title" and images[0].get("id") == "53", "PS2 loading must use title without PSP"
    assert not any(props.get("id") == "4" and props.get("sheet") == "1"
                   for kind, props in loading if kind == "image"), "AleFunky on PS2 loading"
    logo = [props for kind, props in menu if kind == "image" and
            props.get("id") == "4" and props.get("sheet") == "1"]
    assert len(logo) == 1 and logo[0].get("ps2hide") == "1", "AleFunky should be hidden only on PS2"

    # Use actual source sprite measurements, not just x coordinate assertions.
    title_w, title_h = Image.open(ROOT / "gfx/ui/ui_2/title.png").size
    alt_w, alt_h = Image.open(ROOT / "gfx/ui/ui_2/title_alt.png").size
    p = images[0]
    x, y = float(p["x"]), float(p["y"])
    sx, sy = float(p["sx"]), float(p["sy"])
    for w, h in ((title_w, title_h), (alt_w, alt_h)):
        assert 48 <= x - w * sx / 2 and x + w * sx / 2 <= 352, "Title clipped in 4:3"
        assert 8 <= y - h * sy / 2 and y + h * sy / 2 <= 232, "Title clipped vertically"

    bars = [props for kind, props in loading if kind == "progressbar"]
    assert len(bars) == 1 and bars[0].get("tag") == "loadprogress"
    bar_w, bar_h = Image.open(ROOT / "gfx/progress_bars/slidergroove.png").size
    bx, by = float(bars[0]["x"]), float(bars[0]["y"])
    bs = float(bars[0]["scale"])
    assert 48 <= bx - bar_w * bs / 2 and bx + bar_w * bs / 2 <= 352
    assert 8 <= by - bar_h * bs / 2 and by + bar_h * bs / 2 <= 232

    for kind, item in loading:
        if kind == "label":
            assert item.get("align") == "CENTER" and float(item["x"]) == 200
            assert 12 < float(item["y"]) < 235
    assert len([1 for kind, item in loading if kind == "label" and item.get("tag") == "splashtext"]) == 1

    code = (ROOT / "source/menus/loading_screen.c").read_text(encoding="utf-8")
    assert '"romfs:/menus/loading_screen_top_ps2.txt"' in code
    assert "#ifndef __PS2__\n    // Bottom Screen" in code, "3DS second screen must not overlay PS2 loading"
    for name in ("source/main.c", "source/menus/external_levels.c"):
        src = (ROOT / name).read_text(encoding="utf-8")
        assert "GDPS2_CROP_X + GDPS2_VISIBLE_W - 12" in src, f"Loading... at 3DS edge in {name}"

    for source in (LOADING, MENU):
        stage = STAGED / source.name
        if stage.exists():
            assert stage.read_bytes() == source.read_bytes(), f"ROMFS file outdated: {source.name}"

    print("PASS: PS2 loading single-screen 4:3, logo/bar centered, AleFunky hidden, in-level loading inside viewport")


if __name__ == "__main__":
    main()
