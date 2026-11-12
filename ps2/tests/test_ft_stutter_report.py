#!/usr/bin/env python3
"""Smoke checks for diagnostic-window parsing (also works on legacy logs)."""

import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory


TOOL = Path(__file__).resolve().parents[1] / "tools" / "ft_stutter_report.py"
spec = importlib.util.spec_from_file_location("ft_stutter_report", TOOL)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


with TemporaryDirectory() as directory:
    path = Path(directory) / "ft.log"
    path.write_text(
        "GDPS2 FT VBLANK: gap1=117 gap2=2 gap3plus=0 gap_max=2 (120 presented frames)\n"
        "GDPS2 FT GS: frames=120 flush_avg=0.00ms queue_avg=0.00ms "
        "flip_avg=15.00ms flip_max=18.50ms tex_avg=0.0ms\n"
        "GDPS2 FT VRAM: budget=2976KB pages_avg=13 pages_max=15 "
        "est_uploads_avg=4 est_uploads_max=12 est_upload_kb_avg=800 "
        "est_upload_kb_max=2352 (shadow LRU)\n"
        "GDPS2 FT: frames=120 avg=16.67ms max=34.23ms over20=1 over33=1 "
        "update_avg=2.50ms update_max=8.00ms draw_avg=14.17ms "
        "draw_max=26.23ms max_physics_steps=5\n",
        encoding="utf-8",
    )
    rows = report.read_windows(path)
    assert len(rows) == 1
    assert rows[0]["gap2"] == 2 and rows[0]["gap_max"] == 2
    assert rows[0]["vram_upload_kb_max_est"] == 2352
    assert "over33=1" in report.summarize(path, rows, 0)
    assert "no FT windows" in report.summarize(path, rows, 1)
    path.write_text(
        "GDPS2 FT: frames=120 avg=16.66ms max=18.0ms over20=0 over33=0 "
        "update_avg=1.0ms update_max=2.0ms draw_avg=15.0ms "
        "draw_max=17.0ms max_physics_steps=4\n",
        encoding="utf-8",
    )
    legacy = report.read_windows(path)
    assert legacy[0]["gap1"] == ""
    assert "VBlank unavailable" in report.summarize(path, legacy, 0)

print("PASS frametime/VBlank report: peak, histogram, VRAM estimate and legacy")
