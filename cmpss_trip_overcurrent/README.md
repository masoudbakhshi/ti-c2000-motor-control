# Hardware Overcurrent Protection via CMPSS and Trip Zones

**Author:** Masoud Bakhshi — [www.plan22.net](https://www.plan22.net) | [LinkedIn](https://www.linkedin.com/in/masoud-bakhshi-78490846)

**License:** MIT — attribution required (see below)

---

## Overview

Hardware overcurrent protection for a GaN inverter leg using the F28388D CMPSS (Comparator
Subsystem) and ePWM Trip Zones. The CMPSS compares the measured phase current against ±8 A
thresholds in hardware (<100 ns) and asserts a Trip Zone signal that shuts down ePWM1 outputs
with no CPU intervention required.

Demonstrates: CMPSS digital filter blanking, EPWM XBAR routing, CBC vs. OST trip modes,
ISR-based fault logging, software fault injection testing, and UART fault telemetry.

---

## Hardware

| Component | Description |
|---|---|
| TMDSCNCD28388D | F28388D controlCARD, C2000 dual-core DSP |
| TMDSHSECDOCK | HSEC180 docking station |
| BOOSTXL-3PHGANINV | 48 V GaN inverter, shunt-based current sensing |
| ACS712-20A | Hall-effect current sensor, 100 mV/A, VOUT mid-point = 1.65 V |

---

## Protection Design

### Current Sensor (ACS712 20A)

| Parameter | Value |
|---|---|
| Sensitivity | 100 mV/A |
| Zero-current output | 1.65 V (VDDA/2) |
| ADC reference | VDDA = 3.3 V (12-bit, 1 LSB = 0.806 mV) |

### CMPSS DAC Thresholds (±8 A)

| Condition | Voltage | 12-bit DAC |
|---|---|---|
| +8 A trip | 1.65 + 0.80 = 2.45 V | `(2.45/3.3) × 4095 = 3042` |
| −8 A trip | 1.65 − 0.80 = 0.85 V | `(0.85/3.3) × 4095 = 1054` |

### Signal Path

```
CMPIN1P (ADC pin) ──┬──► High comparator (+) ──► DAC_HIGH (3042) ──► CTRIPOUTH ──►┐
                    │                                                                │
                    └──► Low comparator (+)  ──► DAC_LOW (1054) [inverted] ──► CTRIPOUTL ──►┤
                                                                                    │
                                                                 EPWM XBAR TRIP4 ◄──┘
                                                                        │
                                                                 ePWM1 TZ4 (CBC4 / OSHT4)
                                                                        │
                                                         ePWM1A / ePWM1B ──► FORCE LOW
```

### Digital Filter

Blanking window of 32 samples at 200 MHz EPWMCLK with a threshold of 30/32, providing
~160 ns noise rejection to prevent false trips during switching transients.

### Trip Modes

| Mode | `TRIP_MODE` define | Behaviour |
|---|---|---|
| CBC | `0` | Cycle-by-cycle — PWM re-arms on the next counter zero |
| OST | `1` | One-shot — PWM latched off until `clearOST()` is called |

---

## Firmware Files

| File | Description |
|---|---|
| `main.c` | CPU1 init, TZ ISR, ADC ISR, fault log, UART telemetry |
| `cmpss_trip_overcurrent.syscfg` | SysConfig: ePWM1, ADCA, CMPSS1, EPWMXBAR, SCIA |
| `CCS/cmpss_trip_overcurrent.projectspec` | CCS project import spec |

---

## UART Telemetry

115200 8N1 on SCIA (GPIO29 TX / GPIO28 RX — XDS110 backchannel).

Emitted at 10 Hz (every 2000 PWM cycles):

```
IMEAS:0.321,IREF:0.000,OST:0,FAULTS:0
```

Fault events are logged immediately after the telemetry line:

```
FAULT #1 t=550 us TZFLG=0x04
```

On startup, a software fault injection test runs automatically:

```
cmpss_trip_overcurrent — Masoud Bakhshi www.plan22.net
MODE:OST
INJECT: forcing trip event
INJECT: trip confirmed — PWM shut down
INJECT: OST cleared — PWM re-enabled
```

---

## Fault Log

Up to 16 fault entries are stored in a circular buffer (`g_fault_log`):

| Field | Description |
|---|---|
| `timestamp_us` | Microseconds since power-on (50 µs per PWM tick at 20 kHz) |
| `tzflg` | `EPWM_getTripZoneFlagStatus()` at the time of the event |
| `count` | Cumulative fault count |

---

## Build & Flash

1. Import `CCS/cmpss_trip_overcurrent.projectspec` into Code Composer Studio.
2. Select configuration `CPU1_RAM` for RAM-based debug.
3. Build → Flash → connect serial terminal at 115200.
4. To switch between CBC and OST modes, set `#define TRIP_MODE 0` or `1` in `main.c`.

---

## License

MIT License — Copyright (c) 2026 Masoud Bakhshi

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
to whom the Software is furnished to do so, subject to the following conditions:

**Attribution required:** The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
