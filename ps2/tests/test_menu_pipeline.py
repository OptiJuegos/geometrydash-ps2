#!/usr/bin/env python3
"""Host tests for the original PSP layout -> native PS2 atlas pipeline."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ps2" / "tools"))
from make_menu_ps2 import load_layout, load_merge, generate  # noqa: E402


class MenuPipelineTests(unittest.TestCase):
    def test_psp_original_coordinates_and_screen_assignment(self):
        items = load_layout()
        self.assertEqual(len(items), 7)
        self.assertEqual(load_merge(), (40.0, 0.0, 1.0))
        self.assertEqual(items[0]["screen"], 0)
        self.assertEqual((items[0]["x"], items[0]["y"]), (200.0, 36.0))
        self.assertEqual(items[1]["screen"], 1)
        self.assertEqual((items[1]["x"], items[1]["y"]), (160.0, 120.0))
        self.assertEqual(items[1]["action"], "level_select")
        self.assertEqual(items[1]["keys"], ["KEY_A", "KEY_START"])
        self.assertEqual((items[-1]["x"], items[-1]["y"]), (-4.0, 216.0))
        self.assertEqual({item["action"] for item in items if item["action"]},
                         {"level_select", "icon_kit", "creator_menu", "statistics", "settings"})

    def test_atlas_and_generated_header(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            atlas = output / "menu.p2sheet"
            header = output / "menu_layout.h"
            generate(atlas, header)
            self.assertTrue(header.exists())
            generated = header.read_text(encoding="utf-8")
            self.assertIn("PS2_MENU_MERGE_X 40.000000f", generated)
            self.assertIn("PS2_MENU_MERGE_SCALE 1.000000f", generated)
            self.assertIn("160.000000f, 120.000000f", generated)
            self.assertIn("KEY_A | KEY_START", generated)
            self.assertIn("unsigned char screen", generated)

            payload = atlas.read_bytes()
            magic, sprite_count, page_count = struct.unpack_from("<4sII", payload)
            self.assertEqual((magic, sprite_count, page_count), (b"P2H1", 8, 1))
            offset = 12
            pages = []
            for _ in range(page_count):
                w, h, psm = struct.unpack_from("<HHB", payload, offset)
                self.assertEqual((w, h, psm), (512, 512, 2))
                pages.append((w, h))
                offset += 8 + 1024 + w * h

            self.assertEqual(len(payload), offset + sprite_count * 12)
            for i in range(sprite_count):
                page, x, y, w, h, unused = struct.unpack_from("<6H", payload, offset + i * 12)
                self.assertEqual(unused, 0)
                self.assertLess(page, len(pages))
                self.assertLessEqual(x + w, pages[page][0])
                self.assertLessEqual(y + h, pages[page][1])


if __name__ == "__main__":
    unittest.main()
