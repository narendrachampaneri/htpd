"""Figures (FORMAT.md 4.5): read in both forms, their pictures, the commands
and the MCP tool that hand them out."""

from __future__ import annotations

import base64
import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from htpd_reader import check, open_bundle
from htpd_reader.ask import Asker
from htpd_reader.cli import main
from htpd_reader.mcp import Server
from htpd_reader.retriever import Settings

DATA = Path(__file__).resolve().parent / "data"
FOLDER = DATA / "figures.htpd"          # tests/data/figures.pdf converted: a block diagram with a caption, a photo
SINGLE = DATA / "figures-single.htpd"
PNG = b"\x89PNG\r\n\x1a\n"


class Figures(unittest.TestCase):
    def test_both_forms_give_the_same_figures(self) -> None:
        got = []
        for path in (FOLDER, SINGLE):
            b = open_bundle(path)
            self.assertEqual([p for p in check(b) if p.severity == "error"], [], path.name)
            figs = [(f.figure, f.caption, f.page) for s in b.sections for f in s.figures]
            self.assertEqual(figs, [("F1", "Figure 1. Power path of the charger", 1), ("F2", "picture on page 1", 1),
                                    ("F3", "picture on page 2", 2)])
            got.append([b.figure_png(f) for f in ("F1", "F2", "F3")])
        self.assertEqual(got[0], got[1])
        self.assertTrue(all(png and png.startswith(PNG) for png in got[0]))
        self.assertEqual(got[0][1], got[0][2])   # the same photo twice: stored once, given for both

    def test_a_figure_is_no_text(self) -> None:
        b = open_bundle(FOLDER)
        s = b.sections[0]
        self.assertNotIn("![F1", s.text)
        self.assertEqual(s.text.count("Figure 1. Power path of the charger"), 1)   # the caption line itself

    def test_find_a_figure_by_number(self) -> None:
        b = open_bundle(SINGLE)
        self.assertEqual(b.figure("f1")[0].figure, "F1")
        self.assertEqual(b.figure("2")[0].figure, "F2")
        self.assertIsNone(b.figure("F9"))
        self.assertIsNone(b.figure_png("F9"))

    def test_a_missing_picture_is_an_error(self) -> None:
        text = SINGLE.read_text(encoding="utf-8")
        cut = text[: text.index('<!-- htpd figure {"id":"F2"')]
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "x.htpd"
            p.write_text(cut, encoding="utf-8")
            b = open_bundle(p)
            self.assertIn("missing-figure", [x.code for x in check(b)])
            self.assertIsNone(b.figure_png("F2"))
            self.assertTrue(b.figure_png("F1").startswith(PNG))

    def test_the_command_writes_the_picture(self) -> None:
        with tempfile.TemporaryDirectory() as t:
            out = Path(t) / "f1.png"
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                code = main(["figure", str(SINGLE), "F1", "-o", str(out), "--json"])
            self.assertEqual(code, 0)
            data = json.loads(buf.getvalue())
            self.assertEqual((data["figure"], data["page"]), ("F1", 1))
            self.assertEqual(out.read_bytes(), open_bundle(FOLDER).figure_png("F1"))

    def test_the_mcp_tool_returns_an_image(self) -> None:
        r = Server([DATA]).call("figure", {"bundle": "figures", "figure": "F1"})
        self.assertFalse(r["isError"])
        kinds = [c["type"] for c in r["content"]]
        self.assertEqual(kinds, ["text", "image"])
        image = r["content"][1]
        self.assertEqual(image["mimeType"], "image/png")
        self.assertTrue(base64.b64decode(image["data"]).startswith(PNG))
        self.assertNotIn("_image", r["structuredContent"])
        self.assertTrue(Server([DATA]).call("figure", {"bundle": "figures", "figure": "F7"})["isError"])

    def test_the_text_for_the_ai_names_the_figure(self) -> None:
        r = Asker(open_bundle(SINGLE), Settings()).ask("How does power flow from the grid to the battery?")
        self.assertIn("[Figure F1: Figure 1. Power path of the charger]", r.pack.text)


if __name__ == "__main__":
    unittest.main()
