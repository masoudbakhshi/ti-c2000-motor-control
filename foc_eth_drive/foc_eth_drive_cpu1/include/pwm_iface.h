/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * pwm_iface.h
 *
 * Public surface of the PWM layer: peripheral bring up, runtime
 * enable / disable, and a fast CMPA loader called from the ISR.
 */

#ifndef PWM_IFACE_H_
#define PWM_IFACE_H_

#include <stdint.h>
#include <stdbool.h>
#include "svpwm.h"
#include "board_pinmap_boostxl_3phganinv.h"
#include "driverlib.h"

/*
 * pwm_iface_init
 * Purpose : Configure ePWM1/2/3 in symmetric up/down count, center
 *           aligned, with leg B and leg C phase synchronized to leg A.
 *           Configures dead band, trip zone for OC/OV, and SOCA on
 *           ePWM1 firing at TBCTR = 0.
 * ISR safe: no
 */
void pwm_iface_init(void);

/*
 * pwm_iface_apply_cmp
 * Purpose : Load three compare values into ePWM1/2/3 CMPA. Uses shadow
 *           load at CTR = ZRO so the duty change is glitch-free.
 * Inputs  : cmp  CMPA triplet from svpwm_generate
 * ISR safe: yes
 */
static inline void pwm_iface_apply_cmp(const svpwm_cmp_t *cmp)
{
    EPWM_setCounterCompareValue(PWM_LEG_A_BASE, EPWM_COUNTER_COMPARE_A, cmp->cmpa_a);
    EPWM_setCounterCompareValue(PWM_LEG_B_BASE, EPWM_COUNTER_COMPARE_A, cmp->cmpa_b);
    EPWM_setCounterCompareValue(PWM_LEG_C_BASE, EPWM_COUNTER_COMPARE_A, cmp->cmpa_c);
}

/*
 * pwm_iface_force_idle
 * Purpose : Drive all CMPA to 50 percent so the H bridge produces zero
 *           differential voltage. This is the safe coast state used
 *           when the inverter is disabled but the supply is still up.
 * ISR safe: yes
 */
static inline void pwm_iface_force_idle(uint16_t tbprd)
{
    uint16_t mid = (uint16_t)(tbprd >> 1);
    EPWM_setCounterCompareValue(PWM_LEG_A_BASE, EPWM_COUNTER_COMPARE_A, mid);
    EPWM_setCounterCompareValue(PWM_LEG_B_BASE, EPWM_COUNTER_COMPARE_A, mid);
    EPWM_setCounterCompareValue(PWM_LEG_C_BASE, EPWM_COUNTER_COMPARE_A, mid);
}

/*
 * pwm_iface_enable_outputs / pwm_iface_disable_outputs
 * Purpose : Release / force the high impedance state of all six gates.
 *           Implemented by writing the TZ force registers (software
 *           one-shot trip) so the action happens at the next TBCTR
 *           boundary and the gate driver inputs go to a defined off
 *           state immediately.
 * ISR safe: yes
 */
void pwm_iface_enable_outputs(void);
void pwm_iface_disable_outputs(void);

#endif /* PWM_IFACE_H_ */
