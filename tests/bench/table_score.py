"""Scores the tables of an .htpd file against the known cells of a generated PDF (make_tables.py).

    score_table(truth, blocks) -> dict of numbers

`blocks` are the table blocks of the file (the blocks that start with ```text): each a list of
(page, line). `truth` is the <case>.truth.json of the PDF. The comparison is cell by cell:

    tbl_cells / tbl_cells_ok     every cell of the truth (empty ones too) against the cell of the same
                                 column in the matched row of the file, exactly (a span mark counts)
    tbl_rows / tbl_rows_ok       rows whose cells are all exactly right, and as many
    tbl_vals / tbl_vals_row      cells with text: found as a cell of the matched row, in any column
    tbl_vals_ok                  cells with text in the right row and the right column (span marks ignored)
    tbl_pages_ok                 rows that stand on the page where the PDF prints them
    tbl_extra                    rows of the file that match no row of the truth (a cell cut into rows,
                                 a table made of the lines of a chart)

A row of the truth is matched to the row of the file that shares most of its cell texts; a row of the
file is used once.
"""

from __future__ import annotations

import re
from collections import Counter

MARK = re.compile(r" ?\[(?:>+|=)\]$")


def split_row(line: str) -> list[str]:
    """The cells of a row: ' | ' between them; a leading or trailing empty cell shows as '| ' or ' |'."""
    s = line
    if s.startswith("| "):
        s = " " + s
    if s.endswith(" |"):
        s = s + " "
    return [c.strip() for c in s.split(" | ")]


def plain(cell: str) -> str:
    return MARK.sub("", cell)


def score_table(truth: dict, blocks: list[list[tuple[int, str]]]) -> dict:
    out_rows = [(page, split_row(line)) for block in blocks for page, line in block]
    t_rows = truth["rows"]
    s = Counter()
    s["tbl_rows"] = len(t_rows)
    for row in t_rows:
        s["tbl_cells"] += len(row)
        s["tbl_vals"] += sum(1 for c in row if c)
    if truth.get("kind") == "chart":
        s["tbl_extra"] = sum(1 for _p, cells in out_rows if len(cells) >= 2)
        return dict(s)

    used: set[int] = set()
    last = -1
    for r, row in enumerate(t_rows):
        want = Counter(c for c in row if c)
        best, best_key = None, (0, False, 0)
        for k, (_page, cells) in enumerate(out_rows):
            if k in used:
                continue
            have = Counter(plain(c) for c in cells if c)
            score = sum(min(n, have[plain(w)]) for w, n in want.items())
            score += sum(min(n, Counter(cells)[w]) for w, n in want.items())   # exact cells weigh more
            key = (score, k > last, -k)   # most shared cells; on a tie the next row after the last one
            if score > 0 and key > best_key:
                best, best_key = k, key
        if best is None:
            continue
        used.add(best)
        last = best
        page, cells = out_rows[best]
        for c, want_cell in enumerate(row):
            got = cells[c] if c < len(cells) else ""
            if got == want_cell:
                s["tbl_cells_ok"] += 1
            if want_cell:
                if any(plain(x) == plain(want_cell) for x in cells):
                    s["tbl_vals_row"] += 1
                if plain(got) == plain(want_cell):
                    s["tbl_vals_ok"] += 1
        if cells == row:
            s["tbl_rows_ok"] += 1
        pages = truth.get("row_pages") or []
        if r < len(pages) and pages[r] == page:
            s["tbl_pages_ok"] += 1
    s["tbl_extra"] = sum(1 for k, (_p, cells) in enumerate(out_rows) if k not in used and len(cells) >= 2)
    return dict(s)
