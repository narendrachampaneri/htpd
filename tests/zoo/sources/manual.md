---
title: LogBox 3 User Manual
---

# Safety instructions

Read this manual before you install the LogBox. Keep it near the device.

> **Warning:** Disconnect the supply before you open the housing. The terminals can carry dangerous voltages.

The LogBox must only be connected to circuits with a rated voltage of at most 30 volts. Do not use the LogBox in rooms where explosive gases may be present.

# Installation

## Mounting

The LogBox is mounted on a standard DIN rail. Leave at least 20 millimetres of free space above and below the housing so that air can flow through the vents.

1. Hook the upper edge of the housing onto the rail.
2. Press the lower edge against the rail until the latch clicks.
3. Check that the housing does not move.

## Wiring

Connect the sensors before you switch on the supply. Use cables with a cross section of 0.5 to 1.5 square millimetres. Shielded cables are recommended for the analog inputs when the cable is longer than three metres.

| Terminal | Signal | Range |
|---|---|---|
| A1 | Analog input 1 | 0 to 10 V |
| A2 | Analog input 2 | 4 to 20 mA |
| D1 | Digital input | 0 to 30 V |
| P+ | Supply | 12 to 30 V |

# Configuration

The LogBox reads its settings from the file `logbox.conf` on the memory card. The file is plain text with one setting per line.

```
# logbox.conf
interval = 10        # seconds between two samples
channels = A1, A2, D1
upload   = https://example.org/logbox
timezone = UTC
```

The interval can be set from 1 to 3600 seconds. When the file is missing, the LogBox uses an interval of 60 seconds and records all channels.

## Network settings

The Ethernet port uses DHCP by default. To set a fixed address, add the lines `address`, `netmask` and `gateway` to the file. The LogBox must be restarted after a change of the network settings.

# Operation

The status light shows the state of the LogBox:

- Green: recording
- Green, blinking: recording and uploading
- Yellow: memory card almost full
- Red: error, see the log file

The memory card holds about 400 days of data at an interval of 10 seconds with three channels.

# Troubleshooting

## The status light stays red

The LogBox could not read the memory card. Remove the card, check that it is formatted with FAT32 and insert it again.

## No data is uploaded

Check the network cable and the address in the setting `upload`. The log file `logbox.log` on the card lists every failed upload with the reason.

# Technical data

| Property | Value |
|---|---|
| Supply voltage | 12 to 30 V DC |
| Power consumption | 1.2 W |
| Operating temperature | -20 to 60 °C |
| Protection class | IP20 |
| Dimensions | 90 x 52 x 60 mm |
| Weight | 160 g |
