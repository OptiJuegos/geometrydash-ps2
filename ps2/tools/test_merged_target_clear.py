#!/usr/bin/env python3
"""Regression: BOTTOM -> TOP must not erase PS2 single framebuffer.

The original 3DS source can draw its two screens in either order. The PS2
backend merges them on one GS target and must clear exactly once/frame.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GS = (ROOT / "ps2/src/c2d_ps2.c").read_text(encoding="utf-8")
LEVEL = (ROOT / "source/menus/level_select.c").read_text(encoding="utf-8")
MAIN = (ROOT / "source/menus/main_menu.c").read_text(encoding="utf-8")


def body(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 1
    for pos in range(brace + 1, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if not depth:
                return source[brace + 1:pos]
    raise AssertionError(f"Unterminated function {marker}")


def test():
    frame_begin = body(GS, "bool C3D_FrameBegin(")
    target_clear = body(GS, "void C2D_TargetClear(")
    assert frame_begin.count("gsKit_clear(gpu,") == 1, "PS2 GS frame must be cleared once"
    assert "gsKit_prim_sprite(" not in target_clear, "A virtual target clear overwrites the merged framebuffer"
    assert "gsKit_clear(" not in target_clear, "Virtual target clears must be no-ops"
    assert "gsKit_set_scissor(" not in target_clear, "Virtual target clear must not change drawing region"

    level_frame = LEVEL[LEVEL.index("C3D_FrameBegin(C3D_FRAME_SYNCDRAW)"):]
    assert level_frame.index("C2D_SceneBegin(bot)") < level_frame.index("begin_top_eye(eye)"), "Level selector changed draw order"
    assert level_frame.index("ui_screen_draw(&default_screen)") < level_frame.rindex("draw_dots(curr_level_id)"), "Level selector dots must be last on PS2"

    main_frame = MAIN[MAIN.index("C3D_FrameBegin(C3D_FRAME_SYNCDRAW)"):]
    assert main_frame.index("begin_top_eye(eye)") < main_frame.index("C2D_SceneBegin(bot)"), "Main menu changed draw order"
    assert "C2D_TargetClear(target, C2D_Color32(0, 0, 0, 255))" in (ROOT / "source/utils/gfx.c").read_text(encoding="utf-8"), "TOP clear call absent; regression not exercised"

    print("PASS: TOP/BOTTOM target clears preserve GS composition in both orders, frame is cleared once")


if __name__ == "__main__":
    test()
