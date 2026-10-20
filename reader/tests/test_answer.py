"""Tests of the answer from the file: exact lines with pages, or "not found"."""

from __future__ import annotations

import contextlib
import io
import json
import unittest
from pathlib import Path

from htpd_reader import open_bundle
from htpd_reader.answer import NOT_FOUND, answer, pieces
from htpd_reader.ask import Asker
from htpd_reader.cli import main
from htpd_reader.retriever import Settings

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "examples" / "sample.htpd"


class AnswerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.b = open_bundle(SAMPLE)
        self.asker = Asker(self.b, Settings(meaning=False))

    def test_table_row_with_its_header(self) -> None:
        a = answer(self.asker, "What is the reset value of the READY bit?")
        self.assertTrue(a.found)
        first = a.lines[0]
        self.assertEqual(first.text, "0 | READY | R | 0 | Controller is ready to charge")
        self.assertEqual(first.header, "Bit | Name | Access | Reset | Description")
        self.assertEqual((first.page, first.section.number), (5, "A.1"))
        self.assertIn("[page 5, A.1 Status register]", a.text())

    def test_sentence(self) -> None:
        a = answer(self.asker, "At which temperature is the charging current reduced?")
        self.assertTrue(a.found)
        self.assertIn("85 °C", a.lines[0].text)
        self.assertEqual(a.lines[0].page, 3)

    def test_lines_are_exact_text_of_the_file(self) -> None:
        a = answer(self.asker, "contactor coil supply voltage", lines=5)
        for ln in a.lines:
            self.assertTrue(any(ln.text in blk.text for blk in ln.section.blocks), ln.text)

    def test_not_found_says_why(self) -> None:
        a = answer(self.asker, "What is the PCIe link speed?")
        self.assertFalse(a.found)
        self.assertEqual(a.text(), f"{NOT_FOUND} (the file never mentions PCIe)")
        self.assertTrue(answer(self.asker, "the and of").found is False)

    def test_pieces(self) -> None:
        kinds = {p.kind for s in self.b.sections for p in pieces(s)}
        self.assertEqual(kinds, {"row", "sentence"})


class AskCommand(unittest.TestCase):
    def run_cli(self, *args: str) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            code = main(list(args))
        return code, out.getvalue()

    def test_ask(self) -> None:
        code, out = self.run_cli("ask", str(SAMPLE), "What is the reset value of the READY bit?")
        self.assertEqual(code, 0)
        self.assertIn("0 | READY | R | 0", out)
        code, out = self.run_cli("ask", str(SAMPLE), "What is the HDMI version?")
        self.assertEqual((code, out.strip()), (1, "Not found in this file. (the file never mentions HDMI)"))
        code, out = self.run_cli("ask", str(SAMPLE), "READY bit reset", "--json", "--context")
        data = json.loads(out)
        self.assertTrue(data["found"])
        self.assertIn("A.1 Status register", data["context"])


if __name__ == "__main__":
    unittest.main()


class Defaults(unittest.TestCase):
    def test_five_lines_from_five_sections(self) -> None:
        """On the tuning split the exact answer stood in the lines for 0.754 of the
        questions with 5 lines from 5 sections, 0.684 with 3 from 3."""
        import inspect

        from htpd_reader.cli import build_parser

        sig = inspect.signature(answer)
        self.assertEqual((sig.parameters["sections"].default, sig.parameters["lines"].default), (5, 5))
        self.assertEqual(build_parser().parse_args(["ask", "f", "q"]).lines, 5)
