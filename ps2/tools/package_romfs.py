#!/usr/bin/env python3
"""Create a standalone ROMFS folder for the experimental GD3DS -> PS2 ELF.

The game's calls to C2D_SpriteSheetLoad('romfs:/gfx/NAME.t3x') already
substitute '.p2sheet' inside the PS2 backend. Convert all 24 source manifests
with original sprite ordering; copy non-atlas game data without modifying it.
Output is limited to ps2/out/ so source art and user toolchains stay intact.

Usage:
  python ps2/tools/package_romfs.py
  python ps2/tools/package_romfs.py --only-atlas ui_2 --no-media
  python ps2/tools/package_romfs.py --dry-run
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import types

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "psp" / "tools"))
from mkatlas import pack, parse_t3s  # noqa: E402
from mkatlas_ps2 import convert  # noqa: E402

ATLAS_ROOT = ROOT / "gfx"
SOURCE_ROMFS = ROOT / "romfs"
SAFE_OUTPUT = ROOT / "ps2" / "out"
PSP_TOOLS = ROOT / "psp" / "tools"

# GD 2.0's objects and the robots come from the player's own GD (its Resources
# folder) and wiidash's data/ (AleFunky's Wii port), as in Makefile.psp. They
# are optional: without them those objects don't show.
GD_RESOURCES = ROOT / "steam-resources" / "Resources"
WIIDASH = ROOT / "wiidash"
V2_WORK = SAFE_OUTPUT / "v2work"
# the official songs after the main levels' (Makefile.psp's V2_SONGS)
V2_SONGS = ("GeometricalDominator.mp3", "Deadlocked.mp3", "Fingerdash.mp3", "Dash.mp3")
# The OptiProjects logo shown at boot (PS2 only, ps2/src/startup_ps2.c)
STARTUP_LOGO = ROOT / "ps2" / "gfx" / "logogdoptiproject.png"
STARTUP_WORK = SAFE_OUTPUT / "startup_work"
V2_OPTIONAL = {"gfx/sprites_v2.p2sheet", "gfx/robots.p2sheet"} | {
    "songs/" + Path(s).with_suffix(".wav").name for s in V2_SONGS}


def png_sizes(manifest: Path):
    old = Path.cwd()
    try:
        os.chdir(ROOT)
        _, border, paths = parse_t3s(str(manifest.relative_to(ROOT)))
        sizes = []
        for name in paths:
            with Image.open(name) as img:
                sizes.append(img.size)
    finally:
        os.chdir(old)
    if not sizes:
        raise ValueError(f"Empty spritesheet: {manifest}")
    return border, sizes


def analyze_manifest(manifest: Path):
    border, sizes = png_sizes(manifest)
    # mkatlas.pack requires .size only; no RGBA images until conversion.
    class Picture:
        def __init__(self, size):
            self.size = size
    placements, pages = pack([Picture(s) for s in sizes], border)
    for i, (w, h) in enumerate(pages):
        if not 16 <= w <= 512 or not 16 <= h <= 512:
            raise ValueError(f"{manifest}: page {i}: unsupported {w}x{h}")
    for i, (image_size, (pi, x, y, _)) in enumerate(zip(sizes, placements)):
        w, h = image_size
        pw, ph = pages[pi]
        if w <= 0 or h <= 0 or x+w > pw or y+h > ph:
            raise ValueError(f"{manifest}: invalid index {i} -> page {pi}")
    return {
        "atlas": manifest.stem,
        "manifest": str(manifest.relative_to(ROOT)).replace("\\", "/"),
        "sprites": len(sizes),
        "pages": len(pages),
        "page_sizes": pages,
        "gs_texture_bytes": sum(w*h+1024 for w,h in pages),
        "p2sheet_bytes": 12 + sum(8+1024+w*h for w,h in pages)+12*len(sizes),
    }


def verify_p2sheet(filename: Path, sprites_expected: int | None = None):
    with filename.open("rb") as f:
        size = filename.stat().st_size
        hdr = f.read(12)
        if len(hdr) != 12:
            raise ValueError(f"{filename}: missing P2H1 header")
        magic, images, pages = struct.unpack("<4sII", hdr)
        if magic != b"P2H1" or images > 65535 or not 1 <= pages <= 128:
            raise ValueError(f"{filename}: incorrect header")
        if sprites_expected is not None and images != sprites_expected:
            raise ValueError(f"{filename}: sprites changed {sprites_expected} -> {images}")
        dimensions = []
        used_bytes = 0
        for n in range(pages):
            page_header = f.read(8)
            if len(page_header) != 8:
                raise ValueError(f"{filename}: incomplete page {n}")
            w, h, psm = struct.unpack_from("<HHB", page_header)
            if (psm != 2 or not 16 <= w <= 512 or not 16 <= h <= 512 or
                    w & (w-1) or h & (h-1)):
                raise ValueError(f"{filename}: unsupported GS page {n}: {w}x{h}/{psm}")
            dimensions.append((w, h))
            used_bytes += w*h+1024
            f.seek(1024+w*h, os.SEEK_CUR)
        if f.tell() + images*12 != size:
            raise ValueError(f"{filename}: page payload or sprite table truncated")
        for i in range(images):
            info = f.read(12)
            if len(info) != 12:
                raise ValueError(f"{filename}: incomplete sprite {i}")
            pi, x, y, w, h, _ = struct.unpack("<6H", info)
            if pi >= pages or x+w > dimensions[pi][0] or y+h > dimensions[pi][1]:
                raise ValueError(f"{filename}: sprite {i} bounds outside page")
        if f.tell() != size:
            raise ValueError(f"{filename}: extra bytes")
    return {"sprites": images, "pages": pages, "gs_texture_bytes": used_bytes, "bytes": size}


def sha256(path: Path):
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024*1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def manifest_files():
    atlases = sorted(ATLAS_ROOT.rglob("*.t3s"))
    names = [p.stem for p in atlases]
    if len(names) != len(set(names)):
        raise ValueError("Duplicate atlas filenames in source manifest tree")
    return atlases


def check_static_source_paths(stage: Path):
    """Compare literal game paths against the generated ROMFS tree.

    Format strings (e.g. bg_one_%02d.t3x) cannot be checked as literal
    paths and are reported separately. Their atlas families are converted
    from the .t3s list along with every other sheet.
    """
    checked = set()
    dynamic = set()
    missing = set()
    for source in (ROOT / "source").rglob("*.c"):
        for value in re.findall(r'"(romfs:/[^"\r\n]+)"', source.read_text(encoding="utf-8")):
            if "%" in value:
                dynamic.add(value)
                continue
            relative = Path(value[7:])
            if relative.suffix == ".t3x":
                relative = relative.with_suffix(".p2sheet")
            elif relative.suffix == ".mp3" and relative.parts[0] == "songs":
                relative = relative.with_suffix(".wav")
            checked.add(relative.as_posix())
            if not (stage / relative).is_file():
                missing.add(relative.as_posix())
    return sorted(checked), sorted(dynamic), sorted(missing)


def clean_alpha(folder: Path):
    """Lossy GD copies (the "9MB" ones) store icons with two alpha levels per
    file (26-35 and 214-224): the see-through background (never 0) drew as a
    grey box around each robot part. Nearly clear is clear and nearly solid
    is solid; a full GD's antialiased edges barely change."""
    lut = [0 if a < 64 else 255 if a > 192 else a for a in range(256)]
    for png in folder.glob("*.png"):
        with Image.open(png) as img:
            img = img.convert("RGBA")
        r, g, b, a = img.split()
        Image.merge("RGBA", (r, g, b, a.point(lut))).save(png)


