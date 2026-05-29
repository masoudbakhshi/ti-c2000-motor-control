/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * eth_init.c
 *
 * lwIP raw API bring up and EMAC initialization. On the F28388D the
 * EMAC peripheral is wired to the Cortex-M4 (CM) core; this file
 * presents a clean abstraction layer (eth_iface_init / poll / get /
 * send) that maps onto the lwIP API regardless of which core is
 * actually running it. The expected deployment is:
 *
 *   CPU1 : FOC, control, ADC, PWM, telemetry producer
 *   CM   : EMAC + lwIP, command consumer, telemetry consumer
 *   IPC + MSGRAM is used to carry the eth_cmd_pkt_t and eth_telem_pkt_t
 *   between the cores. See docs/CONTROL_DESIGN.md for the layout.
 *
 * To keep this file self contained we provide a small in-RAM mailbox
 * abstraction. The actual IPC marshalling lives in the CM project
 * companion (not part of this CPU1 firmware deliverable but referenced
 * in BUILD_CCS.md).
 */

#include <string.h>
#include "eth_iface.h"
#include "eth_proto.h"
#include "user_config.h"

/* --- In-RAM single-slot command mailbox written by the CM via IPC --- */
typedef struct
{
    volatile uint32_t       valid;     /* 0 = empty, 1 = full */
    eth_cmd_pkt_t           pkt;
} cmd_mailbox_t;

#pragma DATA_SECTION(g_cmd_mailbox, "MSGRAM_CM_TO_CPU")
static cmd_mailbox_t g_cmd_mailbox;

/* --- In-RAM telemetry FIFO. CM polls this and forwards over UDP. ---- */
typedef struct
{
    volatile uint32_t       head;
    volatile uint32_t       tail;
    eth_telem_pkt_t         slot[TELEM_RING_LEN];
} telem_fifo_t;

#pragma DATA_SECTION(g_telem_fifo, "MSGRAM_CPU_TO_CM")
static telem_fifo_t g_telem_fifo;

bool eth_iface_init(void)
{
    /* Zero the IPC mailboxes. The CM-side init routine, running on
     * the Cortex-M4 image, brings up the EMAC and lwIP, configures
     * the static IP from ETH_LOCAL_IP_STR / ETH_NETMASK_STR and binds
     * the UDP sockets. Until that runs the FIFOs simply queue up.
     */
    g_cmd_mailbox.valid = 0u;
    memset((void *)&g_cmd_mailbox.pkt, 0, sizeof(g_cmd_mailbox.pkt));
    g_telem_fifo.head = 0u;
    g_telem_fifo.tail = 0u;
    return true;
}

void eth_iface_poll(void)
{
    /* On CPU1 nothing to do: lwIP runs on the CM core. The IPC
     * watchdog (CM side) catches stalled message rams. We keep this
     * function as a no-op so the super loop in main.c can call it
     * uniformly with the abstract API.
     */
}

bool eth_iface_get_latest_cmd(eth_cmd_pkt_t *out)
{
    if (g_cmd_mailbox.valid == 0u)
    {
        return false;
    }

    *out = g_cmd_mailbox.pkt;
    g_cmd_mailbox.valid = 0u;

    /* CRC has already been validated by the CM companion image before
     * the packet was deposited in the mailbox. CPU1 only checks the
     * magic word to defend against a stale MSGRAM after reset.
     */
    if (out->magic != ETH_CMD_MAGIC)
    {
        return false;
    }
    return true;
}

bool eth_iface_send_telemetry(const eth_telem_pkt_t *pkt)
{
    /* Monotonic producer, mask-only slot index, unconditional overwrite.
     * The CM core tracks its own consumer cursor in CM-local memory
     * (forbidden to write into this MSGRAM bank per the project rule
     * documented in [[feedback-cm-no-msgram-write]]). If CM falls
     * behind by more than TELEM_RING_LEN samples, it discards the
     * stale tail and resumes from the freshest available slot.
     */
    uint32_t head = g_telem_fifo.head;
    g_telem_fifo.slot[head & TELEM_RING_MASK] = *pkt;
    g_telem_fifo.head = head + 1u;
    return true;
}
