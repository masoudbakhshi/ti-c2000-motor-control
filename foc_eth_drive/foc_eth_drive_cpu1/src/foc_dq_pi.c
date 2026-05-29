/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * foc_dq_pi.c
 *
 * Synchronous frame current control. The plant is the per-axis
 * dq-frame Kirchhoff model of a balanced star RL load:
 *   v_d = Rs i_d + Ls di_d/dt - omega_e Ls i_q
 *   v_q = Rs i_q + Ls di_q/dt + omega_e Ls i_d
 *
 * The Internal Model Control (IMC) tuning sets Kp = omega_bw * Ls and
 * Ki = omega_bw * Rs which exactly cancels the plant pole and gives a
 * first order closed loop with bandwidth omega_bw. The cross coupling
 * terms are decoupled in a feed forward manner.
 */

#include "foc_dq_pi.h"
#include "user_config.h"
#include "util_fastmath.h"

void foc_init(foc_state_t *st)
{
    st->d.kp     = 1.0f;
    st->d.ki_ts  = 0.0f;
    st->d.integ  = 0.0f;
    st->d.u_sat  = 0.0f;

    st->q.kp     = 1.0f;
    st->q.ki_ts  = 0.0f;
    st->q.integ  = 0.0f;
    st->q.u_sat  = 0.0f;

    st->Ls         = DEFAULT_LS_H;
    st->Rs         = DEFAULT_RS_OHM;
    st->omega_e    = 0.0f;
    st->v_lim_axis = 1.0f;
}

void foc_set_gains(foc_state_t *st, float Rs, float Ls, float f_bw, float Ts)
{
    /* Sanity clamp plant parameters to avoid pathological gains. */
    if (Rs < 1.0e-3f) { Rs = 1.0e-3f; }
    if (Ls < 1.0e-6f) { Ls = 1.0e-6f; }
    if (f_bw < 1.0f)  { f_bw = 1.0f;  }
    if (f_bw > F_BW_MAX_HZ) { f_bw = F_BW_MAX_HZ; }

    float omega_bw = TWO_PI_F * f_bw;
    float kp       = omega_bw * Ls;
    float ki       = omega_bw * Rs;
    float ki_ts    = ki * Ts;

    st->Rs = Rs;
    st->Ls = Ls;

    st->d.kp    = kp;
    st->d.ki_ts = ki_ts;

    st->q.kp    = kp;
    st->q.ki_ts = ki_ts;
}

/*
 * pi_axis_step
 * Single axis PI step with back calculation anti windup. Inlined inside
 * foc_step below. Keeping it as a static inline helper preserves code
 * locality for the ISR while staying readable.
 */
static inline float pi_axis_step(pi_axis_t *ax,
                                 float      err,
                                 float      ff,
                                 float      v_lim)
{
    float u_p   = ax->kp * err;
    float u_i   = ax->integ + ax->ki_ts * err;
    float u_pre = u_p + u_i + ff;
    float u_sat = util_clampf(u_pre, -v_lim, +v_lim);

    /* Back calculation:
     *   u_i_corrected = u_i_pre + (1/Kp) * (u_sat - u_pre)
     * Kp is guaranteed non zero by foc_set_gains (Ls > 0).
     */
    float inv_kp = 1.0f / ax->kp;
    ax->integ    = u_i + inv_kp * (u_sat - u_pre);
    ax->u_sat    = u_sat;
    return u_sat;
}

void foc_step(foc_state_t *st,
              const dq_t  *i_ref,
              const dq_t  *i_meas,
              dq_t        *v_out)
{
    float e_d = i_ref->d - i_meas->d;
    float e_q = i_ref->q - i_meas->q;

    /* Decoupling feed forward (passive RL load, no back EMF):
     *   v_d_ff = - omega_e * Ls * i_q
     *   v_q_ff = + omega_e * Ls * i_d
     */
    float wL    = st->omega_e * st->Ls;
    float ff_d  = -wL * i_meas->q;
    float ff_q  = +wL * i_meas->d;

    v_out->d = pi_axis_step(&st->d, e_d, ff_d, st->v_lim_axis);
    v_out->q = pi_axis_step(&st->q, e_q, ff_q, st->v_lim_axis);
}
