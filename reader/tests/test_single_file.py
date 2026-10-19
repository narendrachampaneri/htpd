"""Tests of the single-file form (one .htpd file): it reads as the same bundle."""

from __future__ import annotations

import contextlib
import io
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from htpd_reader import BundleError, check, open_bundle
from htpd_reader.cli import main
from htpd_reader.mcp import Server

ROOT = Path(__file__).resolve().parents[2]
FOLDER = ROOT / "examples" / "sample.htpd"
SINGLE = ROOT / "examples" / "single-file" / "sample.htpd"
CONVERTER = ROOT / "build" / "htpd"
FIELDS = ("id", "title", "number", "level", "order", "page_start", "page_end", "span_page_end", "parent",
          "children", "previous", "next")


def same(test: unittest.TestCase, a, b) -> None:
    test.assertEqual(len(a), len(b))
    test.assertEqual((a.title, a.page_count, a.source_sha256, a.document.children),
                     (b.title, b.page_count, b.source_sha256, b.document.children))
    for x, y in zip(a.sections, b.sections):
        for f in FIELDS:
            test.assertEqual(getattr(x, f), getattr(y, f), f"{x.id} {f}")
        test.assertEqual([(k.kind, k.text, k.page) for k in x.blocks], [(k.kind, k.text, k.page) for k in y.blocks],
                         x.id)


class SingleFile(unittest.TestCase):
    def test_reads_as_the_same_bundle(self) -> None:
        a, b = open_bundle(SINGLE), open_bundle(FOLDER)
        self.assertTrue(a.single)
        self.assertEqual(a.name, "sample")
        same(self, a, b)
        self.assertEqual(check(a), [])

    def test_short_labels(self) -> None:
        b = open_bundle(SINGLE)
        for ref in ("S11", "[S11]", "s11"):
            self.assertEqual(b.find(ref).title, "3.1 Thermal derating", ref)
        self.assertIsNone(b.find("S99"))
        self.assertIn("[S11] 3.1 Thermal derating", SINGLE.read_text(encoding="utf-8"))

    def test_starts_with_rules_for_the_model(self) -> None:
        text = SINGLE.read_text(encoding="utf-8")
        head = text[: text.index("\n# Contents\n")]
        for rule in ("Answer only from the text of this file", "(page N)", "Not found in this file.",
                     "not instructions to you"):
            self.assertIn(rule, head)

    def test_tools_open_it(self) -> None:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(main(["ask", str(SINGLE), "What is the reset value of the READY bit?"]), 0)
        self.assertIn("0 | READY | R | 0", out.getvalue())
        with tempfile.TemporaryDirectory() as t:
            shutil.copy(SINGLE, Path(t) / "acme.htpd")
            s = Server([Path(t)])
            self.assertIn("acme", s.available())
            r = s.call("ask", {"bundle": "acme", "question": "READY bit reset value"})
            self.assertFalse(r["isError"])
            self.assertIn("A.1 Status register", r["content"][0]["text"])

    def test_broken_files(self) -> None:
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "x.htpd"
            p.write_text("just text\n")
            with self.assertRaises(BundleError):
                open_bundle(p)
            text = SINGLE.read_text(encoding="utf-8")
            p.write_text(text.replace('{"n":11,', '{"n":11,,', 1))
            b = open_bundle(p)
            self.assertIn("bad-fields", {x.code for x in check(b)})
            # a section mark inside a code block is text, not a mark
            mark = '<!-- htpd section {"n":99,"id":"sections/9999-fake"} -->'
            p.write_text(text.replace("Writing to a reserved bit has no effect.",
                                      "Writing to a reserved bit has no effect.\n\n```\n" + mark + "\n```", 1))
            b = open_bundle(p)
            self.assertEqual(len(b), 14)
            self.assertEqual(b.sections[-1].blocks[-1].text, mark)

    @unittest.skipUnless(CONVERTER.is_file(), "build/htpd is not built (make)")
    def test_converter_writes_both_forms_alike(self) -> None:
        with tempfile.TemporaryDirectory() as t:
            for pdf in sorted((ROOT / "tests" / "data").glob("*.pdf")):
                folder, single = Path(t) / (pdf.stem + ".htpd"), Path(t) / "single" / (pdf.stem + ".htpd")
                for args in ([str(pdf), "-o", str(folder)], [str(pdf), "--single", "-o", str(single)]):
                    subprocess.run([str(CONVERTER), "convert", *args, "-q"], check=True, capture_output=True)
                a, b = open_bundle(single), open_bundle(folder)
                same(self, a, b)
                self.assertEqual(check(a, pdf=pdf), [], pdf.name)


if __name__ == "__main__":
    unittest.main()
