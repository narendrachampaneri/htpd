"""Tests of phase 1: reading a bundle exactly, and checking it.

Plain unittest, so both `python -m pytest` and
`python -m unittest discover -s reader/tests -t .` run them. No internet.
"""

from __future__ import annotations

import contextlib
import io
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from htpd_reader import Bundle, BundleError, check, open_bundle, search
from htpd_reader.bundle import Block, parse_body
from htpd_reader.check import errors
from htpd_reader.cli import main
from htpd_reader.render import render_text_part

from reader.tests.bundlegen import C, P, Sec, T, edit, make_bundle

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "examples" / "sample.htpd"
CONVERTER = ROOT / "build" / "htpd"


def text_part(path: Path) -> str:
    """The # Text part of a file, as bytes on the disk, up to the next part."""
    body = path.read_text(encoding="utf-8")
    start = body.index("\n# Text\n") + 1
    rest = body[start:]
    ends = [rest.find(f"\n{p}\n") + 1 for p in ("# Subsections", "# Neighbours") if rest.find(f"\n{p}\n") != -1]
    return rest[: min(ends)] if ends else rest


def codes(problems) -> set[str]:
    return {p.code for p in problems}


class TmpCase(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def bundle(self, sections: list[Sec], name: str = "t.htpd") -> Path:
        return make_bundle(self.tmp / name, sections)


# ---------------------------------------------------------------- the sample


class SampleBundle(unittest.TestCase):
    def setUp(self) -> None:
        self.b = open_bundle(SAMPLE)

    def test_counts_and_fields(self) -> None:
        self.assertEqual(len(self.b), 14)
        self.assertEqual(self.b.title, "ACME DC Charger Controller Reference Manual")
        self.assertEqual(self.b.page_count, 5)
        n = self.b.node("sections/0008-2-1-1-limits")
        self.assertEqual((n.number, n.level, n.page_start, n.page_end), ("2.1.1", 3, 2, 2))
        self.assertEqual(n.parent, "sections/0007-2-1-supply")
        self.assertEqual(n.previous, "sections/0007-2-1-supply")
        self.assertEqual(n.next, "sections/0009-2-2-contactor")
        self.assertEqual(n.children, [])

    def test_tree_and_order(self) -> None:
        n = self.b.find("2.1.1")
        self.assertEqual([a.title for a in self.b.ancestors(n)], ["2 Electrical characteristics", "2.1 Supply"])
        top = self.b.children(self.b.document)
        self.assertEqual(top[0].title, "Front matter")
        self.assertEqual([n.id for n, _ in self.b.walk()], [s.id for s in self.b.sections])
        # walking next from the first section visits every section once
        cur, seen = self.b.sections[0], []
        while cur is not None:
            seen.append(cur.id)
            cur = self.b.next(cur)
        self.assertEqual(seen, [s.id for s in self.b.sections])

    def test_blocks_and_pages(self) -> None:
        limits = self.b.find("2.1.1")
        self.assertEqual([(x.kind, x.page) for x in limits.blocks], [("table", 2)])
        self.assertEqual(limits.blocks[0].rows()[-1], ["Output current", "0", "", "200", "A"])
        derate = self.b.find("3.1")
        self.assertEqual(derate.pages, [3, 4])
        self.assertTrue(derate.blocks[0].text.startswith("If the temperature exceeds 85 °C"))

    def test_find_by_id_file_and_number(self) -> None:
        for ref in ("sections/0011-3-1-thermal-derating", "0011-3-1-thermal-derating.md",
                    "sections/0011-3-1-thermal-derating.md", "3.1", "Section 3.1"):
            self.assertEqual(self.b.find(ref).id, "sections/0011-3-1-thermal-derating", ref)
        self.assertIsNone(self.b.find("9.9"))

    def test_check_finds_nothing(self) -> None:
        self.assertEqual(check(self.b), [])

    def test_text_is_byte_for_byte(self) -> None:
        for s in self.b.sections:
            self.assertEqual(render_text_part(s.blocks), text_part(self.b.root / s.path), s.id)

    def test_search(self) -> None:
        hits = search(self.b, "temperature exceeds")
        self.assertEqual(hits[0].node.id, "sections/0011-3-1-thermal-derating")
        self.assertEqual(hits[0].block.page, 3)
        self.assertEqual(search(self.b, "READY reserved")[0].node.number, "A.1")
        self.assertEqual(search(self.b, "nonexistentword"), [])
        self.assertEqual(search(self.b, "   "), [])


# ---------------------------------------------------------------- hard cases


class HardText(TmpCase):
    def test_four_backticks_hold_three(self) -> None:
        code = "line one\n```\nline three"
        b = open_bundle(self.bundle([Sec("1 A", blocks=[C(1, code), P(1, "After.")])]))
        f = b.root / b.sections[0].path
        self.assertIn("````\nline one\n```\nline three\n````\n", f.read_text())
        self.assertEqual([(x.kind, x.text) for x in b.sections[0].blocks], [("code", code), ("paragraph", "After.")])
        self.assertEqual(check(b), [])

    def test_paragraph_starting_with_backslash(self) -> None:
        b = open_bundle(self.bundle([Sec("1 A", blocks=[P(1, "\\n is a newline"), P(1, "# not a heading")])]))
        f = (b.root / b.sections[0].path).read_text()
        self.assertIn("\n\\\\n is a newline\n", f)
        self.assertIn("\n\\# not a heading\n", f)
        self.assertEqual([x.text for x in b.sections[0].blocks], ["\\n is a newline", "# not a heading"])

    def test_part_heading_and_page_mark_inside_block(self) -> None:
        code = "# Text\n<!-- page 9 -->\n# Neighbours"
        b = open_bundle(self.bundle([Sec("1 A", blocks=[C(1, code), P(2, "Later.")])]))
        s = b.sections[0]
        self.assertEqual([(x.kind, x.page, x.text) for x in s.blocks],
                         [("code", 1, code), ("paragraph", 2, "Later.")])
        self.assertEqual(check(b), [])

    def test_empty_table_cell(self) -> None:
        b = open_bundle(self.bundle([Sec("1 A", blocks=[T(1, "a | b | c\n1 |  | 3")])]))
        self.assertEqual(b.sections[0].blocks[0].rows(), [["a", "b", "c"], ["1", "", "3"]])

    def test_code_keeps_spaces_and_empty_lines(self) -> None:
        code = "int main() {\n\n    return 0;   \n}"
        b = open_bundle(self.bundle([Sec("1 A", blocks=[C(1, code)])]))
        self.assertEqual(b.sections[0].blocks[0].text, code)

    def test_no_text_marker(self) -> None:
        b = open_bundle(self.bundle([Sec("1 A", blocks=[]), Sec("1.1 B", level=2, blocks=[P(1, "x")])]))
        self.assertEqual(b.sections[0].blocks, [])
        self.assertEqual(check(b), [])

    def test_marker_words_after_a_page_mark_are_text(self) -> None:
        line = "_No text directly under this heading._"
        b = open_bundle(self.bundle([Sec("1 A", blocks=[P(1, line)])]))
        self.assertEqual([x.text for x in b.sections[0].blocks], [line])

    def test_round_trip_of_hand_made_bundle(self) -> None:
        secs = [Sec("1 A", blocks=[P(1, "> quote"), P(1, "---"), P(2, "<b>"), P(2, "[x]: y"), T(2, "a | b | c")]),
                Sec("2 B", blocks=[C(3, "````\nfour"), P(3, "- list item stays")])]
        b = open_bundle(self.bundle(secs))
        for s in b.sections:
            self.assertEqual(render_text_part(s.blocks), text_part(b.root / s.path))
        self.assertEqual(b.sections[0].blocks[0].text, "> quote")
        self.assertEqual(b.sections[1].blocks[1].text, "- list item stays")

    def test_parse_body_marks_text_without_page(self) -> None:
        _, blocks, _, problems = parse_body("# Text\n\nloose\n", 1, "x.md")
        self.assertEqual(blocks, [Block("paragraph", "loose", None, 3)])
        self.assertEqual(codes(problems), {"text-without-page"})


# ---------------------------------------------------------------- broken bundles


class Broken(TmpCase):
    def good(self) -> Path:
        return self.bundle([Sec("1 A", blocks=[P(1, "a")]),
                            Sec("1.1 B", level=2, blocks=[P(1, "b")]),
                            Sec("1.2 C", level=2, blocks=[P(2, "c")]),
                            Sec("2 D", blocks=[P(2, "d")])])

    def assertFinds(self, root: Path, code: str) -> None:
        found = check(open_bundle(root))
        self.assertIn(code, codes(errors(found)), [str(p) for p in found])

    def test_good_one_is_clean(self) -> None:
        self.assertEqual(check(open_bundle(self.good())), [])

    def test_broken_link(self) -> None:
        r = self.good()
        edit(r / "sections/0001-1-a.md", "* Next: [1.1 B](0002-1-1-b.md)", "* Next: [1.1 B](0099-gone.md)")
        self.assertFinds(r, "broken-link")

    def test_broken_link_in_sections_index(self) -> None:
        r = self.good()
        edit(r / "sections/index.md", "(0004-2-d.md)", "(0004-2-x.md)")
        self.assertFinds(r, "broken-link")

    def test_children_and_parent_disagree(self) -> None:
        r = self.good()
        edit(r / "sections/0003-1-2-c.md", 'parent: "sections/0001-1-a"', 'parent: "document"')
        self.assertFinds(r, "parent-child-mismatch")

    def test_child_missing_from_parent_list(self) -> None:
        r = self.good()
        edit(r / "sections/0001-1-a.md", '    - "sections/0003-1-2-c"\n', "")
        self.assertFinds(r, "parent-child-mismatch")

    def test_child_listed_by_a_second_parent(self) -> None:
        r = self.good()
        edit(r / "sections/0004-2-d.md", "children: []", 'children:\n    - "sections/0003-1-2-c"')
        found = [p for p in check(open_bundle(r)) if p.code == "parent-child-mismatch"]
        self.assertEqual([p.file for p in found], ["sections/0004-2-d.md"])

    def test_missing_node(self) -> None:
        r = self.good()
        edit(r / "sections/0002-1-1-b.md", 'next: "sections/0003-1-2-c"', 'next: "sections/0077-x"')
        self.assertFinds(r, "missing-node")

    def test_previous_and_next_disagree(self) -> None:
        r = self.good()
        edit(r / "sections/0003-1-2-c.md", 'previous: "sections/0002-1-1-b"', 'previous: "sections/0001-1-a"')
        self.assertFinds(r, "order-link")

    def test_next_missing(self) -> None:
        r = self.good()
        edit(r / "sections/0003-1-2-c.md", '  next: "sections/0004-2-d"\n', "")
        self.assertFinds(r, "order-link")

    def test_unclosed_block(self) -> None:
        r = self.bundle([Sec("1 A", blocks=[C(1, "x")])])
        edit(r / "sections/0001-1-a.md", "```\nx\n```\n", "```\nx\n")
        self.assertFinds(r, "unclosed-block")

    def test_page_mark_outside_section_pages(self) -> None:
        r = self.good()
        edit(r / "sections/0004-2-d.md", "<!-- page 2 -->", "<!-- page 1 -->")
        self.assertFinds(r, "page-mark-range")

    def test_wrong_id(self) -> None:
        r = self.good()
        edit(r / "sections/0004-2-d.md", 'id: "sections/0004-2-d"', 'id: "sections/0004-2-e"')
        self.assertFinds(r, "wrong-id")

    def test_section_count(self) -> None:
        r = self.good()
        edit(r / "document.md", "sections: 4", "sections: 5")
        self.assertFinds(r, "section-count")

    def test_document_children(self) -> None:
        r = self.good()
        edit(r / "document.md", '    - "sections/0004-2-d"\n', "")
        self.assertFinds(r, "parent-child-mismatch")

    def test_bad_yaml(self) -> None:
        r = self.good()
        edit(r / "sections/0004-2-d.md", 'title: "2 D"', 'title: "2 D')
        self.assertFinds(r, "bad-fields")

    def test_no_fields(self) -> None:
        r = self.good()
        p = r / "sections/0004-2-d.md"
        p.write_text(p.read_text().replace("---\n", "", 1))
        self.assertFinds(r, "no-fields")

    def test_order_number(self) -> None:
        r = self.good()
        edit(r / "sections/0004-2-d.md", "order: 4", "order: 7")
        self.assertFinds(r, "order")

    def test_page_range(self) -> None:
        r = self.good()
        edit(r / "sections/0004-2-d.md", "span_page_end: 2", "span_page_end: 9")
        self.assertFinds(r, "page-range")

    def test_cycle(self) -> None:
        r = self.good()
        edit(r / "sections/0001-1-a.md", 'parent: "document"', 'parent: "sections/0002-1-1-b"')
        found = check(open_bundle(r))
        self.assertIn("cycle", codes(found))
        list(open_bundle(r).walk())  # must not loop

    def test_source_mismatch(self) -> None:
        r = self.good()
        edit(r / "sections/0004-2-d.md", "ab" * 32, "cd" * 32)
        self.assertFinds(r, "source-mismatch")

    def test_pdf_checksum(self) -> None:
        pdf = ROOT / "tests" / "data" / "sample.pdf"
        self.assertEqual(check(open_bundle(SAMPLE), pdf=pdf), [])
        other = ROOT / "tests" / "data" / "fonts.pdf"
        self.assertIn("source-mismatch", codes(check(open_bundle(SAMPLE), pdf=other)))

    def test_not_a_bundle(self) -> None:
        with self.assertRaises(BundleError):
            Bundle.open(self.tmp)
        with self.assertRaises(BundleError):
            Bundle.open(self.tmp / "nothing")

    def test_file_order_past_9999(self) -> None:
        r = self.good()
        for old, new in (("0003-1-2-c", "9999-1-2-c"), ("0004-2-d", "10000-2-d")):
            (r / f"sections/{old}.md").rename(r / f"sections/{new}.md")
        b = open_bundle(r)
        self.assertEqual([s.path for s in b.sections[2:]], ["sections/9999-1-2-c.md", "sections/10000-2-d.md"])


# ---------------------------------------------------------------- command line


class CommandLine(unittest.TestCase):
    def run_cli(self, *args: str) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            code = main(list(args))
        return code, out.getvalue()

    def test_contents(self) -> None:
        code, out = self.run_cli("contents", str(SAMPLE))
        self.assertEqual(code, 0)
        self.assertIn("    2.1.1 Limits (page 2)  [sections/0008-2-1-1-limits]", out)
        code, out = self.run_cli("contents", str(SAMPLE), "--json")
        data = json.loads(out)
        self.assertEqual(len(data["contents"]), 14)
        self.assertEqual(data["contents"][7]["depth"], 2)

    def test_section(self) -> None:
        code, out = self.run_cli("section", str(SAMPLE), "2.1.1")
        self.assertEqual(code, 0)
        self.assertIn("Output current | 0 |  | 200 | A", out)
        self.assertIn("--- page 2 ---", out)
        code, out = self.run_cli("section", str(SAMPLE), "3.1", "--json")
        data = json.loads(out)
        self.assertEqual(data["blocks"][0]["page"], 3)
        self.assertEqual(data["path"][-1], "3.1 Thermal derating")
        code, _ = self.run_cli("section", str(SAMPLE), "42")
        self.assertEqual(code, 2)

    def test_search(self) -> None:
        code, out = self.run_cli("search", str(SAMPLE), "contactor", "--json")
        self.assertEqual(code, 0)
        self.assertTrue(json.loads(out))
        code, out = self.run_cli("search", str(SAMPLE), "zzzzz")
        self.assertEqual((code, out.strip()), (1, "not found"))

    def test_check(self) -> None:
        code, out = self.run_cli("check", str(SAMPLE), "--json")
        self.assertEqual((code, json.loads(out)["ok"]), (0, True))
        with tempfile.TemporaryDirectory() as t:
            r = Path(t) / "b"
            shutil.copytree(SAMPLE, r)
            edit(r / "sections/0002-revision-history.md", "next: \"sections/0003-1-introduction\"",
                 "next: \"sections/0004-1-1-scope\"")
            code, out = self.run_cli("check", str(r))
            self.assertEqual(code, 1)
            self.assertIn("[order-link]", out)

    def test_not_a_bundle(self) -> None:
        code, _ = self.run_cli("contents", "/nonexistent/folder")
        self.assertEqual(code, 2)


# ---------------------------------------------------------------- with the converter


@unittest.skipUnless(CONVERTER.is_file(), "build/htpd is not built (make)")
class WithConverter(unittest.TestCase):
    """Bundles made right now from the test PDFs must read and check clean."""

    def test_test_pdfs(self) -> None:
        with tempfile.TemporaryDirectory() as t:
            for pdf in sorted((ROOT / "tests" / "data").glob("*.pdf")):
                out = Path(t) / (pdf.stem + ".htpd")
                subprocess.run([str(CONVERTER), "convert", str(pdf), "-o", str(out)],
                               check=True, capture_output=True)
                b = open_bundle(out)
                self.assertEqual(check(b, pdf=pdf), [], pdf.name)
                for s in b.sections:
                    self.assertEqual(render_text_part(s.blocks), text_part(b.root / s.path), f"{pdf.name} {s.id}")


if __name__ == "__main__":
    unittest.main()
