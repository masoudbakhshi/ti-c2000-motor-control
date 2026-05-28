# adc_ethernet_scope - F28388D firmware

Dual-core F2838x firmware for the ADC -> UDP oscilloscope project.
The two cores run different code, get flashed separately, and talk
to each other through shared MSGRAM.

## Layout

```
adc_ethernet_scope/
  README.md            this file - parent overview, points at both children
  adc_scope_cpu1/      C28x main core ("MCU") - CCS project name = folder name
    main_cpu1.c
    adc_scope_cpu1.syscfg
    adc_scope_cpu1.projectspec
    README.md
  adc_scope_cm/        Cortex-M4 Connectivity Manager - CCS project name = folder name
    main_cm.c
    adc_scope_cm.projectspec
    README.md
```

CCS project names match the folder names: `adc_scope_cpu1` and
`adc_scope_cm`. The names are deliberately the same on disk and in
the workspace so CCS can re-import in-place without creating any
duplicate working copies (CCS computes the project location as
`<workspace-base>/<project-name>/`, so equal names are the only way
to keep one canonical source).

## Core split

| Side | Toolchain | Responsibility |
|------|-----------|----------------|
| `adc_scope_cpu1/` | TI C2000 Compiler 22.6.x LTS | Sample ADCINA0 at 10 kHz (ePWM1 SOCA + ADC-A SOC0). Convert each result to volts. Publish into a 128-deep float ring in `MSGRAM_CPU_TO_CM`. Boot the CM core via `Device_bootCM(BOOTMODE_BOOT_TO_FLASH_SECTOR0)`. |
| `adc_scope_cm/`   | TI ARM CGT 20.2.x LTS (NOT TI Clang) | Bring up lwIP (NO_SYS) on the on-chip Ethernet MAC, static IP `192.168.10.10/24`. Poll the shared ring from a 1 ms SysTick. Pack 50 samples per UDP packet, send 200 packets/s to `192.168.10.20:5005`. |

## Build + flash order (strict)

Per F2838x dual-core flash protocol:

1. Set `adc_scope_cpu1` active build configuration to `CPU1_FLASH` in CCS
2. Build `adc_scope_cpu1`
3. Build `adc_scope_cm`
4. Load `adc_scope_cpu1.out` to CPU1
5. Run CPU1 (executes `Device_init()` then `Device_bootCM()`)
6. Pause CPU1
7. Load `adc_scope_cm.out` to CM
8. Run CM, then resume CPU1

Skipping the "run CPU1 first" step leaves CM in reset and the flash
programmer reports `FMSTAT=0x1040` or "held in reset".

## Tested verified behavior

| Check | Result |
|-------|--------|
| CPU1 builds clean at `CPU1_FLASH` | yes, zero warnings in our sources |
| CM builds clean at `CM_FLASH` | yes (five `#179-D` warnings in TI lwIP files suppressed by `--diag_suppress=179`) |
| ARP from Pi resolves `192.168.10.10` to `A8:63:F2:00:39:02` | yes (~0.29 ms RTT) |
| Pi receives exactly 200 packets/s, 50 samples per packet | yes (verified with `sniff.py`) |
| Loss over multi-minute window | 0% |

See the [repo README](../README.md) for the full hardware switch
matrix, ASCII architecture diagram, packet wire format, and
troubleshooting tree.

## Companion Pi-side project

The other half of this system lives in the sibling repository
[`RaspberryPi`](https://github.com/masoudbakhshi/RaspberryPi) at
[`RaspberryPi/06_Ethernet_Scope/`](https://github.com/masoudbakhshi/RaspberryPi/tree/main/06_Ethernet_Scope).
It contains:

* `sniff.py` — headless CLI sniffer used for bring-up/diagnostics;
  prints packet rate, loss, sample rate, and bad-packet counters to
  stdout. Use this first after every flash/reboot to confirm the
  link.
* `app.py` — Streamlit + Plotly live dashboard with KPIs (current /
  mean / min / max / RMS / packet rate / loss % / link uptime), a
  pause toggle, and a CSV-export button. Use this once `sniff.py`
  confirms the link is healthy.
* `receiver/packet.py` — the **single source of truth** for the
  UDP wire format. `adc_scope_cm/main_cm.c` in this repo is
  hand-aligned against it byte for byte; if you change the packet
  layout, both files must move together in the same commit pair.
* `setup_static_ip.sh` — Pi-side network config (`nmcli` for Pi OS
  bookworm, `dhcpcd.conf` for older). Sets eth0 to `192.168.10.20/24`.

End-to-end verified bring-up sequence:

1. Switch on the controlCARD, set switches per the [repo README](../README.md)
2. In CCS, build + flash `adc_scope_cpu1` (CPU1_FLASH) and
   `adc_scope_cm` (CM_FLASH) in the strict order above
3. On the Pi: `sudo ./setup_static_ip.sh`
4. On the Pi: `python3 sniff.py` — expect `200.00 pkt/s, 0 lost,
   0 bad`
5. On the Pi: `streamlit run app.py` — open
   `http://<pi-ip>:8501` in any browser on the network

## How to re-import in CCS

If the .project/.cproject metadata is missing or out of date, in CCS:

1. `File -> Import -> Code Composer Studio -> CCS Projects`
2. Select directory: this `adc_ethernet_scope/` folder
3. CCS will discover both `adc_scope_cpu1.projectspec` and
   `adc_scope_cm.projectspec` and offer to import them in-place
4. Make sure the import option "Copy projects into workspace" is
   **off** so CCS uses these source folders as the project
   locations directly (no duplicate working copy)

## Author

**Masoud Bakhshi**

* Website  : <https://www.plan22.net>
* Email    : <info@plan22.net>
* LinkedIn : <https://www.linkedin.com/in/masoud-bakhshi-78490846/>

MIT with required attribution. See [../LICENSE](../LICENSE).
