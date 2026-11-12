#!/usr/bin/env python3
"""PS2 logo regression: do not change the original PSP title sprites."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "gfx/ui/ui_2"
ATLAS = ASSETS / "ui_2.t3s"


def main():
    lines = [s.strip() for s in ATLAS.read_text(encoding="utf-8").splitlines()
             if s.strip() and not s.lstrip().startswith("--")]
    assert lines[0] == "title.png" and lines[3] == "title_alt.png", "PSP title ids changed"
    assert lines[53:55] == ["title_ps2.png", "title_alt_ps2.png"], "PS2 sprite indices not appended"
    assert len(lines) == 55, "Unexpected UI atlas ordering"
    for src, dest in (("title.png", "title_ps2.png"),
                      ("title_alt.png", "title_alt_ps2.png")):
        original = Image.open(ASSETS / src).convert("RGBA")
        ps2 = Image.open(ASSETS / dest).convert("RGBA")
        assert original.size == ps2.size == (428, 96)
        # All original Geometry Dash art outside the subtitle mask is
        # preserved, without shrinking/stretching the rest of the title.
        for region in ((0, 0, 428, 51), (0, 51, 238, 96), (337, 51, 428, 96)):
            assert original.crop(region).tobytes() == ps2.crop(region).tobytes(), (dest, region)
        assert ps2.crop((238, 51, 337, 96)).getchannel("A").getbbox() is None

    menu = (ROOT / "source/menus/main_menu.c").read_text(encoding="utf-8")
    assert '"romfs:/menus/main_menu_top_ps2.txt"' in menu
    assert 'ui_image_set_image(title, 54, 1);' in menu
    assert 'ui_image_set_image(title, 3, 1);' in menu, "PSP/3DS alternate title altered"
    for name in ("main_menu_top_ps2.txt", "loading_screen_top_ps2.txt"):
        text = (ROOT / "romfs/menus" / name).read_text(encoding="utf-8")
        assert 'id=53 sheet=1' in text, name
        stage = ROOT / "ps2/out/release/romfs/menus" / name
        if stage.exists():
            assert stage.read_bytes() == (ROOT / "romfs/menus" / name).read_bytes(), name
    print("PASS: PS2 menu/loading title has no PSP subtitle; original PSP/3DS title IDs and art preserved")


if __name__ == "__main__":
    main()
