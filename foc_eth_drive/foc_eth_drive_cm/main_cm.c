/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * f28388d_gan_foc_eth_cm - Cortex-M4 (CM) companion image for the
 * f28388d_gan_foc_eth FOC drive.
 *
 * The CM core owns the EMAC and lwIP. It bridges the two MSGRAM
 * mailboxes the CPU1 firmware (project f28388d_gan_foc_eth) uses to
 * the wire:
 *
 *   CPU1 writes telemetry into CPU1_TO_CM MSGRAM bank.
 *     -> CM reads from address 0x20080000, computes CRC, sends UDP
 *        to the Pi at 192.168.10.20:5002.
 *   Pi sends command UDP to F28388D at port 5001.
 *     -> CM receives, validates magic + CRC, deposits into the
 *        CM_TO_CPU1 MSGRAM bank at address 0x20082000.
 *
 * Per [[feedback-cm-no-msgram-write]]: CM never writes into the
 * CPU1_TO_CM bank; the consumer tail is held in CM-local memory
 * (s_telem_tail). CM is allowed to write to the CM_TO_CPU1 bank.
 *
 * Network:
 *   CM IP   : 192.168.10.10 / 24
 *   Pi IP   : 192.168.10.20 / 24
 *   UDP RX  : 5001 (commands from Pi)
 *   UDP TX  : src 5002, dst 5002 (telemetry to Pi)
 *
 * Wire format and CRC layer match include/eth_proto.h on the CPU1 side.
 */

#include <string.h>
#include <stdint.h>

#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_nvic.h"
#include "inc/hw_types.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_emac.h"

#include "driverlib_cm/ethernet.h"
#include "driverlib_cm/gpio.h"
#include "driverlib_cm/interrupt.h"
#include "driverlib_cm/flash.h"
#include "driverlib_cm/sysctl.h"
#include "driverlib_cm/systick.h"

#include "utils/lwiplib.h"
#include "driver/device_cm.h"
#include "driver/enet.h"
#include "board_drivers/pinout.h"

#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/timeouts.h"

#include "lwipopts.h"

/* ---- Wire format (must match include/eth_proto.h on CPU1 side) ---- */

#define ETH_CMD_MAGIC       0xC0DE0001u
#define ETH_TELEM_MAGIC     0xFEED0001u

#define ETH_CMD_FLAG_ENABLE       0x0001u
#define ETH_CMD_FLAG_RESET_FAULT  0x0002u

#define ETH_CMD_PKT_SIZE    36u
#define ETH_TELEM_PKT_SIZE  52u

#define TELEM_RING_LEN      32u
#define TELEM_RING_MASK     (TELEM_RING_LEN - 1u)

#pragma pack(push, 1)
typedef struct
{
    uint32_t magic;
    uint32_t seq;
    float    id_ref_A;
    float    iq_ref_A;
    float    fe_ref_Hz;
    float    Rs_ohm;
    float    Ls_H;
    float    f_bw_Hz;
    uint16_t enable_flags;
    uint16_t crc16;
} eth_cmd_pkt_t;

typedef struct
{
    uint32_t magic;
    uint32_t seq;
    uint32_t timestamp_us;
    float    id_ref_A;
    float    id_meas_A;
    float    iq_ref_A;
    float    iq_meas_A;
    float    ia_A;
    float    ib_A;
    float    ic_A;
    float    vdc_V;
    float    theta_e_rad;
    uint16_t status_flags;
    uint16_t crc16;
} eth_telem_pkt_t;

typedef struct
{
    volatile uint32_t valid;          /* 0 = empty, 1 = full */
    eth_cmd_pkt_t     pkt;
} cmd_mailbox_t;

typedef struct
{
    volatile uint32_t head;
    volatile uint32_t tail;
    eth_telem_pkt_t   slot[TELEM_RING_LEN];
} telem_fifo_t;
#pragma pack(pop)

/* ---- MSGRAM bank addresses on the CM side ---- */
#define CPU1_TO_CM_ADDR   0x20080000u   /* CM reads CPU1 telemetry */
#define CM_TO_CPU1_ADDR   0x20082000u   /* CM writes commands */

static volatile const telem_fifo_t * const g_telem =
    (volatile const telem_fifo_t *)CPU1_TO_CM_ADDR;

