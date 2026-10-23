#!/usr/bin/env python3
"""Make the document zoo: the same documents made into PDFs by many programs.

    python3 tests/zoo/make_zoo.py build/zoo            # every maker that is installed
    python3 tests/zoo/make_zoo.py build/zoo --only web,scan

The sources are the Markdown files in tests/zoo/sources. Each is made into a
PDF the way real documents are made:

    word        pandoc -> .docx -> LibreOffice (headings without numbers, bookmarks)
    latex       pandoc -> LaTeX: numbered headings, a printed table of contents, bookmarks
    latex-nobm  the same without bookmarks: only the printed contents and the numbers
    latex-2col  two columns
    latex-book  a book: chapters on new pages, running headers with the chapter
    web         HTML printed by Chromium, with a fixed bar at the top and a contents box (a web page saved as PDF)
    wkhtml      HTML printed by wkhtmltopdf
    slides      pandoc -> .pptx -> LibreOffice (a slide deck)
    scan        the word PDF as pictures (a scan at 200 dpi): needs OCR
    scan-skew   the same, turned by 1.5 degrees, with noise
    outline     every letter drawn as a shape (no text layer at all): needs OCR

Because the text of every document is known, tests/zoo/run_zoo.py can measure
how much of it each .htpd file holds, in order, and which headings it found.
Needs: pandoc, LibreOffice, a LaTeX with xelatex, Chromium (Playwright),
wkhtmltopdf, ImageMagick, img2pdf, pdftoppm, reportlab and fontTools. A maker
whose program is missing is skipped.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCES = HERE / "sources"

WEB_CSS = """
body { font-family: 'DejaVu Sans', sans-serif; font-size: 11pt; margin: 0; }
.bar { position: fixed; top: 0; left: 0; right: 0; height: 34px; background: #0b5cad; color: white;
       font: bold 13px sans-serif; padding: 9px 16px; box-sizing: border-box; }
