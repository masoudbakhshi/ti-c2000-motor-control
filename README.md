# TI C2000 Motor Control

Texas Instruments C2000 F28388D firmware projects targeting the
TMDSCNCD28388D controlCARD on a TMDSHSECDOCK baseboard.

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

## Projects

| Folder                  | Target | Purpose                                              |
|-------------------------|--------|------------------------------------------------------|
| `blink_led/`            | CPU1   | Minimal blink starter                                |
| `cmpss_trip_overcurrent/` | CPU1 | Overcurrent trip via CMPSS comparator                |
| `pi_current_cla/`       | CLA1   | Incremental PI current loop on CLA at 20 kHz         |
| `svpwm_adc/`            | CPU1+CPU2 | SVPWM modulator + 3-shunt ADC sampling            |
| `adc_ethernet_scope/`   | CPU1+CM | ADCINA0 -> UDP scope. Two subprojects: `cpu1/` (C28x sampler + IPC producer) and `cm/` (lwIP UDP transmitter). See [`adc_ethernet_scope/README.md`](adc_ethernet_scope/README.md). |

The Pi-side receiver and Streamlit dashboard for `adc_ethernet_scope`
lives at
[`RaspberryPi/06_Ethernet_Scope/`](https://github.com/masoudbakhshi/RaspberryPi/tree/main/06_Ethernet_Scope) in
the sibling repository.

---

# ADC -> Ethernet Scope

`adc_ethernet_scope/cpu1/` + `adc_ethernet_scope/cm/` implement an ADCINA0 oscilloscope that
streams 10 000 samples/s over UDP to a Raspberry Pi. The signal path
is:

```
  +------------+        SOCA @ 10 kHz       +-----------+
  |  ADCINA0   |---->[ ADC-A SOC0 ]-------->|  ADC1 ISR |
  |  (HSEC pin |                            |  on CPU1  |
  |   9 / dock |                            +-----+-----+
  |   J4 pin1) |                                  |
  +------------+                          volts = raw / 4095 * 3.3
                                                  v
                              +-----------------------------------+
                              |  shared block in MSGRAM_CPU_TO_CM |
                              |     uint32 magic, producer_seq,   |
                              |     sample_rate, ring_size        |
                              |     float  ring[128]              |
                              |     (CPU1 writes, CM reads)       |
                              +-----------------+-----------------+
                                                |
                                                v   (SysTick poll, 1 ms)
                                       +-----------------+
                                       |  CM main loop   |
                                       |  - drain ring   |
                                       |  - pack 50 sa.  |
                                       |  - udp_sendto() |
                                       +--------+--------+
                                                |   UDP, 224 B / packet
                                                |   200 packets/s
                                                v
                +----------------+   direct CAT-5e   +------------------+
                | DP83822H PHY   |==================>|  Raspberry Pi    |
                | controlCARD J4 |   192.168.10.0/24 |  192.168.10.20   |
                +----------------+                   |  :5005           |
                                                     +------------------+
                                                     | udp_receiver.py  |
                                                     | -> Streamlit     |
                                                     +------------------+
```

## Bring this up in 10 minutes

1. **Switches on the controlCARD** (TMDSCNCD28388D, see SPRUIL8B):

   | Switch        | Position           | Why                                                |
   |---------------|--------------------|----------------------------------------------------|
   | `S1:A` pos 1  | ON  (right)        | XDS100v2 emulator enabled for JTAG flash           |
   | `S1:A` pos 2  | OFF (left)         | Free GPIO28 for normal use                          |
   | `S2` pos 1    | ON  (right)        | Boot mode bit 0 = 1                                |
   | `S2` pos 2    | ON  (right)        | Boot mode bit 1 = 1, i.e. boot-from-flash (mode 03) |
   | `S3` pos 1    | LEFT               | ADC-A `VREFHI` = `VDDA` = 3.3 V                    |

   `S3` *must* be left so the ADC scaling `raw / 4095 * 3.3` matches
   the firmware. If you need a 3.0 V precision reference instead,
   change the literal in `adc_ethernet_scope/cpu1/main_cpu1.c` and the README.

2. **Power the dock** (TMDSHSECDOCK, see SPRUIJ6A): plug a USB-mini
   cable into `J17` and flip `SW1` to `USB-ON`. `D1` on the dock
   should light. The controlCARD's `D5` (green) should also light.

3. **JTAG**: plug a USB-mini cable into `J1:A` on the controlCARD.

4. **Ethernet**: plug a CAT-5e cable into the controlCARD's `J4`
   (the single RJ45 jack on the LEFT - NOT `J5`/`J6`, those are
   EtherCAT). The other end goes directly to the Pi's RJ45.

5. **Build, in CCS**:
   - `Project -> Build` `adc_scope_cpu1` (CPU1_FLASH config)
   - `Project -> Build` `adc_scope_cm`   (CM_FLASH config)

6. **Flash, strict order** (per F28388 dual-core flash protocol):
   1. Load `adc_scope_cpu1.out` to CPU1
   2. Run CPU1 - this executes `Device_init()` + `Device_bootCM()`
   3. Pause CPU1
   4. Load `adc_scope_cm.out` to CM
   5. Run CM, then run CPU1

   Skipping the run-CPU1-first step leaves CM in reset and the flash
   programmer reports `FMSTAT=0x1040` or "held in reset".

7. **On the Pi**: see
   [`RaspberryPi/06_Ethernet_Scope/README.md`](https://github.com/masoudbakhshi/RaspberryPi/blob/main/06_Ethernet_Scope/README.md).
   The 60-second version:
   ```bash
   sudo ./setup_static_ip.sh
   ping -c 3 192.168.10.10
   pip install -r requirements.txt
   python3 sniff.py
   streamlit run app.py
   ```

---

## Network

| Side          | IP             | Netmask         | UDP port |
|---------------|----------------|-----------------|----------|
| F28388D CM    | `192.168.10.10`| `255.255.255.0` | src 5006 |
| Raspberry Pi  | `192.168.10.20`| `255.255.255.0` | dst 5005 |

MAC: `A8:63:F2:00:39:02` (TI OUI, factory-printed on the controlCARD).
Swap the last three octets if you move to a different card.

## Wire packet

Little-endian, 224 bytes for the default `N = 50` samples per packet.

| offset | type        | name           | value                    |
|--------|-------------|----------------|--------------------------|
| 0      | uint32      | magic          | `0xC2000ADC`             |
| 4      | uint16      | version        | `1`                      |
| 6      | uint16      | sample_count   | `N` (default 50)         |
| 8      | uint32      | seq            | monotonic packet counter |
| 12     | uint64      | t_us           | CM time-since-boot, us   |
| 20     | uint32      | sample_rate    | `10000`                  |
| 24     | float[N]    | samples        | volts                    |

The canonical definition lives in
[`RaspberryPi/06_Ethernet_Scope/receiver/packet.py`](https://github.com/masoudbakhshi/RaspberryPi/blob/main/06_Ethernet_Scope/receiver/packet.py);
the firmware's `adc_ethernet_scope/cm/main_cm.c` is hand-aligned to match it
byte for byte.

## Shared block (MSGRAM_CPU_TO_CM)

Written **only** by CPU1, read **only** by CM. The CPU1->CM MSGRAM
bank is write-locked from the CM side; storing into it from CM raises
an `IMPRECISERR` bus fault.  The consumer index lives as a CM-local
`s_consumer_seq` static.

```c
struct adc_shared_block {
    uint32_t magic;            /* 0xC2000ADC */
    uint32_t producer_seq;     /* CPU1 increments after each sample */
    uint32_t sample_rate_hz;   /* 10000 */
    uint32_t ring_size;        /* 128 */
    float    ring[128];        /* volts */
};
```

528 bytes total, fits in the 1 KB `CPU1TOCMMSGRAM0` bank.

## Safety

`ADCINA0` absolute maximum is `VREFHI` (3.3 V) referenced to `VSSA`.
**Never drive this pin outside the 0..3.3 V range.** The ADC inputs
on the controlCARD are clamped by protection diodes but you must not
rely on them for steady-state protection. Use a known-good bench
source, current-limited.

---

## Acceptance criteria (self-review)

| #  | Criterion                                                          | Verified at generation |
|----|---------------------------------------------------------------------|------------------------|
| 1  | Pi can ping `192.168.10.10`                                         | NO (needs HW + PHY TX) |
| 2  | `sniff.py` shows 200 pkt/s, monotonic seq, 50 samples/pkt           | NO (needs HW link)     |
| 3  | DC voltage shows within +/-30 mV on the dashboard                   | NO (needs bench input) |
| 4  | 100 Hz sine renders cleanly; RMS within 5 %                         | NO (needs fn-gen)      |
| 5  | 10-minute soak: packet loss < 0.1 %                                 | NO (needs HW + 10 min) |
| 6  | Cable hot-unplug then reconnect; dashboard recovers without restart | NO (needs HW)          |
| -- | CPU1 project builds with no errors                                  | YES (CPU1_RAM)         |
| -- | CM project builds with no errors                                    | YES (CM_FLASH)         |
| -- | Pi packet parser round-trips and detects loss                       | YES (local self-test)  |

Items 1-6 require the live cable + working PHY TX path. Two-direction
PHY behaviour on this particular controlCARD has been intermittent in
the past (see Troubleshooting below). The firmware is exercised
against the canonical C2000Ware lwIP example sequence and built with
the same toolchain (`ti-cgt-tms470_20.2.7.LTS`) the example targets.

---

## Troubleshooting

* **`ping 192.168.10.10` times out, no green LED on `J4`.**
  Check `S3 pos 1 = LEFT` (so VDDA is the ADC reference), check the
  cable is in `J4` (not `J5`/`J6`), and try a second known-good
  cable - a single broken TX pair gives a one-way link that looks
  "up" on one side but never reaches the wire on the other.

* **F28388 EMAC TX counter (`0x400C0718`) stops at exactly 8.**
  Classic "8 ARP retries then silence" pattern - the MAC is sending,
  the PHY isn't getting it on the wire. Check the cable first, then
  the PHY 100BASE-TX line driver health. The adc_scope_cm firmware
  does **not** kick the auto-neg restart after `lwIPStart(0)` - in
  prior debugging that kick tore down a working link.

* **Build of `adc_scope_cm` reports five `#179-D` warnings.**
  These are in TI's `enet.c` and `f2838xif.c` (unused local
  variables). The projectspec adds `--diag_suppress=179` so a fresh
  re-import is clean. Existing imports can either re-import or add
  the flag manually under `Project -> Properties -> Build -> ARM
  Compiler -> Advanced -> Diagnostic Options`.

* **Flash programmer says CM is "held in reset" or `FMSTAT=0x1040`.**
  You loaded CM before running CPU1's `Device_bootCM()`. Erase CM
  flash and follow the strict order: load CPU1 -> run CPU1 -> pause
  -> load CM.

* **Streamlit dashboard shows 0 pkt/s but `sniff.py` works.**
  Both must bind the same UDP port. Default is 5005 on both sides.

---

## Author

**Masoud Bakhshi**

* Website  : <https://www.plan22.net>
* Email    : <info@plan22.net>
* LinkedIn : <https://www.linkedin.com/in/masoud-bakhshi-78490846/>

Licensed under the MIT License with an additional attribution
requirement - see [LICENSE](LICENSE). Any derivative work must
visibly credit the author with the name, website, email and
LinkedIn references above.
