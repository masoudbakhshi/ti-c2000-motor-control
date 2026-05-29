# TI C2000 Motor Control

Texas Instruments C2000 F28388D firmware projects targeting the
TMDSCNCD28388D controlCARD on a TMDSHSECDOCK baseboard, plus the
BOOSTXL-3PhGaNInv power stage where the project needs a real
inverter.

Each subfolder is a self-contained CCS project (or a pair of
sub-projects in the dual-core cases). Imports happen in-place from
the per-project `.projectspec`. No generated source, build outputs,
CCS workspace metadata or copied SDK files are committed - those are
regenerated locally on first import.

## Hardware

| Board               | Description                                           |
|---------------------|-------------------------------------------------------|
| `TMDSCNCD28388D`    | F28388D controlCARD - dual C28x + Cortex-M4 + dual CLA |
| `TMDSHSECDOCK`      | 180-pin HSEC8 baseboard with JTAG and analog headers  |
| `BOOSTXL-3PhGaNInv` | 48 V three-phase GaN inverter (used by motor projects) |

## Toolchain

* Code Composer Studio (CCS) 20.5.1
* C2000Ware 26.01.00.00 at `C:/ti/c2000/C2000Ware_26_01_00_00`
* SysConfig 1.27.x (bundled with CCS)
* TI C2000 Compiler 22.6.x LTS (for C28x cores)
* TI ARM CGT 20.2.x LTS (for the CM core - lwIP examples require this
  toolchain, not TI Clang)
* Debug probe: XDS100v2 (the on-card emulator on this controlCARD)

## Projects

| Folder                  | Cores    | Purpose                                              |
|-------------------------|----------|------------------------------------------------------|
| [`blink_led/`](blink_led/)                       | CPU1      | Minimal blink starter |
| [`cmpss_trip_overcurrent/`](cmpss_trip_overcurrent/) | CPU1      | Overcurrent trip via CMPSS comparator |
| [`pi_current_cla/`](pi_current_cla/)             | CLA1      | Incremental PI current loop on CLA at 20 kHz |
| [`svpwm_adc/`](svpwm_adc/)                       | CPU1+CPU2 | SVPWM modulator plus 3-shunt ADC sampling |
| [`adc_ethernet_scope/`](adc_ethernet_scope/)     | CPU1+CM   | ADCINA0 -> UDP scope over Ethernet. Two sub-projects: `adc_scope_cpu1/` (C28x sampler + IPC producer) and `adc_scope_cm/` (lwIP UDP transmitter). |
| [`foc_eth_drive/`](foc_eth_drive/)               | CPU1+CM   | Synchronous-frame current FOC for a three-phase RL load on BOOSTXL-3PhGaNInv, commanded over Ethernet. Two sub-projects: `foc_eth_drive_cpu1/` (FOC + ADC + ePWM + safety) and `foc_eth_drive_cm/` (lwIP UDP bridge). |

Each project's own README documents its bring-up sequence, wire
format (where applicable), and acceptance criteria.

## Companion Raspberry Pi repository

Projects that stream data over Ethernet to or from a Pi have a Pi
counterpart in the sibling repository
[`RaspberryPi`](https://github.com/masoudbakhshi/RaspberryPi):

| C2000 project                                                                                                              | Pi counterpart                                                                                                              |
|----------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------|
| [`adc_ethernet_scope/`](adc_ethernet_scope/)                                                                                | [`RaspberryPi/06_Ethernet_Scope/`](https://github.com/masoudbakhshi/RaspberryPi/tree/main/06_Ethernet_Scope)                  |
| [`foc_eth_drive/`](foc_eth_drive/)                                                                                          | [`RaspberryPi/07_FOC_Eth_Drive_Scope/`](https://github.com/masoudbakhshi/RaspberryPi/tree/main/07_FOC_Eth_Drive_Scope)        |

For any project that has a co-developed Pi side, the wire packet
format is the contract between the two repos: changes must land in
both at the same time.

## Repository policy

A strict allowlist keeps generated and vendor files out of the repo:

* `.c`
* `.h` (limited to projects whose source layout requires it, gated
  per-project in `.gitignore`)
* `.syscfg`
* `.projectspec`
* `README.md`
* `.gitignore`

Build outputs (`CPU1_RAM/`, `CPU1_FLASH/`, `CM_FLASH/`, `Debug/`,
`Release/`), CCS workspace metadata (`.project`, `.cproject`,
`.settings/`, `.theia/`), generated SysConfig output (`syscfg/`),
local hardware documentation (`HW_Documentations/`), and copied
C2000Ware vendor files are all gitignored. Anyone cloning the repo
imports the relevant projectspec into CCS and the project regenerates
cleanly.

## Author

**Masoud Bakhshi**

* Website  : <https://www.plan22.net>
* Email    : <info@plan22.net>
* LinkedIn : <https://www.linkedin.com/in/masoud-bakhshi-78490846/>

Licensed under the MIT License with an additional attribution
requirement - see [LICENSE](LICENSE). Any derivative work must
visibly credit the author with the name, website, email and
LinkedIn references above.
