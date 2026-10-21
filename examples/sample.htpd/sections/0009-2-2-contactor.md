---
type: Section
title: "2.2 Contactor"
description: "The output contactor is rated for 200 A. It is controlled by the control board, which receives its commands from the MCU."
resource: "sample.pdf#page=2"
tags: [htpd, section]
generated: { by: htpd/0.1.1, at: 2026-10-21T07:47:03Z }
sources:
  - id: source-pdf
    resource: "sample.pdf#page=2"
    title: "ACME DC Charger Controller Reference Manual, pages 2-3"
htpd:
  version: "0.1"
  node: section
  id: "sections/0009-2-2-contactor"
  order: 9
  number: "2.2"
  level: 2
  heading_from: bookmark
  page_start: 2
  page_end: 3
  span_page_end: 3
  parent: "sections/0006-2-electrical-characteristics"
  children: []
  previous: "sections/0008-2-1-1-limits"
  next: "sections/0010-3-charging-sequence"
  source_file: "sample.pdf"
  source_sha256: "0fe9f080d96c46019fc87678346270a9a53924adb7f54f5d3d32ad2f8a834ada"
---

# Location

[ACME DC Charger Controller Reference Manual](../document.md) > [2 Electrical characteristics](0006-2-electrical-characteristics.md) > **2.2 Contactor**

Source: `sample.pdf`, pages 2-3.[^source-pdf]

# Text

<!-- page 2 -->

The output contactor is rated for 200 A. It is controlled by the control board, which receives its commands from the MCU.

<!-- page 3 -->

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

# Neighbours

* Previous: [2.1.1 Limits](0008-2-1-1-limits.md)
* Next: [3 Charging sequence](0010-3-charging-sequence.md)
* Document: [ACME DC Charger Controller Reference Manual](../document.md)

[^source-pdf]: sample.pdf, pages 2-3
