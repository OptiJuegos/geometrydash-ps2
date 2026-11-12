"""Validate PS2 P2H1 package metadata without needing the EE toolchain."""
import json
import struct
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ps2" / "tools"))
from package_romfs import analyze_manifest, manifest_files, verify_p2sheet  # noqa: E402


class FullAtlasTests(unittest.TestCase):
    def test_all_manifest_names_and_geometry(self):
        atlases = manifest_files()
        self.assertEqual(len(atlases), 24)
        rows = [analyze_manifest(path) for path in atlases]
        self.assertEqual(sum(row["sprites"] for row in rows), 2895)
        self.assertEqual(sum(row["pages"] for row in rows), 43)
        self.assertGreater(sum(row["gs_texture_bytes"] for row in rows), 4*1024*1024)
        for row in rows:
            self.assertLessEqual(row["pages"], 128)
            self.assertGreater(row["sprites"], 0)
            self.assertGreater(row["gs_texture_bytes"], 0)

    def test_valid_and_invalid_binary_atlas(self):
        header = struct.pack("<4sII", b"P2H1", 1, 1)
        page = struct.pack("<HHB3x", 16, 16, 2)
        palette = bytes(1024)
        texels = bytes(16*16)
        sprite = struct.pack("<6H", 0, 4, 5, 8, 9, 0)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.p2sheet"
            blob = header+page+palette+texels+sprite
            path.write_bytes(blob)
            result = verify_p2sheet(path, 1)
            self.assertEqual(result["gs_texture_bytes"], 1280)
            self.assertEqual(result["sprites"], 1)

            path.write_bytes(blob[:-1])
            with self.assertRaises(ValueError):
                verify_p2sheet(path)

            path.write_bytes(b"PSH1" + blob[4:])
            with self.assertRaises(ValueError):
                verify_p2sheet(path)

            invalid_sprite = struct.pack("<6H", 0, 12, 5, 8, 9, 0)
            path.write_bytes(header+page+palette+texels+invalid_sprite)
            with self.assertRaises(ValueError):
                verify_p2sheet(path)

    def test_generated_package_when_present(self):
        report = ROOT / "ps2/out/package/ps2_assets_report.json"
        if not report.is_file():
            self.skipTest("Full package not yet generated; run package_romfs.py")
        info = json.loads(report.read_text(encoding="utf-8"))
        stage = report.parent / "romfs/gfx"
        self.assertTrue(info["generated"])
        self.assertEqual(len(info["atlases"]), 24)
        for row in info["atlases"]:
            path = stage / (row["atlas"]+".p2sheet")
            result = verify_p2sheet(path, row["sprites"])
            self.assertEqual(result["pages"], row["pages"])
            self.assertEqual(result["gs_texture_bytes"], row["gs_texture_bytes"])
        self.assertTrue((report.parent / "romfs/menus/main_menu.txt").is_file())


if __name__ == "__main__":
    unittest.main()
