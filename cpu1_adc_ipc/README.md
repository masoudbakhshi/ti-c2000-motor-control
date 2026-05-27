# cpu1_adc_ipc

F28388D **CPU1** project: ADCINA0 sampler + CPU1->CM IPC producer.

* ADC-A SOC0 = ADCIN0 (HSEC8 pin 9 = dock J4 pin 1)
* ePWM1 fires SOCA at 10 kHz (TBPRD = 4999, TBCLK = 50 MHz - SysConfig HSPCLKDIV=2 on top of EPWMCLKDIV=2 halves the clock that ends up at the time-base)
* ADC1 EOC ISR converts each result to volts (V = raw / 4095 * 3.3) and
  writes it into a 128-deep float ring in `MSGRAM_CPU_TO_CM`
* `Device_bootCM()` brings the Cortex-M4 core up from its own flash
* SysConfig handles all pinmux, peripheral init, and `Board_init()`

See the [repo README](../README.md) for the full bring-up sequence,
hardware switch matrix and acceptance criteria.

**Toolchain:** TI C2000 Compiler 22.6.x LTS (CCS 20.5.1 default).
SysConfig 1.27.x. C2000Ware 26.01.00.00.

**Active build configurations:** `CPU1_RAM` (default for bring-up via
JTAG) and `CPU1_FLASH` (for standalone boot). Switch in CCS via
`Project -> Build Configurations -> Set Active`.
