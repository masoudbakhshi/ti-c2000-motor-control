# foc_eth_drive_cpu1

F28388D **CPU1** (C28x) project: synchronous-frame current control
(Field-Oriented Control) on a three-phase RL load driven by a
BOOSTXL-3PhGaNInv 48 V GaN inverter, plus the CPU1->CM IPC producer.
The CM half lives at [`../foc_eth_drive_cm/`](../foc_eth_drive_cm/);
the parent overview is at [`../README.md`](../README.md).

The **Pi-side dashboard and command path** that talks to this
firmware lives in the sibling repo at
[`RaspberryPi/07_FOC_Eth_Drive_Scope/`](https://github.com/masoudbakhshi/RaspberryPi/tree/main/07_FOC_Eth_Drive_Scope).

| Field    | Value                                          |
|----------|------------------------------------------------|
| Author   | Masoud Bakhshi                                 |
| Website  | https://www.plan22.net                         |
| Email    | info@plan22.net                                |
| LinkedIn | https://www.linkedin.com/in/masoud-bakhshi-78490846/ |

## License

MIT License with required attribution. See the parent package README.

## Responsibilities of this image

- Configure ADCA/ADCB to sample the three phase currents (INA240
  outputs on the BOOSTXL) plus the DC bus voltage, triggered from
  ePWM1 SOCA at the carrier valley.
- Run ePWM1/2/3 in symmetric up/down count mode at 20 kHz with phase
  synchronization and 200 ns dead time.
- Inside the ADCA1 EOC ISR:
    1. Read the raw counts.
    2. During the first `ADC_CAL_SAMPLES` ticks accumulate the zero
       current offset for each phase.
    3. Convert to physical units (A, V).
    4. Run Clarke -> Park -> two decoupled PI controllers ->
       inverse Park -> min/max common-mode-injection SVPWM ->
       three CMPA values.
    5. Honour the protection layer (overcurrent, over/under voltage,
       Ethernet watchdog). When any flag is latched, force the CMPA
       triplet to 50 % and drive a TZ one-shot trip.
    6. Push a telemetry sample into the MSGRAM ring shared with CM
       (every `TELEM_DECIMATION` ISRs).
- Mux every MII signal onto its fixed F2838x pin and release the
  DP83822 PHY out of reset / power-down (the EMAC peripheral is owned
  by the CM core but the pin-mux is a CPU1 responsibility).
- Boot the Cortex-M4 core with `Device_bootCM(BOOTMODE_BOOT_TO_FLASH_SECTOR0)`.
- Drain command packets the CM core deposits in MSGRAM, apply
  `id_ref`, `iq_ref`, `fe_ref`, `Rs`, `Ls`, `f_bw`, and the
  enable/reset flags.

The PI tuning uses an Internal Model Control / pole-zero cancellation
design: `Kp = 2*pi*f_bw * Ls`, `Ki = 2*pi*f_bw * Rs`. Anti-windup uses
back-calculation. Cross-coupling is decoupled via feed forward in
both axes.

## Folder layout

```
foc_eth_drive_cpu1/
  F2838x_FLASH_lnk_cpu1.cmd    - linker (FLASH + MSGRAM + .TI.ramfunc)
  foc_eth_drive_cpu1.projectspec
  BRINGUP_CHECKLIST.md
  targetConfigs/
    TMS320F28388D.ccxml        - XDS100v2 probe + F28388D device
  include/
    user_config.h              - all user tunables (no magic numbers elsewhere)
    board_pinmap_boostxl_3phganinv.h
    adc_iface.h pwm_iface.h    - inline accessors used by the ISR
    foc_dq_pi.h svpwm.h clarke_park.h
    eth_proto.h eth_iface.h    - wire format + IPC mailbox abstraction
    prot_safety.h util_fastmath.h
  src/
    main.c sysctl_init.c
    pwm_init.c adc_init.c
    isr_control.c              - the 20 kHz ISR pipeline
    foc_dq_pi.c svpwm.c clarke_park.c
    prot_safety.c
    eth_init.c eth_rx_cmd.c eth_tx_telemetry.c
    eth_phy_bringup.c          - MII pin-mux + Device_bootCM
    util_fastmath.c
  docs/
    BUILD_CCS.md
    CONTROL_DESIGN.md
    PINOUT.md
    README.md                  - old long-form README, retained
```

## Build

```
File -> Import -> CCS Projects -> foc_eth_drive_cpu1.projectspec
Project -> Build Project
```

The projectspec carries pathVariables for C2000Ware 26.x at
`C:/ti/c2000/C2000Ware_26_01_00_00/`. If your install lives elsewhere
edit the projectspec before importing.

## ISR timing budget

| Stage                       | Cycles | Microseconds at 200 MHz |
|-----------------------------|--------|--------------------------|
| ADC read + scale            | ~200   | ~1.0                     |
| Clarke + Park               | ~80    | ~0.4                     |
| PI(d) + PI(q) + anti-windup | ~220   | ~1.1                     |
| Inverse Park + iClarke      | ~80    | ~0.4                     |
| SVPWM min/max injection     | ~120   | ~0.6                     |
| CMPA writes                 | ~30    | ~0.15                    |
| Safety tick                 | ~80    | ~0.4                     |
| Telemetry push (1 in N)     | ~80    | ~0.4                     |
| **Total**                   | ~890   | ~4.45                    |

The 50 us Fsw period leaves more than 9x headroom.
