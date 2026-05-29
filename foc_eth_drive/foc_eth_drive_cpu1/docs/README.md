# F28388D GaN FOC Ethernet Project

## License

MIT License with required attribution. See repository LICENSE.

Author: Masoud Bakhshi - www.plan22.net - LinkedIn

## Overview

This project implements a synchronous frame current controller (FOC) for
a three-phase RL load driven by the BOOSTXL-3PhGaNInv 48 V GaN inverter
stacked on a TMDSCNCD28388D controlCARD on a TMDSHSECDOCK. The
controller is commanded over Ethernet by a Raspberry Pi 4 host running
a Streamlit dashboard.

Key numbers:

- Switching frequency Fsw = 20 kHz, symmetric up/down count carrier.
- ADC sample point at the carrier valley.
- Control loop runs at 20 kHz from the ADC EOC ISR.
- Default closed-loop current bandwidth 1 kHz, capped at Fsw / 10 = 2 kHz.
- Per-axis voltage limit = Vdc / sqrt(3) (linear SVPWM region).
- Telemetry at 5 kHz over UDP.

## Project layout

```
f28388d_gan_foc_eth/
  targetConfigs/TMS320F28388D.ccxml
  F2838x_RAM_lnk_cpu1.cmd
  F2838x_FLASH_lnk_cpu1.cmd
  F2838x_Headers_nonBIOS_cpu1.cmd
  include/
    user_config.h
    board_pinmap_boostxl_3phganinv.h
    adc_iface.h
    pwm_iface.h
    foc_dq_pi.h
    svpwm.h
    clarke_park.h
    eth_proto.h
    eth_iface.h
    prot_safety.h
    util_fastmath.h
  src/
    main.c
    sysctl_init.c
    pwm_init.c
    adc_init.c
    isr_control.c
    foc_dq_pi.c
    svpwm.c
    clarke_park.c
    eth_init.c
    eth_rx_cmd.c
    eth_tx_telemetry.c
    prot_safety.c
    util_fastmath.c
  docs/
    README.md
    PINOUT.md
    BUILD_CCS.md
    CONTROL_DESIGN.md
```

## Dual-core deployment note

On the F28388D the EMAC peripheral is connected to the Cortex-M4 (CM)
core. This deliverable is the CPU1 image: it runs the FOC, ADC, PWM
and protection layer. The Ethernet stack lives in a companion CM image
(not part of this folder). The two images exchange `eth_cmd_pkt_t` and
`eth_telem_pkt_t` over dedicated MSGRAM banks declared in the linker
files (`MSGRAM_CPU1_TO_CM`, `MSGRAM_CM_TO_CPU1`). The `eth_iface_*`
functions in `src/eth_init.c` hide this detail from the rest of the
application.

If you want to run the entire stack on a single Cortex-M4 core, port
`isr_control.c` and the FOC modules to the CM toolchain (TI Arm Clang)
and call the lwIP raw API directly inside `eth_init.c`. The control
loop math is identical.

## Building

See `docs/BUILD_CCS.md` for the step by step CCS workflow.

## Bring up

See `BRINGUP_CHECKLIST.md` at the project root for the 10-step
commissioning sequence.
