---
type: Document
title: "OCR test"
description: "2 pages converted into 2 sections. Source: ocr.pdf."
resource: "ocr.pdf"
tags: [htpd, document]
generated: { by: htpd/0.1.1, at: 2026-10-21T07:47:03Z }
sources:
  - id: source-pdf
    resource: "ocr.pdf"
    title: "OCR test"
htpd:
  version: "0.1"
  node: document
  id: "document"
  source_file: "ocr.pdf"
  source_sha256: "6c700b831518c5cc329919c2e76dfa6bcb65d114214f03ae9fed528c96fd0f3f"
  source_bytes: 31039
  pdf_title: "OCR test"
  pdf_author: "anonymous"
  pages: 2
  sections: 2
  headings_from: none
  body_font_size: 13.0
  children:
    - "sections/0001-page-1"
    - "sections/0002-page-2"
---

# Contents

* [Page 1](sections/0001-page-1.md) - page 1 - starts: `1 Wiring`
* [Page 2](sections/0002-page-2.md) - page 2 - starts: `2 Scanned notes`

# Conversion notes

* Source file: `ocr.pdf` (31039 bytes, SHA-256 `6c700b831518c5cc329919c2e76dfa6bcb65d114214f03ae9fed528c96fd0f3f`).[^source-pdf]
* No headings were found; the text is split by page.
* No running headers or footers were found.
* Table rows are kept as lines of text with ` | ` between cells (blocks marked `text`). A cell with several lines is joined, and a table stored cell by cell is put together by position, where the columns are certain; otherwise the lines stay as they stand. An empty cell is only shown where the positions on the page leave no doubt.
* Text in a monospace font (code, listings) is kept line by line with its spacing (blocks without a mark).
* Text in pictures was read by OCR (tesseract 5.3.4): 1 scanned page and 1 picture on pages with text. Such text may hold misread words; the sections that hold it list those pages as `ocr_pages`. 0 words OCR was not sure of were left out, not guessed.
* Figures are not converted as pictures; words inside drawings are only there when the PDF has them as text or OCR read them.

[^source-pdf]: ocr.pdf