static volatile cmd_mailbox_t * const g_cmd =
    (volatile cmd_mailbox_t *)CM_TO_CPU1_ADDR;

/* ---- Network configuration ---- */
#define MAKE_IP(a,b,c,d)   ((uint32_t)((a) << 24) | ((uint32_t)((b) & 0xFFu) << 16) | ((uint32_t)((c) & 0xFFu) << 8) | ((uint32_t)((d) & 0xFFu)))

#define CM_IPADDR          MAKE_IP(192u, 168u, 10u, 10u)
#define CM_NETMASK         MAKE_IP(255u, 255u, 255u, 0u)
#define CM_GATEWAY         MAKE_IP(192u, 168u, 10u, 1u)

#define DST_IPADDR         MAKE_IP(192u, 168u, 10u, 20u)
#define UDP_PORT_TELEM     5002u
#define UDP_PORT_CMD       5001u

/* ---- Ethernet ISR signal flags ---- */
#define RX_ISR_BIT         1
#define TX_ISR_BIT         4
#define TMR_ISR_BIT        8
#define RX_ISR_MASK       (1u << RX_ISR_BIT)
#define TX_ISR_MASK       (1u << TX_ISR_BIT)
#define TMR_ISR_MASK      (1u << TMR_ISR_BIT)
static volatile uint32_t g_uiISRsignal = 0;

/* ---- SysTick = 1 ms (CM clock 125 MHz) ---- */
#define SYSTICK_RELOAD     125000u

/* ---- lwIP globals required by the f2838xif / lwiplib ports ---- */
Ethernet_Handle       emac_handle;
Ethernet_InitConfig  *pInitCfg;
extern Ethernet_Device Ethernet_device_struct;

/* ---- UDP sockets ---- */
static struct udp_pcb *s_tx_pcb;
static struct udp_pcb *s_rx_pcb;
static ip_addr_t       s_dst_addr;

/* ---- Consumer state (CM-local, never written to MSGRAM) ---- */
static uint32_t s_telem_tail = 0u;
static uint32_t s_cmd_seen_seq = 0u;
static uint32_t s_t_us = 0u;

/* ---- LED D2 = GPIO34 (negative logic), heartbeat ---- */
#define LED_D2_GPIO        34
static uint32_t s_tx_blink = 0u;

void Ethernet_transmitISRCustom(void);
void Ethernet_receiveISRCustom(void);

/* ---- lwIP <-> EMAC pbuf glue (canonical TI pattern) ---- */
Ethernet_Pkt_Desc *
Ethernet_getPacketBufferCustom(void)
{
    Ethernet_Pkt_Desc *p = lwIP_getFreePacket();
    ENET_DRIVER_STATS_INC(RXgetPacketBuffer);
    return p;
}

Ethernet_Pkt_Desc *
Ethernet_receivePacketCallbackCustom(Ethernet_Handle handleApplication,
                                     Ethernet_Pkt_Desc *pPacket)
{
    Ethernet_Pkt_Desc *r;
    ENET_DRIVER_STATS_INC(RXPacketCallback);
    r = lwIPEthernetIntHandler(pPacket);
    return r;
}

void
Ethernet_releaseTxPacketBufferCustom(Ethernet_Handle handleApplication,
                                     Ethernet_Pkt_Desc *pPacket)
{
    lwIPEthernetIntHandler(pPacket);
    ENET_DRIVER_STATS_INC(TXreleasePacket);
}

/*
 * crc16_ccitt
 * Purpose : CRC-16-CCITT (poly 0x1021, init 0xFFFF, no reflection, no
 *           final XOR). Matches the firmware Pi-side implementation.
 * Inputs  : data  byte buffer
 *           len   byte count
 * Returns : 16-bit CRC.
 */
