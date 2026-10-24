#!/usr/bin/env python3
"""Makes PDFs whose reading order is known, for the benchmark (run_bench.py --set layouts).

    python3 tests/bench/make_layouts.py build/layouts

For every case it writes <case>.pdf and <case>.truth.json: the paragraphs of every page in the
order a person reads them. run_bench.py converts the PDF and scores the text in that order
(layout_score.py): words in reading order (longest common subsequence), paragraphs found whole,
paragraphs split, units that hold more than one paragraph. Needs reportlab only; the PDFs are the
same on every run.

The truth is written down when the page is built, not read back from the PDF. What is read after
what:

  * columns: left to right; a paragraph that runs from the bottom of a column to the top of the next
    is one paragraph;
  * a full-width title, heading or figure ends the columns above it and starts new columns below;
  * a side box, a pull quote and a margin note are read after the paragraph they stand in or beside
    (never in the middle of a sentence), as a paragraph of their own;
  * a caption directly after its figure;
  * footnotes at the end of the page.

The cases (each also with the order in which the PDF stores the text changed - see VARIANTS):

    single        one column, a heading, three paragraphs, a list (the control: must not change)
    cols2         two columns
    cols3         three columns
    colbreak      a paragraph runs over from the first to the second column (and one ends at the bottom)
    fullwidth     a full-width title and heading, a full-width figure with a caption between two bands of columns
    sidebox       a framed box in a column, stored in the middle of the paragraph it interrupts
    sidebox_shaded  the same, the box is a shaded area without a frame
    pullquote     a pull quote in large italics in a column, stored in the middle of a paragraph
    caption       a figure with its caption beside it, body text below
    footnotes     two columns with footnotes across the bottom
    margin        a wide text column with notes in the margin beside the paragraphs
    tablecols     two columns around a full-width table and a code listing
    newsletter    masthead, three columns, a lead story over two, a shaded box, a pull quote, a figure
    paper         a scientific paper: title, authors and abstract full width, then two columns over two pages

Variants (VARIANTS): `natural` - the order of reading; `last` - the box, quote, caption or note
is stored after the text of the page; `interleaved` - row by row across the columns (a line of the
left column, a line of the right column, ...); `reversed` - from the bottom of the page to the top;
`shuffled` - no order at all.
"""

from __future__ import annotations

import argparse
import json
import random
import sys
from dataclasses import dataclass, field
from pathlib import Path

from reportlab import rl_config
from reportlab.lib import colors
from reportlab.lib.pagesizes import letter
from reportlab.pdfbase.pdfmetrics import stringWidth
from reportlab.pdfgen.canvas import Canvas

rl_config.invariant = 1

W, H = letter

# ---------------------------------------------------------------------------------------------
# Text for the paragraphs: words that rarely repeat, so a wrong order shows


def vocabulary() -> list[str]:
    rng = random.Random(7)
    common = ("the of and a to in is that for with as on by this are from or an be at which it can the of "
              "and more than each when all both between over under").split()
    syll = ["ka", "vo", "ren", "tal", "mir", "dus", "pel", "quo", "zan", "bri", "lor", "fex", "sum", "nid",
            "oth", "gra", "wen", "yul", "cor", "ber", "ast", "ilo", "pran", "dek", "mon", "sul", "tir"]
    words = set()
    while len(words) < 700:
        words.add("".join(rng.choice(syll) for _ in range(rng.choice((2, 2, 3)))))
    return common * 3 + sorted(words)


VOCAB = vocabulary()


def sentences(seed: int, count: int, first_words: int = 0) -> str:
    rng = random.Random(seed)
    out = []
    for _ in range(count):
        n = rng.randint(8, 15)
        ws = [rng.choice(VOCAB) for _ in range(n)]
        ws[0] = ws[0].capitalize()
        out.append(" ".join(ws) + ".")
    return " ".join(out)


# ---------------------------------------------------------------------------------------------
# The page model


@dataclass
class Style:
    font: str = "Helvetica"
    size: float = 10
    leading: float = 12.5
    color: tuple = (0, 0, 0)


