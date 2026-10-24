#!/usr/bin/env python3
"""Tests of the benchmark tool itself (tests/bench/run_bench.py). No internet, no private PDFs.

    python3 -m unittest discover -s tests/bench -p 'test_bench.py'

The tests that convert need build/htpd and pdftotext; they are skipped without them.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

import run_bench as rb  # noqa: E402
import table_score as ts  # noqa: E402

CONVERTER = ROOT / "build" / "htpd"
SAMPLE = ROOT / "tests" / "data" / "sample.pdf"
NEEDS_BUILD = unittest.skipUnless(CONVERTER.exists() and shutil.which("pdftotext"), "needs build/htpd and pdftotext")


class Words(unittest.TestCase):
    def test_punctuation_and_hyphens_do_not_make_words(self):
        self.assertEqual(rb.tokens("inter- rupt, 3.3 V"), ["inter", "rupt", "3", "3", "v"])
        self.assertEqual(rb.tokens("inter-rupt"), ["inter", "rupt"])

    def test_non_ascii_symbols_are_words_but_ascii_symbols_are_not(self):
        self.assertEqual(rb.tokens("85 °C ± 2 | a < b"), ["85", "°", "c", "±", "2", "a", "b"])

    def test_ligatures_fold(self):
        self.assertEqual(rb.tokens("ﬁnal"), ["final"])

    def test_marks_stay_inside_words(self):
        self.assertEqual(len(rb.tokens("हिन्दी भाषा")), 2)


class Align(unittest.TestCase):
    def test_identical(self):
        a = "the quick brown fox".split()
        ina, inb = rb.align(a, a)
        self.assertTrue(all(ina) and all(inb))

    def test_missing_and_extra_pieces(self):
        pdf = "a b c d e f g".split()
        got = "a b x y e f g z".split()
        ina, inb = rb.align(pdf, got)
        self.assertEqual(rb.pieces(pdf, ina), [["c", "d"]])
        self.assertEqual(rb.pieces(got, inb), [["x", "y"], ["z"]])

    def test_order_matters(self):
        ina, _ = rb.align("a b c d".split(), "c d a b".split())
        self.assertEqual(sum(ina), 2)

    def test_is_a_longest_common_subsequence(self):
        import itertools
        import random
        rnd = random.Random(1)
        for _ in range(200):
            a = [rnd.choice("abc") for _ in range(rnd.randint(0, 9))]
            b = [rnd.choice("abc") for _ in range(rnd.randint(0, 9))]
            ina, inb = rb.align(a, b)
            ma = [w for w, m in zip(a, ina) if m]
            mb = [w for w, m in zip(b, inb) if m]
            self.assertEqual(ma, mb)
            best = 0
            for r in range(len(a), 0, -1):
                if any(rb.subsequence(list(c), b) for c in itertools.combinations(a, r)):
                    best = r
                    break
            self.assertEqual(len(ma), best)


class Furniture(unittest.TestCase):
    NOTES = ["  * header, 4 pages: `ACME Manual Rev 1.2`",
             "  * footer, 9 pages, 3 different texts, for example: `Intro | Page 1`"]

    def test_listed_patterns_are_read(self):
        got = rb.removed_furniture(self.NOTES)
        self.assertEqual(got[0], (True, ["acme", "manual", "rev", "#", "#"], False))
        self.assertEqual(got[1], (False, ["intro", "page", "#"], True))

    def test_strip_takes_the_header_once_and_only_in_the_band(self):
        pats = [(True, ["acme", "manual", "rev", "#", "#"])]
        page = [rb.tokens("ACME Manual Rev 1.2"), rb.tokens("text"), rb.tokens("ACME Manual Rev 1.2")]
        out, n = rb.strip_furniture(page, pats)
        self.assertEqual(n, 1)
        self.assertEqual(out[0], [])
        self.assertEqual(out[2], ["acme", "manual", "rev", "1", "2"])  # the second one is not removed

    def test_a_page_number_must_be_a_whole_line(self):
        pats = [(False, ["#"])]
        out, n = rb.strip_furniture([rb.tokens("text"), rb.tokens("see section 4")], pats)
        self.assertEqual(n, 0)
        self.assertEqual(out[1], ["see", "section", "4"])
        out, n = rb.strip_furniture([rb.tokens("see section 4"), rb.tokens("7")], pats)
        self.assertEqual((n, out[0], out[1]), (1, ["see", "section", "4"], []))

    def test_a_changing_header_is_expanded_from_the_pdf(self):
        pats = rb.removed_furniture(self.NOTES)[1:]
        pages = [[rb.tokens("body"), rb.tokens(f"{c} | Page {n}")]
                 for n, c in enumerate(["Alpha", "Alpha", "Alpha", "Beta", "Beta", "Beta"], 1)]
        pats = [(False, ["intro", "page", "#"], True)]
        got = rb.expand_furniture(pats, pages)
        norms = {tuple(w) for _h, w in got}
        self.assertIn(("alpha", "page", "#"), norms)
        self.assertIn(("beta", "page", "#"), norms)

    def test_a_header_whose_title_never_repeats_is_found_by_its_end(self):
        pats = [(True, ["preamble", "page", "#"], True)]
        pages = [[rb.tokens(f"12.{n}. Title number {n} | Page {n}"), rb.tokens("body text")]
                 for n in range(1, 6)]
        got = rb.expand_furniture(pats, pages)
        out, n = rb.strip_furniture(pages[0], got)
        self.assertEqual((n, out[0], out[1]), (1, [], ["body", "text"]))

    def test_the_end_of_a_header_needs_three_different_titles(self):
        pats = [(True, ["preamble", "page", "#"], True)]
        pages = [[rb.tokens(f"12.{n}. Title {n} | Page {n}"), rb.tokens("body text")] for n in range(1, 3)]
        got = rb.expand_furniture(pats, pages)
        out, n = rb.strip_furniture(pages[0], got)
        self.assertEqual(n, 0)

    def test_a_header_inside_a_merged_line_is_found(self):
        pats = [(True, ["acme", "manual", "rev", "#", "#"])]
        page = [rb.tokens("ACME Manual Rev 1.2    Register map")]
        out, n = rb.strip_furniture(page, pats)
        self.assertEqual((n, out[0]), (1, ["register", "map"]))


HTPD = """---
htpd_file: "0.1"
pages: 2
sections: 2
---

