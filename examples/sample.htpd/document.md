---
type: Document
title: "ACME DC Charger Controller Reference Manual"
description: "5 pages converted into 14 sections. Source: sample.pdf."
resource: "sample.pdf"
tags: [htpd, document]
generated: { by: htpd/0.1.0, at: 2026-10-07T03:37:13Z }
sources:
  - id: source-pdf
    resource: "sample.pdf"
    title: "ACME DC Charger Controller Reference Manual"
htpd:
  version: "0.1"
  node: document
  id: "document"
  source_file: "sample.pdf"
  source_sha256: "0fe9f080d96c46019fc87678346270a9a53924adb7f54f5d3d32ad2f8a834ada"
  source_bytes: 8693
  pdf_title: "ACME DC Charger Controller Reference Manual"
  pdf_author: "ACME Power"
  pages: 5
  sections: 14
  headings_from: bookmark
  body_font_size: 10.0
  children:
    - "sections/0001-front-matter"
    - "sections/0002-revision-history"
    - "sections/0003-1-introduction"
    - "sections/0006-2-electrical-characteristics"
    - "sections/0010-3-charging-sequence"
    - "sections/0013-appendix-a-register-map"
---

# Contents

* [Front matter](sections/0001-front-matter.md) - page 1
* [Revision history](sections/0002-revision-history.md) - page 2
* [1 Introduction](sections/0003-1-introduction.md) - page 2
  * [1.1 Scope](sections/0004-1-1-scope.md) - page 2
  * [1.2 Terms](sections/0005-1-2-terms.md) - page 2
* [2 Electrical characteristics](sections/0006-2-electrical-characteristics.md) - pages 2-3
  * [2.1 Supply](sections/0007-2-1-supply.md) - page 2
    * [2.1.1 Limits](sections/0008-2-1-1-limits.md) - page 2
  * [2.2 Contactor](sections/0009-2-2-contactor.md) - pages 2-3
* [3 Charging sequence](sections/0010-3-charging-sequence.md) - pages 3-4
  * [3.1 Thermal derating](sections/0011-3-1-thermal-derating.md) - pages 3-4
  * [3.2 Shutdown](sections/0012-3-2-shutdown.md) - page 4
* [Appendix A Register map](sections/0013-appendix-a-register-map.md) - page 5
  * [A.1 Status register](sections/0014-a-1-status-register.md) - page 5

# Conversion notes

* Source file: `sample.pdf` (8693 bytes, SHA-256 `0fe9f080d96c46019fc87678346270a9a53924adb7f54f5d3d32ad2f8a834ada`).[^source-pdf]
* Headings come from the PDF bookmarks (13 bookmarks, 13 located in the page text).
* Removed 8 running header/footer lines (2 patterns):
  * header, 4 pages: `ACME DC Charger Controller Reference Manual Rev 1.2`
  * footer, 4 pages: `ACME Confidential Page 2`
* Table rows are kept as lines of text with ` | ` between cells (blocks marked `text`). An empty cell is only shown where the positions on the page leave no doubt; a cell with several lines of text is not put together again.
* Text in a monospace font (code, listings) is kept line by line with its spacing (blocks without a mark).
* Figures and images are not converted.

[^source-pdf]: sample.pdf
