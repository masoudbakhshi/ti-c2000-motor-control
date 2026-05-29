/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * clarke_park.c
 *
 * Reference implementation of the Clarke and Park transforms used by
 * the synchronous frame current controller. See clarke_park.h for the
 * mathematical definitions.
 */

#include "clarke_park.h"
#include "user_config.h"

void clarke_abc_to_ab(const abc_t *in, ab_t *out)
{
    /* Amplitude invariant Clarke:
     *   alpha = a
     *   beta  = (a + 2 b) / sqrt(3)
     * Using (a + 2 b)/sqrt(3) is preferred over (b - c)/sqrt(3) because
     * it does not require the c sample if balanced load is assumed.
     */
    out->alpha = in->a;
    out->beta  = (in->a + 2.0f * in->b) * ONE_OVER_SQRT3_F;
}

void park_ab_to_dq(const ab_t *in, float sin_th, float cos_th, dq_t *out)
{
    out->d =  in->alpha * cos_th + in->beta * sin_th;
    out->q = -in->alpha * sin_th + in->beta * cos_th;
}

void ipark_dq_to_ab(const dq_t *in, float sin_th, float cos_th, ab_t *out)
{
    out->alpha = in->d * cos_th - in->q * sin_th;
    out->beta  = in->d * sin_th + in->q * cos_th;
}

void iclarke_ab_to_abc(const ab_t *in, abc_t *out)
{
    const float half_sqrt3 = 0.5f * SQRT3_F;
    out->a = in->alpha;
    out->b = -0.5f * in->alpha + half_sqrt3 * in->beta;
    out->c = -0.5f * in->alpha - half_sqrt3 * in->beta;
}