# How to read this file

1. Answer only from this file. A block that starts with ```text is a table.

# Contents

* [S1] Alpha - pages 1-2
  * [S2] 1.1 Beta - page 2

# Conversion notes

* 2 figures (pictures and drawings) are stored as PNG pictures: in the single file under `# Figures`.

# Sections

<!-- htpd section {"n":1,"id":"sections/0001-alpha","number":null,"level":1,"pages":[1,2,2],"parent":"document"} -->
## [S1] Alpha

Path: Alpha - page 1

<!-- page 1 -->

First line of page one.

![F1: Figure 3. Clock tree](#F1)

<!-- page 2 -->

```text
a | b | c
1 | 2 | 3
```

<!-- htpd links -->
* `x` → [S2] 1.1 Beta - page 2

[^source-pdf]: x.pdf

<!-- htpd section {"n":2,"id":"sections/0002-beta","number":"1.1","level":2,"pages":[2,2,2],"parent":"sections/0001-alpha"} -->
## [S2] 1.1 Beta

Path: Alpha > 1.1 Beta - page 2

<!-- page 2 -->

\\# not a heading

![F2: drawing on page 2](#F2)

[^source-pdf]: x.pdf

# Figures

<!-- htpd figure {"id":"F1","page":1,"kind":"drawing","caption":"Figure 3. Clock tree","width":1,"height":1,"type":"image/png"} -->
```base64
AAAA
```
"""


class Parse(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = Path(self.dir.name) / "x.htpd"
        self.path.write_text(HTPD, encoding="utf-8")
        self.h = rb.parse_htpd(self.path)

    def tearDown(self):
        self.dir.cleanup()

    def test_front_contents_notes(self):
        self.assertEqual(self.h.front["pages"], "2")
        self.assertEqual(self.h.contents, [(0, "Alpha"), (1, "1.1 Beta")])
        self.assertEqual(self.h.sections, 2)

    def test_text_goes_to_its_page_and_structure_lines_are_not_text(self):
        p1, p2 = self.h.page_lines[1], self.h.page_lines[2]
        self.assertEqual(p1, ["Alpha", "First line of page one."])
        self.assertNotIn("Path: Alpha - page 1", p1)
        self.assertIn("a | b | c", p2)
        self.assertIn("# not a heading", p2)  # one backslash taken off
        self.assertFalse(any("htpd links" in x or "x.pdf" in x or "Figure 3" in x for x in p1 + p2))

    def test_titles_are_marked(self):
        self.assertEqual(self.h.page_titles[1], {0})
        self.assertEqual(self.h.page_titles[2], {2})  # after the two table lines

    def test_rows(self):
        self.assertEqual(self.h.page_rows[2], ["a | b | c", "1 | 2 | 3"])

    def test_figures_and_their_images(self):
        labels = [(f["page"], f["label"], f["generated"]) for f in self.h.figures]
        self.assertEqual(labels, [(1, "3", False), (2, None, True)])
        self.assertEqual(self.h.image_ids, {"F1"})

    def test_figure_part_is_counted_as_figure_bytes(self):
        text = HTPD.encode()
        self.assertEqual(self.h.figure_bytes, len(text) - text.rindex(b"\n# Figures\n") - 1)

    def test_fenced_hash_line_is_no_part_change(self):
        src = HTPD.replace("a | b | c", "# Figures")
        self.path.write_text(src, encoding="utf-8")
        h = rb.parse_htpd(self.path)
        self.assertEqual(h.figure_bytes, len(src.encode()) - src.encode().rindex(b"\n# Figures\n") - 1)
        self.assertIn("# Figures", h.page_lines[2])


class Pdf(unittest.TestCase):
    def test_outline_of_the_sample(self):
        titles = rb.pdf_outline(SAMPLE)
        self.assertEqual(len(titles), 13)
        self.assertEqual(titles[0], "Revision history")
        self.assertIn("2.1.1 Limits", titles)
        self.assertEqual(titles[-1], "A.1 Status register")

    def test_outline_of_a_pdf_without_bookmarks(self):
        self.assertEqual(rb.pdf_outline(ROOT / "tests" / "data" / "sample_nobookmarks.pdf"), [])

    def test_outline_of_a_broken_file_is_unknown(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.pdf"
            p.write_bytes(b"%PDF-1.4 not a pdf")
            self.assertIsNone(rb.pdf_outline(p))

    def test_captions(self):
        lines = ["Figure 3. Clock tree", "see Figure 4 for the rest", "Figure 5: Power", "Fig. 6 - Reset",
                 "Figure 7", "Figure 2-3 Clock tree ........ 45", "Figure 9 shows the clock"]
        self.assertEqual(rb.pdf_captions(lines), ["3", "5", "6", "7"])

    def test_table_lines_need_a_neighbour(self):
        lines = ["Name      Min    Max", "A         1      2", "text only", "", "", "Title    Rev    Date"]
        self.assertEqual(len(rb.table_lines(lines)), 2)

    def test_printed_contents(self):
        page = [f"{i} Title number {i} ........ {i + 3}" for i in range(1, 7)]
        self.assertEqual(len(rb.printed_contents([page])), 6)
        self.assertEqual(rb.printed_contents([page[:3]]), [])


class Rows(unittest.TestCase):
    def test_found_and_stray(self):
        pdf = [["a", "1", "2"], ["b", "3", "4"]]
        got = [["a", "wrapped", "1", "2"], ["x", "y", "z"]]
        self.assertEqual(rb.match_rows(pdf, got), (1, 1))


class Compare(unittest.TestCase):
    def doc(self, **kw):
        base = {"time_s": 1.0, "htpd_bytes": 1000, "coverage_order": 0.99, "coverage_words": 0.99,
                "lost_words": 3, "moved_words": 0, "invented_words": 1, "toc_found": 5, "rows_found": 7,
                "figs_found": 2, "pages_ok": 4, "notes_ok": 1}
        base.update(kw)
        return base

    def report(self, **kw):
        docs = {"x/a": self.doc(**kw)}
        return {"tag": "t", "meta": {"converter_sha": "s", "date": "d"}, "docs": docs, "total": rb.totals(docs)}

    def run_compare(self, before, after):
        import contextlib
        import io
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            code = rb.compare(before, after, None)
        return code, buf.getvalue()

    def test_same_is_ok(self):
        code, out = self.run_compare(self.report(), self.report())
        self.assertEqual(code, 0)
        self.assertIn("nothing changed", out)

    def test_worse_accuracy_is_marked_and_fails(self):
        code, out = self.run_compare(self.report(), self.report(rows_found=6, lost_words=4))
        self.assertEqual(code, 1)
        self.assertIn("rows found", out)
        self.assertIn("WORSE", out)

    def test_better_accuracy_passes(self):
        code, out = self.run_compare(self.report(), self.report(rows_found=9, lost_words=0))
        self.assertEqual(code, 0)
        self.assertIn("better", out)

    def test_limits(self):
        self.assertEqual(self.run_compare(self.report(), self.report(time_s=1.09))[0], 0)
        self.assertEqual(self.run_compare(self.report(), self.report(time_s=1.2))[0], 1)
        code, out = self.run_compare(self.report(), self.report(htpd_bytes=1049))
        self.assertEqual(code, 0)  # within the limit, but the number is marked
        self.assertIn("WORSE", out)
        code, out = self.run_compare(self.report(), self.report(htpd_bytes=1060))
        self.assertEqual(code, 1)
        self.assertIn("OVER THE LIMIT", out)

    def test_time_noise_is_not_worse(self):
        code, _ = self.run_compare(self.report(time_s=0.50), self.report(time_s=0.52))
        self.assertEqual(code, 0)

    def test_a_document_that_fails_now_is_worse(self):
        after = self.report()
        after["docs"]["x/a"] = {"error": "boom"}
        after["total"] = rb.totals(after["docs"])
        code, out = self.run_compare(self.report(), after)
        self.assertEqual(code, 1)
        self.assertIn("CONVERSION FAILED", out)


@NEEDS_BUILD
class EndToEnd(unittest.TestCase):
    def test_sample(self):
        with tempfile.TemporaryDirectory() as d:
            r = subprocess.run([sys.executable, str(HERE / "run_bench.py"), str(SAMPLE.parent), "--only", "/sample$",
                                "--repeat", "1", "--jobs", "1", "--tag", "t", "--out", d],
                               capture_output=True, text=True, timeout=300)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            rep = json.loads((Path(d) / "t.json").read_text())
            m = rep["docs"]["data/sample"]
            self.assertEqual(m["pages"], 5)
            self.assertEqual(m["coverage_order"], 1.0)
            self.assertEqual(m["lost_words"], 0)
            self.assertEqual((m["toc_found"], m["toc_missing"]), (13, 0))
            self.assertEqual((m["rows_found"], m["rows_pdf"]), (10, 10))
            self.assertEqual(m["notes_ok"], 1)
            self.assertTrue((Path(d) / "t.md").exists())
            code = subprocess.run([sys.executable, str(HERE / "run_bench.py"), "--compare", str(Path(d) / "t.json"),
                                   str(Path(d) / "t.json")], capture_output=True, text=True).returncode
            self.assertEqual(code, 0)


if __name__ == "__main__":
    unittest.main()


class TableScore(unittest.TestCase):
    TRUTH = {"rows": [["Name", "Min", "Max"], ["Supply", "3.0", ""], ["Group [=]", "A", "1"], ["", "B", "2"]],
             "row_pages": [1, 1, 1, 2]}

    def blocks(self, *lines):
        return [[(1, line) for line in lines]]

    def test_every_cell_right_scores_full(self):
        s = ts.score_table(self.TRUTH, [[(1, "Name | Min | Max"), (1, "Supply | 3.0 | "), (1, "Group [=] | A | 1"),
                                         (2, "| B | 2")]])
        self.assertEqual((s["tbl_cells"], s["tbl_cells_ok"]), (12, 12))
        self.assertEqual((s["tbl_rows"], s["tbl_rows_ok"]), (4, 4))
        self.assertEqual(s["tbl_pages_ok"], 4)
        self.assertEqual(s.get("tbl_extra", 0), 0)

    def test_a_cell_in_the_wrong_column_is_not_in_the_right_place_but_in_the_right_row(self):
        s = ts.score_table(self.TRUTH, self.blocks("Name | Min | Max", "Supply |  | 3.0", "Group [=] | A | 1", "| B | 2"))
        self.assertEqual(s["tbl_vals_ok"], 9)       # "3.0" is in the wrong column
        self.assertEqual(s["tbl_vals_row"], 10)     # but in the right row
        self.assertEqual(s["tbl_rows_ok"], 3)

    def test_a_row_cut_in_two_leaves_an_extra_row_and_a_wrong_cell(self):
        s = ts.score_table(self.TRUTH, self.blocks("Name | Min | Max", "Supply | 3", "| .0 | ", "Group [=] | A | 1", "| B | 2"))
        self.assertEqual(s["tbl_extra"], 1)
        self.assertLess(s["tbl_rows_ok"], 4)

    def test_the_mark_of_a_span_does_not_matter_for_the_place_of_a_value(self):
        s = ts.score_table(self.TRUTH, self.blocks("Name | Min | Max", "Supply | 3.0 | ", "Group | A | 1", "| B | 2"))
        self.assertEqual(s["tbl_vals_ok"], 10)
        self.assertEqual(s["tbl_cells_ok"], 11)     # strictly the mark is missing

    def test_a_chart_has_no_rows(self):
        truth = {"rows": [], "kind": "chart"}
        self.assertEqual(ts.score_table(truth, self.blocks("Jan | Feb | Mar"))["tbl_extra"], 1)
        self.assertEqual(ts.score_table(truth, [])["tbl_extra"], 0)

    def test_split_row_reads_empty_cells_at_the_ends(self):
        self.assertEqual(ts.split_row("| B | 2"), ["", "B", "2"])
        self.assertEqual(ts.split_row("a |  | b"), ["a", "", "b"])
        self.assertEqual(ts.split_row("a | b | "), ["a", "b", ""])

    def test_a_row_level_page_mark_inside_a_table_block_moves_the_page_of_the_next_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Path(tmp) / "t.htpd"
            f.write_text(HTPD.replace("a | b | c\n1 | 2 | 3\n", "a | b | c\n<!-- page 3 -->\n1 | 2 | 3\n"),
                         encoding="utf-8")
            h = rb.parse_htpd(f)
            self.assertEqual(h.table_blocks[0], [(2, "a | b | c"), (3, "1 | 2 | 3")])


class Generator(unittest.TestCase):
    @unittest.skipUnless(shutil.which("pdftotext"), "needs pdftotext")
    def test_the_generator_makes_a_pdf_and_a_truth_for_a_case(self):
        import make_tables
        with tempfile.TemporaryDirectory() as tmp:
            make_tables.build("multipage", Path(tmp))
            truth = json.loads((Path(tmp) / "multipage.truth.json").read_text(encoding="utf-8"))
            self.assertGreaterEqual(max(truth["row_pages"]), 3)
            self.assertEqual(truth["rows"][0], ["Part", "Name", "Qty", "Price"])
            firsts = [r for r, row in enumerate(truth["rows"]) if row == truth["rows"][0]]
            self.assertGreaterEqual(len(firsts), 3)        # the header repeats on each page
            self.assertEqual(truth["row_pages"][firsts[1]], truth["row_pages"][firsts[1] + 1])
