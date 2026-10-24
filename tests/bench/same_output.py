#!/usr/bin/env python3
"""Converts every PDF of the bench sets with two builds of the converter and lists the files that differ.

    python3 tests/bench/same_output.py --a /path/to/old/htpd [--b build/htpd] [--set public] [--args '--ocr off']

A change that is meant to leave most documents alone (the reading order of S4: a single-column page in good
order must give the same bytes) is checked with this: the line `generated` of the front matter (the time) is
left out of the comparison. Exit code 1 when a file differs. Both builds must stand next to their libpdfium.so.
"""

from __future__ import annotations

import argparse
import difflib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import run_bench as rb  # noqa: E402


def convert(converter: Path, pdf: Path, out: Path, extra: list[str]) -> str | None:
    r = subprocess.run([str(converter), "convert", str(pdf), "--single", "-o", str(out), "--force", "-q", *extra],
                       capture_output=True, text=True, timeout=3600)
    if r.returncode != 0 or not out.exists():
        return None
    text = out.read_text(encoding="utf-8", errors="replace")
    return re.sub(r"^generated: .*$", "generated: -", text, flags=re.M)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", type=Path, required=True, help="the converter before")
    ap.add_argument("--b", type=Path, default=ROOT / "build" / "htpd", help="the converter after")
    ap.add_argument("--set", default="public", help="sets of run_bench.py (default public)")
    ap.add_argument("--args", default="", help="extra options for both")
    ap.add_argument("--only", help="regex on set/name")
    ap.add_argument("--diff", action="store_true", help="show the changed lines of each difference")
    args = ap.parse_args()
    names = list(rb.PUBLIC) if args.set == "public" else args.set.split(",")
    docs = []
    for s in names:
        docs += [(s, p) for p in rb.SETS[s]()]
    if args.only:
        docs = [(s, p) for s, p in docs if re.search(args.only, f"{s}/{p.stem}")]
    extra = args.args.split()
    same = diff = 0
    with tempfile.TemporaryDirectory() as tmp:
        for s, pdf in docs:
            a = convert(args.a, pdf, Path(tmp) / "a.htpd", extra)
            b = convert(args.b, pdf, Path(tmp) / "b.htpd", extra)
            name = f"{s}/{pdf.stem}"
            if a == b:
                same += 1
                continue
            diff += 1
            changed = "failed" if a is None or b is None else \
                sum(1 for l in difflib.unified_diff(a.splitlines(), b.splitlines(), lineterm="", n=0) if l[:1] in "+-" and l[:3] not in ("+++", "---"))
            print(f"DIFFERENT {name}: {changed} lines")
            if args.diff and a and b:
                for l in list(difflib.unified_diff(a.splitlines(), b.splitlines(), lineterm="", n=0))[:60]:
                    print("   ", l[:160])
    print(f"{same} documents give the same file, {diff} differ")
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
