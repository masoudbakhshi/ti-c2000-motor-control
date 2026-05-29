/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * foc_dq_pi.h
 *
 * Two decoupled PI current controllers in the synchronous dq frame.
 * Backward Euler discretization, back calculation anti windup, with
 * explicit cross coupling feed forward terms suitable for an RL load
 * driven by an SVPWM voltage source inverter.
 */

#ifndef FOC_DQ_PI_H_
#define FOC_DQ_PI_H_

#include <stdint.h>
#include <stdbool.h>
#include "clarke_park.h"

/* Per axis PI controller state. */
typedef struct
{
    float kp;       /* proportional gain          [V/A]   */
    float ki_ts;    /* Ki * Ts (pre-multiplied)   [V/A]   */
    float integ;    /* integrator accumulator     [V]     */
    float u_sat;    /* last saturated output      [V]     */
} pi_axis_t;

/* FOC controller state container. */
typedef struct
{
    pi_axis_t d;
    pi_axis_t q;
    float     Ls;          /* commanded stator inductance        [H]   */
    float     Rs;          /* commanded stator resistance        [Ohm] */
    float     omega_e;     /* electrical angular frequency       [rad/s] */
    float     v_lim_axis;  /* per axis voltage limit             [V]    */
} foc_state_t;

/*
 * foc_init
 * Purpose : Reset all integrator states and apply a default gain set.
 * Inputs  : st  controller state to initialize
 * ISR safe: yes (only when current loop is disabled)
 */
void foc_init(foc_state_t *st);

/*
 * foc_set_gains
 * Purpose : Recompute Kp and Ki from plant parameters using IMC tuning.
 *           Kp = 2*pi*f_bw * Ls
 *           Ki = 2*pi*f_bw * Rs
 *           f_bw is internally capped at FSW_HZ / 10 to honour the
 *           1.5 Ts digital delay model.
 *
 * Inputs  : st     controller state
 *           Rs     stator resistance [Ohm]
 *           Ls     stator inductance [H]
 *           f_bw   desired closed loop current bandwidth [Hz]
 *           Ts     control period [s]
 * Outputs : st->d.kp / ki_ts, st->q.kp / ki_ts
 * ISR safe: yes (idempotent, single writer)
 */
void foc_set_gains(foc_state_t *st, float Rs, float Ls, float f_bw, float Ts);

/*
 * foc_set_omega_e
 * Purpose : Update the electrical angular frequency used by the cross
 *           coupling feed forward terms. Called once per command update.
 * Inputs  : st       controller state
 *           omega_e  electrical angular frequency [rad/s]
 * ISR safe: yes
 */
static inline void foc_set_omega_e(foc_state_t *st, float omega_e)
{
    st->omega_e = omega_e;
}

/*
 * foc_set_voltage_limit
 * Purpose : Set the per-axis voltage limit. Recommended value is
 *           Vdc / sqrt(3) which corresponds to the linear range of
 *           SVPWM.
 * Inputs  : st     controller state
 *           v_lim  voltage limit [V]
 * ISR safe: yes
 */
static inline void foc_set_voltage_limit(foc_state_t *st, float v_lim)
{
    st->v_lim_axis = v_lim;
}

/*
 * foc_step
 * Purpose : Run one iteration of the dq current controllers and return
 *           the d and q voltage commands including the decoupling feed
 *           forward and anti-windup correction.
 *
 *           e_d = i_d_ref - i_d_meas
 *           e_q = i_q_ref - i_q_meas
 *           u_p_x = Kp * e_x
 *           u_i_x[k] = u_i_x[k-1] + Ki * Ts * e_x
 *           v_d_pre = u_p_d + u_i_d - omega_e * Ls * i_q_meas
 *           v_q_pre = u_p_q + u_i_q + omega_e * Ls * i_d_meas
 *           v_x_sat = clamp(v_x_pre, -V_lim, +V_lim)
 *           u_i_x  += (1/Kp) * (v_x_sat - v_x_pre)
 *
 * Inputs  : st       controller state
 *           i_ref    d/q current references [A]
 *           i_meas   d/q measured currents  [A]
 * Outputs : v_out    d/q voltage commands [V]
 * ISR safe: yes
 * Budget  : approx 30 floating point ops.
 */
void foc_step(foc_state_t *st,
              const dq_t  *i_ref,
              const dq_t  *i_meas,
              dq_t        *v_out);

#endif /* FOC_DQ_PI_H_ */
