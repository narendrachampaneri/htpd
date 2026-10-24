---
type: Section
title: "3.1 Thermal derating"
description: "If the temperature exceeds 85 °C, the controller shall reduce the charging current unless emergency mode is enabled."
resource: "sample.pdf#page=3"
tags: [htpd, section]
generated: { by: htpd/0.1.2, at: 2026-10-24T05:39:17Z }
sources:
  - id: source-pdf
    resource: "sample.pdf#page=3"
    title: "ACME DC Charger Controller Reference Manual, pages 3-4"
htpd:
  version: "0.1"
  node: section
  id: "sections/0011-3-1-thermal-derating"
  order: 11
  number: "3.1"
  level: 2
  heading_from: bookmark
  page_start: 3
  page_end: 4
  span_page_end: 4
  parent: "sections/0010-3-charging-sequence"
  children: []
  previous: "sections/0010-3-charging-sequence"
  next: "sections/0012-3-2-shutdown"
  source_file: "sample.pdf"
  source_sha256: "0fe9f080d96c46019fc87678346270a9a53924adb7f54f5d3d32ad2f8a834ada"
---

# Location

[ACME DC Charger Controller Reference Manual](../document.md) > [3 Charging sequence](0010-3-charging-sequence.md) > **3.1 Thermal derating**

Source: `sample.pdf`, pages 3-4.[^source-pdf]

# Text

<!-- page 3 -->

If the temperature exceeds 85 °C, the controller shall reduce the charging current unless emergency mode is enabled.

If the output current exceeds 180 A for more than 2 s, the controller shall open the contactor.

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

<!-- page 4 -->

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

The controller supervises the power stage and the vehicle interface. It samples the bus voltage, the output current and three temperature sensors every 10 ms and reports the values over the internal CAN bus. All limits in this chapter apply over the full operating temperature range unless a different condition is stated in the table.

# Neighbours

* Previous: [3 Charging sequence](0010-3-charging-sequence.md)
* Next: [3.2 Shutdown](0012-3-2-shutdown.md)
* Document: [ACME DC Charger Controller Reference Manual](../document.md)

[^source-pdf]: sample.pdf, pages 3-4