def build_v2_sheet(gfx: Path, name: str, tool: str, gd_res: Path, extra: list, inputs: list,
                   clean=False):
    """Runs a PSP sheet tool (mkv2sheet.py / mkrobots.py) for its .t3s, then
    converts that to a .p2sheet. Skipped when the .p2sheet is newer than its
    inputs."""
    final = gfx / (name + ".p2sheet")
    sources = [PSP_TOOLS / tool, ROOT / "ps2" / "tools" / "mkatlas_ps2.py", Path(__file__)] + inputs
    # the folders it was made from: another GD copy makes it again
    made_from = gfx.parent.parent / (final.name + ".from")  # next to romfs/, not shipped
    from_id = "|".join(str(p) for p in [gd_res, *extra])
    if (final.is_file() and made_from.is_file() and made_from.read_text(encoding="utf-8") == from_id
            and all(final.stat().st_mtime >= p.stat().st_mtime for p in sources)):
        return verify_p2sheet(final)
    work = V2_WORK / name
    # Run in here for its .t3s and PNGs; its last step, the PSP's .psheet
    # (mkatlas.py, which wants libimagequant), is left out
    spec = importlib.util.spec_from_file_location(name + "_tool", PSP_TOOLS / tool)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.subprocess = types.SimpleNamespace(check_call=lambda *a, **k: 0)
    argv = sys.argv
    sys.argv = [tool, str(gd_res), str(work), str(V2_WORK / (name + ".psheet")), *map(str, extra)]
    try:
        module.main()
    finally:
        sys.argv = argv
    if clean:
        clean_alpha(work)
    temporary = final.with_name(final.name + ".tmp")
    old = Path.cwd()
    try:
        convert(work / (name + ".t3s"), temporary)
    finally:
        os.chdir(old)
    checked = verify_p2sheet(temporary)
    os.replace(temporary, final)
    made_from.write_text(from_id, encoding="utf-8")
    return checked


