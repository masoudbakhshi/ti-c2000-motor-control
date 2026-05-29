/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * util_fastmath.h
 *
 * Small set of inline math helpers used throughout the ISR. Kept in a
 * header to allow the compiler to inline them inside the EOC ISR
 * without an extra function call.
 */

#ifndef UTIL_FASTMATH_H_
#define UTIL_FASTMATH_H_

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "user_config.h"

/*
 * util_clampf
 * Purpose : Saturate a float to a closed interval.
 * Inputs  : x   value to saturate
 *           lo  lower bound
 *           hi  upper bound
 * Returns : saturated value in [lo, hi]
 * Units   : caller defined
 * ISR safe: yes
 */
static inline float util_clampf(float x, float lo, float hi)
{
    if (x > hi) { return hi; }
    if (x < lo) { return lo; }
    return x;
}

/*
 * util_wrap_2pi
 * Purpose : Wrap an angle to [0, 2*pi).
 * Inputs  : a  angle in radians (any value, expected within a few revs)
 * Returns : wrapped angle in [0, 2*pi) radians
 * ISR safe: yes
 * Note    : uses subtraction only; valid for inputs within +/- a few
 *           revolutions per call (which is always the case in the ISR
 *           because the increment per ISR is 2*pi*fe*Ts).
 */
static inline float util_wrap_2pi(float a)
{
    while (a >= TWO_PI_F) { a -= TWO_PI_F; }
    while (a <  0.0f)     { a += TWO_PI_F; }
    return a;
}

/*
 * util_max3 / util_min3
 * Purpose : Branch-free min / max of three floats. Used by min/max SVPWM.
 * Inputs  : a, b, c
 * Returns : max or min of {a, b, c}
 * ISR safe: yes
 */
static inline float util_max3(float a, float b, float c)
{
    float m = (a > b) ? a : b;
    return (m > c) ? m : c;
}

static inline float util_min3(float a, float b, float c)
{
    float m = (a < b) ? a : b;
    return (m < c) ? m : c;
}

/*
 * util_sincosf
 * Purpose : Compute sin and cos of a single argument. Lets the compiler
 *           emit a fused C28x FPU sequence.
 * Inputs  : x      angle in radians
 *           s_out  pointer to sin result
 *           c_out  pointer to cos result
 * ISR safe: yes
 */
static inline void util_sincosf(float x, float *s_out, float *c_out)
{
    *s_out = sinf(x);
    *c_out = cosf(x);
}

#endif /* UTIL_FASTMATH_H_ */
