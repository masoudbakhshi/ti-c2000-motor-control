/* Author: Masoud Bakhshi — www.plan22.net */
#include "driverlib.h"
#include "device.h"
#include "board.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* ── Hardware constants ─────────────────────────────────────────────── */
#define SYSCLK_HZ    200000000UL
#define FSW_HZ       20000UL
#define TBPRD        5000U          /* SYSCLK / (2 × FSW) = 200 M / 40 k */
#define VBUS         24.0f          /* DC bus voltage (V) */
#define IPEAK        10.0f          /* ADC full-scale ±current (A) */

/* ── PI design constants ────────────────────────────────────────────── */
/* R-L load: R=6 Ω, L=1 mH, bandwidth omega_c = 2π×1000 rad/s         */
/* Kp = L × omega_c = 0.001 × 6283.2 = 6.2832 V/A                     */
/* Ki = R × omega_c = 6 × 6283.2 = 37699 V/(A·s)                      */
/* Ki×Ts = 37699 × 50e-6 = 1.88496                                     */
#define KP           6.28319f
#define KI_TS        1.88496f       /* Ki × Ts, Ts = 1/FSW */

/* ── Reference waveform ─────────────────────────────────────────────── */
/* REF_MODE 0 = step, 1 = sine sweep for Bode measurement               */
#define REF_MODE     1
#define REF_AMP      0.5f           /* reference amplitude (A) */
#define REF_FREQ_HZ  50.0f          /* sine frequency (Hz) */
#define DTHETA_REF   (2.0f * 3.14159265f * REF_FREQ_HZ / (float)FSW_HZ)

/* ── UART cadence ───────────────────────────────────────────────────── */
#define UART_DIV     2000U          /* 20 000 ticks / 10 Hz */

/* ================================================================== */
/*  CLA ↔ CPU shared variables                                         */
/*                                                                     */
/*  Cpu1ToCla1MsgRAM  — CPU writes, CLA reads                         */
/*  Cla1ToCpuMsgRAM   — CLA writes, CPU reads                         */
/* ================================================================== */
#pragma DATA_SECTION(g_iref,   "cpuToCla1MsgRAM")
volatile float g_iref   = 0.0f;    /* current reference (A) */

#pragma DATA_SECTION(g_duty,   "cla1ToCpuMsgRAM")
volatile float g_duty   = 0.5f;    /* normalised duty 0–1 */

#pragma DATA_SECTION(g_imeas,  "cla1ToCpuMsgRAM")
volatile float g_imeas  = 0.0f;    /* measured phase current (A) */

/* PI integrator state stored in CLA→CPU RAM so CPU can monitor */
#pragma DATA_SECTION(g_u_prev, "cla1ToCpuMsgRAM")
volatile float g_u_prev = 0.0f;    /* previous control output [-0.5, 0.5] */

#pragma DATA_SECTION(g_e_prev, "cla1ToCpuMsgRAM")
volatile float g_e_prev = 0.0f;    /* previous error (A) */

/* ── CPU-side ISR state ─────────────────────────────────────────────── */
volatile uint16_t g_uartReady;

/* ── CLA task forward declarations ──────────────────────────────────── */
extern __interrupt void Cla1Task1(void);
extern __interrupt void Cla1Task1End(void);

/* ================================================================== */
/*  uart_send — blocking SCI-A transmit, non-FIFO                     */
/* ================================================================== */
static void uart_send(const char *s)
{
    while (*s) {
        SCI_writeCharBlockingNonFIFO(SCIA_BASE, (uint16_t)*s);
        s++;
    }
}

/* ================================================================== */
/*  ePWM1 ISR — fires at CTR=ZERO, 20 kHz                             */
/*                                                                     */
/*  Sequence each period:                                              */
/*    CTR=PRD  → SOCA → ADC SOC0 starts                               */
/*    ADC done → ADCAINT1 → CLA Task 1 runs PI, writes g_duty         */
/*    CTR=ZERO → this ISR: reads g_duty, updates CMPA, updates g_iref */
/* ================================================================== */
__interrupt void epwm1ISR(void)
{
    static float    theta_ref = 0.0f;
    static uint32_t count     = 0U;
    float    duty;
    uint16_t cmpa;

    /* Read duty computed by CLA; clamp defensively */
    duty = g_duty;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    /* CMPA = (1 - duty) × TBPRD, AQ: set on up-CMPA, clr on dn-CMPA */
    cmpa = (uint16_t)((1.0f - duty) * (float)TBPRD + 0.5f);
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpa);

    /* Update current reference for next CLA cycle */
#if (REF_MODE == 1)
    g_iref = REF_AMP * sinf(theta_ref);
    theta_ref += DTHETA_REF;
    if (theta_ref >= 2.0f * 3.14159265f)
        theta_ref -= 2.0f * 3.14159265f;
#else
    g_iref = REF_AMP;
#endif

    if (++count >= UART_DIV) {
        count        = 0U;
        g_uartReady  = 1U;
    }

    EPWM_clearEventTriggerInterruptFlag(EPWM1_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */
void main(void)
{
    char  buf[80];
    float iref, imeas, duty;

    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    Board_init();   /* SysConfig-generated: ePWM1, ADCA, CLA, SCIA */

    Interrupt_register(INT_EPWM1, &epwm1ISR);
    Interrupt_enable(INT_EPWM1);

    EINT;
    ERTM;

    for (;;) {
        if (g_uartReady) {
            g_uartReady = 0U;
            iref  = g_iref;
            imeas = g_imeas;
            duty  = g_duty;
            snprintf(buf, sizeof(buf),
                     "REF:%.4f,MEAS:%.4f,DUTY:%.4f\r\n",
                     (double)iref, (double)imeas, (double)duty);
            uart_send(buf);
        }
    }
}
