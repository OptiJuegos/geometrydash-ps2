#!/usr/bin/env python3
"""Summarize PS2 frametime/VBlank telemetry without changing the game.

Each GDPS2 FT* diagnostic counter reports approximately 120 frames at once.
Frame-end and main-loop windows can have a small phase offset; comparisons
between their rows are indicators, not evidence of a single-frame cause.
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


FRAME = re.compile(
    r"^GDPS2 FT: frames=(\d+) avg=([\d.]+)ms max=([\d.]+)ms "
    r"over20=(\d+) over33=(\d+) update_avg=([\d.]+)ms "
    r"update_max=([\d.]+)ms draw_avg=([\d.]+)ms "
    r"draw_max=([\d.]+)ms max_physics_steps=(\d+)"
)
VBLANK = re.compile(
    r"^GDPS2 FT VBLANK: gap1=(\d+) gap2=(\d+) "
    r"gap3plus=(\d+) gap_max=(\d+)"
)
GS = re.compile(
    r"^GDPS2 FT GS: frames=\d+ flush_avg=[\d.]+ms "
    r"queue_avg=[\d.]+ms flip_avg=([\d.]+)ms "
    r"flip_max=([\d.]+)ms"
)
VRAM = re.compile(
    r"^GDPS2 FT VRAM: budget=\d+KB pages_avg=\d+ "
    r"pages_max=(\d+) est_uploads_avg=\d+ est_uploads_max=(\d+) "
    r"est_upload_kb_avg=\d+ est_upload_kb_max=(\d+)"
)


def read_windows(path: Path) -> list[dict]:
    groups: dict[str, list[tuple]] = {"frame": [], "vblank": [], "gs": [], "vram": []}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        for name, expression in (("frame", FRAME), ("vblank", VBLANK), ("gs", GS), ("vram", VRAM)):
            match = expression.search(line)
            if match:
                groups[name].append(match.groups())
                break

    windows = []
    for i, values in enumerate(groups["frame"]):
        frames, avg, peak, over20, over33, upd_avg, upd_peak, draw_avg, draw_peak, steps = values
        row = {
            "window": i + 1, "frames": int(frames), "avg_ms": float(avg),
            "peak_ms": float(peak), "over20": int(over20), "over33": int(over33),
            "update_avg_ms": float(upd_avg), "update_peak_ms": float(upd_peak),
            "draw_avg_ms": float(draw_avg), "draw_peak_ms": float(draw_peak),
            "max_physics_steps": int(steps),
            "gap1": "", "gap2": "", "gap3plus": "", "gap_max": "",
            "flip_avg_ms": "", "flip_max_ms": "",
            "vram_pages_max_est": "", "vram_uploads_max_est": "", "vram_upload_kb_max_est": "",
        }
        if i < len(groups["vblank"]):
            row.update(zip(("gap1", "gap2", "gap3plus", "gap_max"),
                           map(int, groups["vblank"][i])))
        if i < len(groups["gs"]):
            row.update(zip(("flip_avg_ms", "flip_max_ms"),
                           map(float, groups["gs"][i])))
        if i < len(groups["vram"]):
            row.update(zip(("vram_pages_max_est", "vram_uploads_max_est", "vram_upload_kb_max_est"),
                           map(int, groups["vram"][i])))
        windows.append(row)
    return windows


def summarize(path: Path, windows: list[dict], skip: int) -> str:
    rows = windows[skip:]
    if not rows:
        return f"{path}: no FT windows after skip={skip}"
    total = sum(row["frames"] for row in rows)
    over20 = sum(row["over20"] for row in rows)
    over33 = sum(row["over33"] for row in rows)
    peak = max(row["peak_ms"] for row in rows)
    avg = sum(row["avg_ms"] * row["frames"] for row in rows) / total
    out = [f"{path}: {len(rows)} windows, {total} frames; "
           f"avg={avg:.2f}ms peak={peak:.2f}ms over20={over20} over33={over33}"]
    measured = [row for row in rows if row["gap1"] != ""]
    if measured:
        gap1 = sum(row["gap1"] for row in measured)
        gap2 = sum(row["gap2"] for row in measured)
        gap3 = sum(row["gap3plus"] for row in measured)
        out.append(f"  VBlank ({len(measured)} windows): gap1={gap1}, gap2={gap2}, "
                   f"gap3plus={gap3}, max_gap={max(row['gap_max'] for row in measured)}")
        out.append("  gap2/gap3plus indicate repeated VBlanks between presentations; "
                   "gap1 alone does not prove smooth camera motion")
    else:
        out.append("  VBlank unavailable in this log (older build or host log disabled)")
    biggest = sorted(rows, key=lambda row: (row["over33"], row["over20"], row["peak_ms"]), reverse=True)[:5]
    for row in biggest:
        line = f"  window {row['window']}: peak={row['peak_ms']:.2f}ms "
        line += f"over20={row['over20']} over33={row['over33']} "
        line += f"update_max={row['update_peak_ms']:.2f}ms draw_max={row['draw_peak_ms']:.2f}ms"
        if row["gap1"] != "":
            line += f" vblank_gap2={row['gap2']} gap3plus={row['gap3plus']}"
        out.append(line)
    out.append("  Note: VRAM counters are shadow-LRU estimates, NOT measured GS uploads; "
               "diagnostic windows may be offset")
    return "\n".join(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--skip", type=int, default=0,
                        help="skip N startup 120-frame windows, e.g. --skip 1")
    parser.add_argument("--csv", type=Path, help="export window rows (one log only)")
    args = parser.parse_args()
    if args.skip < 0:
        parser.error("--skip must not be negative")
    if args.csv and len(args.logs) != 1:
        parser.error("--csv requires exactly one log")
    for path in args.logs:
        rows = read_windows(path)
        print(summarize(path, rows, args.skip))
        if args.csv and rows:
            with args.csv.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows[args.skip:])
            print(f"  CSV: {args.csv}")


if __name__ == "__main__":
    main()
