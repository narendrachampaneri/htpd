---
type: Section
title: "2 Electrical characteristics"
description: "The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature ..."
resource: "sample.pdf#page=2"
tags: [htpd, section]
generated: { by: htpd/0.1.0, at: 2026-10-20T09:41:14Z }
sources:
  - id: source-pdf
    resource: "sample.pdf#page=2"
    title: "ACME DC Charger Controller Reference Manual, page 2"
htpd:
  version: "0.1"
  node: section
  id: "sections/0006-2-electrical-characteristics"
  order: 6
  number: "2"
  level: 1
  heading_from: bookmark
  page_start: 2
  page_end: 2
  span_page_end: 3
  parent: "document"
  children:
    - "sections/0007-2-1-supply"
    - "sections/0009-2-2-contactor"
  previous: "sections/0005-1-2-terms"
  next: "sections/0007-2-1-supply"
  source_file: "sample.pdf"
  source_sha256: "0fe9f080d96c46019fc87678346270a9a53924adb7f54f5d3d32ad2f8a834ada"
---

# Location

[ACME DC Charger Controller Reference Manual](../document.md) > **2 Electrical characteristics**

Source: `sample.pdf`, page 2.[^source-pdf]

# Text

<!-- page 2 -->

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

# Subsections

* [2.1 Supply](0007-2-1-supply.md) - page 2
* [2.2 Contactor](0009-2-2-contactor.md) - pages 2-3

# Neighbours

* Previous: [1.2 Terms](0005-1-2-terms.md)
* Next: [2.1 Supply](0007-2-1-supply.md)
* Document: [ACME DC Charger Controller Reference Manual](../document.md)

[^source-pdf]: sample.pdf, page 2