def build_startup_logo(gfx: Path):
    """The boot logo as gfx/startup_logo.p2sheet: 640x480 split into two
    320x480 halves (left, right), as a GS page is 512 wide at most."""
    final = gfx / "startup_logo.p2sheet"
    if not STARTUP_LOGO.is_file():
        print(f"Warning: no {STARTUP_LOGO}: no logo at boot")
        return None
    sources = [STARTUP_LOGO, ROOT / "ps2" / "tools" / "mkatlas_ps2.py", Path(__file__)]
    if final.is_file() and all(final.stat().st_mtime >= p.stat().st_mtime for p in sources):
        return verify_p2sheet(final, 2)
    STARTUP_WORK.mkdir(parents=True, exist_ok=True)
    with Image.open(STARTUP_LOGO) as img:
        img = img.convert("RGBA")
    if img.size != (640, 480):
        img = img.resize((640, 480), Image.LANCZOS)
    img.crop((0, 0, 320, 480)).save(STARTUP_WORK / "left.png")
    img.crop((320, 0, 640, 480)).save(STARTUP_WORK / "right.png")
    manifest = STARTUP_WORK / "startup_logo.t3s"
    manifest.write_text("--atlas --border edge -f rgba8888 -z auto\nleft.png\nright.png\n", encoding="utf-8")
    temporary = final.with_name(final.name + ".tmp")
    old = Path.cwd()
    try:
        convert(manifest, temporary)
    finally:
        os.chdir(old)
    checked = verify_p2sheet(temporary, 2)
    os.replace(temporary, final)
    return checked


def build_v2_sheets(gfx: Path, gd_res: Path, wiidash: Path):
    rows = []
    if not (gd_res / "icons").is_dir():
        print(f"Warning: no GD Resources at {gd_res}: no 2.0 objects, robots or 2.0 songs")
        return rows
    if (wiidash / "data").is_dir():
        info = build_v2_sheet(gfx, "sprites_v2", "mkv2sheet.py", gd_res, [wiidash],
                              [ROOT / "psp" / "gfx2" / "sprites_v2.list"])
        rows.append({"atlas": "sprites_v2", **info})
    else:
        print(f"Warning: no wiidash data/ at {wiidash}: the 2.0 objects won't show")
    info = build_v2_sheet(gfx, "robots", "mkrobots.py", gd_res, [], [], clean=True)
    rows.append({"atlas": "robots", **info})
    return rows


def is_song(path: Path):
    return path.suffix.lower() == ".mp3" and path.relative_to(SOURCE_ROMFS).parts[0] == "songs"


def song_source_id(src: Path):
    st = src.stat()
    return f"{src.resolve()}|{st.st_size}|{st.st_mtime_ns}"


