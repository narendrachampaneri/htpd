"""Convert every PDF of a folder and check the converter survives it.

    python3 tests/stress/make_stress_pdfs.py build/stress
    PYTHONPATH=reader python3 tests/stress/run.py build/stress [--pdftotext]

For each PDF:
* the converter must end on its own within the time limit, with exit code 0
  (converted) or an error message and code 1 or 2 (a PDF it cannot read) -
  never a crash, never a hang;
* a converted bundle must pass `htpd-read check` with no error;
* with --pdftotext (poppler-utils), the words of the PDF as pdftotext reads
  them are compared with the words of the bundle: the share found is printed,
  and the words that are missing, so a person can see whether they are only
  removed headers and footers or rotated text, as the conversion notes say.

Exit code 1 when any PDF crashed, hung or gave a bundle with errors.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "reader"))

from htpd_reader import BundleError, check, open_bundle  # noqa: E402
from htpd_reader.check import errors  # noqa: E402

PASSWORDS = {"encrypted_user_pw.pdf": "secret"}
_WORD = re.compile(r"[^\W_]+", re.UNICODE)


def words(text: str) -> Counter[str]:
    return Counter(w.lower() for w in _WORD.findall(text))


def bundle_words(root: Path) -> Counter[str]:
    b = open_bundle(root)
    text = "\n".join([b.title] + [s.title + "\n" + s.text for s in b.sections])
    return words(text)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--converter", default=str(ROOT / "build" / "htpd"))
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--pdftotext", action="store_true")
    args = ap.parse_args()
    folder = Path(args.folder)
    out = folder / "_bundles"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir()
    bad = 0
    for pdf in sorted(folder.glob("*.pdf")):
        dest = out / (pdf.stem + ".htpd")
        cmd = [args.converter, "convert", str(pdf), "-o", str(dest), "--force", "-q"]
        if pdf.name in PASSWORDS:
            cmd += ["--password", PASSWORDS[pdf.name]]
        t0 = time.perf_counter()
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            print(f"HANG   {pdf.name}: more than {args.timeout} s")
            bad += 1
            continue
        dt = time.perf_counter() - t0
        if p.returncode < 0 or p.returncode >= 128:
            print(f"CRASH  {pdf.name}: exit {p.returncode} {p.stderr.strip()[:200]}")
            bad += 1
            continue
        if p.returncode != 0:
            print(f"REFUSE {pdf.name} ({dt:.2f} s): {p.stderr.strip().splitlines()[-1][:160] if p.stderr.strip() else p.returncode}")
            continue
        try:
            b = open_bundle(dest)
        except BundleError as e:
            print(f"BROKEN {pdf.name}: {e}")
            bad += 1
            continue
        problems = errors(check(b, pdf=pdf))
        status = "OK    " if not problems else "ERRORS"
        bad += bool(problems)
        line = f"{status} {pdf.name} ({dt:.2f} s): {b.page_count} pages, {len(b)} sections"
        if args.pdftotext and shutil.which("pdftotext"):
            cmd = ["pdftotext", "-q"] + (["-upw", PASSWORDS[pdf.name]] if pdf.name in PASSWORDS else []) + [str(pdf), "-"]
            ref = words(subprocess.run(cmd, capture_output=True, text=True).stdout)
            got = bundle_words(dest)
            total = sum(ref.values())
            found = sum(min(n, got[w]) for w, n in ref.items())
            missing = Counter({w: n - got[w] for w, n in ref.items() if n > got[w]})
            share = found / total if total else 1.0
            line += f", words {share:.1%} of pdftotext"
            if missing:
                line += "\n         missing: " + ", ".join(f"{w}×{n}" for w, n in missing.most_common(12))
        print(line)
        for p_ in problems[:5]:
            print(f"         {p_}")
    print(f"\n{bad} PDF(s) crashed, hung or gave a bundle with errors")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
