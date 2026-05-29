/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * prot_safety.c
 *
 * Implementation of the software protection layer described in
 * prot_safety.h.
 */

#include "prot_safety.h"
#include "user_config.h"

static inline float absf_local(float x) { return (x < 0.0f) ? -x : x; }

void prot_init(prot_state_t *st, uint32_t eth_wdt_limit_ticks)
{
    st->flags           = 0u;
    st->eth_wdt_counter = 0u;
    st->eth_wdt_limit   = eth_wdt_limit_ticks;
    st->user_enable     = false;
    st->calib_done      = false;
}

void prot_feed_eth_watchdog(prot_state_t *st)
{
    st->eth_wdt_counter = 0u;
    st->flags &= (uint16_t)~PROT_FLAG_ETH_WDT;
}

bool prot_tick(prot_state_t *st,
               float ia, float ib, float ic, float vdc)
{
    bool gate_ok = true;

    /* --- Overcurrent (latched) ---------------------------------------- */
    if ((absf_local(ia) > I_TRIP_A) ||
        (absf_local(ib) > I_TRIP_A) ||
        (absf_local(ic) > I_TRIP_A))
    {
        st->flags |= PROT_FLAG_OC_TRIP;
    }

    /* --- Overvoltage (latched) ---------------------------------------- */
    if (vdc > VDC_TRIP_V)
    {
        st->flags |= PROT_FLAG_OV_TRIP;
    }

    /* --- Undervoltage (transient) ------------------------------------- */
    if (vdc < VDC_UVLO_V)
    {
        st->flags |= PROT_FLAG_UV;
    }
    else
    {
        st->flags &= (uint16_t)~PROT_FLAG_UV;
    }

    /* --- Ethernet watchdog (transient) -------------------------------- */
    st->eth_wdt_counter++;
    if (st->eth_wdt_counter > st->eth_wdt_limit)
    {
        st->flags |= PROT_FLAG_ETH_WDT;
    }

    /* --- Calibration latch -------------------------------------------- */
    if (st->calib_done)
    {
        st->flags |= PROT_FLAG_CALIB;
    }
    else
    {
        st->flags &= (uint16_t)~PROT_FLAG_CALIB;
    }

    /* --- Decide whether to drive gates -------------------------------- */
    if (st->flags & (PROT_FLAG_OC_TRIP | PROT_FLAG_OV_TRIP |
                     PROT_FLAG_UV     | PROT_FLAG_ETH_WDT))
    {
        gate_ok = false;
    }

    if (!st->user_enable || !st->calib_done)
    {
        gate_ok = false;
    }

    if (gate_ok)
    {
        st->flags |= PROT_FLAG_ENABLED;
    }
    else
    {
        st->flags &= (uint16_t)~PROT_FLAG_ENABLED;
    }

    return gate_ok;
}

void prot_clear_latched(prot_state_t *st)
{
    st->flags &= (uint16_t)~(PROT_FLAG_OC_TRIP | PROT_FLAG_OV_TRIP);
    st->eth_wdt_counter = 0u;
    st->flags &= (uint16_t)~PROT_FLAG_ETH_WDT;
}