static uint16_t
crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    uint32_t b;
    for (i = 0u; i < len; ++i)
    {
        crc ^= ((uint16_t)data[i]) << 8;
        for (b = 0u; b < 8u; ++b)
        {
            if (crc & 0x8000u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/*
 * ethernet_init
 * Standard F2838x EMAC bring-up (lifted from the C2000Ware enet_lwip_udp
 * example pattern).
 */
static void
ethernet_init(const unsigned char *mac)
{
    Ethernet_InitInterfaceConfig cfg;
    uint32_t macLower, macHigher;
    uint8_t *temp;

    cfg.ssbase    = EMAC_SS_BASE;
    cfg.enet_base = EMAC_BASE;
    cfg.phyMode   = ETHERNET_SS_PHY_INTF_SEL_MII;

    cfg.ptrPlatformInterruptDisable = &Platform_disableInterrupt;
    cfg.ptrPlatformInterruptEnable  = &Platform_enableInterrupt;
    cfg.ptrPlatformPeripheralEnable = &Platform_enablePeripheral;
    cfg.ptrPlatformPeripheralReset  = &Platform_resetPeripheral;
    cfg.ptrCoreInterruptDisable     = (void(*)(void))&Interrupt_disableInProcessor;
    cfg.ptrCoreInterruptEnable      = (void(*)(void))&Interrupt_enableInProcessor;

    cfg.peripheralNum   = SYSCTL_PERIPH_CLK_ENET;
    cfg.interruptNum[0] = INT_EMAC;
    cfg.interruptNum[1] = INT_EMAC_TX0;
    cfg.interruptNum[2] = INT_EMAC_TX1;
    cfg.interruptNum[3] = INT_EMAC_RX0;
    cfg.interruptNum[4] = INT_EMAC_RX1;

    pInitCfg = Ethernet_initInterface(cfg);
    Ethernet_getInitConfig(pInitCfg);
    pInitCfg->dmaMode.InterruptMode = ETHERNET_DMA_MODE_INTM_MODE2;
    pInitCfg->pfcbRxPacket   = &Ethernet_receivePacketCallbackCustom;
    pInitCfg->pfcbGetPacket  = &Ethernet_getPacketBufferCustom;
    pInitCfg->pfcbFreePacket = &Ethernet_releaseTxPacketBufferCustom;
    pInitCfg->numChannels    = 1U;

    Ethernet_getHandle((Ethernet_Handle)1, pInitCfg, &emac_handle);

    Ethernet_disableDmaInterrupt(Ethernet_device_struct.baseAddresses.enet_base, 0,
                                 (ETHERNET_DMA_CH0_INTERRUPT_ENABLE_TBUE |
                                  ETHERNET_DMA_CH0_INTERRUPT_ENABLE_NIE));
    Ethernet_enableMTLInterrupt(Ethernet_device_struct.baseAddresses.enet_base, 0,
                                ETHERNET_MTL_Q0_INTERRUPT_CONTROL_STATUS_RXOIE);

    HWREG(Ethernet_device_struct.baseAddresses.enet_base + ETHERNET_O_MMC_RX_INTERRUPT_MASK)     = 0xFFFFFFFFu;
    HWREG(Ethernet_device_struct.baseAddresses.enet_base + ETHERNET_O_MMC_IPC_RX_INTERRUPT_MASK) = 0xFFFFFFFFu;
    HWREG(Ethernet_device_struct.baseAddresses.enet_base + ETHERNET_O_MMC_TX_INTERRUPT_MASK)     = 0xFFFFFFFFu;

    Interrupt_enableInProcessor();

    Interrupt_registerHandler(INT_EMAC_TX0, Ethernet_transmitISRCustom);
    Interrupt_registerHandler(INT_EMAC_RX0, Ethernet_receiveISRCustom);
    Interrupt_registerHandler(INT_EMAC,     Ethernet_genericISRCustom);

    temp     = (uint8_t *)&macLower;
    temp[0]  = mac[0]; temp[1] = mac[1]; temp[2] = mac[2]; temp[3] = mac[3];
    temp     = (uint8_t *)&macHigher;
    temp[0]  = mac[4]; temp[1] = mac[5];

    Ethernet_setMACAddr(EMAC_BASE, 0, macHigher, macLower, ETHERNET_CHANNEL_0);

    Ethernet_clearMACConfigurationCustom(Ethernet_device_struct.baseAddresses.enet_base,
                                         ETHERNET_MAC_CONFIGURATION_RE);
    Ethernet_setMACConfigurationCustom(Ethernet_device_struct.baseAddresses.enet_base,
                                       ETHERNET_MAC_CONFIGURATION_RE);
}

/*
 * SysTick handler. 1 ms tick. Drives lwIP timers and our wall clock.
 */
void
SysTickIntHandler(void)
{
    lwIPTimer(1);
    s_t_us += 1000u;
    g_uiISRsignal |= TMR_ISR_MASK;
}

/*
 * drain_telemetry
 * Purpose : Pull every available telemetry sample from the CPU1-side
 *           ring, stamp the CRC, and send it as a UDP datagram. One
 *           datagram per sample (52 bytes wire).
 */
static void
drain_telemetry(void)
{
    uint32_t head = g_telem->head;
    __asm("    DSB");

    /* If we are more than ring-length behind, the producer wrapped. */
    if ((head - s_telem_tail) > TELEM_RING_LEN)
    {
        s_telem_tail = head - TELEM_RING_LEN;
    }

    while (s_telem_tail != head)
    {
        uint32_t slot = s_telem_tail & TELEM_RING_MASK;
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, ETH_TELEM_PKT_SIZE,
                                    PBUF_RAM);
        if (p == NULL)
        {
            break;   /* lwIP out of memory; try again next tick. */
        }

        eth_telem_pkt_t *out = (eth_telem_pkt_t *)p->payload;
        const volatile eth_telem_pkt_t *src = &g_telem->slot[slot];

        out->magic        = src->magic;
        out->seq          = src->seq;
        out->timestamp_us = src->timestamp_us;
        out->id_ref_A     = src->id_ref_A;
        out->id_meas_A    = src->id_meas_A;
        out->iq_ref_A     = src->iq_ref_A;
        out->iq_meas_A    = src->iq_meas_A;
        out->ia_A         = src->ia_A;
        out->ib_A         = src->ib_A;
        out->ic_A         = src->ic_A;
        out->vdc_V        = src->vdc_V;
        out->theta_e_rad  = src->theta_e_rad;
        out->status_flags = src->status_flags;
        out->crc16        = crc16_ccitt((const uint8_t *)out,
                                        ETH_TELEM_PKT_SIZE - 2u);

        udp_sendto(s_tx_pcb, p, &s_dst_addr, UDP_PORT_TELEM);
        pbuf_free(p);

        s_telem_tail++;

        if ((++s_tx_blink % 1000u) == 0u)
        {
            GPIO_togglePin(LED_D2_GPIO);
        }
    }
}

/*
 * udp_rx_cb
 * Purpose : lwIP UDP receive callback for the command port (5001).
 *           Validates length, magic and CRC. On success, copies the
 *           packet into the CM_TO_CPU1 mailbox so CPU1 sees it on
 *           its next super-loop pass.
 * Inputs  : arg (unused), pcb, p (pbuf), addr, port
 */
static void
udp_rx_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
          const ip_addr_t *addr, u16_t port)
{
    (void)arg; (void)pcb; (void)addr; (void)port;

    if (p == NULL)
    {
        return;
    }

    if (p->tot_len == ETH_CMD_PKT_SIZE)
    {
        eth_cmd_pkt_t pkt;
        pbuf_copy_partial(p, &pkt, ETH_CMD_PKT_SIZE, 0);

        if (pkt.magic == ETH_CMD_MAGIC)
        {
            uint16_t expected = crc16_ccitt((const uint8_t *)&pkt,
                                            ETH_CMD_PKT_SIZE - 2u);
            if (expected == pkt.crc16)
            {
                /* Only forward fresh sequence numbers. */
                if (pkt.seq != s_cmd_seen_seq)
                {
                    s_cmd_seen_seq = pkt.seq;
                    g_cmd->valid = 0u;
                    g_cmd->pkt   = pkt;
                    __asm("    DSB");
                    g_cmd->valid = 1u;
                }
            }
        }
    }

    pbuf_free(p);
}

int
main(void)
{
    unsigned char macArray[8];

    Custom_CM_init();

    SYSTICK_setPeriod(SYSTICK_RELOAD);
    SYSTICK_enableCounter();
    SYSTICK_registerInterruptHandler(SysTickIntHandler);
    SYSTICK_enableInterrupt();

    Interrupt_enableInProcessor();

    /* Factory MAC on the TMDSCNCD28388D controlCARD. See memory entry
     * user-controlcard-mac.
     */
    macArray[0] = 0xA8u;
    macArray[1] = 0x63u;
    macArray[2] = 0xF2u;
    macArray[3] = 0x00u;
    macArray[4] = 0x39u;
    macArray[5] = 0x02u;

    lwIPInit(0, macArray, CM_IPADDR, CM_NETMASK, CM_GATEWAY,
             IPADDR_USE_STATIC);
    ethernet_init(macArray);
    lwIPStart(0);

    /* TX socket for telemetry (Pi-side scope is listening on 5002). */
    s_tx_pcb = udp_new();
    if (s_tx_pcb != NULL)
    {
        udp_bind(s_tx_pcb, IP_ADDR_ANY, UDP_PORT_TELEM);
    }
    s_dst_addr.addr = lwip_htonl(DST_IPADDR);

    /* RX socket for commands from the Pi (port 5001). */
    s_rx_pcb = udp_new();
    if (s_rx_pcb != NULL)
    {
        udp_bind(s_rx_pcb, IP_ADDR_ANY, UDP_PORT_CMD);
        udp_recv(s_rx_pcb, udp_rx_cb, NULL);
    }

    /* No IPC_sync with CPU1; see [[project-ipc-sync-deadlock]]. CPU1
     * stamps the telemetry ring before we ever look at it.
     */
    IPC_clearFlagLtoR(IPC_CM_L_CPU1_R, IPC_FLAG_ALL);

    Interrupt_setPriority(INT_EMAC_TX0, 2);
    Interrupt_setPriority(INT_EMAC_RX0, 1);
    Interrupt_enable(INT_EMAC_TX0);
    Interrupt_enable(INT_EMAC_RX0);
    Interrupt_enable(INT_EMAC);

    GPIO_writePin(LED_D2_GPIO, 1);

    for (;;)
    {
        uint32_t s = g_uiISRsignal;

        if ((s & RX_ISR_MASK) != 0u)
        {
            Ethernet_removePacketsFromRxQueue(
                (Ethernet_DescCh *)&Ethernet_device_struct.dmaObj.rxDma[ETHERNET_DMA_CHANNEL_NUM_0],
                ETHERNET_COMPLETION_NORMAL);
            ETHERNET_DISABLE_INTERRUPTS();
            g_uiISRsignal &= (uint32_t)~RX_ISR_MASK;
            ETHERNET_ENABLE_INTERRUPTS();
        }

        if ((s & TX_ISR_MASK) != 0u)
        {
            Ethernet_removePacketsFromTxQueue(
                (Ethernet_DescCh *)&Ethernet_device_struct.dmaObj.txDma[ETHERNET_DMA_CHANNEL_NUM_0],
                ETHERNET_COMPLETION_NORMAL);
            ETHERNET_DISABLE_INTERRUPTS();
            g_uiISRsignal &= (uint32_t)~TX_ISR_MASK;
            ETHERNET_ENABLE_INTERRUPTS();
        }

        if ((s & TMR_ISR_MASK) != 0u)
        {
            sys_check_timeouts();
            drain_telemetry();
            ETHERNET_DISABLE_INTERRUPTS();
            g_uiISRsignal &= (uint32_t)~TMR_ISR_MASK;
            ETHERNET_ENABLE_INTERRUPTS();
        }
    }
}

void
Ethernet_transmitISRCustom(void)
{
    ENET_DRIVER_STATS_INC(TXinterrupt);
    Ethernet_clearDMAChannelInterrupt(Ethernet_device_struct.baseAddresses.enet_base,
                                      ETHERNET_DMA_CHANNEL_NUM_0,
                                      ETHERNET_DMA_CH0_STATUS_TI);
    g_uiISRsignal |= TX_ISR_MASK;
}

void
Ethernet_receiveISRCustom(void)
{
    ENET_DRIVER_STATS_INC(RXinterrupt);
    Ethernet_clearDMAChannelInterrupt(Ethernet_device_struct.baseAddresses.enet_base,
                                      ETHERNET_DMA_CHANNEL_NUM_0,
                                      ETHERNET_DMA_CH0_STATUS_RI);
    g_uiISRsignal |= RX_ISR_MASK;
}

/*
 * lwIPHostTimerHandler
 * Required by lwiplib.c even when unused.
 */
void
lwIPHostTimerHandler(void)
{
}
