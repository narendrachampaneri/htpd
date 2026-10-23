---
title: CC-200 Charge Controller Datasheet
---

# Overview

The CC-200 is a charge controller for direct current chargers with one or two charging guns. It handles the communication with the vehicle, the safety functions of the charger and the connection to the back office.

The controller is built around a dual-core application processor and a separate safety microcontroller. The application processor runs Linux and the charging software. The safety microcontroller supervises the control pilot signal, the contactors and the emergency stop inputs.

## Key features

- Two independent charging guns, each with its own control pilot and proximity pilot
- Power line communication for ISO 15118 and DIN 70121
- Contactor drivers with mirror contact feedback
- Eight inputs for PT1000 temperature sensors
- Isolated CAN, RS-485 and two Ethernet ports
- Display output for a touch screen of up to 15 inches

## Intended use

The CC-200 is intended for stationary electric vehicle supply equipment. It must be installed inside the housing of the charger by trained personnel. It is not intended for mobile use or for use outside a closed housing.

# Architecture

## Application processor

The application processor has two Cortex-A55 cores running at up to 1.7 GHz, one gigabyte of LPDDR4 memory and eight gigabytes of eMMC storage. Secure boot is supported by the hardware but not enabled in the default software.

## Safety microcontroller

The safety microcontroller generates the control pilot signal with a duty cycle accuracy of 0.1 percent. It opens the contactors within 20 milliseconds when an emergency stop input is released or when the insulation monitor reports a fault.

## Temperature measurement

Each PT1000 input measures from minus 40 to plus 200 degrees Celsius. A broken sensor wire is detected within one second and reported as a fault. The charging current is reduced in steps of 10 percent when a connector pin exceeds 90 degrees Celsius.

# Electrical characteristics

## Absolute maximum ratings

Stresses above these values may damage the device permanently.

| Symbol | Parameter | Min | Max | Unit |
|---|---|---|---|---|
| VIN | Supply voltage | -0.5 | 32 | V |
| VIO | Voltage on any digital input | -0.5 | 30 | V |
| TSTG | Storage temperature | -40 | 85 | °C |
| IOUT | Output current per driver | | 2.5 | A |

## Recommended operating conditions

| Symbol | Parameter | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| VIN | Supply voltage | 18 | 24 | 30 | V |
| TA | Ambient temperature | -40 | 25 | 70 | °C |
| PIDLE | Power consumption, idle | | 2.5 | | W |
| PLOAD | Power consumption, full load | | 6 | | W |

# Connectors

## X1 Power input

The power input accepts 18 to 30 volts. It is protected against reverse polarity and against surges up to 1 kilovolt.

| Pin | Name | Description |
|---|---|---|
| 1 | VIN | Supply voltage |
| 2 | GND | Ground |
| 3 | PE | Protective earth |

## X2 Contactor outputs

| Pin | Name | Description |
|---|---|---|
| 1 | HV1_OUT | Contactor 1 coil |
| 2 | HV1_MIRROR | Contactor 1 mirror contact |
| 3 | HV2_OUT | Contactor 2 coil |
| 4 | HV2_MIRROR | Contactor 2 mirror contact |

# Ordering information

| Order code | Guns | Display output | Housing |
|---|---|---|---|
| CC-200-1 | 1 | No | Open frame |
| CC-200-2 | 2 | No | Open frame |
| CC-200-2D | 2 | Yes | Aluminium |

# Revision history

| Revision | Date | Changes |
|---|---|---|
| 1.0 | 2026-03-01 | First release |
| 1.1 | 2026-07-15 | Added ordering code CC-200-2D |
