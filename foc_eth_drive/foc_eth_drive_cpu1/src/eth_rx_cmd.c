/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * eth_rx_cmd.c
 *
 * Command dispatcher. Called from the main super loop. Pops the most
 * recent command packet via the eth_iface abstraction, validates it,
 * applies the new references and gains, feeds the Ethernet watchdog
 * and toggles user_enable / reset_faults.
 *
 * This file does not call lwIP directly; the actual socket lives on
 * the CM core (see eth_init.c).
 */

#include <stdbool.h>
#include "eth_iface.h"
#include "eth_proto.h"
#include "foc_dq_pi.h"
#include "prot_safety.h"
#include "pwm_iface.h"
#include "user_config.h"

extern volatile float g_id_ref_A;
extern volatile float g_iq_ref_A;
extern volatile float g_fe_ref_Hz;
extern volatile bool  g_user_enable;
extern foc_state_t    g_foc;
extern prot_state_t   g_prot;

/*
 * eth_rx_cmd_service
 * Purpose : Dispatch the most recent command packet, if any. Returns
 *           true when a packet was consumed.
 * Inputs  : none
 * Outputs : updates global setpoints and protection state.
 * Units   : matches the packet wire format.
 * ISR safe: no (main loop only).
 * Budget  : approx 2 us per invocation.
 */
bool eth_rx_cmd_service(void)
{
    eth_cmd_pkt_t cmd;
    if (!eth_iface_get_latest_cmd(&cmd))
    {
        return false;
    }

    /* Apply references atomically. Writes to single 32-bit floats are
     * atomic on the C28x so no critical section is required.
     */
    g_id_ref_A  = cmd.id_ref_A;
    g_iq_ref_A  = cmd.iq_ref_A;
    g_fe_ref_Hz = cmd.fe_ref_Hz;

    /* Tune the controller for the freshly reported plant. f_bw is
     * internally capped by foc_set_gains.
     */
    foc_set_gains(&g_foc, cmd.Rs_ohm, cmd.Ls_H, cmd.f_bw_Hz, TS_CTRL_S);

    /* User enable / disable. enable_flags carries the control bits. */
    g_user_enable      = ((cmd.enable_flags & ETH_CMD_FLAG_ENABLE) != 0u);
    g_prot.user_enable = g_user_enable;

    if ((cmd.enable_flags & ETH_CMD_FLAG_RESET_FAULT) != 0u)
    {
        prot_clear_latched(&g_prot);
        pwm_iface_enable_outputs();
    }

    /* Feed the watchdog last so any rejected packet does not reset it. */
    prot_feed_eth_watchdog(&g_prot);
    return true;
}
