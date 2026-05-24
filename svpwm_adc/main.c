#include "driverlib.h"
#include "device.h"
#include "board.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* ── Hardware constants ─────────────────────────────────────────────── */
#define SYSCLK_HZ    200000000UL
#define FSW_HZ       20000UL
#define TBPRD        5000U           /* SYSCLK / (2 × FSW) = 200 M / 40 k */

/* ── SVPWM operating point ──────────────────────────────────────────── */
#define ELEC_HZ      1.0f            /* output electrical frequency (Hz)  */
#define DTHETA       (2.0f * 3.14159265f * ELEC_HZ / (float)FSW_HZ)
#define VREF         0.70f           /* normalised voltage ref (0–1)      */
#define VDC          1.00f           /* normalised DC bus                  */

/* ── UART cadence ───────────────────────────────────────────────────── */
#define UART_DIV     2000U           /* 20 000 ISR ticks / 10 Hz          */

/*
 * Phase-role lookup: c_role[sector_index][phase] → {0=max, 1=mid, 2=min}
 *
 * Sector  Active vectors   Va   Vb   Vc
 *   1     V1(100),V2(110)  max  mid  min   (odd  sector index 0,2,4)
 *   2     V2(110),V3(010)  mid  max  min   (even sector index 1,3,5)
 *   3     V3(010),V4(011)  min  max  mid
 *   4     V4(011),V5(001)  min  mid  max
 *   5     V5(001),V6(101)  mid  min  max
 *   6     V6(101),V1(100)  max  min  mid
 */
static const uint8_t c_role[6][3] = {
    {0, 1, 2},
    {1, 0, 2},
    {2, 0, 1},
    {2, 1, 0},
    {1, 2, 0},
    {0, 2, 1},
};

/* ── CLA → CPU shared variable (written by CLA Task 1) ─────────────── */
#pragma DATA_SECTION(g_adcResult, "cla1ToCpuMsgRAM")
volatile uint16_t g_adcResult;

/* ── State visible to main loop ─────────────────────────────────────── */
volatile uint16_t g_sector;
volatile uint16_t g_cmpa[3];   /* CMPA registers for ePWM1, ePWM2, ePWM3 */
volatile uint16_t g_uartReady;

/* ── CLA task forward declarations ──────────────────────────────────── */
extern __interrupt void Cla1Task1(void);
extern __interrupt void Cla1Task1End(void);

/* ================================================================== */
/*  SVPWM duty-cycle computation                                       */
/*                                                                     */
/*  T1 = Vref × sin(π/3 − α) / Vdc                                   */
/*  T2 = Vref × sin(α)        / Vdc                                   */
/*  Symmetric zero-vector split:  T0 = 1 − T1 − T2                   */
/*                                                                     */
/*  Tmax = (1 + T1 + T2) / 2                                          */
/*  Tmin = (1 − T1 − T2) / 2                                          */
/*  Tmid = (1 − T1 + T2) / 2  for odd sectors  (index 0, 2, 4)       */
/*       = (1 + T1 − T2) / 2  for even sectors (index 1, 3, 5)       */
/*                                                                     */
/*  CMPA = TBPRD × (1 − duty)  [AQ: set on CTR=CMPA up, clr on down] */
/* ================================================================== */
static void svpwm_update(float theta)
{
    const float pi3 = 3.14159265f / 3.0f;
    int32_t s, ph;
    float alpha, T1, T2, Tmax, Tmin, Tmid, Ton[3];
    int32_t c;

    s = (int32_t)(theta / pi3);
    if (s < 0) s = 0;
    if (s > 5) s = 5;

    alpha = theta - (float)s * pi3;
    T1    = VREF * sinf(pi3 - alpha) / VDC;
    T2    = VREF * sinf(alpha)        / VDC;

    Tmax = (1.0f + T1 + T2) * 0.5f;
    Tmin = (1.0f - T1 - T2) * 0.5f;
    Tmid = (s % 2 == 0) ? (1.0f - T1 + T2) * 0.5f
                         : (1.0f + T1 - T2) * 0.5f;

    if (Tmin < 0.0f) { Tmax = 1.0f; Tmin = 0.0f; }

    Ton[0] = Tmax;
    Ton[1] = Tmid;
    Ton[2] = Tmin;

    g_sector = (uint16_t)(s + 1);

    for (ph = 0; ph < 3; ph++) {
        c = (int32_t)((1.0f - Ton[c_role[s][ph]]) * (float)TBPRD + 0.5f);
        if (c < 0)               c = 0;
        if (c > (int32_t)TBPRD)  c = (int32_t)TBPRD;
        g_cmpa[ph] = (uint16_t)c;
    }

    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, g_cmpa[0]);
    EPWM_setCounterCompareValue(EPWM2_BASE, EPWM_COUNTER_COMPARE_A, g_cmpa[1]);
    EPWM_setCounterCompareValue(EPWM3_BASE, EPWM_COUNTER_COMPARE_A, g_cmpa[2]);
}

/* ================================================================== */
/*  ePWM1 ISR — fires at CTR=ZERO, 20 kHz                             */
/*  Updates SVPWM duty cycles each switching period.                   */
/*  g_adcResult is ready: ADC SOC fired at CTR=PRD one half-cycle     */
/*  earlier, CLA Task 1 stored the result before CTR returns to ZERO. */
/* ================================================================== */
__interrupt void epwm1ISR(void)
{
    static float    theta = 0.0f;
    static uint32_t count = 0U;

    svpwm_update(theta);

    theta += DTHETA;
    if (theta >= 2.0f * 3.14159265f)
        theta -= 2.0f * 3.14159265f;

    if (++count >= UART_DIV) {
        count = 0U;
        g_uartReady = 1U;
    }

    EPWM_clearEventTriggerInterruptFlag(EPWM1_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
}

/* ================================================================== */
/*  CLA Task 1 — triggered by ADCA INT1 (end of SOC0 conversion)      */
/*  Runs on the CLA processor; writes result to CPU-visible shared    */
/*  variable in Cla1ToCpuMsgRAM.                                       */
/* ================================================================== */
#pragma CODE_SECTION(Cla1Task1,    "Cla1Prog")
__interrupt void Cla1Task1(void)
{
    g_adcResult = ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER0);
}

#pragma CODE_SECTION(Cla1Task1End, "Cla1Prog")
__interrupt void Cla1Task1End(void) {}

/* ================================================================== */
/*  UART helpers — SCI-A, non-FIFO polling                            */
/* ================================================================== */
static void uart_send(const char *s)
{
    while (*s) {
        SCI_writeCharBlockingNonFIFO(SCIA_BASE, (uint16_t)*s);
        s++;
    }
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */
void main(void)
{
    char buf[64];
    uint16_t adc, sec, ca, cb, cc;

    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    Board_init();   /* SysConfig-generated: ePWM1/2/3, ADCA, CLA, SCIA */

    Interrupt_register(INT_EPWM1, &epwm1ISR);
    Interrupt_enable(INT_EPWM1);

    EINT;
    ERTM;

    for (;;) {
        if (g_uartReady) {
            g_uartReady = 0U;
            adc = g_adcResult;
            sec = g_sector;
            ca  = g_cmpa[0];
            cb  = g_cmpa[1];
            cc  = g_cmpa[2];
            snprintf(buf, sizeof(buf),
                     "S:%u,ADC:%u,A:%u,B:%u,C:%u\r\n",
                     (unsigned)sec, (unsigned)adc,
                     (unsigned)ca,  (unsigned)cb, (unsigned)cc);
            uart_send(buf);
        }
    }
}
