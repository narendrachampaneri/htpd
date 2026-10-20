---
type: Document
title: "links"
description: "3 pages converted into 4 sections. Source: links.pdf."
resource: "links.pdf"
tags: [htpd, document]
generated: { by: htpd/0.1.0, at: 2026-10-20T09:39:49Z }
sources:
  - id: source-pdf
    resource: "links.pdf"
    title: "links"
htpd:
  version: "0.1"
  node: document
  id: "document"
  source_file: "links.pdf"
  source_sha256: "e1110a19192c42a759eb5ee09a1bdee710d828b968cf1b7350475a23cb5d310e"
  source_bytes: 4000
  pdf_title: "untitled"
  pdf_author: "anonymous"
  pages: 3
  sections: 4
  headings_from: bookmark
  body_font_size: 10.0
  children:
    - "sections/0001-1-overview"
    - "sections/0002-2-registers"
    - "sections/0004-3-appendix"
---

# Contents

* [1 Overview](sections/0001-1-overview.md) - page 1
* [2 Registers](sections/0002-2-registers.md) - page 2
  * [2.1 dmcontrol](sections/0003-2-1-dmcontrol.md) - page 2
* [3 Appendix](sections/0004-3-appendix.md) - page 3

# Conversion notes

* Source file: `links.pdf` (4000 bytes, SHA-256 `e1110a19192c42a759eb5ee09a1bdee710d828b968cf1b7350475a23cb5d310e`).[^source-pdf]
* Headings come from the PDF bookmarks (4 bookmarks, 4 located in the page text).
* No running headers or footers were found.
* Table rows are kept as lines of text with ` | ` between cells (blocks marked `text`). A cell with several lines is joined, and a table stored cell by cell is put together by position, where the columns are certain; otherwise the lines stay as they stand. An empty cell is only shown where the positions on the page leave no doubt.
* 3 links of the PDF from one section to another are listed after the text of each section, with the section they lead to.
* Text in a monospace font (code, listings) is kept line by line with its spacing (blocks without a mark).
* Figures and images are not converted.

[^source-pdf]: links.pdf
