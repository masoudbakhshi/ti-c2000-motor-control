/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * isr_control.c
 *
 * Control ISR. Triggered by ADCA1 EOC at the carrier valley at
 * Fsw = 20 kHz. Total execution budget is 25 microseconds; the FOC
 * pipeline (read ADC, calibrate, Clarke, Park, two PI loops, inverse
 * Park, SVPWM, CMPA load) measures approximately 4 to 6 microseconds
 * on the C28x FPU core at 200 MHz with this code compiled at -O2.
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "user_config.h"
#include "board_pinmap_boostxl_3phganinv.h"
#include "adc_iface.h"
#include "pwm_iface.h"
#include "clarke_park.h"
#include "foc_dq_pi.h"
#include "svpwm.h"
#include "prot_safety.h"
#include "util_fastmath.h"
#include "eth_proto.h"
#include "driverlib.h"
#include "device.h"

/* --- Shared state, populated by main and read by ISR ----------------- */
volatile float    g_id_ref_A    = 0.0f;
volatile float    g_iq_ref_A    = 0.0f;
volatile float    g_fe_ref_Hz   = 0.0f;
volatile bool     g_user_enable = false;

foc_state_t  g_foc;
prot_state_t g_prot;

/* Telemetry SPSC ring. Producer = ISR, consumer = main loop. */
volatile uint32_t g_ring_head = 0u;
volatile uint32_t g_ring_tail = 0u;
eth_telem_pkt_t   g_ring[TELEM_RING_LEN];

volatile uint32_t g_isr_count = 0u;

static uint32_t   s_cal_count = 0u;
static uint32_t   s_cal_acc_a = 0u;
static uint32_t   s_cal_acc_b = 0u;
static uint32_t   s_cal_acc_c = 0u;
adc_calib_t       g_calib     = { 2048u, 2048u, 2048u };

static float      s_theta_e_rad = 0.0f;

/*
 * isr_ring_push
 * Purpose : Lockless push into the telemetry ring. Drops on overflow.
 * Inputs  : p  packet to enqueue
 * ISR safe: yes (single producer).
 * Budget  : approx 0.3 us.
 */
static inline void isr_ring_push(const eth_telem_pkt_t *p)
{
    uint32_t head = g_ring_head;
    uint32_t next = (head + 1u) & TELEM_RING_MASK;
    if (next == g_ring_tail) { return; }
    g_ring[head] = *p;
    g_ring_head  = next;
}

/*
 * isr_calibrate
 * Purpose : Accumulate raw ADC samples during the startup window. When
 *           ADC_CAL_SAMPLES have been seen, store the average as the
 *           zero current offset and arm the calib_done flag.
 * Inputs  : ia, ib, ic  raw ADC counts for the three phase currents.
 * Returns : true while calibration is still in progress.
 * ISR safe: yes.
 * Budget  : approx 0.4 us.
 */
static inline bool isr_calibrate(uint16_t ia, uint16_t ib, uint16_t ic)
{
    if (s_cal_count >= ADC_CAL_SAMPLES) { return false; }
    s_cal_acc_a += ia;
    s_cal_acc_b += ib;
    s_cal_acc_c += ic;
    s_cal_count++;
    if (s_cal_count == ADC_CAL_SAMPLES)
    {
        g_calib.off_a = (uint16_t)(s_cal_acc_a / ADC_CAL_SAMPLES);
        g_calib.off_b = (uint16_t)(s_cal_acc_b / ADC_CAL_SAMPLES);
        g_calib.off_c = (uint16_t)(s_cal_acc_c / ADC_CAL_SAMPLES);
        g_prot.calib_done = true;
    }
    return true;
}

/*
 * isr_run_foc_pipeline
 * Purpose : Run one Clarke->Park->PI(d/q)->iPark->SVPWM pass and load
 *           the CMPA registers, honoring the protection layer.
 * Inputs  : ia, ib, ic, vdc  measured physical quantities [A, A, A, V]
 *           i_dq_out         output dq measurement (used by telemetry)
 *           sin_th, cos_th   output sin/cos of theta_e (for telemetry)
 * ISR safe: yes.
 * Budget  : approx 3 us.
 */
