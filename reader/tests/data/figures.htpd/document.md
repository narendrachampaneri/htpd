---
type: Document
title: "Figure test"
description: "2 pages converted into 2 sections. Source: figures.pdf."
resource: "figures.pdf"
tags: [htpd, document]
generated: { by: htpd/0.1.1, at: 2026-10-21T08:50:01Z }
sources:
  - id: source-pdf
    resource: "figures.pdf"
    title: "Figure test"
htpd:
  version: "0.1"
  node: document
  id: "document"
  source_file: "figures.pdf"
  source_sha256: "ff0b1f5f431ac3555e7c94eaf50efcee5fe94f765c5fb2a2046110dce73b177d"
  source_bytes: 11022
  pdf_title: "Figure test"
  pdf_author: "anonymous"
  pages: 2
  sections: 2
  headings_from: none
  body_font_size: 10.0
  children:
    - "sections/0001-page-1"
    - "sections/0002-page-2"
---

# Contents

* [Page 1](sections/0001-page-1.md) - page 1 - starts: `1 Power path`
* [Page 2](sections/0002-page-2.md) - page 2 - starts: `The same photo again, on the second page.`

# Conversion notes

* Source file: `figures.pdf` (11022 bytes, SHA-256 `ff0b1f5f431ac3555e7c94eaf50efcee5fe94f765c5fb2a2046110dce73b177d`).[^source-pdf]
* No headings were found; the text is split by page.
* No running headers or footers were found.
* Table rows are kept as lines of text with ` | ` between cells (blocks marked `text`). A cell with several lines is joined, and a table stored cell by cell is put together by position, where the columns are certain; otherwise the lines stay as they stand. An empty cell is only shown where the positions on the page leave no doubt.
* Text in a monospace font (code, listings) is kept line by line with its spacing (blocks without a mark).
* 3 figures (pictures and drawings) are stored as PNG pictures: in a bundle folder in `figures/`, in the single file under `# Figures` at its end. In the text, an image line `![F<n>: caption]` with the place of its picture stands where a figure is.

[^source-pdf]: figures.pdf
