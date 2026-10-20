---
type: Section
title: "3 Charging sequence"
description: "The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature ..."
resource: "sample.pdf#page=3"
tags: [htpd, section]
generated: { by: htpd/0.1.0, at: 2026-10-20T09:41:14Z }
sources:
  - id: source-pdf
    resource: "sample.pdf#page=3"
    title: "ACME DC Charger Controller Reference Manual, page 3"
htpd:
  version: "0.1"
  node: section
  id: "sections/0010-3-charging-sequence"
  order: 10
  number: "3"
  level: 1
  heading_from: bookmark
  page_start: 3
  page_end: 3
  span_page_end: 4
  parent: "document"
  children:
    - "sections/0011-3-1-thermal-derating"
    - "sections/0012-3-2-shutdown"
  previous: "sections/0009-2-2-contactor"
  next: "sections/0011-3-1-thermal-derating"
  source_file: "sample.pdf"
  source_sha256: "0fe9f080d96c46019fc87678346270a9a53924adb7f54f5d3d32ad2f8a834ada"
---

# Location

[ACME DC Charger Controller Reference Manual](../document.md) > **3 Charging sequence**

Source: `sample.pdf`, page 3.[^source-pdf]

# Text

<!-- page 3 -->

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

# Subsections

* [3.1 Thermal derating](0011-3-1-thermal-derating.md) - pages 3-4
* [3.2 Shutdown](0012-3-2-shutdown.md) - page 4

# Neighbours

* Previous: [2.2 Contactor](0009-2-2-contactor.md)
* Next: [3.1 Thermal derating](0011-3-1-thermal-derating.md)
* Document: [ACME DC Charger Controller Reference Manual](../document.md)

[^source-pdf]: sample.pdf, page 3
