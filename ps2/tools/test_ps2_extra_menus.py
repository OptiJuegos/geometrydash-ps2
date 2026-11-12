#!/usr/bin/env python3
"""Check standalone 4:3 conversions against real tags/source and sprite sizes.

This does not execute PCSX2 or touch existing game saves.
"""
import re
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
MENUS = ROOT / "romfs/menus"
STAGED = ROOT / "ps2/out/release/romfs/menus"


def items(name):
    result = []
    for line in (MENUS / name).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line in ("{", "}"):
            continue
        tag, _, body = line.partition(" ")
        props = dict((k, quoted if quoted != "" else bare)
                     for k, quoted, bare in re.findall(r'(\w+)=(?:"([^"]*)"|([^\s]+))', body))
        result.append((tag, props))
    return result


def tagged(rows, tag):
    return [e for _, e in rows if tag in e.get("tag", "").split(",")]


def ensure_load(source, *names):
    text = (ROOT / source).read_text(encoding="utf-8")
    assert "#ifdef __PS2__" in text, source
    for name in names:
        assert '"romfs:/menus/' + name + '"' in text, (source, name)


def ensure_stage(*names):
    for name in names:
        stage = STAGED / name
        if stage.exists():
            assert (MENUS / name).read_bytes() == stage.read_bytes(), name


def main():
    names = (
        "how_to_play_ps2.txt", "how_to_play_top_ps2.txt",
        "level_complete_ps2.txt", "level_complete_top_ps2.txt",
        "external_pop_up_ps2.txt", "external_pop_up_top_ps2.txt",
        "online_level_info_pop_up_ps2.txt", "online_level_info_pop_up_top_ps2.txt",
        "soggy_ps2.txt", "soggy_top_ps2.txt",
    )
    for n in names:
        assert (MENUS / n).exists(), n
    for source, a, b in (
        ("source/menus/how_to_play.c", names[0], names[1]),
        ("source/menus/level_complete.c", names[2], names[3]),
        ("source/menus/external_popup.c", names[4], names[5]),
        ("source/menus/online_level_infobox.c", names[6], names[7]),
        ("source/menus/soggy.c", names[8], names[9]),
    ):
        ensure_load(source, a, b)
    # Tutorial contains eight page title/art/instruction groups; no second
    # top-screen illustration can overlap the touch buttons.
    tutorial = items(names[0])
    assert not items(names[1]);
    for i in range(1, 9):
        assert len(tagged(tutorial, f"page{i}")) == 3
    assert len(tagged(tutorial, "nextbutton")) == 1
    assert {e.get("action") for t, e in tutorial if t in ("button", "windowbutton")} >= {"next", "exit"}
    for t, e in tutorial:
        if t == "image" and "page" in e.get("tag", ""):
            w, h = (305, 91)  # tutorial_01..08 original PNG dimensions
            x, y, scale = float(e["x"]), float(e["y"]), float(e["scale"])
            assert 0 <= x - w*scale/2 and x+w*scale/2 <= 320
            assert 0 <= y - h*scale/2 and y+h*scale/2 <= 240

    # Result screen requires tags used by the 3DS animation/reward code.
    result_top = items(names[3]); result_bottom = items(names[2]);
    for tag in ("window", "levelcomplete", "practicecomplete", "attempts", "jumps", "time",
                "coinParticle1", "coinParticle2", "coinParticle3", "starParticle", "rewardCircle",
                "coin1", "coin2", "coin3", "coin1full", "coin2full", "coin3full",
                "coinfull", "star", "startext", "funnytext"):
        assert tagged(result_top, tag), ("completion", tag)
    assert tagged(result_bottom, "endDarken")
    assert tagged(result_bottom, "endDarken")[0].get("ps2hide") == "1", "Bottom darken obscures top result"
    assert next(kind for kind, _ in result_top) == "darken", "Top must dim world before rendering rewards"
    assert sum(e.get("action") == "restart" for _, e in result_bottom) == 1
    assert sum(e.get("action") == "exit" for _, e in result_bottom) == 1
    sheet2 = [v for v in (ROOT / "gfx/ui/ui_2/ui_2.t3s").read_text(encoding="utf-8").splitlines()
              if v.strip() and not v.startswith("--")]
    table = next(e for kind, e in result_top if kind == "image" and e.get("sheet") == "1" and e.get("id") == "14")
    w, h = Image.open(ROOT / "gfx/ui/ui_2" / sheet2[14]).size
    x, y, scale = float(table["x"]), float(table["y"]), float(table["scale"])
    assert 40 <= x-w*scale/2 and x+w*scale/2 <= 360
    btn_y = min(float(e["y"]) for _, e in result_bottom if e.get("action") in ("restart", "exit"))
    assert y+h*scale/2 < btn_y - 24, "result art covers action buttons"

    popup = items(names[4]); popup_top = items(names[5]);
    assert sum(t == "window" for t, _ in popup) == 1
    assert all(t not in ("window", "darken") for t, _ in popup_top)
    for tag in ("levelname", "creatorname", "description", "downloadcount", "likecount", "stars",
                "difficultyface", "levelid", "likeimage"):
        assert len(tagged(popup_top, tag)) == 1, tag
    for tag in ("songid", "normalprogress", "practiceprogress", "normalprogressvalue", "practiceprogressvalue"):
        assert len(tagged(popup, tag)) == 1, tag
    assert {e.get("action") for t, e in popup if t == "button"} >= {"exit", "info", "play"}
    popup_code = (ROOT / "source/menus/external_popup.c").read_text(encoding="utf-8")
    assert "if (in_infobox) return;" in popup_code, "Parent metadata drawn over foreground infobox"

    info = items(names[6]); assert not items(names[7])
    assert any("available in this version" in e.get("text", "") for t, e in info if t == "label"), "Online modal must not invent stats"
    for tag in ("levelname", "levelcreator", "uploaddate", "updatedate", "starsrequested",
                "gdversion", "totalattempts", "totaljumps", "normalprogressvalue", "practiceprogressvalue"):
        assert len(tagged(info, tag)) == 1, tag
        assert tagged(info, tag)[0].get("ps2hide") == "1", tag
    assert sum(e.get("action") == "exit" for t, e in info if t == "windowbutton") == 1

    soggy = items(names[8]); assert not items(names[9])
    assert sum(t == "image" and e.get("id") == "19" for t, e in soggy) == 1
    assert {e.get("action") for t, e in soggy if t == "button"} == {"exit", "boop"}

    ensure_stage(*names)
    print("PASS: PS2 tutorial, completion, custom level popups and Soggy use one 4:3 screen with required gameplay tags")


if __name__ == "__main__":
    main()
