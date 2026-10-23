#!/usr/bin/env python3
"""Convert the document zoo and measure each .htpd file against the known text.

    python3 tests/zoo/make_zoo.py build/zoo
    PYTHONPATH=reader python3 tests/zoo/run_zoo.py build/zoo [--converter build/htpd] [--show NAME]

For every PDF made from tests/zoo/sources/<doc>.md it reports:

    text      share of the words of the source that the file holds, in order
    noise     share of the words of the file that are not in the source
              (left-over page headers, footers, the bar of a web page, misread words)
    heads     headings of the source found as section titles
    extra     section titles that are no heading of the source
    tables    table rows of the source found as table rows (" | ")

`--show NAME` prints what is missing and what is extra for one PDF.
"""

from __future__ import annotations

import argparse
import difflib
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "reader"))

from htpd_reader import open_bundle  # noqa: E402

WORD = re.compile(r"[^\s.,;:!?()\[\]{}\"'|*`>#/%±\u2022\u25e6\u25aa\u2013\u2014-]+")


def words(text: str) -> list[str]:
    return [w.lower() for w in WORD.findall(text.replace("°", " "))]


def norm_title(t: str) -> str:
    t = re.sub(r"^(\d+(\.\d+)*|[A-Z](\.\d+)+)\.?\s+", "", t.strip())
    return " ".join(words(t))


def source_truth(md: Path) -> dict:
    text = md.read_text(encoding="utf-8")
    title = re.search(r"^title:\s*(.+)$", text, re.M).group(1)
    text = re.sub(r"^---.*?---\n", "", text, flags=re.S)
    heads, rows, body = [], [], []
    in_code = False
    for line in text.splitlines():
        if line.startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            body.append(line)
            continue
        if m := re.match(r"#+\s+(.*)", line):
            heads.append(m.group(1))
            body.append(m.group(1))
        elif line.startswith("|"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if not all(set(c) <= set("-: ") for c in cells):
                rows.append(cells)
                body.append(" ".join(cells))
        else:
            body.append(re.sub(r"[*`>]", "", line))
    return {"title": title, "heads": heads, "rows": rows, "words": words("\n".join(body))}


def file_text(bundle) -> tuple[list[str], list[tuple[str, str]], list[list[str]], dict]:
    """Words in reading order (titles included), titles with their source, table rows."""
    out, titles, rows = [], [], []
    stats = {"sections": 0, "figures": 0, "ocr": False}
    for node, _depth in bundle.walk():
        stats["sections"] += 1
        hf = node.heading_from or "none"
        titles.append((node.title, hf))
        if hf not in ("none", "pages", "running-title"):
            out += words(norm_title(node.title))
        stats["ocr"] = stats["ocr"] or bool(node.ocr_pages)
        for b in node.blocks:
            if b.kind == "figure":
                stats["figures"] += 1
                continue
            out += words(b.text)
            if b.kind == "table":
                rows += [[c.strip() for c in r.split(" | ")] for r in b.text.splitlines()]
    return out, titles, rows, stats


def measure(pdf: Path, md: Path, converter: Path, out_dir: Path) -> dict:
    bundle_dir = out_dir / (pdf.stem + ".htpd")
    r = subprocess.run([str(converter), "convert", str(pdf), "-o", str(bundle_dir), "--force", "-q"],
                       capture_output=True, text=True, timeout=600)
    if r.returncode != 0:
        return {"name": pdf.stem, "error": r.stderr.strip()[:200]}
    truth = source_truth(md)
    b = open_bundle(bundle_dir)
    got, titles, rows, stats = file_text(b)
    sm = difflib.SequenceMatcher(None, truth["words"], got, autojunk=False)
    matched = sum(m.size for m in sm.get_matching_blocks())
    want_heads = {norm_title(h) for h in truth["heads"]}
    real_titles = [(t, hf) for t, hf in titles if hf not in ("none", "pages", "running-title")]
    found = {norm_title(t) for t, _ in real_titles}
    title_words = norm_title(truth["title"])
    extra = [t for t, _ in real_titles if norm_title(t) not in want_heads and norm_title(t) != title_words]
    got_rows = {" ".join(words(" ".join(r))) for r in rows}
    rows_found = sum(1 for r in truth["rows"] if " ".join(words(" ".join(r))) in got_rows)
    return {
        "name": pdf.stem,
        "text": matched / max(1, len(truth["words"])),
        "noise": (len(got) - matched) / max(1, len(got)),
        "heads": len(want_heads & found) / max(1, len(want_heads)),
        "extra": len(extra),
        "tables": rows_found / max(1, len(truth["rows"])),
        "sections": stats["sections"],
        "figures": stats["figures"],
        "ocr": stats["ocr"],
        "_missing_heads": sorted(want_heads - found),
        "_extra_heads": extra,
        "_diff": [(op, " ".join(truth["words"][i1:i2]), " ".join(got[j1:j2]))
                  for op, i1, i2, j1, j2 in sm.get_opcodes() if op != "equal"],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", type=Path)
    ap.add_argument("--converter", type=Path, default=ROOT / "build" / "htpd")
    ap.add_argument("--show", help="print the differences for this PDF (name without .pdf)")
    ap.add_argument("--json", type=Path, help="write all results to this file")
    args = ap.parse_args()
    out_dir = args.folder / "_htpd"
    results = []
    for pdf in sorted(args.folder.glob("*.pdf")):
        if args.show and pdf.stem != args.show:
            continue
        md = HERE / "sources" / (pdf.stem.split("-")[0] + ".md")
        results.append(measure(pdf, md, args.converter, out_dir))
    print(f"{'document':26} {'text':>6} {'noise':>6} {'heads':>6} {'extra':>6} {'tables':>7} {'sect':>5} {'fig':>4}")
    for r in results:
        if "error" in r:
            print(f"{r['name']:26} ERROR {r['error']}")
            continue
        print(f"{r['name']:26} {r['text']:6.3f} {r['noise']:6.3f} {r['heads']:6.2f} {r['extra']:6d} "
              f"{r['tables']:7.2f} {r['sections']:5d} {r['figures']:4d}{'  ocr' if r['ocr'] else ''}")
    ok = [r for r in results if "error" not in r]
    if ok:
        mean = lambda k: sum(r[k] for r in ok) / len(ok)  # noqa: E731
        print(f"{'mean':26} {mean('text'):6.3f} {mean('noise'):6.3f} {mean('heads'):6.2f} "
              f"{sum(r['extra'] for r in ok):6d} {mean('tables'):7.2f}")
    if args.show:
        for r in ok:
            print("\nmissing headings:", r["_missing_heads"])
            print("extra headings:  ", r["_extra_heads"])
            for op, want, got in r["_diff"]:
                print(f"  {op:8} source: {want[:110]!r}\n  {'':8} file:   {got[:110]!r}")
    if args.json:
        args.json.write_text(json.dumps([{k: v for k, v in r.items() if not k.startswith("_")} for r in results],
                                        indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
