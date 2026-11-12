"""Protect the exact MIT-licensed upstream cJSON files used by the PS2 port."""
import hashlib
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
THIRD_PARTY = ROOT / "ps2" / "third_party" / "cjson"
HASHES = {
    "cJSON.c": "298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d",
    "cJSON.h": "25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee",
    "LICENSE": "a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c",
}


class CjsonVendorTests(unittest.TestCase):
    def test_upstream_hashes(self):
        for filename, expected in HASHES.items():
            with self.subTest(filename=filename):
                data = (THIRD_PARTY / filename).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), expected)


if __name__ == "__main__":
    unittest.main()
