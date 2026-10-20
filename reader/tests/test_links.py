"""Links of the PDF from one section to another (FORMAT.md 4.4), in both forms."""

from __future__ import annotations

import contextlib
import io
import json
import unittest
from pathlib import Path

from htpd_reader import check, open_bundle
from htpd_reader.cli import main
from htpd_reader.mcp import Server

DATA = Path(__file__).resolve().parent / "data"
FOLDER = DATA / "links.htpd"
SINGLE = DATA / "links-single.htpd"
EXPECTED = {
    "1 Overview": [("dmcontrol", "sections/0003-2-1-dmcontrol", 2)],
    "2 Registers": [],
    "2.1 dmcontrol": [("Overview", "sections/0001-1-overview", 1)],
    "3 Appendix": [("Registers", "sections/0002-2-registers", 2)],
}


class Links(unittest.TestCase):
    def test_both_forms_give_the_same_links(self) -> None:
        for path in (FOLDER, SINGLE):
            b = open_bundle(path)
            got = {s.title: [(r.text, r.target, r.page) for r in s.refs] for s in b.sections}
            self.assertEqual(got, EXPECTED, path.name)
            for s in b.sections:
                for r in s.refs:
                    self.assertIn(r.target, b.nodes)

    def test_links_are_not_text(self) -> None:
        for path in (FOLDER, SINGLE):
            b = open_bundle(path)
            text = " ".join(blk.text for s in b.sections for blk in s.blocks)
            self.assertNotIn("→", text)
            self.assertNotIn("htpd links", text)
            self.assertEqual([p for p in check(b) if p.severity == "error"], [])

    def test_a_link_to_a_missing_section_is_an_error(self) -> None:
        bad = SINGLE.read_text(encoding="utf-8").replace("[S3] 2.1 dmcontrol", "[S9] 2.1 dmcontrol")
        tmp = DATA / "_broken.htpd"
        try:
            tmp.write_text(bad, encoding="utf-8")
            b = open_bundle(tmp)
            self.assertTrue(any(p.code == "broken-link" for p in b.problems))
            self.assertEqual(b.find("S1").refs, [])
        finally:
            tmp.unlink()

    def test_section_shows_its_links(self) -> None:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            main(["section", str(SINGLE), "S1"])
        self.assertIn("Links:\n  dmcontrol -> 2.1 dmcontrol (page 2)", out.getvalue())
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            main(["section", str(FOLDER), "1", "--json"])
        self.assertEqual(json.loads(out.getvalue())["links"],
                         [{"text": "dmcontrol", "target": "sections/0003-2-1-dmcontrol", "page": 2}])
        text, data = Server([DATA]).t_section({"bundle": "links", "section": "3"})
        self.assertIn("Link: Registers -> 2 Registers (page 2)", text)
        self.assertEqual(len(data["links"]), 1)


if __name__ == "__main__":
    unittest.main()
