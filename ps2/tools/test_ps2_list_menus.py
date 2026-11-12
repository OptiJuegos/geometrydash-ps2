#!/usr/bin/env python3
"""Verify PS2 list/search/online-level UI composition without emulator/save writes."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MENUS = ROOT / "romfs/menus"
STAGED = ROOT / "ps2/out/release/romfs/menus"


def read(name):
    rows = []
    for line in (MENUS / name).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line in ("{", "}"):
            continue
        kind, _, rest = line.partition(" ")
        attrs = {k: quoted if quoted else bare for k, quoted, bare in
                 re.findall(r'(\w+)=(?:"([^"]*)"|([^\s]+))', rest)}
        rows.append((kind, attrs))
    return rows


def tags(rows):
    return {tag for _, item in rows for tag in item.get("tag", "").split(",") if tag}


def assert_paths(name, source):
    code = (ROOT / "source/menus" / source).read_text(encoding="utf-8")
    assert "#ifdef __PS2__" in code, source
    for suffix in ("_ps2.txt", "_top_ps2.txt"):
        ps2name = name + suffix
        assert (MENUS / ps2name).exists(), ps2name
        assert ('"romfs:/menus/' + ps2name + '"') in code, (source, ps2name)
        staged = STAGED / ps2name
        if staged.exists():
            assert staged.read_bytes() == (MENUS / ps2name).read_bytes(), ps2name


def main():
    fixtures = (
        ("external_levels", "external_levels.c", "list", {"exit", "go_back"}),
        ("saved_levels", "saved_levels.c", "list", {"exit"}),
        ("search_menu", "search_menu.c", None, {"exit", "openfilters", "clearfilters", "serverswitcher", "disclaimer"}),
        ("online_level_menu", "online_level_menu.c", None, {"exit", "play", "info", "download", "comments"}),
    )
    for name, source, required_list, required_actions in fixtures:
        assert_paths(name, source)
        bottom, top = read(name + "_ps2.txt"), read(name + "_top_ps2.txt")
        assert any(kind == "bggradient" for kind, _ in bottom), name
        assert all(item.get("ps2hide") == "1" for kind, item in top if kind == "bggradient"), name
        assert all(not (kind == "darken" and item.get("ps2hide") != "1") for kind, item in top), name
        if required_list:
            assert required_list in tags(bottom)
        actions = {item.get("action") for _, item in bottom if item.get("action")}
        assert required_actions.issubset(actions), (name, required_actions-actions)
        for kind, item in bottom:
            if kind in ("window", "list"):
                x = float(item.get("x", 0)); w = float(item.get("w", 0));
                if w >= 1:
                    assert 0 <= x-w/2 and x+w/2 <= 320, (name, kind, item)
        for kind, item in top:
            if kind == "window" and item.get("ps2hide") != "1":
                x = float(item["x"]); w = float(item["w"])
                assert 40 <= x-w/2 and x+w/2 <= 360, (name, item)
    # Online top text is the only additional visible header, rather than
    # the old full-screen 3DS window that covered play/progress buttons.
    online_top = read("online_level_menu_top_ps2.txt")
    for tag in ("levelname", "creatorname", "difficultyface", "downloadcount", "likecount", "description", "levelid"):
        assert tag in tags(online_top), tag
    print("PASS: PS2 external/saved lists, search and online level menus use 4:3 layers with preserved navigation")


if __name__ == "__main__":
    main()
