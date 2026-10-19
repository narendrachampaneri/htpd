"""Tests of the small YAML reader used when PyYAML is not installed."""

from __future__ import annotations

import unittest

from htpd_reader.miniyaml import MiniYAMLError, load


class MiniYaml(unittest.TestCase):
    def test_fields_of_a_section_file(self) -> None:
        text = '''type: Section
title: "2.1 \\"Supply\\" \\\\ rail"
tags: [htpd, section]
generated: { by: htpd/0.1.0, at: 2026-10-07T03:37:13Z }
sources:
  - id: source-pdf
    resource: "sample.pdf#page=2"
    title: "ACME, page 2"
htpd:
  version: "0.1"
  order: 7
  number: "2.1"
  body_font_size: 10.0
  children:
    - "sections/0008-2-1-1-limits"
  empty: []
  pdf_author: "\\x41\\u00B0\\tend"'''
        self.assertEqual(load(text), {
            "type": "Section", "title": '2.1 "Supply" \\ rail', "tags": ["htpd", "section"],
            "generated": {"by": "htpd/0.1.0", "at": "2026-10-07T03:37:13Z"},
            "sources": [{"id": "source-pdf", "resource": "sample.pdf#page=2", "title": "ACME, page 2"}],
            "htpd": {"version": "0.1", "order": 7, "number": "2.1", "body_font_size": 10.0,
                     "children": ["sections/0008-2-1-1-limits"], "empty": [], "pdf_author": "A°\tend"}})

    def test_scalars(self) -> None:
        self.assertEqual(load("a: 1\nb: -2\nc: 1.5\nd: true\ne: null\nf: word\ng:"),
                         {"a": 1, "b": -2, "c": 1.5, "d": True, "e": None, "f": "word", "g": None})

    def test_errors_are_never_guessed(self) -> None:
        for bad in ('a: "open', "a: [1, 2", "a: 'single'", "a: |", "a: 1\n   b: 2", "a: \"x\\q\"",
                    "- a\nb: 1", "\tkey: 1"):
            with self.assertRaises(MiniYAMLError, msg=bad):
                load(bad)

    def test_empty(self) -> None:
        self.assertIsNone(load(""))


if __name__ == "__main__":
    unittest.main()
