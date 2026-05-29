/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * clarke_park.h
 *
 * Clarke and Park (forward and inverse) transforms used by the FOC
 * control loop. Amplitude-invariant Clarke is chosen so that the
 * alpha/beta components have the same peak as the original phase
 * currents. This matches the convention used by the TI MotorControl
 * SDK and gives directly interpretable Volt-per-phase values out of
 * the inverse Park.
 */

#ifndef CLARKE_PARK_H_
#define CLARKE_PARK_H_

#include <stdint.h>

/* Three phase quantity. Units carried by caller. */
typedef struct
{
    float a;
    float b;
    float c;
} abc_t;

/* Two phase orthogonal quantity (stator frame). */
typedef struct
{
    float alpha;
    float beta;
} ab_t;

/* Two phase synchronous (rotor) frame. */
typedef struct
{
    float d;
    float q;
} dq_t;

/*
 * clarke_abc_to_ab
 * Purpose : Amplitude-invariant Clarke transform.
 *           alpha = a
 *           beta  = (a + 2 b) / sqrt(3)
 *           which is equivalent to (b - c) / sqrt(3) when a + b + c = 0.
 * Inputs  : in   three phase quantity in any consistent unit
 * Outputs : out  two phase alpha/beta in the same unit
 * ISR safe: yes
 * Budget  : approx 5 floating point ops.
 */
void clarke_abc_to_ab(const abc_t *in, ab_t *out);

/*
 * park_ab_to_dq
 * Purpose : Forward Park transform from stator alpha/beta to rotor d/q.
 *           d =  alpha * cos(th) + beta * sin(th)
 *           q = -alpha * sin(th) + beta * cos(th)
 * Inputs  : in       alpha/beta input
 *           sin_th   pre-computed sin of electrical angle
 *           cos_th   pre-computed cos of electrical angle
 * Outputs : out      d/q output
 * ISR safe: yes
 * Budget  : 4 multiplies, 2 add/sub.
 */
void park_ab_to_dq(const ab_t *in, float sin_th, float cos_th, dq_t *out);

/*
 * ipark_dq_to_ab
 * Purpose : Inverse Park transform from rotor d/q back to stator alpha/beta.
 *           alpha = d * cos(th) - q * sin(th)
 *           beta  = d * sin(th) + q * cos(th)
 * Inputs  : in       d/q input
 *           sin_th   pre-computed sin of electrical angle
 *           cos_th   pre-computed cos of electrical angle
 * Outputs : out      alpha/beta output
 * ISR safe: yes
 */
void ipark_dq_to_ab(const dq_t *in, float sin_th, float cos_th, ab_t *out);

/*
 * iclarke_ab_to_abc
 * Purpose : Inverse Clarke transform.
 *           a = alpha
 *           b = -0.5 * alpha + 0.5 * sqrt(3) * beta
 *           c = -0.5 * alpha - 0.5 * sqrt(3) * beta
 * Inputs  : in   alpha/beta input
 * Outputs : out  three phase output
 * ISR safe: yes
 */
void iclarke_ab_to_abc(const ab_t *in, abc_t *out);

#endif /* CLARKE_PARK_H_ */
