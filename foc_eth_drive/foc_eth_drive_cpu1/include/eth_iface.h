/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * eth_iface.h
 *
 * Thin wrapper around the lwIP raw API for the two UDP endpoints
 * used by the application. The actual EMAC and lwIP plumbing lives
 * in eth_init.c. On the F28388D the EMAC is connected to the CM
 * (Cortex-M4) core; the project README documents the dual-core
 * deployment recipe. From the application code point of view we hide
 * that detail behind these four functions.
 */

#ifndef ETH_IFACE_H_
#define ETH_IFACE_H_

#include <stdint.h>
#include <stdbool.h>
#include "eth_proto.h"

/*
 * eth_iface_init
 * Purpose : Bring up the EMAC + lwIP, assign the static IP, create
 *           the command RX socket on UDP_PORT_CMD and the telemetry
 *           TX socket targeted at the Pi (the Pi IP is learned from
 *           the first received command packet).
 * Returns : true on success, false otherwise.
 * ISR safe: no
 */
bool eth_iface_init(void);

/*
 * eth_iface_poll
 * Purpose : Service lwIP timers and the EMAC driver. Must be called
 *           from the cooperative super loop at least every few ms.
 * ISR safe: no
 */
void eth_iface_poll(void);

/*
 * eth_iface_get_latest_cmd
 * Purpose : Pop the most recent valid command packet, if any, into the
 *           caller supplied buffer. Older queued packets are dropped
 *           to minimize control latency.
 * Inputs  : out  destination buffer
 * Returns : true if a fresh packet was available, false otherwise.
 * ISR safe: no (called from main loop)
 */
bool eth_iface_get_latest_cmd(eth_cmd_pkt_t *out);

/*
 * eth_iface_send_telemetry
 * Purpose : Transmit a telemetry packet over UDP. CRC is computed by
 *           the caller (so the ISR can fill it before queueing).
 * Inputs  : pkt  pointer to a fully formed telemetry packet
 * Returns : true on successful enqueue, false otherwise.
 * ISR safe: no
 */
bool eth_iface_send_telemetry(const eth_telem_pkt_t *pkt);

#endif /* ETH_IFACE_H_ */