BODY = Style()
HEAD = Style("Helvetica-Bold", 13, 16)
TITLE = Style("Helvetica-Bold", 22, 27)
SUB = Style("Helvetica", 11, 14)
BOXT = Style("Helvetica-Bold", 10, 12.5)
BOXB = Style("Helvetica", 9, 11)
QUOTE = Style("Times-Italic", 15, 19, (0.15, 0.15, 0.45))
CAPTION = Style("Helvetica-Oblique", 8.5, 10.5)
FOOT = Style("Helvetica", 7.5, 9)
MARGIN = Style("Helvetica-Oblique", 8, 10)
MONO = Style("Courier", 8.5, 10.5)
CELLS = Style("Helvetica", 9, 11)


@dataclass
class Item:
    """One line of text, drawn by one drawString."""
    page: int
    x: float
    y: float            # baseline, from the top of the page
    text: str
    style: Style
    para: str = ""


@dataclass
class Para:
    pid: str
    kind: str
    text: str
    page: int
    items: list[Item] = field(default_factory=list)


@dataclass
class Frame:
    page: int
    x: float
    top: float
    w: float
    bottom: float


class Doc:
    def __init__(self) -> None:
        self.pages = 0
        self.read: list[list[Para]] = []      # per page: the paragraphs in reading order
        self.store: list[list[Item]] = []     # per page: the lines in the order the PDF stores them
        self.shapes: list[list[tuple]] = []   # per page: drawn first
        self.n = 0

    def page(self) -> int:
        self.read.append([])
        self.store.append([])
        self.shapes.append([])
        self.pages += 1
        return self.pages - 1

    def pid(self, kind: str) -> str:
        self.n += 1
        return f"{kind}{self.n}"


def wrap(text: str, style: Style, width: float) -> list[str]:
    lines: list[str] = []
    cur = ""
    for word in text.split():
        trial = word if not cur else cur + " " + word
        if stringWidth(trial, style.font, style.size) <= width or not cur:
            cur = trial
        else:
            lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    return lines


class Flow:
    """Text that fills a chain of frames, like the columns of a page."""

    def __init__(self, doc: Doc, frames: list[Frame]) -> None:
        self.doc, self.frames, self.fi = doc, frames, 0
        self.y = frames[0].top
        self.fresh = True    # at the top of a frame: no space above

    def _room(self, leading: float) -> Frame:
        while self.y + leading > self.frames[self.fi].bottom + 0.01:
            self.fi += 1
            if self.fi >= len(self.frames):
                raise SystemExit(f"make_layouts: the text does not fit its frames (page {self.frames[-1].page}, x {self.frames[-1].x:.0f})")
            self.y = self.frames[self.fi].top
            self.fresh = True
        return self.frames[self.fi]

    def skip(self, height: float) -> None:
        self.y += height

    def para(self, kind: str, text: str, style: Style = BODY, gap: float = 6.0, indent: float = 0.0,
             interrupt: tuple[int, float, object] | None = None, store: bool = True,
             read: bool = True, pid: str | None = None) -> Para:
        """Places the lines of a paragraph. `interrupt` = (lines before, height, callback): after that
        many lines the flow leaves `height` points free and calls callback(frame, top) - a box or a
        quote stands there."""
        if not self.fresh:
            self.y += gap
        pid = pid or self.doc.pid(kind)
        frame0 = self._room(style.leading)
        lines = wrap(text, style, frame0.w - indent)
        # a paragraph never runs over a page break: it starts on the next page instead
        room = int((frame0.bottom - self.y) / style.leading + 0.01)
        if len(lines) > room and self.fi + 1 < len(self.frames) and self.frames[self.fi + 1].page != frame0.page:
            self.fi += 1
            self.y = self.frames[self.fi].top
            self.fresh = True
            frame0 = self.frames[self.fi]
        p = Para(pid, kind, text, frame0.page)
        stop = interrupt[0] if interrupt else -1
        # a box stands in the middle of a sentence: not after a line that ends one
        while interrupt and 0 < stop < len(lines) - 1 and lines[stop - 1].rstrip().endswith((".", "!", "?")):
            stop += 1
        for k, line in enumerate(lines):
            if interrupt and k == stop:
                f = self.frames[self.fi]
                top = self.y + 4
                self.y += interrupt[1] + 8
                interrupt[2](f, top)
            f = self._room(style.leading)
            self.fresh = False
            base = self.y + style.leading * 0.8
            it = Item(f.page, f.x + indent, base, line, style, pid)
            p.items.append(it)
            self.y += style.leading
        if len({it.page for it in p.items}) > 1:
            raise SystemExit(f"make_layouts: paragraph {pid} runs over a page break")
        if store:
            self.doc.store[p.page].extend(p.items)
        if read:
            self.doc.read[p.page].append(p)
        return p


