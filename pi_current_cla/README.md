# Discrete PI Current Controller on C2000 CLA

**Author:** Masoud Bakhshi - [www.plan22.net](https://www.plan22.net) | [LinkedIn](https://www.linkedin.com/in/masoud-bakhshi-78490846)

**License:** MIT - attribution required (see below)

---

## Overview

A 20 kHz PI current controller running on the CLA (Control Law Accelerator) coprocessor of
the TMS320F28388D, tested on a single inverter leg driving an R-L load. The CLA executes
the discrete PI algorithm in under 2 µs, freeing the C28x CPU for supervisory tasks and
UART telemetry.

Demonstrates: bandwidth-based gain design, Tustin-guided discretization, incremental (velocity)
form PI with inherent anti-windup, CLA task scheduling, and ADC synchronisation.

---

## Hardware

| Component | Description |
|---|---|
| TMDSCNCD28388D | F28388D controlCARD, C2000 dual-core DSP |
| TMDSHSECDOCK | HSEC180 docking station |
| BOOSTXL-3PHGANINV | 48 V GaN inverter, shunt-based current sensing |
| R-L load | 6 Ω / 1 mH, rated 10 A continuous (one inverter leg) |

---

## Control Design

| Parameter | Value |
|---|---|
| Switching frequency | 20 kHz |
| Sampling period Ts | 50 µs |
| Bandwidth target ωc | 2π × 1000 rad/s ≈ 6283 rad/s |
| Load R | 6 Ω |
| Load L | 1 mH |
| Kp = L × ωc | 6.283 V/A |
| Ki = R × ωc | 37 699 V/(A·s) |
| Ki × Ts | 1.885 |
| Phase margin | ≥ 45° (pole-zero cancellation at ωz = R/L) |

**Discretisation (incremental / velocity form):**

```
du[k] = Kp × (e[k] − e[k−1]) + Ki·Ts × e[k]
u[k]  = clamp(u[k−1] + du[k], −0.5, 0.5)
duty  = 0.5 + u[k]
```

Anti-windup is natural: `u[k−1]` is always the clamped value, so the integrator
unwinds immediately when the error reverses sign.

---

## Timing Diagram (one 50 µs period)

```
CTR=ZERO ──────────────────────────── CTR=PRD ─────────────────── CTR=ZERO
   │                                     │                            │
   │ CPU ISR:                            │ SOCA → ADC SOC0            │
   │  read g_duty → update CMPA          │   ~150 ns later:           │
   │  update g_iref (sine)               │   ADCAINT1 → CLA Task 1   │
   │  schedule UART                      │     read ADC result        │
   │                                     │     run incremental PI     │
   │                                     │     write g_duty           │
   │ ← - - - - - - - 25 µs - - - - - - → ← - ~2 µs - →              │
```

---

## Firmware Files

| File | Description |
|---|---|
| `main.c` | CPU1 init, ePWM1 ISR, reference generation, UART telemetry |
| `cla_pi.c` | CLA Task 1: ADC read, PI computation, duty output |
| `pi_current_cla.syscfg` | SysConfig: ePWM1, ADCA, CLA, SCIA peripherals |
| `CCS/pi_current_cla.projectspec` | CCS project import spec |

---

## Shared Memory Map

| Section | Variable | Direction | Description |
|---|---|---|---|
| `Cpu1ToCla1MsgRAM` | `g_iref` | CPU → CLA | Current reference (A) |
| `Cla1ToCpuMsgRAM` | `g_duty` | CLA → CPU | Normalised duty [0, 1] |
| `Cla1ToCpuMsgRAM` | `g_imeas` | CLA → CPU | Measured current (A) |
| `Cla1ToCpuMsgRAM` | `g_u_prev` | CLA → CPU | PI state u[k−1] |
| `Cla1ToCpuMsgRAM` | `g_e_prev` | CLA → CPU | PI state e[k−1] |

---

## UART Telemetry

115200 8N1 on SCIA (GPIO29 TX / GPIO28 RX - XDS110 backchannel).
Emitted at 10 Hz:

```
REF:0.4986,MEAS:0.4921,DUTY:0.5412
```

A Python script on the host reads this stream and plots the measured Bode response
(amplitude and phase) against the theoretical open-loop transfer function at each
injected sine frequency.

---

## Build & Flash

1. Import `CCS/pi_current_cla.projectspec` into Code Composer Studio.
2. Select configuration `CPU1_RAM` for RAM-based debug.
3. Build → Flash → connect serial terminal at 115200.

---

## Validation Targets

| Metric | Target |
|---|---|
| Step response settling | < 5 ms |
| Steady-state error | < 1% of setpoint |
| CLA task execution | < 2 µs (verified via GPIO toggle) |
| Closed-loop bandwidth | ≥ 800 Hz (−3 dB from Bode sweep) |

---

## License

MIT License - Copyright (c) 2025 Masoud Bakhshi

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
to whom the Software is furnished to do so, subject to the following conditions:

**Attribution required:** The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
