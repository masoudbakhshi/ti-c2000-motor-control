/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * eth_tx_telemetry.c
 *
 * Telemetry pump. Drains the SPSC ring filled by the control ISR and
 * hands packets to the eth_iface abstraction for delivery. Called
 * from the main super loop at the highest sustainable rate.
 */

#include <stdint.h>
#include "user_config.h"
#include "eth_iface.h"
#include "eth_proto.h"

extern volatile uint32_t g_ring_head;
extern volatile uint32_t g_ring_tail;
extern eth_telem_pkt_t   g_ring[TELEM_RING_LEN];

/*
 * eth_tx_telemetry_service
 * Purpose : Drain up to max_per_call telemetry samples from the SPSC
 *           ring. Returns the number of samples actually transmitted.
 *           Capping per call prevents the loop from starving command
 *           reception or the lwIP poll.
 * Inputs  : max_per_call  cap on packets pulled in this invocation
 * Returns : number of packets forwarded.
 * ISR safe: no (main loop only, single consumer).
 * Budget  : approx 4 us per packet.
 */
uint32_t eth_tx_telemetry_service(uint32_t max_per_call)
{
    uint32_t sent = 0u;
    while ((g_ring_tail != g_ring_head) && (sent < max_per_call))
    {
        eth_telem_pkt_t *p = &g_ring[g_ring_tail];
        if (!eth_iface_send_telemetry(p))
        {
            /* Downstream FIFO is full. Try again later, keep producer
             * pointer untouched.
             */
            break;
        }
        g_ring_tail = (g_ring_tail + 1u) & TELEM_RING_MASK;
        sent++;
    }
    return sent;
}
