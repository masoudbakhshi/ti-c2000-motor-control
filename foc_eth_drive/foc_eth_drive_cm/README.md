# foc_eth_drive_cm

F28388D **Cortex-M4** (CM) project: lwIP-on-EMAC UDP bridge that
exchanges command + telemetry packets with a Raspberry Pi at the wire
boundary, talking to the CPU1 side through two MSGRAM mailbox banks.
The CPU1 half lives at [`../foc_eth_drive_cpu1/`](../foc_eth_drive_cpu1/);
the parent overview is at [`../README.md`](../README.md).

The **Pi-side dashboard and command path** that consumes the
telemetry and produces the references for this firmware lives in the
sibling repo at
[`RaspberryPi/07_FOC_Eth_Drive_Scope/`](https://github.com/masoudbakhshi/RaspberryPi/tree/main/07_FOC_Eth_Drive_Scope).

| Field    | Value                                          |
|----------|------------------------------------------------|
| Author   | Masoud Bakhshi                                 |
| Website  | https://www.plan22.net                         |
| Email    | info@plan22.net                                |
| LinkedIn | https://www.linkedin.com/in/masoud-bakhshi-78490846/ |

## License

MIT License with required attribution. See the parent package README.

## Why a separate CM image

On the F28388D the EMAC peripheral is connected to the Cortex-M4 core.
CPU1 cannot reach it directly. The two cores therefore split the work
across two CCS projects:

| Core      | Owns                                             |
|-----------|--------------------------------------------------|
| CPU1 C28x | Current control (ADC + ePWM + FOC + SVPWM)       |
| CM   M4   | Ethernet (EMAC + lwIP + UDP + wire CRC)          |

Each side has its own flash bank, its own toolchain (TI C2000 v22.6
for CPU1, TI ARM CGT v20.2.7 for CM), and its own debug session.

## What this image does

- Brings up the EMAC and the lwIP raw API with a static IP
  `192.168.10.10/24` and the controlCARD's factory TI MAC
  `A8:63:F2:00:39:02`.
- Binds a UDP socket on port 5001 for command packets from the Pi.
  On every received packet it validates the 32-bit magic word and
  recomputes the CRC-16-CCITT trailer. Valid packets are deposited
  into the CM_TO_CPU1 MSGRAM bank for CPU1 to consume.
- Binds a UDP socket on port 5002 for telemetry to the Pi at
  `192.168.10.20:5002`. Every SysTick (1 ms) drain the CPU1_TO_CM
  MSGRAM ring, recompute the CRC at the wire boundary, and ship one
  pbuf per sample.
- Tracks its consumer cursor in CM-local RAM only. The CPU1_TO_CM
  bank is read-only from the CM side; writing into it raises an
  IMPRECISERR bus fault on the M4. The producer-side ring uses a
  monotonic head index with unconditional overwrite, so CM can fall
  behind without ever wedging CPU1.

## Folder layout

```
foc_eth_drive_cm/
  2838x_flash_lnk_cm_lwip.cmd  - linker (FLASH + MSGRAM banks)
  foc_eth_drive_cm.projectspec
  targetConfigs/
    TMS320F28388D.ccxml        - XDS100v2 probe + CM4 device
  device_cm.c                  - Custom_CM_init clocks the CM
  startup_ccs.c                - reset handler + vector table
  pinout.c                     - LED + GPIO bring-up
  enet.c                       - low-level Ethernet helpers
  f2838xif.c                   - lwIP <-> EMAC pbuf interface
  lwiplib.c                    - lwIP raw API (NO_SYS) glue
  lwipopts.h lwippools.h       - lwIP build-time configuration
  main_cm.c                    - the application surface above
```

## Build

```
File -> Import -> CCS Projects -> foc_eth_drive_cm.projectspec
Project -> Build Project
```

The single configuration is `CM_FLASH`.

## Wire format (must match foc_eth_drive_cpu1)

Both packets are little endian and 16-bit aligned so the CPU1 (C28x
with 16-bit char) and the CM (M4 with 8-bit char) see the same byte
layout.

```
Command (Pi -> MCU, port 5001, 36 bytes):
  uint32 magic        = 0xC0DE0001
  uint32 seq
  float32 id_ref_A
  float32 iq_ref_A
  float32 fe_ref_Hz
  float32 Rs_ohm
  float32 Ls_H
  float32 f_bw_Hz
  uint16  enable_flags    bit 0 = enable, bit 1 = reset_faults
  uint16  crc16

Telemetry (MCU -> Pi, port 5002, 52 bytes):
  uint32 magic        = 0xFEED0001
  uint32 seq
  uint32 timestamp_us
  float32 id_ref_A, id_meas_A, iq_ref_A, iq_meas_A
  float32 ia_A, ib_A, ic_A
  float32 vdc_V, theta_e_rad
  uint16  status_flags
  uint16  crc16
```

## Flash order

This image is the **second** thing to load. Per the parent package
README, CPU1 must run first so that its `Device_bootCM()` call hands
control to CM at the FLASH sector 0 entry point. Loading CM before
CPU1 has done that handoff produces "CM held in reset" or
`FMSTAT=0x1040`.