.bar span { float: right; font-weight: normal; }
main { margin: 56px 40px 30px 40px; }
nav#TOC { border: 1px solid #ccc; padding: 4px 14px; margin-bottom: 18px; font-size: 10pt; }
nav#TOC ul { list-style: none; padding-left: 14px; }
table { border-collapse: collapse; margin: 8px 0; }
td, th { border: 1px solid #999; padding: 3px 8px; }
pre { background: #f2f2f2; padding: 6px; }
"""


def run(cmd: list[str], **kw) -> None:
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600, **kw)


def have(*programs: str) -> bool:
    return all(shutil.which(p) for p in programs)


def soffice_pdf(src: Path, out: Path) -> None:
    with tempfile.TemporaryDirectory() as t:
        run(["soffice", "--headless", f"-env:UserInstallation=file://{t}/profile", "--convert-to", "pdf",
             "--outdir", t, str(src)])
        shutil.move(Path(t) / (src.stem + ".pdf"), out)


def make_word(md: Path, out: Path, tmp: Path) -> None:
    docx = tmp / (md.stem + ".docx")
    run(["pandoc", str(md), "-o", str(docx)])
    soffice_pdf(docx, out)


def main_font(md: Path) -> str:
    """A font that has the letters of the document (Gujarati, Hindi: FreeSerif)."""
    text = md.read_text(encoding="utf-8")
    return "FreeSerif" if any("\u0900" <= c <= "\u0aff" for c in text) else "DejaVu Serif"


def latex(md: Path, out: Path, *extra: str) -> None:
    with tempfile.TemporaryDirectory() as t:
        tex = Path(t) / "doc.tex"
        run(["pandoc", str(md), "-s", "-o", str(tex), "-N", "--toc", "-V", "geometry:margin=2.2cm",
             "-V", f"mainfont={main_font(md)}", "-V", "monofont=DejaVu Sans Mono", *extra])
        # lmodern is not always installed; xelatex uses the fonts above anyway
        source = tex.read_text(encoding="utf-8").replace("\\usepackage{lmodern}", "")
        if "classoption=twocolumn" in extra:   # longtable cannot be in two columns: a plain tabular
            source = re.sub(r"\\begin\{longtable\}\[\]", r"\\begin{tabular}", source)
            source = source.replace("\\end{longtable}", "\\end{tabular}\\par")
            source = re.sub(r"^\\end(firsthead|head|foot|lastfoot)\s*$", "", source, flags=re.M)
        tex.write_text(source, encoding="utf-8")
        for _ in range(2):   # twice: the printed table of contents
            run(["xelatex", "-interaction=nonstopmode", "doc.tex"], cwd=t)
        shutil.move(Path(t) / "doc.pdf", out)


def make_latex(md, out, tmp):
    latex(md, out)


def make_latex_nobm(md, out, tmp):
    latex(md, out, "-V", "hyperrefoptions=bookmarks=false")


def make_latex_book(md, out, tmp):
    latex(md, out, "-V", "documentclass=book", "--top-level-division=chapter")


def make_latex_2col(md, out, tmp):
    latex(md, out, "-V", "classoption=twocolumn", "-V", "geometry:margin=1.6cm")


def web_html(md: Path, tmp: Path) -> Path:
    title = re.search(r"^title:\s*(.+)$", md.read_text(encoding="utf-8"), re.M).group(1)
    body = subprocess.run(["pandoc", str(md), "--toc", "-s", "-t", "html5", "--metadata", "pagetitle=" + title],
                          check=True, capture_output=True, text=True).stdout
    body = re.sub(r"<style>.*?</style>", "<style>" + WEB_CSS + "</style>", body, flags=re.S)
    body = body.replace("<body>", f'<body><div class="bar">Team Space<span>Log in | Try it free</span></div><main>')
    body = body.replace("</body>", "</main></body>")
    html = tmp / (md.stem + ".html")
    html.write_text(body, encoding="utf-8")
    return html


def make_web(md: Path, out: Path, tmp: Path) -> None:
    html = web_html(md, tmp)
    script = tmp / "print.js"
    script.write_text("""
const { chromium } = require('playwright');
(async () => {
  const exe = ['/opt/pw-browsers/chromium', process.env.CHROMIUM].find(p => p && require('fs').existsSync(p));
  const b = await chromium.launch(exe ? { executablePath: exe } : {});
  const p = await b.newPage();
  await p.goto('file://' + process.argv[2]);
  await p.pdf({ path: process.argv[3], format: 'A4', displayHeaderFooter: true,
                headerTemplate: '<div style="font-size:8px;width:100%;padding-left:20px"><span class="date"></span> <span class="title"></span></div>',
                footerTemplate: '<div style="font-size:8px;width:100%;text-align:right;padding-right:20px"><span class="pageNumber"></span>/<span class="totalPages"></span></div>',
                margin: { top: '40px', bottom: '40px' } });
  await b.close();
})();
""")
    env = dict(os.environ, NODE_PATH="/opt/npm-tools/node_modules:" + os.environ.get("NODE_PATH", ""))
    run(["node", str(script), str(html), str(out)], env=env)


def make_wkhtml(md: Path, out: Path, tmp: Path) -> None:
    run(["wkhtmltopdf", "--quiet", "--enable-local-file-access", str(web_html(md, tmp)), str(out)])


def make_slides(md: Path, out: Path, tmp: Path) -> None:
    pptx = tmp / (md.stem + ".pptx")
    run(["pandoc", str(md), "-o", str(pptx), "--slide-level=2"])
    soffice_pdf(pptx, out)


def make_scan(md: Path, out: Path, tmp: Path, skew: bool = False) -> None:
    word = tmp / "word.pdf"
    make_word(md, word, tmp)
    run(["pdftoppm", "-r", "200", "-gray", "-png", str(word), str(tmp / "page")])
    pages = sorted(tmp.glob("page-*.png"))
    if skew:
        for p in pages:
            run(["convert", str(p), "-background", "white", "-rotate", "1.5", "-attenuate", "0.3",
                 "+noise", "Gaussian", "-colorspace", "Gray", str(p)])
    run(["img2pdf", "--pagesize", "A4", "-o", str(out), *map(str, pages)])


def make_scan_skew(md, out, tmp):
    make_scan(md, out, tmp, skew=True)


def make_outline(md: Path, out: Path, tmp: Path) -> None:
    """Every letter drawn as a shape, as some programs save web pages."""
    from fontTools.pens.basePen import BasePen
    from fontTools.ttLib import TTFont
    from reportlab.lib.pagesizes import A4
    from reportlab.pdfgen import canvas

    fonts = {}
    for key, name in (("r", "DejaVu Sans"), ("b", "DejaVu Sans:bold"), ("m", "DejaVu Sans Mono")):
        path = subprocess.run(["fc-match", "-f", "%{file}", name], capture_output=True, text=True).stdout
        f = TTFont(path)
        fonts[key] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm, f["hmtx"])

    class Pen(BasePen):
        def __init__(self, glyphs, path, x, y, s):
            super().__init__(glyphs)
            self.p, self.x, self.y, self.s = path, x, y, s

        def pt(self, q):
            return self.x + q[0] * self.s, self.y + q[1] * self.s

        def _moveTo(self, q): self.p.moveTo(*self.pt(q))
        def _lineTo(self, q): self.p.lineTo(*self.pt(q))
        def _curveToOne(self, a, b, c): self.p.curveTo(*self.pt(a), *self.pt(b), *self.pt(c))
        def _closePath(self): self.p.close()

    c = canvas.Canvas(str(out), pagesize=A4)
    W, H = A4
    margin = 56
    y = H - margin

    def width(text, key, size):
        f, gs, cmap, upm, hmtx = fonts[key]
        return sum(hmtx[cmap.get(ord(ch), ".notdef")][0] for ch in text) * size / upm

    def draw(text, x, yy, key, size):
        f, gs, cmap, upm, hmtx = fonts[key]
        path = c.beginPath()
        s = size / upm
        for ch in text:
            g = cmap.get(ord(ch), ".notdef")
            gs[g].draw(Pen(gs, path, x, yy, s))
            x += hmtx[g][0] * s
        c.drawPath(path, stroke=0, fill=1)

    def newline(h):
        nonlocal y
        y -= h
        if y < margin:
            c.showPage()
            y = H - margin - h

    def wrap(text, key, size, indent=0):
        words, line = text.split(), ""
        for w in words:
            t = (line + " " + w).strip()
            if width(t, key, size) > W - 2 * margin - indent and line:
                newline(size * 1.45)
                draw(line, margin + indent, y, key, size)
                line = w
            else:
                line = t
        if line:
            newline(size * 1.45)
            draw(line, margin + indent, y, key, size)

    text = md.read_text(encoding="utf-8")
    text = re.sub(r"^---.*?---\n", "", text, flags=re.S)
    in_code = False
    raw_lines = text.splitlines()
    table_id, xs = None, []

    def id_of_table(all_lines, i):   # the first line of the table that line i is in
        while i > 0 and all_lines[i - 1].startswith("|"):
            i -= 1
        return i

    def table_rows(all_lines, i):
        i = id_of_table(all_lines, i)
        out = []
        while i < len(all_lines) and all_lines[i].startswith("|"):
            if not all(set(c.strip()) <= set("-: ") for c in all_lines[i].strip().strip("|").split("|")):
                out.append(all_lines[i])
            i += 1
        return out

    for index, raw in enumerate(raw_lines):
        line = raw.rstrip()
        if line.startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            newline(13)
            draw(line, margin, y, "m", 9)
            continue
        if not line.strip():
            newline(5)
            continue
        plain = re.sub(r"[*`]", "", line)
        if m := re.match(r"(#+)\s+(.*)", plain):
            size = {1: 17, 2: 13, 3: 11.5}.get(len(m.group(1)), 11)
            newline(8)
            wrap(m.group(2), "b", size)
        elif plain.startswith("|"):
            cells = [x.strip() for x in plain.strip("|").split("|")]
            if all(set(x) <= set("-: ") for x in cells):
                continue
            # columns as wide as their widest cell in this table, 14 points apart
            if table_id != id_of_table(raw_lines, index):
                table_id = id_of_table(raw_lines, index)
                rows = [[x.strip() for x in r.strip().strip("|").split("|")] for r in table_rows(raw_lines, index)]
                widths = [max(width(r[k], "r", 9.5) for r in rows if k < len(r)) for k in range(len(cells))]
                xs = [margin]
                for w in widths[:-1]:
                    xs.append(xs[-1] + w + 14)
            newline(15)
            for i, cell in enumerate(cells):
                draw(cell, xs[i], y, "r", 9.5)
        elif m := re.match(r"(\s*)([-*]|\d+\.)\s+(.*)", plain):
            mark = "•" if m.group(2) in "-*" else m.group(2)
            newline(15)
            draw(mark, margin + 8, y, "r", 10.5)
            y += 15
            wrap(m.group(3), "r", 10.5, indent=24)
        else:
            wrap(plain.lstrip("> "), "r", 10.5)
    c.save()


MAKERS = {
    "word": (make_word, ("pandoc", "soffice")),
    "latex": (make_latex, ("pandoc", "xelatex")),
    "latex-nobm": (make_latex_nobm, ("pandoc", "xelatex")),
    "latex-2col": (make_latex_2col, ("pandoc", "xelatex")),
    "latex-book": (make_latex_book, ("pandoc", "xelatex")),
    "web": (make_web, ("pandoc", "node")),
    "wkhtml": (make_wkhtml, ("pandoc", "wkhtmltopdf")),
    "slides": (make_slides, ("pandoc", "soffice")),
    "scan": (make_scan, ("pandoc", "soffice", "pdftoppm", "img2pdf")),
    "scan-skew": (make_scan_skew, ("pandoc", "soffice", "pdftoppm", "img2pdf", "convert")),
    "outline": (make_outline, ()),
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", type=Path)
    ap.add_argument("--only", help="comma-separated makers")
    args = ap.parse_args()
    args.folder.mkdir(parents=True, exist_ok=True)
    wanted = args.only.split(",") if args.only else list(MAKERS)
    for md in sorted(SOURCES.glob("*.md")):
        for name in wanted:
            make, needs = MAKERS[name]
            out = args.folder / f"{md.stem}-{name}.pdf"
            if not have(*needs):
                print(f"skip   {out.name} (needs {', '.join(needs)})")
                continue
            with tempfile.TemporaryDirectory() as t:
                try:
                    make(md, out, Path(t))
                    print(f"made   {out.name}")
                except Exception as e:   # noqa: BLE001 - one maker failing must not stop the others
                    print(f"FAILED {out.name}: {e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