def convert_song(src: Path, dst: Path, ffmpeg: str, made_from: dict):
    """MP3 -> IMA-ADPCM WAV (4 bits/sample). ps2/src/ima_wav.h decodes it.

    Skipped when the WAV was made from this same MP3 (path, size and date: a
    copied-in MP3 keeps its old date, newer than nothing).
    """
    source_id = song_source_id(src)
    if dst.is_file() and made_from.get(dst.name) == source_id:
        return
    temporary = dst.with_name(dst.name + ".tmp.wav")
    result = subprocess.run(
        [ffmpeg, "-v", "error", "-y", "-i", str(src), "-vn", "-map_metadata", "-1",
         "-c:a", "adpcm_ima_wav", str(temporary)],
        capture_output=True, text=True)
    if result.returncode != 0:
        temporary.unlink(missing_ok=True)
        raise ValueError(f"ffmpeg failed on {src.name}: {result.stderr.strip()}")
    os.replace(temporary, dst)
    made_from[dst.name] = source_id


def package(output: Path, names: set[str] | None = None,
            include_media=True, dry_run=False, keep_mp3=False, ffmpeg=None,
            gd_res: Path = GD_RESOURCES, wiidash: Path = WIIDASH):
    # Avoid ever copying onto original source, or user-supplied toolchains.
    output = output.resolve()
    try:
        output.relative_to(SAFE_OUTPUT.resolve())
    except ValueError as error:
        raise ValueError("Output must be inside ps2/out/") from error
    if output == SAFE_OUTPUT.resolve():
        raise ValueError("Output must be a child of ps2/out/")
    stage = output / "romfs"
    atlases = manifest_files()
    if names:
        existing = {p.stem for p in atlases}
        if missing := names-existing:
            raise ValueError(f"Unknown atlas(es): {sorted(missing)}")
        atlases = [p for p in atlases if p.stem in names]

    rows = [analyze_manifest(p) for p in atlases]
    source_files = sorted(p for p in SOURCE_ROMFS.rglob("*")
                          if p.is_file() and p.suffix.lower() != ".t3x")
    if not include_media:
        source_files = [p for p in source_files if
                        p.relative_to(SOURCE_ROMFS).parts[0] not in ("songs", "sfx")]
    songs = [p for p in source_files if is_song(p)]
    if include_media:
        # a copy in romfs/songs wins over the GD folder's
        songs += [gd_res / s for s in V2_SONGS
                  if (gd_res / s).is_file() and not (SOURCE_ROMFS / "songs" / s).is_file()]
    if songs and not dry_run and not ffmpeg:
        raise ValueError("ffmpeg not found: install it, pass --ffmpeg PATH, "
                         "or use --keep-mp3 to ship the MP3 songs as they are")
    if not keep_mp3:
        source_files = [p for p in source_files if not is_song(p)]
    copied_bytes = sum(p.stat().st_size for p in source_files)
    gpu_bytes = sum(row["gs_texture_bytes"] for row in rows)
    print(f"PS2 ROMFS: {len(rows)} sheets, {sum(r['sprites'] for r in rows)} sprites,"
          f" {sum(r['pages'] for r in rows)} GS T8 pages, {gpu_bytes/1048576:.2f} MiB total")
    print(f"Other game files: {len(source_files)} ({copied_bytes/1048576:.2f} MiB)")

    if not dry_run:
        stage.mkdir(parents=True, exist_ok=True)
        gfx = stage / "gfx"
        gfx.mkdir(parents=True, exist_ok=True)
        for original, row in zip(atlases, rows):
            final = gfx / (original.stem + ".p2sheet")
            temporary = final.with_name(final.name + ".tmp")
            old = Path.cwd()
            try:
                convert(original, temporary)
            finally:
                os.chdir(old)
            checked = verify_p2sheet(temporary, row["sprites"])
            if checked["pages"] != row["pages"] or checked["gs_texture_bytes"] != row["gs_texture_bytes"]:
                raise ValueError(f"{original}: unexpected page sizing")
            os.replace(temporary, final)
            row["sha256"] = sha256(final)
        if not names:
            if (info := build_startup_logo(gfx)) is not None:
                rows.append({"atlas": "startup_logo", **info, "sha256": sha256(gfx / "startup_logo.p2sheet")})
            for row in build_v2_sheets(gfx, gd_res, wiidash):
                row["sha256"] = sha256(gfx / (row["atlas"] + ".p2sheet"))
                rows.append(row)
        for src in source_files:
            dst = stage / src.relative_to(SOURCE_ROMFS)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
        song_dir = stage / "songs"
        song_bytes = 0
        if songs and not keep_mp3 and song_dir.is_dir():
            for stale in song_dir.glob("*.mp3"):  # from an earlier package run
                try:
                    stale.unlink()
                except PermissionError:  # e.g. PCSX2 has the file open
                    print(f"Warning: could not remove {stale} (in use)")
        # which MP3 each WAV came from (next to romfs/, not shipped in it)
        made_from_file = output / "song_sources.json"
        try:
            made_from = json.loads(made_from_file.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            made_from = {}
        try:
            for src in songs:
                song_dir.mkdir(parents=True, exist_ok=True)
                dst = song_dir / (src.stem + ".wav")
                convert_song(src, dst, ffmpeg, made_from)
                song_bytes += dst.stat().st_size
        finally:
            if songs:
                made_from_file.write_text(json.dumps(made_from, indent=1) + "\n", encoding="utf-8")
        if songs:
            print(f"Songs: {len(songs)} MP3 -> IMA-ADPCM WAV, {song_bytes/1048576:.2f} MiB")

    checked, dynamic, missing = ([], [], [])
    if not dry_run and not names and include_media:
        checked, dynamic, missing = check_static_source_paths(stage)
        if optional := [m for m in missing if m in V2_OPTIONAL]:
            print(f"Warning: without the player's GD files: {optional}")
            missing = [m for m in missing if m not in V2_OPTIONAL]
        if missing:
            raise ValueError(f"Missing {len(missing)} original game ROMFS paths: {missing[:15]}")

    # These are T8 pages in aggregate, NOT simultaneous VRAM requirements.
    # The double frame-buffer lower bound ignores alignment and GS state.
    buffer_bytes = 640*448*2*2
    info = {
        "format": "GDPS2 ROMFS staging v1",
        "generated": not dry_run,
        "sprite_format": "P2H1/T8 + GS CSM1 CLUT",
        "render": {
            "gs_vram_total": 4*1024*1024,
            "ntsc_framebuffers_640x448_ct16_double_lower_bound": buffer_bytes,
            "estimated_vram_after_framebuffers_upper_bound": 4*1024*1024-buffer_bytes,
            "all_atlas_pages_bytes": gpu_bytes,
            "simultaneous_upload_possible": gpu_bytes + buffer_bytes <= 4*1024*1024,
            "runtime_policy": "gsKit TexManager inline page streaming",
            "note": "Texture VRAM, transfer queue, extra framebuffer alignment and RAM are not measured by this static report",
        },
        "atlases": rows,
        "copied_file_count": len(source_files),
        "copied_file_bytes": copied_bytes,
        "literal_game_paths_checked": len(checked),
        "literal_game_paths_missing": missing,
        "dynamic_game_paths_not_literal": dynamic,
    }
    if not dry_run:
        target = output / "ps2_assets_report.json"
        target.write_text(json.dumps(info, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")
        print(f"Stage ready: {stage}")
        print(f"Report: {target}")
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=SAFE_OUTPUT/"package")
    parser.add_argument("--only-atlas", action="append", help="Convert selected atlas by name; repeat as needed")
    parser.add_argument("--no-media", action="store_true", help="Skip original MP3/WAV assets (faster staging)")
    parser.add_argument("--dry-run", action="store_true", help="Validate sprite bounds and report without writing")
    parser.add_argument("--keep-mp3", action="store_true",
                        help="Also copy the MP3 songs (the WAVs are still made and preferred)")
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"),
                        help="ffmpeg used to make the song WAVs (default: from PATH)")
    parser.add_argument("--gd-resources", type=Path, default=GD_RESOURCES,
                        help="your GD's Resources folder: 2.0 songs and robots (default: steam-resources/Resources)")
    parser.add_argument("--wiidash", type=Path, default=WIIDASH,
                        help="wiidash's folder, with its data/: the 2.0 objects' sprites (default: wiidash)")
    opts = parser.parse_args()
    package(opts.output, set(opts.only_atlas) if opts.only_atlas else None,
            not opts.no_media, opts.dry_run, opts.keep_mp3, opts.ffmpeg,
            opts.gd_resources.resolve(), opts.wiidash.resolve())


if __name__ == "__main__":
    main()
