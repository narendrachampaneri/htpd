"""Text read from pictures by OCR (FORMAT.md 3.2 `ocr_pages`): both forms say
which pages hold it, and the answer and the packed text say so too."""

from __future__ import annotations

import unittest
from pathlib import Path

from htpd_reader import check, open_bundle
from htpd_reader.answer import answer
from htpd_reader.ask import Asker
from htpd_reader.retriever import Settings

DATA = Path(__file__).resolve().parent / "data"
FOLDER = DATA / "ocr.htpd"     # tests/data/ocr.pdf converted (a picture of text on page 1, a scan on page 2)
SINGLE = DATA / "ocr-single.htpd"


class Ocr(unittest.TestCase):
    def test_both_forms_name_the_pages_read_by_ocr(self) -> None:
        for path in (FOLDER, SINGLE):
            b = open_bundle(path)
            self.assertEqual([s.ocr_pages for s in b.sections], [[1], [2]], path.name)
            self.assertEqual([p for p in check(b) if p.severity == "error"], [])

    def test_a_section_without_ocr_has_no_pages(self) -> None:
        b = open_bundle(ROOT_SAMPLE)
        self.assertTrue(all(s.ocr_pages == [] for s in b.sections))

    def test_the_answer_says_its_page_has_ocr_text(self) -> None:
        for path in (FOLDER, SINGLE):
            a = answer(Asker(open_bundle(path), Settings()), "What is the fuse rated?")
            self.assertTrue(a.found)
            self.assertEqual(a.lines[0].text, "The fuse is rated 16 A.")
            self.assertTrue(a.lines[0].ocr)
            self.assertTrue(a.lines[0].to_dict()["ocr"])
            self.assertIn("[page 2, Page 2, page has OCR text]", a.text())

    def test_the_packed_text_says_which_pages_were_read_by_ocr(self) -> None:
        r = Asker(open_bundle(SINGLE), Settings()).ask("Which pin is CAN low?")
        self.assertIn("text of page 1 read by OCR", r.pack.text)
        self.assertIn("Pin 4 is CAN low.", r.pack.text)


ROOT_SAMPLE = Path(__file__).resolve().parents[2] / "examples" / "sample.htpd"

if __name__ == "__main__":
    unittest.main()
