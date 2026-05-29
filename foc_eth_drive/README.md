# foc_eth_drive

Dual-core synchronous-frame current controller (Field-Oriented Control)
for a three-phase RL load, running on the Texas Instruments
TMS320F28388D controlCARD with the BOOSTXL-3PhGaNInv 48 V GaN
inverter, commanded over Ethernet by a Raspberry Pi 4.

| Field    | Value                                          |
|----------|------------------------------------------------|
| Author   | Masoud Bakhshi                                 |
| Website  | https://www.plan22.net                         |
| Email    | info@plan22.net                                |
| LinkedIn | https://www.linkedin.com/in/masoud-bakhshi-78490846/ |

## License

MIT License with required attribution. Derivative works MUST keep the
author name, website, email and LinkedIn references together in any
user-facing README, "About" panel or splash screen. Removing them
violates the license.

## What this package contains

Two sibling CCS projects that together form the embedded half of the
drive:

```
foc_eth_drive/
  foc_eth_drive_cpu1/   - C28x application: ADC, ePWM, FOC, safety
  foc_eth_drive_cm/     - Cortex-M4 application: lwIP, EMAC, UDP
```

CPU1 runs the 20 kHz current control ISR; the Cortex-M4 (CM) bridges
the on-chip MSGRAM mailboxes to UDP on the wire. The two cores share
nothing except two dedicated MSGRAM banks.

## Companion Pi-side project

The Raspberry Pi 4 host that drives this firmware lives in the
sibling repository
[`RaspberryPi`](https://github.com/masoudbakhshi/RaspberryPi) at
[`07_FOC_Eth_Drive_Scope/`](https://github.com/masoudbakhshi/RaspberryPi/tree/main/07_FOC_Eth_Drive_Scope).
It contains:

* `app.py` - Streamlit + Plotly four-panel live dashboard with
  `Id`/`Iq` references and measurements, three phase currents, and
  `Vdc` + `theta_e`. 100 ms refresh.
* `eth_io.py` - the **single source of truth** for the UDP wire
  format. The two firmware projects `foc_eth_drive_cpu1` and
  `foc_eth_drive_cm` in this repo are hand-aligned against it byte
  for byte; if you change the packet layout, both repos must move
  together in the same commit pair.
* `scope_buffer.py` - thread-safe NumPy-backed rolling sample buffer
  shared by `app.py` and the receiver thread.
* `controller_design.py` - closed-form preview of the IMC-tuned
  `Kp`/`Ki` from `Rs`, `Ls`, `f_bw` so the operator can see the gains
  before they are programmed.
* `deploy.sh` - one-shot Pi setup (venv, pip install, static IP on
  `eth0`, systemd unit, self tests).

## Architecture at a glance

```
+-----------+   ePWM SOCA @20kHz   +-----------+   MSGRAM   +-----------+
| BOOSTXL   |  -----------------> | F28388D   |  <-------> | F28388D   |
| 3-phase   |                     | CPU1 C28x |            | CM CM4    |
| GaN inv.  |  <---  3xCMPA  ---  | FOC + SVPWM            | lwIP+EMAC |
+-----------+                     +-----------+            +-----+-----+
                                                                 |
                                                                 | UDP 5001 (cmd)
                                                                 | UDP 5002 (telem)
                                                                 v
                                                          Raspberry Pi 4
                                                          eth0 192.168.10.20
```

| Quantity                 | Value                                          |
|--------------------------|------------------------------------------------|
| Switching frequency      | 20 kHz, symmetric up/down carrier              |
| Current loop bandwidth   | Up to 2 kHz (capped at Fsw/10)                 |
| Telemetry rate           | 5 kHz nominal (decimated from 20 kHz)          |
| Command rate             | 200 Hz nominal, 200 ms Ethernet watchdog       |
| Wire CRC                 | CRC-16-CCITT, poly 0x1021, init 0xFFFF         |
| Static IPs               | MCU 192.168.10.10/24, Pi 192.168.10.20/24      |

## Bring-up sequence

Strict flash order matters because the CM boot mode is set by CPU1:

1. Build [`foc_eth_drive_cpu1`](foc_eth_drive_cpu1/) (configuration
   `CPU1_RAM`, compiled with `--define=_FLASH` so the runtime copies
   `.TI.ramfunc` to RAM).
2. Build [`foc_eth_drive_cm`](foc_eth_drive_cm/) (configuration
   `CM_FLASH`).
3. On the Pi, deploy
   [`07_FOC_Eth_Drive_Scope`](https://github.com/masoudbakhshi/RaspberryPi/tree/main/07_FOC_Eth_Drive_Scope)
   from the companion repo (`sudo ./deploy.sh` does everything in one
   shot).
4. With the XDS100v2 probe, launch the `foc_eth_drive_cpu1` debug
   session and load the `.out`.
5. Run CPU1 for a couple of seconds so it executes `Device_init()` +
   `Device_bootCM(BOOTMODE_BOOT_TO_FLASH_SECTOR0)`.
6. Pause CPU1.
7. Connect to the Cortex-M4 core and load `foc_eth_drive_cm.out`.
8. Resume CM, then resume CPU1.

This is the only order that avoids `FMSTAT=0x1040` and "held in reset"
on CM.

Once both cores are running, open `http://<pi-ip>:8501` from any
browser on the Pi's network to see the live four-panel scope.

## Repository policy

Per the project file allowlist only these file types are committed
under this folder:

- `.c`
- `.syscfg`
- `.projectspec`
- `README.md`
- `.gitignore`

Build outputs, CCS workspace metadata (`.project`, `.cproject`,
`.settings/`), generated source, and the C2000Ware vendor tree are
**not** part of the repository. Anyone cloning this repo imports the
two projectspecs into CCS and the project regenerates cleanly.
