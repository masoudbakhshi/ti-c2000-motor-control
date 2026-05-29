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

## Pinout (controlCARD HSEC dock + BOOSTXL-3PhGaNInv)

The BOOSTXL-3PhGaNInv stacks on top of the TMDSCNCD28388D controlCARD
through the TMDSHSECDOCK 100-pin HSEC connector. The BoosterPack site
on the dock routes the BoosterPack header pins to specific HSEC pins,
which in turn map to F28388D GPIOs and ADC channels. The resolved
end-to-end map is:

| Signal       | Direction | F28388D pin | HSEC dock pin | BOOSTXL pin | Notes                                       |
|--------------|-----------|-------------|----------------|-------------|---------------------------------------------|
| EPWM1A (AH)  | OUT       | GPIO0       | HSEC 49        | J1.40       | Leg A high side gate (LMG5113)              |
| EPWM1B (AL)  | OUT       | GPIO1       | HSEC 51        | J1.39       | Leg A low side gate                         |
| EPWM2A (BH)  | OUT       | GPIO2       | HSEC 53        | J1.38       | Leg B high side gate                        |
| EPWM2B (BL)  | OUT       | GPIO3       | HSEC 55        | J1.37       | Leg B low side gate                         |
| EPWM3A (CH)  | OUT       | GPIO4       | HSEC 57        | J1.36       | Leg C high side gate                        |
| EPWM3B (CL)  | OUT       | GPIO5       | HSEC 59        | J1.35       | Leg C low side gate                         |
| ADCINA0      | IN        | ADCINA0     | HSEC 9         | J3.30       | INA240 phase-A current feedback             |
| ADCINA1      | IN        | ADCINA1     | HSEC 11        | J3.29       | INA240 phase-B current feedback             |
| ADCINA2      | IN        | ADCINA2     | HSEC 13        | J3.28       | INA240 phase-C current feedback             |
| ADCINB0      | IN        | ADCINB0     | HSEC 15        | J3.27       | DC-bus voltage divider output               |
| nFAULT       | IN        | GPIO40      | HSEC 79        | J2.14       | GaN driver fault, open drain, active low    |
| LED D1       | OUT       | GPIO31      | controlCARD    | n/a         | ISR scope probe (toggled every ISR)         |
| LED D2       | OUT       | GPIO34      | controlCARD    | n/a         | CPU1 heartbeat                              |
| EMAC PHY     | bidir     | CM core     | J4 RJ45        | n/a         | 100BASE-T MII to Raspberry Pi 4             |

The CM core owns the EMAC peripheral; CPU1 only muxes the MII signals
to the on-card DP83822 PHY and releases the PHY from reset and
power-down. The MII pin set is fixed by the F2838x device and is
configured in
[`foc_eth_drive_cpu1/src/eth_phy_bringup.c`](foc_eth_drive_cpu1/src/eth_phy_bringup.c):
MDIO on GPIO105/106, TX_CLK on GPIO44, RX_CLK on GPIO111, TXD[0..3] on
GPIO75/122/123/124, TX_EN on GPIO118, RXD[0..3] on GPIO114/115/116/117,
RX_DV on GPIO112, RX_ERR on GPIO113, CRS/COL on GPIO109/110, PHY power
down on GPIO108 and PHY reset on GPIO119.

Wiring caveats:

- The HSEC pin numbers above are the printed HSEC numbers on the
  connector. The dock silkscreen for the BoosterPack header uses a
  separate numbering scheme; the two schemes disagree on this dock,
  so wire to the HSEC pin number rather than the silkscreen pin.
- Use the RJ45 marked J4 (Ethernet) on the dock. J5 and J6 are
  EtherCAT-only and will not enumerate with the lwIP MII bring-up.

Power supply and connectors:

| Rail        | Source                | Connector            | Notes                              |
|-------------|-----------------------|----------------------|-------------------------------------|
| Inverter DC | Bench supply 24-48 V  | BOOSTXL J5           | Through a 1 A fuse on first bring-up |
| Logic 3.3 V | USB host              | TMDSHSECDOCK USB-C   | Also carries the XDS100v2 JTAG link |
| Ethernet    | controlCARD MII PHY   | TMDSHSECDOCK J4 RJ45 | Direct cable to Pi eth0 (no router) |

For the CPU1 subproject's local copy of this table see
[`foc_eth_drive_cpu1/docs/PINOUT.md`](foc_eth_drive_cpu1/docs/PINOUT.md);
both copies are hand-aligned against the same firmware constants in
[`foc_eth_drive_cpu1/include/board_pinmap_boostxl_3phganinv.h`](foc_eth_drive_cpu1/include/board_pinmap_boostxl_3phganinv.h).

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