def place(doc: Doc, page: int, kind: str, text: str, x: float, top: float, w: float, style: Style,
          store: bool = True, read: bool = True, pid: str | None = None) -> Para:
    """A paragraph at a fixed place (a title, a box, a note)."""
    pid = pid or doc.pid(kind)
    p = Para(pid, kind, text, page)
    y = top
    for line in wrap(text, style, w):
        p.items.append(Item(page, x, y + style.leading * 0.8, line, style, pid))
        y += style.leading
    if store:
        doc.store[page].extend(p.items)
    if read:
        doc.read[page].append(p)
    return p


def height_of(text: str, style: Style, w: float) -> float:
    return len(wrap(text, style, w)) * style.leading


def columns(page: int, count: int, top: float, bottom: float, left: float = 54.0, right: float = 558.0,
            gap: float = 28.0) -> list[Frame]:
    w = (right - left - gap * (count - 1)) / count
    return [Frame(page, left + i * (w + gap), top, w, bottom) for i in range(count)]


def figure(doc: Doc, page: int, x: float, top: float, w: float, h: float) -> None:
    """A block diagram without text: boxes and arrows."""
    sh = doc.shapes[page]
    sh.append(("rect", x, top, w, h, 0.8, None))
    bw, bh = w * 0.22, h * 0.28
    for i in range(3):
        bx = x + w * 0.08 + i * w * 0.30
        sh.append(("rect", bx, top + h * 0.18, bw, bh, 0.8, None))
        sh.append(("rect", bx, top + h * 0.58, bw, bh, 0.8, None))
        sh.append(("line", bx + bw, top + h * 0.32, bx + w * 0.30, top + h * 0.32, 0.8))
        sh.append(("line", bx + bw / 2, top + h * 0.46, bx + bw / 2, top + h * 0.58, 0.8))


def box(doc: Doc, page: int, x: float, top: float, w: float, title: str, body: str, shaded: bool,
        store: bool, read: bool = True) -> tuple[list[Para], float]:
    """A framed or shaded box with a title and a text. Returns its paragraphs and its height."""
    pad = 6.0
    inner = w - 2 * pad
    ht = height_of(title, BOXT, inner)
    hb = height_of(body, BOXB, inner)
    h = ht + hb + 3 * pad
    if shaded:
        doc.shapes[page].append(("rect", x, top, w, h, 0, 0.88))
    else:
        doc.shapes[page].append(("rect", x, top, w, h, 1.0, None))
    t = place(doc, page, "boxtitle", title, x + pad, top + pad, inner, BOXT, store=store, read=False)
    b = place(doc, page, "boxtext", body, x + pad, top + pad + ht + pad / 2, inner, BOXB, store=store, read=False)
    return [t, b], h


def merged(paras: list[Para], kind: str, pid: str) -> Para:
    """Title and text of a box are one paragraph for the truth."""
    p = Para(pid, kind, " ".join(q.text for q in paras), paras[0].page)
    for q in paras:
        p.items += q.items
    return p


def rule(doc: Doc, page: int, x0: float, x1: float, y: float) -> None:
    doc.shapes[page].append(("line", x0, y, x1, y, 0.6))


# ---------------------------------------------------------------------------------------------
# The cases. Each builds a Doc; `variant` decides where boxes, quotes, captions and notes are stored.


