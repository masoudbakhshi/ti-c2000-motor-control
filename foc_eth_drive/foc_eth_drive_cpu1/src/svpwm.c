/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * svpwm.c
 *
 * Min/max common mode injection SVPWM. Equivalent to classical sector
 * based SVPWM in the linear region of operation, branch free, and
 * fast enough to be evaluated inside the 20 kHz current control ISR.
 */

#include "svpwm.h"
#include "user_config.h"
#include "util_fastmath.h"

void svpwm_generate(const abc_t *v_abc,
                    float        vdc,
                    uint16_t     tbprd,
                    svpwm_cmp_t *out)
{
    /* Guard against bad Vdc to avoid divide-by-zero. The safety layer
     * inhibits PWM when vdc < VDC_UVLO_V so this is a belt and braces
     * check.
     */
    float vdc_safe = (vdc < 1.0f) ? 1.0f : vdc;
    float inv_vdc  = 1.0f / vdc_safe;

    float v_max = util_max3(v_abc->a, v_abc->b, v_abc->c);
    float v_min = util_min3(v_abc->a, v_abc->b, v_abc->c);
    float v_cm  = 0.5f * (v_max + v_min);

    float va = v_abc->a - v_cm;
    float vb = v_abc->b - v_cm;
    float vc = v_abc->c - v_cm;

    float duty_a = 0.5f + va * inv_vdc;
    float duty_b = 0.5f + vb * inv_vdc;
    float duty_c = 0.5f + vc * inv_vdc;

    /* Saturation in duty domain to keep the inverter in the linear region
     * after the common mode injection. The linear SVPWM range is exactly
     * [0, 1] after min/max injection so simple clamping here is correct.
     */
    duty_a = util_clampf(duty_a, 0.0f, 1.0f);
    duty_b = util_clampf(duty_b, 0.0f, 1.0f);
    duty_c = util_clampf(duty_c, 0.0f, 1.0f);

    float tbprd_f = (float)tbprd;

    int32_t cmp_a = (int32_t)(duty_a * tbprd_f + 0.5f);
    int32_t cmp_b = (int32_t)(duty_b * tbprd_f + 0.5f);
    int32_t cmp_c = (int32_t)(duty_c * tbprd_f + 0.5f);

    int32_t lo = (int32_t)PWM_MIN_PULSE_TICKS;
    int32_t hi = (int32_t)tbprd - (int32_t)PWM_MIN_PULSE_TICKS;

    if (cmp_a < lo) { cmp_a = lo; } else if (cmp_a > hi) { cmp_a = hi; }
    if (cmp_b < lo) { cmp_b = lo; } else if (cmp_b > hi) { cmp_b = hi; }
    if (cmp_c < lo) { cmp_c = lo; } else if (cmp_c > hi) { cmp_c = hi; }

    out->cmpa_a = (uint16_t)cmp_a;
    out->cmpa_b = (uint16_t)cmp_b;
    out->cmpa_c = (uint16_t)cmp_c;
}
