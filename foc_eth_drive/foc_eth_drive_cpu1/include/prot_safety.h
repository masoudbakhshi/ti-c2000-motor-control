/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * prot_safety.h
 *
 * Software protection layer. Hardware trip zones on the ePWM modules
 * provide first line of defence; this software layer runs in the ISR
 * to also catch sustained overcurrent / over- and undervoltage and to
 * mute the inverter when the Ethernet command stream stops.
 */

#ifndef PROT_SAFETY_H_
#define PROT_SAFETY_H_

#include <stdint.h>
#include <stdbool.h>

/* Status flag bits used in the telemetry packet. */
#define PROT_FLAG_ENABLED   (1u << 0)
#define PROT_FLAG_OC_TRIP   (1u << 1)
#define PROT_FLAG_OV_TRIP   (1u << 2)
#define PROT_FLAG_UV        (1u << 3)
#define PROT_FLAG_ETH_WDT   (1u << 4)
#define PROT_FLAG_CALIB     (1u << 5)

/* Aggregated, latched protection state. */
typedef struct
{
    uint16_t flags;
    uint32_t eth_wdt_counter;   /* ISR ticks since last valid command */
    uint32_t eth_wdt_limit;     /* trip threshold (ISR ticks)         */
    bool     user_enable;
    bool     calib_done;
} prot_state_t;

/*
 * prot_init
 * Purpose : Initialize the protection state. Defaults to disabled,
 *           Ethernet watchdog armed but not tripped, calibration not
 *           done.
 * Inputs  : st  protection state to reset
 *           eth_wdt_limit_ticks  number of ISR ticks (20 kHz) that
 *                                may elapse without a command before
 *                                tripping the Ethernet watchdog
 * ISR safe: yes (call outside the ISR before enabling interrupts)
 */
void prot_init(prot_state_t *st, uint32_t eth_wdt_limit_ticks);

/*
 * prot_feed_eth_watchdog
 * Purpose : Mark that a valid command packet has just been received.
 *           Clears the eth watchdog timer.
 * ISR safe: yes
 */
void prot_feed_eth_watchdog(prot_state_t *st);

/*
 * prot_tick
 * Purpose : Per-ISR safety evaluation. Returns true when PWM should be
 *           allowed to drive the gates, false otherwise. Updates flags.
 *
 *           Decisions:
 *             |Ia|, |Ib|, |Ic| > I_TRIP_A   -> latch OC, gate off
 *             Vdc > VDC_TRIP_V              -> latch OV, gate off
 *             Vdc < VDC_UVLO_V              -> set UV (non latched), gate off
 *             eth_wdt_counter > limit       -> set ETH_WDT, gate off
 *             user_enable false             -> gate off (no latch)
 *
 * Inputs  : st       protection state
 *           ia, ib, ic, vdc  measured quantities [A, A, A, V]
 * Returns : true  -> drive PWM
 *           false -> all duty values must be set to 50 percent (zero
 *                    differential voltage) and the gate output must be
 *                    forced via TZ if hardware trip is required.
 * ISR safe: yes
 * Budget  : approx 20 floating point compares.
 */
bool prot_tick(prot_state_t *st,
               float ia, float ib, float ic, float vdc);

/*
 * prot_clear_latched
 * Purpose : Clear the latched OC and OV flags. Called from the command
 *           parser when reset_faults is requested by the host. Does not
 *           re enable the inverter; user must also assert the enable bit.
 * ISR safe: yes
 */
void prot_clear_latched(prot_state_t *st);

#endif /* PROT_SAFETY_H_ */
