---
type: Section
title: "A.1 Status register"
description: "Bit | Name | Access | Reset | Description; 0 | READY | R | 0 | Controller is ready to charge; 1 | FAULT | R | 0 | A fault is latched; 2 | ..."
resource: "sample.pdf#page=5"
tags: [htpd, section]
generated: { by: htpd/0.1.0, at: 2026-10-07T03:37:13Z }
sources:
  - id: source-pdf
    resource: "sample.pdf#page=5"
    title: "ACME DC Charger Controller Reference Manual, page 5"
htpd:
  version: "0.1"
  node: section
  id: "sections/0014-a-1-status-register"
  order: 14
  number: "A.1"
  level: 2
  heading_from: bookmark
  page_start: 5
  page_end: 5
  span_page_end: 5
  parent: "sections/0013-appendix-a-register-map"
  children: []
  previous: "sections/0013-appendix-a-register-map"
  source_file: "sample.pdf"
  source_sha256: "0fe9f080d96c46019fc87678346270a9a53924adb7f54f5d3d32ad2f8a834ada"
---

# Location

[ACME DC Charger Controller Reference Manual](../document.md) > [Appendix A Register map](0013-appendix-a-register-map.md) > **A.1 Status register**

Source: `sample.pdf`, page 5.[^source-pdf]

# Text

<!-- page 5 -->

```text
Bit | Name | Access | Reset | Description
0 | READY | R | 0 | Controller is ready to charge
1 | FAULT | R | 0 | A fault is latched
2 | DERATE | R | 0 | Thermal derating is active
7:3 | Reserved | R | 0 | Read as zero
```

Writing to a reserved bit has no effect.

# Neighbours

* Previous: [Appendix A Register map](0013-appendix-a-register-map.md)
* Document: [ACME DC Charger Controller Reference Manual](../document.md)

[^source-pdf]: sample.pdf, page 5