static inline void isr_run_foc_pipeline(float ia, float ib, float ic, float vdc,
                                        dq_t  *i_dq_out,
                                        float *sin_th_out,
                                        float *cos_th_out)
{
    float omega_e = TWO_PI_F * g_fe_ref_Hz;
    s_theta_e_rad = util_wrap_2pi(s_theta_e_rad + omega_e * TS_CTRL_S);

    abc_t i_abc = { ia, ib, ic };
    ab_t  i_ab;
    float sin_th, cos_th;
    util_sincosf(s_theta_e_rad, &sin_th, &cos_th);
    clarke_abc_to_ab(&i_abc, &i_ab);
    park_ab_to_dq(&i_ab, sin_th, cos_th, i_dq_out);

    foc_set_omega_e(&g_foc, omega_e);
    foc_set_voltage_limit(&g_foc, vdc * ONE_OVER_SQRT3_F);

    dq_t i_ref = { g_id_ref_A, g_iq_ref_A };
    dq_t v_dq;
    foc_step(&g_foc, &i_ref, i_dq_out, &v_dq);

    ab_t  v_ab;
    abc_t v_abc;
    svpwm_cmp_t cmp;
    ipark_dq_to_ab(&v_dq, sin_th, cos_th, &v_ab);
    iclarke_ab_to_abc(&v_ab, &v_abc);
    svpwm_generate(&v_abc, vdc, PWM_TBPRD_TICKS, &cmp);

    if (prot_tick(&g_prot, ia, ib, ic, vdc))
    {
        pwm_iface_apply_cmp(&cmp);
        pwm_iface_enable_outputs();
    }
    else
    {
        pwm_iface_force_idle(PWM_TBPRD_TICKS);
        pwm_iface_disable_outputs();
    }

    *sin_th_out = sin_th;
    *cos_th_out = cos_th;
}

/*
 * isr_publish_telemetry
 * Purpose : Pack the current control snapshot into an eth_telem_pkt_t
 *           and push it into the SPSC ring. Called once every
 *           TELEM_DECIMATION ISRs.
 * Inputs  : i_dq                 measured d/q current
 *           ia, ib, ic, vdc      measured physical quantities
 * ISR safe: yes.
 * Budget  : approx 0.8 us (mostly the CRC over 50 bytes).
 */
static inline void isr_publish_telemetry(const dq_t *i_dq,
                                         float ia, float ib, float ic,
                                         float vdc)
{
    eth_telem_pkt_t p;
    p.magic        = ETH_TELEM_MAGIC;
    p.seq          = g_isr_count;
    p.timestamp_us = (uint32_t)(g_isr_count * (uint32_t)TS_CTRL_US);
    p.id_ref_A     = g_id_ref_A;
    p.id_meas_A    = i_dq->d;
    p.iq_ref_A     = g_iq_ref_A;
    p.iq_meas_A    = i_dq->q;
    p.ia_A         = ia;
    p.ib_A         = ib;
    p.ic_A         = ic;
    p.vdc_V        = vdc;
    p.theta_e_rad  = s_theta_e_rad;
    p.status_flags = g_prot.flags;
    p.crc16        = 0u;   /* CM fills the wire CRC before TX */
    isr_ring_push(&p);
}

/*
 * isr_control_entry
 * Purpose : ADCA1 EOC ISR. Pinned to RAM for jitter-free execution.
 *           Orchestrates the four helper stages: calibrate, convert
 *           to physical units, run the FOC pipeline, publish telemetry.
 * ISR safe: this IS the ISR.
 * Budget  : < 25 us. Actual ~4.5 us at 200 MHz, -O2.
 */
#pragma CODE_SECTION(isr_control_entry, ".TI.ramfunc")
__interrupt void isr_control_entry(void)
{
    GPIO_writePin(GPIO_ISR_PROBE, 1);

    uint16_t ia_raw, ib_raw, ic_raw, vdc_raw;
    adc_iface_read_raw(&ia_raw, &ib_raw, &ic_raw, &vdc_raw);

    if (isr_calibrate(ia_raw, ib_raw, ic_raw))
    {
        pwm_iface_force_idle(PWM_TBPRD_TICKS);
    }
    else
    {
        float ia  = adc_iface_to_phase_current(ia_raw, g_calib.off_a);
        float ib  = adc_iface_to_phase_current(ib_raw, g_calib.off_b);
        float ic  = adc_iface_to_phase_current(ic_raw, g_calib.off_c);
        float vdc = adc_iface_to_vdc(vdc_raw);

        dq_t  i_dq;
        float sin_th, cos_th;
        isr_run_foc_pipeline(ia, ib, ic, vdc, &i_dq, &sin_th, &cos_th);

        if ((g_isr_count % TELEM_DECIMATION) == 0u)
        {
            isr_publish_telemetry(&i_dq, ia, ib, ic, vdc);
        }
    }

    g_isr_count++;
    GPIO_writePin(GPIO_ISR_PROBE, 0);

    ADC_clearInterruptStatus(ADC_EOC_BASE, ADC_EOC_INT_NUMBER);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}