def case_single(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    fl = Flow(d, [Frame(p, 72, 90, 468, 700)])
    fl.para("title", "Supply and clock", TITLE)
    fl.para("body", sentences(1, 5))
    fl.para("body", sentences(2, 6))
    fl.para("body", sentences(3, 4))
    fl.para("item", "• " + sentences(4, 1), indent=0, gap=8)
    fl.para("item", "• " + sentences(5, 1), gap=2)
    fl.para("body", sentences(6, 7))
    return d


def case_cols2(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    fl = Flow(d, columns(p, 2, 80, 560))
    for i in range(1, 7):
        fl.para("body", sentences(10 + i, 5))
    return d


def case_cols3(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    fl = Flow(d, columns(p, 3, 80, 680, gap=22))
    for i in range(1, 10):
        fl.para("body", sentences(20 + i, 4))
    return d


def case_colbreak(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    fl = Flow(d, columns(p, 2, 80, 440))
    fl.para("body", sentences(31, 4))
    fl.para("body", sentences(32, 5))
    long = fl.para("body", sentences(33, 9))      # runs over to the second column
    fl.para("body", sentences(34, 4))
    fl.para("body", sentences(35, 5))
    fl.para("body", sentences(36, 3))
    assert len({it.x for it in long.items}) == 2, "colbreak: the paragraph must run over"
    return d


def case_fullwidth(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    place(d, p, "title", "Charger controller notes", 54, 60, 504, TITLE)
    place(d, p, "sub", "Revision two, October", 54, 92, 504, SUB)
    fl = Flow(d, columns(p, 2, 130, 380))
    for i in range(1, 5):
        fl.para("body", sentences(40 + i, 4))
    place(d, p, "head", "Measured values", 54, 395, 504, HEAD)
    figure(d, p, 54, 422, 504, 110)
    place(d, p, "caption", "Figure 1. Block diagram of the supply stages and the clock tree.", 54, 540, 504, CAPTION)
    fl2 = Flow(d, columns(p, 2, 575, 760))
    for i in range(5, 9):
        fl2.para("body", sentences(40 + i, 3))
    return d


def case_sidebox(variant: str, shaded: bool = False) -> Doc:
    d = Doc()
    p = d.page()
    fr = columns(p, 2, 80, 600)
    fl = Flow(d, fr)
    fl.para("body", sentences(51, 4))
    placed: list[Para] = []

    def make_box(frame: Frame, top: float) -> None:
        parts, _h = box(d, p, frame.x, top, frame.w, "Note on limits", sentences(52, 3), shaded,
                        store=False, read=False)
        placed.extend(parts)

    title, body = "Note on limits", sentences(52, 3)
    bh = height_of(title, BOXT, fr[0].w - 12) + height_of(body, BOXB, fr[0].w - 12) + 18
    interrupted = fl.para("body", sentences(53, 8), interrupt=(3, bh, make_box), store=False)
    # stored at its place in the stream: the box in the middle of the paragraph
    st = d.store[p]
    if variant == "last":
        st.extend(interrupted.items)
    else:
        st.extend(interrupted.items[:3])
        st.extend(placed[0].items + placed[1].items)
        st.extend(interrupted.items[3:])
    d.read[p].append(merged(placed, "box", d.pid("box")))
    fl.para("body", sentences(54, 5))
    fl.para("body", sentences(55, 6))
    fl.para("body", sentences(56, 5))
    if variant == "last":
        d.store[p].extend(placed[0].items + placed[1].items)   # after all the text of the page
    return d


def case_sidebox_shaded(variant: str) -> Doc:
    return case_sidebox(variant, shaded=True)


def case_pullquote(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    fr = columns(p, 2, 80, 620)
    fl = Flow(d, fr)
    fl.para("body", sentences(61, 4))
    quote = "The clock tree decides how much of the supply budget is left for the load."
    qh = height_of(quote, QUOTE, fr[0].w - 16) + 14
    holder: list[Para] = []

    def make_quote(frame: Frame, top: float) -> None:
        holder.append(place(d, p, "quote", quote, frame.x + 8, top + 4, frame.w - 16, QUOTE, store=False, read=False))

    interrupted = fl.para("body", sentences(62, 8), interrupt=(4, qh, make_quote), store=False)
    st = d.store[p]
    if variant == "last":
        st.extend(interrupted.items)
    else:
        st.extend(interrupted.items[:4])
        st.extend(holder[0].items)
        st.extend(interrupted.items[4:])
    d.read[p].append(holder[0])
    d.read[p][-1].kind = "quote"
    fl.para("body", sentences(63, 5))
    fl.para("body", sentences(64, 6))
    fl.para("body", sentences(65, 5))
    if variant == "last":
        d.store[p].extend(holder[0].items)
    # the quote is read after the paragraph it interrupts: move it behind that paragraph in the truth
    return d


def case_caption(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    fl = Flow(d, [Frame(p, 72, 80, 468, 700)])
    fl.para("body", sentences(71, 4))
    top = fl.y + 8
    figure(d, p, 72, top, 220, 110)
    cap = place(d, p, "caption", "Figure 2. Output stage with the current sense resistor and the filter. "
                "Values are given at 25 degrees.", 304, top + 40, 236, CAPTION, store=variant != "last")
    fl.skip(130)
    fl.fresh = False
    fl.para("body", sentences(72, 5))
    fl.para("body", sentences(73, 4))
    if variant == "last":
        d.store[p].extend(cap.items)
    return d


def case_footnotes(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    fl = Flow(d, columns(p, 2, 80, 560))
    for i in range(1, 8):
        fl.para("body", sentences(80 + i, 4))
    rule(d, p, 54, 200, 600)
    notes = []
    y = 606.0
    for i in range(1, 4):
        notes.append(place(d, p, "footnote", f"{i} " + sentences(90 + i, 1), 54, y, 504, FOOT,
                           store=variant != "first"))
        y += height_of(notes[-1].text, FOOT, 504) + 3
    if variant == "first":
        d.store[p] = [it for n in notes for it in n.items] + d.store[p]
    return d


def case_margin(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    main = Frame(p, 54, 80, 350, 700)
    fl = Flow(d, [main])
    notes: list[Para] = []
    paras: list[Para] = []
    for i in range(1, 6):
        start = fl.y + 6 if i > 1 else fl.y
        para = fl.para("body", sentences(100 + i, 5), store=False)
        paras.append(para)
        note = place(d, p, "margin", f"Note {i}. " + sentences(110 + i, 1), 424, start, 134, MARGIN, store=False, read=False)
        notes.append(note)
    st = d.store[p]
    for i, para in enumerate(paras):
        st.extend(para.items)
        if variant != "last":
            st.extend(notes[i].items)
    if variant == "last":
        for n in notes:
            st.extend(n.items)
    # a margin note is read after the paragraph beside it
    d.read[p] = []
    for para, n in zip(paras, notes):
        d.read[p].append(para)
        d.read[p].append(n)
    return d


def case_tablecols(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    fl = Flow(d, columns(p, 2, 80, 250))
    for i in range(1, 4):
        fl.para("body", sentences(120 + i, 4))
    place(d, p, "head", "Electrical values", 54, 265, 504, HEAD)
    xs = [54, 190, 300, 400, 480]
    rows = [["Parameter", "Min", "Typ", "Max", "Unit"],
            ["Supply voltage", "3.0", "3.3", "3.6", "V"],
            ["Output current", "0", "120", "200", "A"],
            ["Clock frequency", "8", "16", "24", "MHz"],
            ["Start-up time", "1.2", "2.0", "3.5", "ms"],
            ["Leakage current", "0.1", "0.4", "1.5", "uA"]]
    y = 292.0
    cells: list[Item] = []
    for r, row in enumerate(rows):
        for x, cell in zip(xs, row):
            cells.append(Item(p, x, y + 9, cell, Style("Helvetica-Bold" if r == 0 else "Helvetica", 9, 11), "table"))
        y += 13
    d.store[p].extend(cells)
    d.read[p].append(Para("table", "table", " ".join(c for row in rows for c in row), p, cells))
    y += 12
    code = ["int read_voltage(void)", "{", "    int raw = adc_read(CH_VSUP);", "    return raw * 3300 / 4095;", "}"]
    listing: list[Item] = []
    for ln in code:
        listing.append(Item(p, 54, y + 9, ln, MONO, "code"))
        y += 10.5
    d.store[p].extend(listing)
    d.read[p].append(Para("code", "code", " ".join(code), p, listing))
    fl2 = Flow(d, columns(p, 2, y + 20, 740))
    for i in range(4, 8):
        fl2.para("body", sentences(120 + i, 4))
    return d


def case_newsletter(variant: str) -> Doc:
    d = Doc()
    p = d.page()
    place(d, p, "title", "The Charger Courier", 54, 40, 504, Style("Helvetica-Bold", 30, 36))
    place(d, p, "sub", "News from the lab, issue 14, autumn", 54, 80, 504, SUB)
    rule(d, p, 54, 558, 100)
    cols = columns(p, 3, 112, 740, gap=20)
    # lead story over the first two columns, then the third column holds a shaded box and a story
    lead = Flow(d, [Frame(p, cols[0].x, 112, cols[1].x + cols[1].w - cols[0].x, 300)])
    lead.para("head", "Lab opens a new test hall", HEAD)
    lead.para("body", sentences(131, 5))
    c1 = Flow(d, [Frame(p, cols[0].x, 280, cols[0].w, 740)])
    c1.para("body", sentences(132, 2))
    quote = "Every cable we tested held its rating."
    qh = height_of(quote, QUOTE, cols[0].w - 16) + 14
    holder: list[Para] = []

    def make_quote(frame: Frame, top: float) -> None:
        holder.append(place(d, p, "quote", quote, frame.x + 8, top + 4, frame.w - 16, QUOTE, store=False, read=False))

    interrupted = c1.para("body", sentences(133, 4), interrupt=(3, qh, make_quote), store=False)
    d.store[p].extend(interrupted.items[:3] + (holder[0].items if variant != "last" else []) + interrupted.items[3:])
    d.read[p].append(holder[0])
    c1.para("body", sentences(134, 2))
    c2 = Flow(d, [Frame(p, cols[1].x, 280, cols[1].w, 740)])
    c2.para("body", sentences(135, 3))
    figtop = c2.y + 8
    figure(d, p, cols[1].x, figtop, cols[1].w, 90)
    place(d, p, "caption", "Figure 3. The new hall with six test benches.", cols[1].x, figtop + 96, cols[1].w, CAPTION)
    c2.skip(130)
    c2.fresh = False
    c2.para("body", sentences(136, 3))
    sbody = sentences(137, 4)
    parts, bh = box(d, p, cols[2].x, 120, cols[2].w, "Dates", sbody, True, store=True, read=False)
    d.read[p].append(merged(parts, "box", d.pid("box")))
    c3 = Flow(d, [Frame(p, cols[2].x, 120 + bh + 14, cols[2].w, 740)])
    c3.para("head", "Staff news", HEAD)
    c3.para("body", sentences(138, 7))
    c3.para("body", sentences(139, 5))
    if variant == "last":
        d.store[p].extend(holder[0].items)
    return d


def case_paper(variant: str) -> Doc:
    d = Doc()
    p0 = d.page()
    place(d, p0, "title", "A study of clock jitter in charger controllers", 54, 50, 504, Style("Helvetica-Bold", 18, 22))
    place(d, p0, "authors", "Mira Kovan, Pel Zant and Gra Wenlor, Institute of Power Electronics", 54, 108, 504, SUB)
    place(d, p0, "head", "Abstract", 54, 135, 504, Style("Helvetica-Bold", 10, 12.5))
    place(d, p0, "abstract", sentences(141, 5), 54, 150, 504, Style("Helvetica", 9, 11))
    p1 = d.page()
    frames = columns(p0, 2, 230, 640) + columns(p1, 2, 60, 600)
    # page 0 columns first, then page 1 columns
    fl = Flow(d, frames)
    fl.para("head", "1 Introduction", Style("Helvetica-Bold", 11, 14))
    for i in range(2, 5):
        fl.para("body", sentences(140 + i * 3, 5))
    fl.para("head", "2 Method", Style("Helvetica-Bold", 11, 14))
    for i in range(5, 8):
        fl.para("body", sentences(140 + i * 3, 5))
    fl.para("head", "3 Results", Style("Helvetica-Bold", 11, 14))
    for i in range(8, 12):
        fl.para("body", sentences(140 + i * 3, 5))
    place(d, p0, "footnote", "1 Work done at the institute, supported by a grant.", 54, 690, 230, FOOT)
    rule(d, p0, 54, 150, 684)
    return d


CASES = {
    "single": (case_single, ["natural", "shuffled", "reversed"]),
    "cols2": (case_cols2, ["natural", "interleaved", "reversed", "shuffled"]),
    "cols3": (case_cols3, ["natural", "interleaved", "shuffled"]),
    "colbreak": (case_colbreak, ["natural", "shuffled"]),
    "fullwidth": (case_fullwidth, ["natural", "interleaved", "shuffled"]),
    "sidebox": (case_sidebox, ["natural", "last"]),
    "sidebox_shaded": (case_sidebox_shaded, ["natural", "last"]),
    "pullquote": (case_pullquote, ["natural", "last"]),
    "caption": (case_caption, ["natural", "last"]),
    "footnotes": (case_footnotes, ["natural", "first"]),
    "margin": (case_margin, ["natural", "last"]),
    "tablecols": (case_tablecols, ["natural", "shuffled"]),
    "newsletter": (case_newsletter, ["natural", "last", "shuffled"]),
    "paper": (case_paper, ["natural", "shuffled"]),
}


def apply_variant(d: Doc, variant: str) -> None:
    rng = random.Random(5)
    for p in range(d.pages):
        items = d.store[p]
        if variant == "interleaved":
            d.store[p] = sorted(items, key=lambda it: (round(it.y / 3), it.x))
        elif variant == "reversed":
            d.store[p] = sorted(items, key=lambda it: (-round(it.y / 3), it.x))
        elif variant == "shuffled":
            rng.shuffle(items)


# ---------------------------------------------------------------------------------------------
# Writing


def draw(d: Doc, path: Path, title: str) -> None:
    c = Canvas(str(path), pagesize=letter, pageCompression=0)
    c.setTitle(title)
    for p in range(d.pages):
        for sh in d.shapes[p]:
            if sh[0] == "rect":
                _k, x, top, w, h, lw, fill = sh
                if fill is not None:
                    c.setFillColorRGB(fill, fill, fill)
                    c.rect(x, H - top - h, w, h, stroke=0, fill=1)
                    c.setFillColorRGB(0, 0, 0)
                else:
                    c.setLineWidth(lw)
                    c.rect(x, H - top - h, w, h, stroke=1, fill=0)
            else:
                _k, x0, y0, x1, y1, lw = sh
                c.setLineWidth(lw)
                c.line(x0, H - y0, x1, H - y1)
        for it in d.store[p]:
            c.setFont(it.style.font, it.style.size)
            c.setFillColorRGB(*it.style.color)
            c.drawString(it.x, H - it.y, it.text)
        c.showPage()
    c.save()


def truth_of(d: Doc, name: str) -> dict:
    pages = []
    for p in range(d.pages):
        pages.append({"page": p + 1,
                      "paragraphs": [{"id": q.pid, "kind": q.kind, "text": q.text} for q in d.read[p]]})
    return {"case": name, "pages": pages}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", type=Path)
    ap.add_argument("--only", help="only the cases whose name contains this")
    args = ap.parse_args()
    args.folder.mkdir(parents=True, exist_ok=True)
    made = 0
    for case, (build, variants) in CASES.items():
        for variant in variants:
            name = case if variant == "natural" else f"{case}_{variant}"
            if args.only and args.only not in name:
                continue
            d = build(variant)
            apply_variant(d, variant)
            draw(d, args.folder / f"{name}.pdf", name)
            (args.folder / f"{name}.truth.json").write_text(
                json.dumps(truth_of(d, name), indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
            made += 1
            print("made", name)
    print(f"{made} layout PDFs in {args.folder}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
