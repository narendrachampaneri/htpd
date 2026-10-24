#!/usr/bin/env python3
"""The HTPD benchmark: convert PDFs, compare each .htpd file with its PDF, report.

    python3 tests/bench/run_bench.py [FOLDER ...] [--set private,eval,zoo,data] [--tag NAME]
    python3 tests/bench/run_bench.py --compare before.json after.json

For every PDF it converts with `build/htpd convert --single` and records

    speed       wall time (best of --repeat runs) and CPU time of the converter
    size        bytes of the .htpd file, and the share of it that is the `# Figures` part
    text        pdftotext -layout of each page against the text under each `<!-- page N -->`:
                  words on their page (as a bag) and in order (longest common subsequence),
                  the pieces of PDF text missing from their page, the extra text,
                  words that stand on another page ("moved"), words that are nowhere ("lost"),
                  words that are not in the PDF ("invented"); running headers and footers that the
                  converter listed as removed count as expected
    headings    the bookmarks of the PDF (or, without any, a printed table of contents) against
                `# Contents`: found / missing / extra
    tables      lines of the PDF set in 3 or more columns against the " | " lines of the file
    figures     "Figure N" captions of the PDF against the "![F" lines of the file; the count
                in the conversion notes against the "![F" lines

and writes <out>/<tag>.json and <out>/<tag>.md. `--compare` prints before -> after for two JSON
files, marks every worse number and says whether the limits of the work rules hold. Details:
tests/bench/README.md.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import resource
import shutil
import subprocess
import sys
import time
import unicodedata
import zlib
from bisect import bisect_left
from collections import Counter, defaultdict
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from layout_score import score_layout  # noqa: E402
from table_score import score_table  # noqa: E402
SCHEMA = 1

# Limits of the work rules: accuracy never worse, time at most +10 %, size at most +5 %.
LIMIT_TIME = 0.10
LIMIT_SIZE = 0.05
TIME_NOISE = 0.10  # a document is marked slower or faster only above this and above TIME_NOISE_S
TIME_NOISE_S = 0.05

SETS = {
    "private": lambda: sorted((ROOT / "private" / "bench").glob("*.pdf")),
    "eval": lambda: sorted((ROOT / "eval" / "docs").glob("*.pdf")),
    "zoo": lambda: sorted((ROOT / "build" / "zoo").glob("*.pdf")),
    "data": lambda: sorted((ROOT / "tests" / "data").glob("*.pdf")),
    "tables": lambda: sorted((ROOT / "build" / "tables").glob("*.pdf")),
    "layouts": lambda: sorted((ROOT / "build" / "layouts").glob("*.pdf")),
}
PUBLIC = ("data", "eval", "zoo", "tables", "layouts")

# ---------------------------------------------------------------------------------------------
# Words


def tokens(text: str) -> list[str]:
    """Words of a text: letters and digits, punctuation dropped (so `inter-` + `rupt` and
    `inter-rupt` are the same words), non-ASCII symbols like ° ± → kept as words of their own."""
    text = unicodedata.normalize("NFKC", text).casefold()
    if text.isascii():
        return re.findall(r"\w+", text)
    out: list[str] = []
    word: list[str] = []
    for c in text:
        cat = unicodedata.category(c)
        if c == "_" or c.isalnum() or cat[0] == "M":
            word.append(c)
            continue
        if word:
            out.append("".join(word))
            word = []
        if cat[0] == "S" and c > "\x7f":
            out.append(c)
    if word:
        out.append("".join(word))
    return out


def credit_joins(P: list[str], H: list[str], pairs: list | None = None) -> tuple[list[str], int]:
    """A name the PDF breaks over two lines of a cell ("AAEL" / "F64") is two words for pdftotext and
    one name in the file ("AAELF64"). Where a word of the file is missing from the PDF's page but is the
    two words of the PDF that the file lacks, put together, the PDF's two words become that one word
    (at the place of the first) - so a name joined right counts as found, not as one lost and one
    invented word. Returns the new PDF words and how many names were joined; `pairs` collects
    (first piece, second piece, name)."""
    cp, ch = Counter(P), Counter(H)
    miss, extra = cp - ch, ch - cp
    if not miss or not extra:
        return P, 0
    P = list(P)
    joined = 0
    for e in sorted(extra, key=lambda w: -len(w)):
        for _ in range(extra[e]):
            done = False
            for k in range(1, len(e)):
                a, b = e[:k], e[k:]
                need = Counter([a, b])
                if any(miss[w] < n for w, n in need.items()):
                    continue
                # The nearest pair: a piece and the same name's next piece after it.
                pos_a = [i for i, w in enumerate(P) if w == a]
                best = None
                for i in pos_a:
                    j = next((x for x in range(i + 1, len(P)) if P[x] == b), None)
                    if j is not None and (best is None or j - i < best[1] - best[0]):
                        best = (i, j)
                if best is None:
                    continue
                ia, ib = best
                P[ia] = e
                del P[ib]
                if pairs is not None:
                    pairs.append((a, b, e))
                miss.subtract(need)
                joined += 1
                done = True
                break
            if not done:
                break
    return P, joined


def digits_to_hash(words: list[str]) -> list[str]:
    return [re.sub(r"\d+", "#", w) for w in words]


def align(a: list[str], b: list[str]) -> tuple[list[bool], list[bool]]:
    """A longest common subsequence of two word lists (Hunt-Szymanski). Returns for each word of
    a and of b whether it is part of the match."""
    in_a = [False] * len(a)
    in_b = [False] * len(b)
    lo = 0
    while lo < len(a) and lo < len(b) and a[lo] == b[lo]:
        in_a[lo] = in_b[lo] = True
        lo += 1
    ha, hb = len(a), len(b)
    while ha > lo and hb > lo and a[ha - 1] == b[hb - 1]:
        ha -= 1
        hb -= 1
        in_a[ha] = in_b[hb] = True
    pos: dict[str, list[int]] = defaultdict(list)
    for j in range(lo, hb):
        pos[b[j]].append(j)
    tails: list[int] = []
    tail_node: list[int] = []
    nodes: list[tuple[int, int, int]] = []
    for i in range(lo, ha):
        for j in reversed(pos.get(a[i], ())):
            k = bisect_left(tails, j)
            nodes.append((i, j, tail_node[k - 1] if k else -1))
            if k == len(tails):
                tails.append(j)
                tail_node.append(len(nodes) - 1)
            else:
                tails[k] = j
                tail_node[k] = len(nodes) - 1
    node = tail_node[-1] if tail_node else -1
    while node >= 0:
        i, j, node = nodes[node]
        in_a[i] = in_b[j] = True
    return in_a, in_b


def pieces(words: list[str], matched: list[bool]) -> list[list[str]]:
    """Runs of words that are not part of the match."""
    out: list[list[str]] = []
    run: list[str] = []
    for w, m in zip(words, matched):
        if m:
            if run:
                out.append(run)
                run = []
        else:
            run.append(w)
    if run:
        out.append(run)
    return out


# ---------------------------------------------------------------------------------------------
# Outline (bookmarks) of a PDF, with the standard library only


class _Reader:
    """Just enough of a PDF reader to follow /Outlines. Objects are found by scanning for
    `N G obj`, so a broken cross-reference table does not matter; objects inside object streams
    are read too. Encrypted files are not read."""

    def __init__(self, data: bytes):
        self.data = data
        self.defs: dict[int, tuple[int, str, int]] = {}  # num -> (order, "file"|"stm", pos or index)
        self.stms: dict[int, list[tuple[int, int]]] = {}
        for m in re.finditer(rb"(?<!\d)(\d+)\s+(\d+)\s+obj\b", data):
            self.defs[int(m.group(1))] = (m.start(), "file", m.end())
        for num, (order, _kind, pos) in list(self.defs.items()):
            head = data[pos:pos + 400]
            if b"/ObjStm" not in head:
                continue
            try:
                d, p = self.value(pos)
                raw = self.stream(d, p)
                n, first = int(d["N"]), int(d["First"])
                nums = [int(x) for x in raw[:first].split()]
                for k in range(n):
                    onum, off = nums[2 * k], nums[2 * k + 1]
                    old = self.defs.get(onum)
                    if old is None or old[0] < order:
                        self.defs[onum] = (order, "stm", (num, first + off))
                self.stms[num] = raw
            except Exception:  # noqa: BLE001 - a damaged object stream is skipped
                continue

    # -- tokens
    def skip(self, data: bytes, p: int) -> int:
        while p < len(data):
            c = data[p:p + 1]
            if c in b" \t\r\n\x0c\x00":
                p += 1
            elif c == b"%":
                while p < len(data) and data[p:p + 1] not in b"\r\n":
                    p += 1
            else:
                break
        return p

    def value(self, p: int, data: bytes | None = None):
        data = self.data if data is None else data
        p = self.skip(data, p)
        c = data[p:p + 1]
        if data[p:p + 2] == b"<<":
            p += 2
            d: dict = {}
            while True:
                p = self.skip(data, p)
                if data[p:p + 2] == b">>":
                    return d, p + 2
                key, p = self.value(p, data)
                val, p = self.value(p, data)
                d[key[1:] if isinstance(key, str) and key.startswith("/") else key] = val
        if c == b"[":
            p += 1
            arr = []
            while True:
                p = self.skip(data, p)
                if data[p:p + 1] == b"]":
                    return arr, p + 1
                v, p = self.value(p, data)
                arr.append(v)
        if c == b"/":
            m = re.compile(rb"/[^\s/<>\[\]()%]*").match(data, p)
            name = re.sub(rb"#([0-9A-Fa-f]{2})", lambda x: bytes([int(x.group(1), 16)]), m.group(0))
            return name.decode("latin-1"), m.end()
        if c == b"(":
            depth, p, out = 1, p + 1, bytearray()
            while depth:
                ch = data[p:p + 1]
                if ch == b"\\":
                    nxt = data[p + 1:p + 2]
                    esc = {b"n": b"\n", b"r": b"\r", b"t": b"\t", b"b": b"\b", b"f": b"\f"}
                    if nxt in esc:
                        out += esc[nxt]
                        p += 2
                    elif nxt.isdigit():
                        m = re.compile(rb"[0-7]{1,3}").match(data, p + 1)
                        out.append(int(m.group(0), 8) & 255)
                        p = m.end()
                    elif nxt in b"\r\n":
                        p += 2 + (1 if data[p + 1:p + 3] == b"\r\n" else 0)
                    else:
                        out += nxt
                        p += 2
                    continue
                if ch == b"(":
                    depth += 1
                elif ch == b")":
                    depth -= 1
                    if not depth:
                        p += 1
                        break
                out += ch
                p += 1
            return bytes(out), p
        if c == b"<":
            end = data.index(b">", p)
            hexs = re.sub(rb"\s", b"", data[p + 1:end])
            if len(hexs) % 2:
                hexs += b"0"
            return bytes.fromhex(hexs.decode()), end + 1
        m = re.compile(rb"[+-]?\d+(?:\.\d*)?|[+-]?\.\d+").match(data, p)
        if m:
            txt = m.group(0)
            if b"." not in txt:
                ref = re.compile(rb"\s+(\d+)\s+R(?![A-Za-z])").match(data, m.end())
                if ref:
                    return ("ref", int(txt)), ref.end()
                return int(txt), m.end()
            return float(txt), m.end()
        m = re.compile(rb"[A-Za-z]+").match(data, p)
        if m:
            word = m.group(0)
            return {b"true": True, b"false": False, b"null": None}.get(word, word.decode()), m.end()
        raise ValueError(f"cannot read at {p}")

    def stream(self, d: dict, p: int) -> bytes:
        data = self.data
        m = re.compile(rb"\s*stream\r?\n").match(data, p)
        if not m:
            raise ValueError("no stream")
        start = m.end()
        end = data.find(b"endstream", start)
        raw = data[start:end]
        filt = d.get("Filter")
        if isinstance(filt, list):
            filt = filt[0] if filt else None
        if filt in ("FlateDecode", "Fl"):
            try:
                return zlib.decompress(raw)
            except zlib.error:
                return zlib.decompressobj().decompress(raw)
        if filt is None:
            return raw
        raise ValueError("unsupported filter")

    def get(self, ref):
        if isinstance(ref, tuple) and ref and ref[0] == "ref":
            num = ref[1]
            _order, kind, pos = self.defs[num]
            if kind == "file":
                return self.value(pos)[0]
            stm, off = pos
            return self.value(off, self.stms[stm])[0]
        return ref


def decode_title(raw) -> str:
    if not isinstance(raw, bytes):
        return ""
    if raw[:2] == b"\xfe\xff":
        return raw[2:].decode("utf-16-be", "replace")
    if raw[:2] == b"\xff\xfe":
        return raw[2:].decode("utf-16-le", "replace")
    if raw[:3] == b"\xef\xbb\xbf":
        return raw[3:].decode("utf-8", "replace")
    return raw.decode("latin-1")


def pdf_outline(path: Path) -> list[str] | None:
    """Titles of the bookmarks of a PDF in document order; [] when it has none; None when the
    file cannot be read (encrypted, damaged)."""
    try:
        data = path.read_bytes()
        if re.search(rb"/Encrypt\s*(\d+\s+\d+\s+R|<<)", data):
            return None
        roots = re.findall(rb"/Root\s+(\d+)\s+\d+\s+R", data)
        if not roots:
            return None
        rd = _Reader(data)
        cat = rd.get(("ref", int(roots[-1])))
        outlines = rd.get(cat.get("Outlines"))
        if not isinstance(outlines, dict) or "First" not in outlines:
            return []
        titles: list[str] = []
        seen: set[int] = set()
        stack = [outlines["First"]]
        while stack:  # depth first, in document order
            ref = stack.pop()
            if not isinstance(ref, tuple) or ref[1] in seen:
                continue
            seen.add(ref[1])
            item = rd.get(ref)
            if "Next" in item:
                stack.append(item["Next"])
            if "First" in item:
                stack.append(item["First"])
            titles.append(decode_title(rd.get(item.get("Title"))).strip())
        return [t for t in titles if t]
    except Exception:  # noqa: BLE001 - an outline that cannot be read is "unknown", not an error
        return None


# ---------------------------------------------------------------------------------------------
# The PDF side: text per page, table-like lines, captions, printed contents


def pdf_pages(path: Path) -> list[list[str]] | None:
    """`pdftotext -layout` as a list of pages, each a list of lines; None if it fails."""
    try:
        r = subprocess.run(["pdftotext", "-layout", "-enc", "UTF-8", str(path), "-"],
                           capture_output=True, timeout=900)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if r.returncode != 0:
        return None
    text = r.stdout.decode("utf-8", "replace")
    pages = text.split("\f")
    if pages and not pages[-1].strip():
        pages.pop()
    return [p.split("\n") for p in pages]


CAPTION = re.compile(r"^(?:figure|fig\.?)\s+([A-Z]?\d+(?:[.\-–]\d+)*[a-z]?)(?:\s*[.:–—-](?=\s|$)|\s*$)",
                     re.I)
LEADER = re.compile(r"(?:\s*\.){3,}\s*\d*\s*$|\s{3,}[ivxlc\d]+\s*$")


def pdf_captions(lines: list[str]) -> list[str]:
    out = []
    for line in lines:
        s = line.strip()
        m = CAPTION.match(s)
        if m and not LEADER.search(s):
            out.append(m.group(1).upper())
    return out


def table_lines(lines: list[str]) -> list[list[str]]:
    """Token lists of the lines of a page set in 3 or more columns (two or more gaps of two
    spaces), when another such line is at most two lines away: a table, not a heading set in
    three pieces."""
    cand = []
    for i, line in enumerate(lines):
        cells = [c for c in re.split(r"\s{2,}", line.strip()) if c]
        if len(cells) >= 3:
            cand.append(i)
    rows = []
    cs = set(cand)
    for i in cand:
        if any(k in cs for k in (i - 2, i - 1, i + 1, i + 2)):
            t = tokens(lines[i])
            if t:
                rows.append(t)
    return rows


def printed_contents(pages: list[list[str]]) -> list[str]:
    """Titles of a table of contents printed in the first pages (lines that end in a page
    number after dots or wide space); [] when fewer than five are found."""
    entry = re.compile(r"^\s*(?P<t>\S.*?[A-Za-zÀ-ɏͰ-￿].*?)\s*(?:(?:\s*\.){2,}|\s{3,})\s*"
                       r"(?P<p>\d{1,4}|[ivxlcdm]{1,6})\s*$", re.I)
    out: list[str] = []
    for lines in pages[:min(40, max(10, len(pages) // 6))]:
        found = []
        for line in lines:
            m = entry.match(line)
            if m:
                found.append(re.sub(r"[\s.]+$", "", m.group("t")))
        if len(found) >= 5:
            out += found
    return out if len(out) >= 5 else []


# ---------------------------------------------------------------------------------------------
# The .htpd side


FENCE_OPEN = re.compile(r"^(`{3,})([A-Za-z0-9]*)\s*$")
PAGE_MARK = re.compile(r"^<!-- page (\d+) -->$")
SECTION_MARK = re.compile(r"^<!-- htpd section (\{.*\}) -->$")
HEADING = re.compile(r"^## \[S(\d+)\] (.*)$")
IMAGE = re.compile(r"^!\[F(\d+): (.*)\]\(([^)]*)\)\s*$")
CONTENTS_LINE = re.compile(r"^(\s*)\* \[S(\d+)\] (.*)$")
CONTENTS_TAIL = re.compile(r" - pages? \d+(?:-\d+)?(?: - starts: .*)?$")
FURNITURE_LINE = re.compile(r"^\s*\* (header|footer), (\d+) pages?(, (\d+) different texts, for example)?: (.*)$")
GENERATED_CAPTION = re.compile(r"^(?:picture|drawing|figure) on page \d+$|^Page \d+: picture of the whole page$", re.I)


class Htpd:
    def __init__(self) -> None:
        self.front: dict[str, str] = {}
        self.contents: list[tuple[int, str]] = []
        self.notes: list[str] = []
        self.sections = 0
        self.page_lines: dict[int, list[str]] = defaultdict(list)
        self.page_titles: dict[int, set[int]] = defaultdict(set)  # indexes in page_lines that are titles
        self.page_rows: dict[int, list[str]] = defaultdict(list)
        self.table_blocks: list[list[tuple[int, str]]] = []   # each table block: (page, row) per row
        # the blocks of the text in file order: (kind, [(page, line), ...]); kind "p" a paragraph (lines up to
        # the next blank line, a block or a picture), "b" a block of rows or code, "t" a heading
        self.units: list[tuple[str, list[tuple[int, str]]]] = []
        self.figures: list[dict] = []
        self.image_ids: set[str] = set()
        self.unpaged_words = 0
        self.size = 0
        self.figure_bytes = 0


def parse_htpd(path: Path) -> Htpd:
    data = path.read_bytes()
    h = Htpd()
    h.size = len(data)
    lines = data.decode("utf-8", "replace").split("\n")
    part = "front"
    fence: tuple[str, bool] | None = None
    block: list[tuple[int, str]] | None = None
    page: int | None = None
    heading: str | None = None  # title waiting for its page
    section_page = 1
    in_links = False
    after_heading = False
    fig_line: int | None = None
    fence_unit: list[tuple[int, str]] = []

    unit: list[tuple[int, str]] = []

    def close_unit() -> None:
        nonlocal unit
        if unit:
            h.units.append(("p", unit))
            unit = []

    def add(line: str, table: bool, text: bool = True) -> None:
        if page is None:
            h.unpaged_words += len(tokens(line))
            return
        h.page_lines[page].append(line)
        if table and " | " in line:
            h.page_rows[page].append(line)
        if text:
            unit.append((page, line))

    def flush_heading(p: int) -> None:
        nonlocal heading
        if heading is not None:
            h.page_titles[p].add(len(h.page_lines[p]))
            h.page_lines[p].append(heading)
            close_unit()
            h.units.append(("t", [(p, heading)]))
            heading = None

    for idx, line in enumerate(lines):
        if fence:
            if line == fence[0]:
                fence = None
                if block is not None:
                    h.table_blocks.append(block)
                    block = None
                if fence_unit:
                    h.units.append(("b", fence_unit))
                    fence_unit = []
            elif part == "sections":
                pm = PAGE_MARK.match(line) if fence[1] else None
                if pm:   # a row-level page mark: the table goes on over a page break (FORMAT.md 4.2)
                    page = int(pm.group(1))
                    continue
                add(line, fence[1], False)
                if page is not None:
                    fence_unit.append((page, line))
                if block is not None and page is not None:
                    block.append((page, line))
            continue
        if line in ("# How to read this file", "# Contents", "# Conversion notes", "# Sections", "# Figures"):
            if heading is not None:
                flush_heading(section_page)
            part = {"# How to read this file": "howto", "# Contents": "contents",
                    "# Conversion notes": "notes", "# Sections": "sections", "# Figures": "figures"}[line]
            if part == "figures":
                fig_line = idx
            continue
        if part == "front":
            m = re.match(r'^([a-z_]+): "?(.*?)"?$', line)
            if m and not line.startswith(" "):
                h.front[m.group(1)] = m.group(2)
        elif part == "contents":
            m = CONTENTS_LINE.match(line)
            if m:
                h.contents.append((len(m.group(1)) // 2, CONTENTS_TAIL.sub("", m.group(3))))
        elif part == "notes":
            if line.strip():
                h.notes.append(line)
        elif part == "figures":
            m = re.match(r'^<!-- htpd figure \{"id":"(F\d+)"', line)
            if m:
                h.image_ids.add(m.group(1))
        elif part == "sections":
            m = SECTION_MARK.match(line)
            if m:
                if heading is not None:
                    flush_heading(section_page)
                h.sections += 1
                close_unit()
                try:
                    section_page = int(json.loads(m.group(1))["pages"][0])
                except (ValueError, KeyError, IndexError, TypeError):
                    section_page = page or 1
                page = None
                in_links = False
                after_heading = False
                continue
            if in_links:
                if line.startswith("* ") or not line.strip():
                    continue
                in_links = False
            if line == "<!-- htpd links -->":
                in_links = True
                continue
            m = HEADING.match(line)
            if m and not after_heading and page is None:
                heading = m.group(2)
                after_heading = True
                continue
            if line.startswith("Path: ") and after_heading and page is None:
                continue
            m = PAGE_MARK.match(line)
            if m:
                page = int(m.group(1))
                flush_heading(page)
                continue
            if line.startswith("[^source-pdf]:") or re.match(r"^<!-- figure F\d+ -->$", line) or not line.strip():
                close_unit()
                continue
            m = FENCE_OPEN.match(line)
            if m:
                close_unit()
                fence = (m.group(1), m.group(2) == "text")
                block = [] if fence[1] else None
                continue
            m = IMAGE.match(line)
            if m:
                close_unit()
                cap = m.group(2).replace("\\]", "]").replace("\\[", "[").replace("\\\\", "\\")
                lm = re.match(r"^(?:figure|fig\.?)\s+([A-Z]?\d+(?:[.\-–]\d+)*[a-z]?)(?:\s*[.:–—-](?=\s|$)|\s*$)",
                              cap, re.I)
                h.figures.append({"id": "F" + m.group(1), "page": page or section_page, "caption": cap,
                                  "label": lm.group(1).upper() if lm else None,
                                  "generated": bool(GENERATED_CAPTION.match(cap)), "ref": m.group(3)})
                continue
            add(line[1:] if line.startswith("\\") else line, False)
    if heading is not None:
        flush_heading(section_page)
    close_unit()
    if fig_line is not None:
        before = "\n".join(lines[:fig_line])
        h.figure_bytes = h.size - len(before.encode("utf-8")) - (1 if fig_line else 0)
    return h


def removed_furniture(notes: list[str]) -> list[tuple[bool, list[str], bool]]:
    """(is_header, normalised words, varies) of each running header/footer the converter listed.
    `varies`: the converter saw several different texts and names only one as an example."""
    out = []
    for line in notes:
        m = FURNITURE_LINE.match(line)
        if not m:
            continue
        sample = m.group(5).strip()
        cm = re.match(r"^(`+) ?(.*?) ?\1$", sample)
        if cm:
            sample = cm.group(2)
        words = digits_to_hash(tokens(sample))
        if words:
            out.append((m.group(1) == "header", words, bool(m.group(3))))
    return out


class TailWords(list):
    """The constant end of a running header that changes ("... | Page #"): a line in the band that
    ends with these words is that header, whatever its title says."""


def expand_furniture(patterns: list[tuple[bool, list[str], bool]], pages: list[list[list[str]]],
                     band: int = 3) -> list[tuple[bool, list[str]]]:
    """Where the converter named only an example of a running header that changes (a chapter
    title with the page), the PDF lines that stand in the same place on at least three pages and
    share two words with the example are the same header. `pages` are word lists per line."""
    out: list[tuple[bool, list[str]]] = []
    for header, words, varies in patterns:
        out.append((header, words))
        if not varies:
            continue
        seen: Counter = Counter()
        for lines in pages:
            live = [digits_to_hash(x) for x in lines if x]
            for norm in {tuple(x) for x in (live[:band] if header else live[-band:])}:
                seen[norm] += 1
        have = set(words)
        for norm, n in seen.items():
            if n >= 3 and len(have & set(norm)) >= 2 and list(norm) != words:
                out.append((header, list(norm)))
        # A title that changes on every page ("12.4. Usage examples | Page 64") never repeats, but
        # its end ("page #") does: a band line with that end, on at least three pages, is the header.
        tail = words[-2:]
        if len(words) > 2 and tail[0].isalpha():
            ends = set()
            for lines in pages:
                raw = [x for x in lines if x]
                if not raw:
                    continue
                edge = raw[0] if header else raw[-1]
                norm = digits_to_hash(edge)
                if len(norm) > len(tail) and norm[-len(tail):] == tail:
                    ends.add(tuple(edge))
            if len(ends) >= 3:  # three different titles, so it is a header that changes
                out.append((header, TailWords(tail)))
    return out


def strip_furniture(lines: list[list[str]], patterns: list[tuple[bool, list[str]]],
                    band: int = 8) -> tuple[list[list[str]], int]:
    """Take each listed header (in the first `band` non-empty lines) and footer (in the last
    `band`) out of a page, once. A pattern of one or two words must be a whole line. `lines` are word lists. Returns the lines and how many were taken."""
    lines = [list(x) for x in lines]
    live = [i for i, x in enumerate(lines) if x]
    taken = 0
    for header, words in sorted(patterns, key=lambda p: -len(p[1])):
        n = len(words)
        cands = live[:band] if header else live[-band:][::-1]
        tail = isinstance(words, TailWords)
        for i in cands:
            norm = digits_to_hash(lines[i])
            if tail:
                if len(norm) > n and norm[-n:] == words:
                    taken += 1
                    lines[i] = []
                    break
                continue
            if n <= 2 and norm != words:
                continue  # a lone page number or word must be the whole line, not a number in the text
            for s in range(len(norm) - n + 1):
                if norm[s:s + n] == words:
                    del lines[i][s:s + n]
                    taken += 1
                    break
            else:
                continue
            break
    return lines, taken


# ---------------------------------------------------------------------------------------------
# Measuring one document

NUM_PREFIX = re.compile(r"^\s*(?:(?:appendix|chapter|section|part|annex)\s+)?(?:\d+(?:\.\d+)*|[A-Z](?:\.\d+)+|[A-Z]|[IVX]+)[.)]?\s+",
                        re.I)


def title_key(title: str) -> tuple[str, ...]:
    stripped = NUM_PREFIX.sub("", title, count=1)
    return tuple(tokens(stripped)) or tuple(tokens(title))


def subsequence_run(small: list[str], big: list[str]) -> bool:
    """Is `small` a run of consecutive words of `big`?"""
    n = len(small)
    if not n:
        return True
    return any(big[i:i + n] == small for i in range(len(big) - n + 1) if big[i] == small[0])


def subsequence(small: list[str], big: list[str]) -> bool:
    it = iter(big)
    return all(w in it for w in small)


def match_rows(pdf_rows: list[list[str]], h_rows: list[list[str]],
               rich: list[bool] | None = None) -> tuple[int, int]:
    """(PDF rows found inside a row of the file, rows of the file that hold no PDF row).
    `rich[k]`: the row k of the file has three cells with text; a row with fewer is never stray (the PDF
    side counts cells with text, an empty cell is not one).
    A word of the PDF row also matches a longer word of the file that starts or ends with it (at least
    five letters): the file has the whole name where pdftotext cut it off at the cell or gave only one
    piece of it ("R_AARCH64_AUTH_GOT_ADR_P" / "REL_LO21" is one name in the file)."""
    index: dict[str, set[int]] = defaultdict(set)
    for k, r in enumerate(h_rows):
        for w in set(r):
            index[w].add(k)
    vocab = sorted(index)

    def rows_with(w: str) -> set[int]:
        found = set(index.get(w, ()))
        if len(w) >= 5:
            for i in range(bisect_left(vocab, w), len(vocab)):
                if not vocab[i].startswith(w):
                    break
                found |= index[vocab[i]]
            for v in vocab:
                if v.endswith(w):
                    found |= index[v]
        return found

    def same(w: str, v: str) -> bool:
        return w == v or (len(w) >= 5 and (v.startswith(w) or v.endswith(w)))

    def in_row(p: list[str], row: list[str]) -> bool:
        it = iter(row)
        return all(any(same(w, v) for v in it) for w in p)

    found = 0
    used: set[int] = set()
    for p in pdf_rows:
        cand: set[int] | None = None
        for w in set(p):
            cand = rows_with(w) if cand is None else cand & rows_with(w)
            if not cand:
                break
        hit = [k for k in sorted(cand or ()) if in_row(p, h_rows[k])]
        if hit:
            found += 1
            used.update(hit)
    if rich is None:
        return found, len(h_rows) - len(used)
    return found, sum(1 for k in range(len(h_rows)) if rich[k] and k not in used)


def rich_row(line: str) -> bool:
    """A row of the file with at least three cells that hold text."""
    return sum(1 for c in line.split(" | ") if c.strip(" |")) >= 3


def analyse(job: dict) -> dict:
    """All comparisons of one converted document. `job` has the PDF and .htpd paths."""
    pdf, out = Path(job["pdf"]), Path(job["htpd"])
    m: dict = {}
    d: dict = {}
    h = parse_htpd(out)
    m["sections"] = h.sections
    m["figure_bytes"] = h.figure_bytes
    m["figure_share"] = h.figure_bytes / h.size if h.size else 0.0
    pages = pdf_pages(pdf)
    npages = int(h.front.get("pages", "0") or 0)
    m["pages"] = npages or (len(pages) if pages else 0)
    furniture = removed_furniture(h.notes)
    if furniture and pages:
        furniture = expand_furniture(furniture, [[tokens(x) for x in p] for p in pages])
    else:
        furniture = [(a, b) for a, b, _v in furniture]

    # ---- text, page by page
    text_layer = bool(pages) and sum(len(tokens(" ".join(p))) for p in pages) >= 10
    m["text_layer"] = text_layer
    if text_layer:
        t = Counter()
        labels = 0
        miss_all: Counter = Counter()
        extra_all: Counter = Counter()
        worst = []
        pdf_rows_all = 0
        fig_pdf: set[tuple[int, str]] = set()
        row_found = row_stray = 0
        for pno in range(1, max(len(pages), max(h.page_lines, default=0)) + 1):
            plines = pages[pno - 1] if pno <= len(pages) else []
            hlines = h.page_lines.get(pno, [])
            pl = [tokens(x) for x in plines]
            pl, taken = strip_furniture(pl, furniture)
            flat = [w for x in pl for w in x]
            hl = []
            for i, x in enumerate(hlines):
                tk = tokens(x)
                if i in h.page_titles.get(pno, ()) and not subsequence_run(tk, flat):
                    labels += 1  # a title the converter made up (running title, "Page 3"): a label, not text
                    continue
                hl.append(tk)
            hl, kept = strip_furniture(hl, furniture)
            t["furniture_removed"] += taken
            t["furniture_kept"] += kept
            P = [w for x in pl for w in x]
            H = [w for x in hl for w in x]
            for c in pdf_captions(plines):
                fig_pdf.add((pno, c))
            P, joins = credit_joins(P, H)
            t["joined_names"] += joins
            prow = table_lines(plines)
            hlist = [r for r in h.page_rows.get(pno, []) if r.count(" | ") >= 2]
            hrow = [tokens(r) for r in hlist]
            pdf_rows_all += len(prow)
            f, s = match_rows(prow, hrow, [rich_row(r) for r in hlist])
            row_found += f
            row_stray += s
            t["rows_htpd"] += len(h.page_rows.get(pno, []))
            if not P and not H:
                continue
            ina, inb = align(P, H)
            order_hit = sum(ina)
            cp, ch = Counter(P), Counter(H)
            miss, extra = cp - ch, ch - cp
            hit = len(P) - sum(miss.values())
            miss_all.update(miss)
            extra_all.update(extra)
            t["pdf_words"] += len(P)
            t["htpd_words"] += len(H)
            t["hit_words"] += hit
            t["order_words"] += order_hit
            t["extra_words"] += len(H) - hit
            if P:
                t["pages_text"] += 1
                if order_hit >= 0.99 * len(P) and (len(H) - hit) <= 0.05 * max(1, len(H)):
                    t["pages_ok"] += 1
            mp, ep = pieces(P, ina), pieces(H, inb)
            t["missing_pieces"] += len(mp)
            t["extra_pieces"] += len(ep)
            if mp or ep:
                worst.append({"page": pno, "missing": len(P) - order_hit, "extra": len(H) - hit,
                              "missing_words": sorted(miss.items(), key=lambda kv: -kv[1])[:12],
                              "extra_words": sorted(extra.items(), key=lambda kv: -kv[1])[:12],
                              "missing_pieces": [" ".join(x[:14]) for x in mp[:5]],
                              "extra_pieces": [" ".join(x[:14]) for x in ep[:5]]})
        moved = sum(min(c, extra_all[w]) for w, c in miss_all.items())
        missing = sum(miss_all.values())
        m.update({
            "pdf_words": t["pdf_words"], "htpd_words": t["htpd_words"], "hit_words": t["hit_words"],
            "order_words": t["order_words"],
            "coverage_words": t["hit_words"] / t["pdf_words"] if t["pdf_words"] else None,
            "coverage_order": t["order_words"] / t["pdf_words"] if t["pdf_words"] else None,
            "missing_words": missing, "extra_words": t["extra_words"],
            "moved_words": moved, "lost_words": missing - moved, "invented_words": t["extra_words"] - moved,
            "extra_share": t["extra_words"] / t["htpd_words"] if t["htpd_words"] else 0.0,
            "pages_text": t["pages_text"], "pages_ok": t["pages_ok"],
            "missing_pieces": t["missing_pieces"], "extra_pieces": t["extra_pieces"],
            "furniture_removed": t["furniture_removed"], "furniture_kept": t["furniture_kept"],
            "joined_names": t["joined_names"],
            "unpaged_words": h.unpaged_words, "title_labels": labels,
        })
        d["worst_pages"] = sorted(worst, key=lambda x: -(x["missing"] + x["extra"]))[:60]
        m.update({"rows_pdf": pdf_rows_all, "rows_htpd": t["rows_htpd"], "rows_found": row_found,
                  "rows_stray": row_stray})
        m["figs_pdf"] = len(fig_pdf)
        have = {(f["page"], f["label"]) for f in h.figures if f["label"]}
        m["figs_found"] = len(fig_pdf & have)
        d["figs_missing"] = sorted(f"p{p} Figure {c}" for p, c in fig_pdf - have)[:20]
    else:
        for k in ("pdf_words", "htpd_words", "hit_words", "order_words", "coverage_words", "coverage_order",
                  "missing_words", "extra_words", "moved_words", "lost_words", "invented_words", "extra_share",
                  "pages_text", "pages_ok", "missing_pieces", "extra_pieces", "furniture_removed",
                  "furniture_kept", "joined_names", "title_labels", "rows_pdf", "rows_found", "rows_stray", "figs_pdf", "figs_found"):
            m[k] = None
        m["rows_htpd"] = sum(len(v) for v in h.page_rows.values())
        m["unpaged_words"] = h.unpaged_words

    # ---- headings
    outline = pdf_outline(pdf)
    source, want = "none", []
    if outline:
        source, want = "bookmarks", outline
    elif pages:
        printed = printed_contents(pages)
        if printed:
            source, want = "printed", printed
    m["toc_source"] = source
    got = Counter(title_key(title) for _depth, title in h.contents)
    if source == "none":
        m.update({"toc_pdf": None, "toc_found": None, "toc_missing": None, "toc_extra": None})
    else:
        left = Counter(got)
        missing_titles = []
        found = 0
        for title in want:
            k = title_key(title)
            if left.get(k, 0) > 0:
                left[k] -= 1
                found += 1
            else:
                missing_titles.append(title)
        m.update({"toc_pdf": len(want), "toc_found": found, "toc_missing": len(want) - found,
                  "toc_extra": sum(left.values())})
        d["toc_missing_titles"] = missing_titles[:30]
        extra_titles = []
        for _depth, title in h.contents:
            k = title_key(title)
            if left.get(k, 0) > 0:
                left[k] -= 1
                extra_titles.append(title)
        d["toc_extra_titles"] = extra_titles[:30]
    m["contents_entries"] = len(h.contents)

    # ---- figures of the file
    m["figs_htpd"] = len(h.figures)
    seen_label: set[tuple[int, str]] = set()
    dup = 0
    for f in h.figures:
        if f["label"]:
            key = (f["page"], f["label"])
            dup += key in seen_label
            seen_label.add(key)
    m["figs_dup"] = dup
    m["figs_uncaptioned"] = sum(1 for f in h.figures if f["generated"] or not f["label"])
    m["figs_no_image"] = sum(1 for f in h.figures if f["ref"].startswith("#") and f["ref"][1:] not in h.image_ids)
    noted = None
    for line in h.notes:
        nm = re.match(r"^\* (\d+) figures? \(pictures and drawings\)", line)
        if nm:
            noted = int(nm.group(1))
    m["notes_figs"] = noted if noted is not None else 0
    m["notes_ok"] = int(m["notes_figs"] == len(h.figures))
    m["htpd_bytes"] = h.size
    truth_file = pdf.with_suffix(".truth.json")
    for k in TABLE_KEYS:
        m[k] = None
    for k in LAYOUT_KEYS:
        m[k] = None
    if truth_file.exists():
        truth = json.loads(truth_file.read_text(encoding="utf-8"))
        if "rows" in truth:
            got = score_table(truth, h.table_blocks)
            for k in TABLE_KEYS:
                m[k] = got.get(k, 0)
        elif "pages" in truth:
            got = score_layout(truth, h.units, tokens, align)
            for k in LAYOUT_KEYS:
                m[k] = got.get(k, 0)
    return {"m": m, "d": d}


# ---------------------------------------------------------------------------------------------
# Running the converter


def convert(converter: Path, pdf: Path, out: Path, repeat: int, extra: list[str]) -> dict:
    out.parent.mkdir(parents=True, exist_ok=True)
    best_wall, best_cpu, error = None, None, None
    for _ in range(repeat):
        out.unlink(missing_ok=True)
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        t0 = time.perf_counter()
        r = subprocess.run([str(converter), "convert", str(pdf), "--single", "-o", str(out), "--force", "-q",
                            *extra], capture_output=True, text=True, timeout=3600)
        wall = time.perf_counter() - t0
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
        if r.returncode != 0 or not out.exists():
            error = (r.stderr or r.stdout).strip()[:300] or f"exit {r.returncode}"
            break
        if best_wall is None or wall < best_wall:
            best_wall, best_cpu = wall, cpu
    return {"time_s": best_wall, "cpu_s": best_cpu, "error": error, "runs": repeat}


# ---------------------------------------------------------------------------------------------
# Metrics: name, label, polarity ("up" higher is better, "down" lower is better, "info" no verdict), format

TABLE_KEYS = ("tbl_cells", "tbl_cells_ok", "tbl_rows", "tbl_rows_ok", "tbl_vals", "tbl_vals_row", "tbl_vals_ok",
              "tbl_pages_ok", "tbl_extra")

LAYOUT_KEYS = ("lay_words", "lay_order", "lay_extra", "lay_pages", "lay_pages_ok", "lay_paras", "lay_paras_ok",
               "lay_split", "lay_joined")

METRICS = [
    ("time_s", "time s", "time", "{:.3f}"),
    ("cpu_s", "cpu s", "noise", "{:.3f}"),
    ("htpd_bytes", "size B", "size", "{:,}"),
    ("figure_bytes", "figure B", "info", "{:,}"),
    ("pages", "pages", "info", "{:,}"),
    ("sections", "sections", "info", "{:,}"),
    ("coverage_order", "words in order", "up", "{:.4%}"),
    ("coverage_words", "words on page", "up", "{:.4%}"),
    ("pages_ok", "pages ok", "up", "{:,}"),
    ("pages_text", "pages with text", "info", "{:,}"),
    ("lost_words", "lost words", "down", "{:,}"),
    ("moved_words", "moved words", "down", "{:,}"),
    ("invented_words", "invented words", "down", "{:,}"),
    ("missing_pieces", "missing pieces", "down", "{:,}"),
    ("extra_pieces", "extra pieces", "down", "{:,}"),
    ("furniture_kept", "headers kept", "down", "{:,}"),
    ("joined_names", "names joined", "info", "{:,}"),
    ("toc_found", "headings found", "up", "{:,}"),
    ("toc_missing", "headings missing", "down", "{:,}"),
    ("toc_extra", "headings extra", "info", "{:,}"),
    ("toc_pdf", "headings in PDF", "info", "{:,}"),
    ("rows_found", "rows found", "up", "{:,}"),
    ("rows_pdf", "rows in PDF", "info", "{:,}"),
    ("rows_htpd", "rows in file", "info", "{:,}"),
    ("rows_stray", "stray rows", "down", "{:,}"),
    ("tbl_cells_ok", "cells exact", "up", "{:,}"),
    ("tbl_cells", "cells in truth", "info", "{:,}"),
    ("tbl_rows_ok", "rows exact", "up", "{:,}"),
    ("tbl_rows", "rows in truth", "info", "{:,}"),
    ("tbl_vals_ok", "values right place", "up", "{:,}"),
    ("tbl_vals_row", "values right row", "up", "{:,}"),
    ("tbl_vals", "values in truth", "info", "{:,}"),
    ("tbl_pages_ok", "rows right page", "up", "{:,}"),
    ("tbl_extra", "extra rows", "down", "{:,}"),
    ("lay_order", "layout words in order", "up", "{:,}"),
    ("lay_words", "layout words in truth", "info", "{:,}"),
    ("lay_extra", "layout extra words", "down", "{:,}"),
    ("lay_pages_ok", "layout pages ok", "up", "{:,}"),
    ("lay_pages", "layout pages", "info", "{:,}"),
    ("lay_paras_ok", "paragraphs whole", "up", "{:,}"),
    ("lay_paras", "paragraphs in truth", "info", "{:,}"),
    ("lay_split", "paragraphs split", "down", "{:,}"),
    ("lay_joined", "blocks joined", "down", "{:,}"),
    ("figs_found", "figures found", "up", "{:,}"),
    ("figs_pdf", "captions in PDF", "info", "{:,}"),
    ("figs_htpd", "figures in file", "info", "{:,}"),
    ("figs_dup", "duplicate figures", "down", "{:,}"),
    ("figs_no_image", "figures no image", "down", "{:,}"),
    ("notes_ok", "notes = figures", "up", "{}"),
]
SUM_KEYS = [k for k, _l, p, _f in METRICS if k not in ("coverage_order", "coverage_words", "notes_ok")]


def totals(docs: dict[str, dict]) -> dict:
    ok = [d for d in docs.values() if "error" not in d]
    t: dict = {}
    for k in SUM_KEYS:
        vals = [d[k] for d in ok if d.get(k) is not None]
        t[k] = sum(vals) if vals else None
    pw = sum(d["pdf_words"] for d in ok if d.get("pdf_words"))
    t["coverage_order"] = sum(d["order_words"] for d in ok if d.get("order_words") is not None) / pw if pw else None
    t["coverage_words"] = sum(d["hit_words"] for d in ok if d.get("hit_words") is not None) / pw if pw else None
    t["notes_ok"] = sum(d["notes_ok"] for d in ok)
    t["documents"] = len(ok)
    t["errors"] = len(docs) - len(ok)
    return t


def fmt(spec: str, v) -> str:
    return "-" if v is None else spec.format(v)


def write_markdown(rep: dict, path: Path) -> None:
    docs, tot = rep["docs"], rep["total"]
    names = sorted(docs)
    w = max([len(n) for n in names] + [8])
    L = [f"# HTPD bench: {rep['tag']}", "",
         f"{tot['documents']} documents, {tot['errors']} errors. Converter `{rep['meta']['converter_sha']}`, "
         f"{rep['meta']['date']}, best of {rep['meta']['repeat']} runs, {rep['meta']['cpus']} CPUs"
         f"{', Tesseract' if rep['meta']['tesseract'] else ', no Tesseract'}.", ""]

    def table(title: str, cols: list[tuple[str, str]]) -> None:
        L.extend([f"## {title}", "", "| document | " + " | ".join(lab for _k, lab in cols) + " |",
                  "|---|" + "---:|" * len(cols)])
        spec = {k: f for k, _l, _p, f in METRICS}
        for n in names + ["TOTAL"]:
            d = tot if n == "TOTAL" else docs[n]
            if "error" in d:
                L.append(f"| {n} | ERROR: {d['error'][:80]} |" + " |" * (len(cols) - 1))
                continue
            row = [fmt(spec[k], d.get(k)) for k, _l in cols]
            L.append(f"| {'**TOTAL**' if n == 'TOTAL' else n} | " + " | ".join(row) + " |")
        L.append("")

    table("Speed and size", [("pages", "pages"), ("time_s", "time s"), ("cpu_s", "cpu s"),
                             ("htpd_bytes", "size B"), ("figure_bytes", "figures B")])
    table("Text (pdftotext -layout against the page marks)",
          [("coverage_order", "in order"), ("coverage_words", "on page"), ("pages_ok", "pages ok"),
           ("pages_text", "of"), ("lost_words", "lost"), ("moved_words", "moved"), ("invented_words", "invented"),
           ("missing_pieces", "missing pieces"), ("extra_pieces", "extra pieces"), ("furniture_kept", "headers kept")])
    table("Headings, tables, figures",
          [("toc_found", "headings found"), ("toc_missing", "missing"), ("toc_extra", "extra"),
           ("rows_found", "rows found"), ("rows_pdf", "of"), ("rows_htpd", "rows in file"), ("rows_stray", "stray"),
           ("figs_found", "figures found"), ("figs_pdf", "of"), ("figs_htpd", "in file"), ("notes_ok", "notes ok")])
    if any(docs[n].get("tbl_cells") is not None for n in names if "error" not in docs[n]):
        table("Tables with known cells (make_tables.py)",
              [("tbl_cells_ok", "cells exact"), ("tbl_cells", "of"), ("tbl_rows_ok", "rows exact"), ("tbl_rows", "of"),
               ("tbl_vals_ok", "values right"), ("tbl_vals_row", "in right row"), ("tbl_vals", "of"),
               ("tbl_pages_ok", "right page"), ("tbl_extra", "extra rows")])
    if any(docs[n].get("lay_words") is not None for n in names if "error" not in docs[n]):
        table("Reading order with known paragraphs (make_layouts.py)",
              [("lay_order", "words in order"), ("lay_words", "of"), ("lay_extra", "extra words"),
               ("lay_pages_ok", "pages ok"), ("lay_pages", "of"), ("lay_paras_ok", "paragraphs whole"),
               ("lay_paras", "of"), ("lay_split", "split"), ("lay_joined", "blocks joined")])
    src = [f"{n}: {docs[n].get('toc_source')}" for n in names if "error" not in docs[n]]
    L += ["Headings come from: " + ", ".join(src) + ".", "",
          "Documents without a text layer (scans) have no PDF text to compare: their text, table and figure "
          "columns show `-`.", ""]
    path.write_text("\n".join(L) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------------------------
# Compare


def verdict(key: str, pol: str, a, b) -> str:
    """'' same, 'better', 'worse', 'changed' (no verdict)."""
    if a is None or b is None or a == b or pol == "noise":
        return ""
    if pol == "time":
        if b > a * (1 + TIME_NOISE) and b - a > TIME_NOISE_S:
            return "worse"
        return "better" if b < a * (1 - TIME_NOISE) and a - b > TIME_NOISE_S else ""
    if pol == "size" or pol == "down":
        return "worse" if b > a else "better"
    if pol == "up":
        if isinstance(a, float) and abs(a - b) < 1e-9:
            return ""
        return "worse" if b < a else "better"
    return "changed"


def compare(before: dict, after: dict, md: Path | None) -> int:
    pol = {k: (lab, p, f) for k, lab, p, f in METRICS}
    out: list[str] = []
    worse_acc = 0  # accuracy numbers (up/down) that got worse, summed over the documents
    worse_other = 0  # time or size numbers marked worse
    failed = 0
    bd, ad = before["docs"], after["docs"]
    only_b, only_a = sorted(set(bd) - set(ad)), sorted(set(ad) - set(bd))
    common = sorted(set(bd) & set(ad))
    mark = {"worse": "WORSE", "better": "better", "changed": "", "": ""}

    def line(name: str, k: str, a, b, v: str) -> str:
        lab, _p, spec = pol[k]
        return f"  {name:28} {lab:18} {fmt(spec, a):>14} -> {fmt(spec, b):<14} {mark[v]}"

    both = []
    for n in common:
        if "error" in ad[n] and "error" not in bd[n]:
            failed += 1
            out.append(f"  {n:28} CONVERSION FAILED now: {ad[n]['error'][:100]}   WORSE")
        if "error" in bd[n] or "error" in ad[n]:
            continue
        both.append(n)
        for k, _lab, p, _f in METRICS:
            a, b = bd[n].get(k), ad[n].get(k)
            v = verdict(k, p, a, b)
            if v == "worse":
                if p in ("up", "down"):
                    worse_acc += 1
                else:
                    worse_other += 1
            if v:
                out.append(line(n, k, a, b, v))
    out_total = ["", "TOTAL (all documents in each file)"]
    for k, _lab, p, _f in METRICS:
        a, b = before["total"].get(k), after["total"].get(k)
        out_total.append(line("", k, a, b, verdict(k, p, a, b)))
    # limits over the documents that converted in both files, so one that is only on one side
    # cannot move them
    tb, ta = totals({n: bd[n] for n in both}), totals({n: ad[n] for n in both})
    t_time = (ta["time_s"] - tb["time_s"]) / tb["time_s"] if tb["time_s"] else 0.0
    t_size = (ta["htpd_bytes"] - tb["htpd_bytes"]) / tb["htpd_bytes"] if tb["htpd_bytes"] else 0.0
    time_ok, size_ok = t_time <= LIMIT_TIME, t_size <= LIMIT_SIZE
    acc_ok = worse_acc == 0 and failed == 0
    lines = ["", f"LIMITS over the {len(both)} documents that converted in both files"]
    if both:
        lines += [f"  time      {tb['time_s']:.2f} s -> {ta['time_s']:.2f} s  ({t_time:+.1%}, limit +{LIMIT_TIME:.0%})   "
                  f"{'OK' if time_ok else 'OVER THE LIMIT'}",
                  f"  size      {tb['htpd_bytes']:,} B -> {ta['htpd_bytes']:,} B  ({t_size:+.1%}, "
                  f"limit +{LIMIT_SIZE:.0%})   {'OK' if size_ok else 'OVER THE LIMIT'}"]
    lines.append(f"  accuracy  {worse_acc} worse numbers, {failed} documents that fail now   "
                 f"{'OK' if acc_ok else 'WORSE: every one needs an explanation'}")
    if worse_other:
        lines.append(f"  ({worse_other} time or size numbers of single documents are marked worse; "
                     "only the totals above have a limit)")
    if only_b:
        lines.append(f"  only in before: {', '.join(only_b)}")
    if only_a:
        lines.append(f"  only in after:  {', '.join(only_a)}")
    head = [f"before: {before['tag']} ({before['meta']['converter_sha']}, {before['meta']['date']})",
            f"after:  {after['tag']} ({after['meta']['converter_sha']}, {after['meta']['date']})", "",
            "CHANGED NUMBERS PER DOCUMENT (time only above the noise; WORSE = a number to explain)"]
    text = "\n".join(head + (out or ["  nothing changed"]) + out_total + lines)
    print(text)
    if md:
        md.write_text("```\n" + text + "\n```\n", encoding="utf-8")
    return 0 if (acc_ok and time_ok and size_ok) else 1


# ---------------------------------------------------------------------------------------------
# Main


def git_sha() -> str:
    try:
        r = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"], capture_output=True, text=True)
        dirty = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no"],
                               capture_output=True, text=True).stdout.strip()
        return r.stdout.strip() + ("+dirty" if dirty else "")
    except OSError:
        return "unknown"


def collect(args) -> list[tuple[str, Path]]:
    found: list[tuple[str, Path]] = []
    for folder in args.folders:
        pdfs = sorted(folder.glob("*.pdf"))
        if not pdfs:
            print(f"warning: no PDF in {folder}", file=sys.stderr)
        found += [(folder.name, p) for p in pdfs]
    if not args.folders:
        names = args.set.split(",") if args.set != "auto" else ["private", *PUBLIC]
        if args.set == "public":
            names = list(PUBLIC)
        for s in names:
            if s not in SETS:
                sys.exit(f"unknown set {s!r}; choose from {', '.join(SETS)}")
            pdfs = SETS[s]()
            if not pdfs and s == "private" and args.set != "private":
                continue
            if not pdfs:
                print(f"note: set {s!r} is empty" + (" (run with --fetch)" if s in ("eval", "zoo", "tables", "layouts") else ""),
                      file=sys.stderr)
            found += [(s, p) for p in pdfs]
    if args.only:
        found = [(s, p) for s, p in found if re.search(args.only, f"{s}/{p.stem}")]
    return found


def fetch() -> None:
    if not any((ROOT / "eval" / "docs").glob("*.pdf")):
        subprocess.run([str(ROOT / "eval" / "get_docs.sh")], check=False)
    if not any((ROOT / "build" / "zoo").glob("*.pdf")):
        subprocess.run([sys.executable, str(ROOT / "tests" / "zoo" / "make_zoo.py"), str(ROOT / "build" / "zoo")],
                       check=False)
    if not any((ROOT / "build" / "tables").glob("*.pdf")):
        subprocess.run([sys.executable, str(HERE / "make_tables.py"), str(ROOT / "build" / "tables")], check=False)
    if not any((ROOT / "build" / "layouts").glob("*.pdf")):
        subprocess.run([sys.executable, str(HERE / "make_layouts.py"), str(ROOT / "build" / "layouts")], check=False)


def run(args) -> int:
    converter = args.converter
    if not converter.exists():
        sys.exit(f"{converter} is missing: run `make` first")
    for tool in ("pdftotext",):
        if not shutil.which(tool):
            sys.exit(f"{tool} is needed (poppler-utils)")
    if args.fetch:
        fetch()
    docs_in = collect(args)
    if not docs_in:
        sys.exit("no PDFs: put yours into private/bench/, or use --fetch for the public set, or give a folder")
    work = args.out / "work"
    work.mkdir(parents=True, exist_ok=True)
    extra = args.converter_args.split() if args.converter_args else []
    timing: dict[str, dict] = {}
    jobs = []
    for s, pdf in docs_in:
        name = f"{s}/{pdf.stem}"
        out = work / s / f"{pdf.stem}.htpd"
        print(f"convert {name} ...", end=" ", flush=True)
        timing[name] = convert(converter, pdf, out, args.repeat, extra)
        t = timing[name]
        print("FAILED: " + t["error"] if t["error"] else f"{t['time_s']:.2f} s")
        if not t["error"]:
            jobs.append({"name": name, "pdf": str(pdf), "htpd": str(out)})
    print(f"compare {len(jobs)} documents with their PDFs ...", flush=True)
    results: dict[str, dict] = {}
    if args.jobs > 1 and len(jobs) > 1:
        with ProcessPoolExecutor(max_workers=args.jobs) as ex:
            for job, res in zip(jobs, ex.map(analyse, jobs)):
                results[job["name"]] = res
    else:
        for job in jobs:
            results[job["name"]] = analyse(job)
    docs: dict[str, dict] = {}
    details: dict[str, dict] = {}
    for s, pdf in docs_in:
        name = f"{s}/{pdf.stem}"
        t = timing[name]
        if t["error"]:
            docs[name] = {"error": t["error"], "pdf_bytes": pdf.stat().st_size}
            continue
        r = results[name]
        docs[name] = {"set": s, "pdf_bytes": pdf.stat().st_size, "time_s": round(t["time_s"], 4),
                      "cpu_s": round(t["cpu_s"], 4), **r["m"]}
        details[name] = r["d"]
    rep = {"schema": SCHEMA, "tag": args.tag,
           "meta": {"date": time.strftime("%Y-%m-%d %H:%M:%S"), "converter_sha": git_sha(), "repeat": args.repeat,
                    "cpus": os.cpu_count(), "tesseract": bool(shutil.which("tesseract")),
                    "converter_args": extra, "sets": sorted({s for s, _ in docs_in})},
           "docs": docs, "total": totals(docs)}
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / f"{args.tag}.json").write_text(json.dumps(rep, indent=1, sort_keys=False) + "\n", encoding="utf-8")
    (args.out / f"{args.tag}.details.json").write_text(json.dumps(details, indent=1, ensure_ascii=False) + "\n",
                                                       encoding="utf-8")
    write_markdown(rep, args.out / f"{args.tag}.md")
    print((args.out / f"{args.tag}.md").read_text(encoding="utf-8"))
    print(f"wrote {args.out / (args.tag + '.json')}, .md and .details.json")
    return 1 if rep["total"]["errors"] else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folders", nargs="*", type=Path, help="folders with PDFs (default: the sets)")
    ap.add_argument("--set", default="auto",
                    help="private, eval, zoo, data (comma list), public (= data,eval,zoo) or auto (private if any, "
                         "then public)")
    ap.add_argument("--only", help="regex on set/name: run only these documents")
    ap.add_argument("--tag", default="bench", help="name of the report files (default bench)")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "bench", help="folder for the reports")
    ap.add_argument("--converter", type=Path, default=ROOT / "build" / "htpd")
    ap.add_argument("--converter-args", help="extra options for `htpd convert`, e.g. '--ocr off'")
    ap.add_argument("--repeat", type=int, default=3, help="conversions per PDF; the fastest counts (default 3)")
    ap.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1),
                    help="parallel comparisons (the conversions always run one after the other)")
    ap.add_argument("--fetch", action="store_true", help="download the eval PDFs and make the zoo when missing")
    ap.add_argument("--compare", nargs=2, metavar=("BEFORE", "AFTER"), type=Path)
    ap.add_argument("--md", type=Path, help="with --compare: also write the comparison here")
    args = ap.parse_args()
    if args.compare:
        b, a = (json.loads(p.read_text(encoding="utf-8")) for p in args.compare)
        return compare(b, a, args.md)
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
