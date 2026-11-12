"""C and Python ISO9660 name mapping must agree for every romfs file."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import disc_names  # noqa: E402

out = ROOT / "ps2" / "out" / "tests"
out.mkdir(parents=True, exist_ok=True)
exe = out / "disc_names_smoke.exe"
subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'ps2' / 'src'}", str(ROOT / "ps2/tests/disc_names_smoke.c"),
                str(ROOT / "ps2/src/disc_names.c"), "-o", str(exe)], check=True)

rels = ["romfs/" + p.relative_to(ROOT / "romfs").as_posix()
        for p in sorted((ROOT / "romfs").rglob("*")) if p.is_file()]
rels += ["romfs/a-b c.txt", "romfs/" + "x" * 40 + ".p2sheet", "romfs/" + "y" * 40]
got = subprocess.run([str(exe), *rels], check=True, capture_output=True, text=True).stdout.split("\n")[:-1]
bad = 0
for rel, c in zip(rels, got):
    py = disc_names.path(rel)
    if c != py or any(len(s) > 31 for s in py.split("/")):
        print("MISMATCH", rel, c, py)
        bad += 1
seen = {}
for rel in rels:
    key = disc_names.path(rel)
    if key in seen:
        print("COLLISION", rel, seen[key]); bad += 1
    seen[key] = rel
print("PASS" if not bad else "FAIL", len(rels), "paths")
sys.exit(1 if bad else 0)
