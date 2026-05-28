# svpwm_adc

Three-phase Space Vector PWM with synchronised ADC sampling on the
TMS320F28388D (TMDSCNCD28388D controlCARD + TMDSHSECDOCK).

## What it does

- Generates three-phase SVPWM at **20 kHz** using ePWM1/2/3 in up-down count
  mode with 125 ns dead-time (active-high complementary outputs).
- Triggers an ADC conversion on ADCINA0 at the PWM carrier **peak**
  (CTR = PRD), the point of minimum current ripple.
- Reads the ADC result inside the ePWM1 CTR=ZERO ISR, half a switching
  period after the conversion fired (well past EOC).
- Streams sector number and raw ADC count over UART (SCIA) at **10 Hz**
  in the format `S:<sector>,ADC:<count>,A:<cmpa>,B:<cmpb>,C:<cmpc>`.

## Hardware

| Item | Part |
|------|------|
| controlCARD | TMDSCNCD28388D |
| Dock | TMDSHSECDOCK |
| Inverter | BOOSTXL-3PHGANINV |
| UART (backchannel) | GPIO28 RX / GPIO29 TX - 115200 8N1 |

## Key parameters

| Parameter | Value |
|-----------|-------|
| SYSCLK | 200 MHz |
| Switching frequency | 20 kHz |
| TBPRD | 5000 |
| Dead-time | 125 ns (25 counts) |
| ADC clock | 50 MHz (÷4) |
| ADC sample window | 15 cycles (75 ns) |
| Vref (normalised) | 0.70 |

## Project structure

```
svpwm_adc/
├── main.c              # SVPWM algorithm, ISR, ADC read, UART output
├── svpwm_adc.syscfg    # SysConfig: ePWM, ADC, SCI, CMD
└── CCS/
    └── svpwm_adc.projectspec
```

## Building

Import `CCS/svpwm_adc.projectspec` into Code Composer Studio. Select the
`CPU1_RAM` configuration for debug or `CPU1_FLASH` for deployment and build.
Requires C2000Ware 26.01.00.00 at `C:/ti/c2000/C2000Ware_26_01_00_00`.

## License

MIT License - Copyright (c) 2024 Masoud Bakhshi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

**Author:** Masoud Bakhshi - [www.plan22.net](https://www.plan22.net) |
[LinkedIn](https://www.linkedin.com/in/masoud-bakhshi-78490846)
