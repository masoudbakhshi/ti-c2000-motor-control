/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * svpwm.h
 *
 * Space Vector PWM using the min/max common mode injection technique.
 * Branch free, fully equivalent in the linear region to the classical
 * sector based SVPWM. Output is three center aligned compare values
 * to be written to the ePWM CMPA registers.
 */

#ifndef SVPWM_H_
#define SVPWM_H_

#include <stdint.h>
#include "clarke_park.h"

/* CMPA values for the three legs. Units are TBCLK ticks, in
 * [PWM_MIN_PULSE_TICKS, PWM_TBPRD_TICKS - PWM_MIN_PULSE_TICKS].
 */
typedef struct
{
    uint16_t cmpa_a;
    uint16_t cmpa_b;
    uint16_t cmpa_c;
} svpwm_cmp_t;

/*
 * svpwm_generate
 * Purpose : Convert a three phase voltage reference and the measured DC
 *           bus voltage into three center aligned CMPA values.
 *
 * Algorithm:
 *   v_max = max(va, vb, vc)
 *   v_min = min(va, vb, vc)
 *   v_cm  = 0.5 * (v_max + v_min)
 *   va'   = va - v_cm
 *   vb'   = vb - v_cm
 *   vc'   = vc - v_cm
 *   duty_x = 0.5 + vx' / Vdc                  (range [0, 1])
 *   CMPA_x = clamp(round(duty_x * TBPRD),
 *                  PWM_MIN_PULSE_TICKS,
 *                  TBPRD - PWM_MIN_PULSE_TICKS)
 *
 * Inputs  : v_abc   three phase desired phase to neutral voltage [V]
 *           vdc     measured DC bus voltage [V]
 *           tbprd   period register value [ticks]
 * Outputs : out     CMPA triplet ready to load into the ePWM modules
 * ISR safe: yes
 * Budget  : approx 20 floating point ops + 3 saturations.
 */
void svpwm_generate(const abc_t *v_abc,
                    float        vdc,
                    uint16_t     tbprd,
                    svpwm_cmp_t *out);

#endif /* SVPWM_H_ */
