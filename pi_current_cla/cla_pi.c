/* Author: Masoud Bakhshi - www.plan22.net */
#include "driverlib.h"
#include "device.h"

/* ── PI gains (bandwidth-based, precomputed) ────────────────────────── */
/* omega_c = 2π×1000 rad/s, L=1 mH, R=6 Ω, Ts=50 µs                   */
/* Kp = L × omega_c = 6.28319 V/A                                       */
/* Ki×Ts = R × omega_c × Ts = 1.88496                                   */
#define KP      6.28319f
#define KI_TS   1.88496f

/* ── ADC conversion: 12-bit, ±IPEAK range, midscale = zero current ──── */
#define IPEAK   10.0f               /* shunt amp full-scale (A) */

/* ── Shared variables (defined in main.c) ──────────────────────────── */
extern volatile float g_iref;      /* Cpu1ToCla1MsgRAM - reference (A) */
extern volatile float g_duty;      /* Cla1ToCpuMsgRAM  - duty 0–1 */
extern volatile float g_imeas;     /* Cla1ToCpuMsgRAM  - measured current (A) */
extern volatile float g_u_prev;    /* Cla1ToCpuMsgRAM  - PI state: u[k-1] */
extern volatile float g_e_prev;    /* Cla1ToCpuMsgRAM  - PI state: e[k-1] */

/* ================================================================== */
/*  CLA Task 1 - triggered by ADCA INT1 at 20 kHz                     */
/*                                                                     */
/*  Incremental PI (velocity form):                                    */
/*    i_meas = (adc / 4096 - 0.5) × 2 × IPEAK                        */
/*    e[k]   = i_ref[k] - i_meas[k]                                   */
/*    du[k]  = Kp × (e[k] - e[k-1]) + Ki×Ts × e[k]                  */
/*    u[k]   = clamp(u[k-1] + du[k], -0.5, 0.5)                      */
/*    duty   = 0.5 + u[k]    (maps [-0.5,0.5] → [0,1])               */
/*                                                                     */
/*  Anti-windup: natural from incremental form with output clamp.      */
/*  CLA writes duty to Cla1ToCpuMsgRAM; CPU reads it in ePWM1 ISR.   */
/* ================================================================== */
#pragma CODE_SECTION(Cla1Task1, "Cla1Prog")
__interrupt void Cla1Task1(void)
{
    uint16_t adc_raw;
    float    i_meas, i_ref, e, du, u;

    /* Read ADC result (ADCA SOC0 - phase A current) */
    adc_raw = ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER0);

    /* Convert to current: midscale (2048) = 0 A, full-scale = ±IPEAK */
    i_meas = ((float)adc_raw * (1.0f / 4096.0f) - 0.5f) * (2.0f * IPEAK);

    /* Load reference written by CPU in previous ePWM1 ISR */
    i_ref = g_iref;

    /* Compute error */
    e = i_ref - i_meas;

    /* Incremental PI update */
    du = KP * (e - g_e_prev) + KI_TS * e;
    u  = g_u_prev + du;

    /* Clamp: u in [-0.5, 0.5] → duty in [0, 1] */
    if (u >  0.5f) u =  0.5f;
    if (u < -0.5f) u = -0.5f;

    /* Update PI state and output */
    g_u_prev = u;
    g_e_prev = e;
    g_imeas  = i_meas;
    g_duty   = 0.5f + u;
}

#pragma CODE_SECTION(Cla1Task1End, "Cla1Prog")
__interrupt void Cla1Task1End(void) {}
