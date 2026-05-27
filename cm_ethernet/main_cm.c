/* Author: Masoud Bakhshi - www.plan22.net */

/*
 * cm_ethernet - F28388D Cortex-M4 (CM) lwIP UDP transmitter.
 *
 * Reads ADCINA0 samples produced by CPU1 from a shared ring buffer
 * (CPU1_TO_CM MSGRAM at CM address 0x20080000), batches 50 floats per
 * UDP packet, and ships them to the Raspberry Pi receiver at
 * 192.168.10.20:5005.
 *
 * Steady-state output: 200 packets/sec @ 224 B per packet (~358 kbit/s).
 *
 * Network static configuration:
 *     CM IP    : 192.168.10.10 / 24
 *     Gateway  : 192.168.10.1   (unused, set for completeness)
 *     MAC      : A8:63:F2:00:39:02   (factory-allocated TI OUI on this card)
 *     UDP src  : 5006
 *     UDP dst  : 192.168.10.20:5005
 *
 * Hard rule:  CM never writes into the CPU1_TO_CM MSGRAM bank.
 *             Writing to that bank from CM raises a Cortex-M
 *             IMPRECISERR bus fault.  The CM-side consumer index is
 *             held in the file-scope variable s_consumer_seq below.
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

/*
 * Shared block layout - must match CPU1's main_cpu1.c byte for byte.
 *
 * On CM the bank starts at 0x20080000 (CPU1TOCMMSGRAM0).
 * CM treats the entire region as read-only; the const qualifier is
 * intentional and catches accidental writes at compile time.
 */
#define ADC_SHARED_MAGIC      0xC2000ADCu
#define ADC_RING_SIZE         128u
#define ADC_RING_MASK         (ADC_RING_SIZE - 1u)
#define ADC_NOTIFY_COUNT      50u

struct adc_shared_block
{
    uint32_t magic;
    uint32_t producer_seq;
    uint32_t sample_rate_hz;
    uint32_t ring_size;
    float    ring[ADC_RING_SIZE];
};

#define ADC_SHARED_ADDR       0x20080000u
static volatile const struct adc_shared_block * const g_shared =
    (volatile const struct adc_shared_block *) ADC_SHARED_ADDR;

/*
 * UDP wire packet.  Little-endian, 224 bytes for SAMPLES_PER_PACKET=50.
 *
 * struct layout (packed by hand - Cortex-M4 byte-addressable, naturally
 * aligned for every field given this ordering):
 *     +0   uint32  magic         = 0xC2000ADC
 *     +4   uint16  version       = 1
 *     +6   uint16  sample_count  = 50
 *     +8   uint32  seq           = monotonic per-packet counter
 *     +12  uint64  t_us          = CM time-since-boot in microseconds
 *     +20  uint32  sample_rate   = 10000
 *     +24  float[50] samples
 */
#define WIRE_MAGIC              0xC2000ADCu
#define WIRE_VERSION            1u
#define SAMPLES_PER_PACKET      50u
#define WIRE_HEADER_BYTES       24u
#define WIRE_PAYLOAD_BYTES      (WIRE_HEADER_BYTES + (SAMPLES_PER_PACKET * 4u))

/* ---- Network configuration ---- */
#define MAKE_IP(a,b,c,d)   ((uint32_t)((a) << 24) | ((uint32_t)((b) & 0xFFu) << 16) | ((uint32_t)((c) & 0xFFu) << 8) | ((uint32_t)((d) & 0xFFu)))

#define CM_IPADDR          MAKE_IP(192u, 168u, 10u, 10u)
#define CM_NETMASK         MAKE_IP(255u, 255u, 255u, 0u)
#define CM_GATEWAY         MAKE_IP(192u, 168u, 10u, 1u)

#define DST_IPADDR         MAKE_IP(192u, 168u, 10u, 20u)
#define DST_PORT           5005u
#define SRC_PORT           5006u

/* ---- Ethernet ISR signal flags (set by ISRs, drained in main loop) ---- */
#define RX_ISR_BIT          1
#define TX_ISR_BIT          4
#define TMR_ISR_BIT         8
#define RX_ISR_MASK        (1u << RX_ISR_BIT)
#define TX_ISR_MASK        (1u << TX_ISR_BIT)
#define TMR_ISR_MASK       (1u << TMR_ISR_BIT)

static volatile uint32_t g_uiISRsignal = 0;

/* ---- SysTick = 1 ms.  Source clock is CM core clock (125 MHz). ---- */
#define SYSTICK_RELOAD     125000u

/* ---- Lwip globals required by the f2838xif/lwiplib ports ---- */
Ethernet_Handle       emac_handle;
Ethernet_InitConfig  *pInitCfg;
extern Ethernet_Device Ethernet_device_struct;

/* ---- Outbound UDP socket ---- */
static struct udp_pcb *s_udp_pcb;
static ip_addr_t       s_dst_addr;

/* ---- Producer/consumer state (CM-LOCAL ONLY; never touch MSGRAM) ---- */
static uint32_t s_consumer_seq = 0;   /* last sample seq we have packetized */
static uint32_t s_packet_seq   = 0;   /* monotonic packet counter */
static uint64_t s_t_us         = 0;   /* CM wall clock, 1 us resolution */

/* ---- Pending samples staged for the next packet ---- */
static float    s_pending[SAMPLES_PER_PACKET];
static uint16_t s_pending_count = 0;

/* ---- LED D2 = GPIO34 (negative logic). ---- */
#define LED_D2_GPIO         34
static uint32_t s_tx_blink_div = 0;

/* ---- Forward declarations of Ethernet ISRs ---- */
void Ethernet_transmitISRCustom(void);
void Ethernet_receiveISRCustom(void);

/*
 * lwIP -> EMAC packet-buffer plumbing.  These callbacks are required
 * by C2000Ware's Ethernet driver and are copied verbatim from the
 * canonical enet_lwip_udp example (the only safe way to wire pbufs
 * into the F2838x DMA descriptors).
 */
Ethernet_Pkt_Desc *
Ethernet_getPacketBufferCustom(void)
{
    Ethernet_Pkt_Desc *pktPtr = lwIP_getFreePacket();
    ENET_DRIVER_STATS_INC(RXgetPacketBuffer);
    return pktPtr;
}

Ethernet_Pkt_Desc *
Ethernet_receivePacketCallbackCustom(Ethernet_Handle handleApplication,
                                     Ethernet_Pkt_Desc *pPacket)
{
    Ethernet_Pkt_Desc *temp_eth_pkt;
    ENET_DRIVER_STATS_INC(RXPacketCallback);
    temp_eth_pkt = lwIPEthernetIntHandler(pPacket);
    return temp_eth_pkt;
}

void
Ethernet_releaseTxPacketBufferCustom(Ethernet_Handle handleApplication,
                                     Ethernet_Pkt_Desc *pPacket)
{
    lwIPEthernetIntHandler(pPacket);
    ENET_DRIVER_STATS_INC(TXreleasePacket);
}

static void
ethernet_init(const unsigned char *mac)
{
    Ethernet_InitInterfaceConfig initInterfaceConfig;
    uint32_t macLower, macHigher;
    uint8_t *temp;

    initInterfaceConfig.ssbase    = EMAC_SS_BASE;
    initInterfaceConfig.enet_base = EMAC_BASE;
    initInterfaceConfig.phyMode   = ETHERNET_SS_PHY_INTF_SEL_MII;

    initInterfaceConfig.ptrPlatformInterruptDisable = &Platform_disableInterrupt;
    initInterfaceConfig.ptrPlatformInterruptEnable  = &Platform_enableInterrupt;
    initInterfaceConfig.ptrPlatformPeripheralEnable = &Platform_enablePeripheral;
    initInterfaceConfig.ptrPlatformPeripheralReset  = &Platform_resetPeripheral;
    /*
     * Cast the bool-returning processor enable/disable helpers to the
     * void(*)(void) callback shape expected by Ethernet_InitInterfaceConfig.
     * The return value is unused by the EMAC driver, so dropping it is safe.
     */
    initInterfaceConfig.ptrCoreInterruptDisable     = (void(*)(void))&Interrupt_disableInProcessor;
    initInterfaceConfig.ptrCoreInterruptEnable      = (void(*)(void))&Interrupt_enableInProcessor;

    initInterfaceConfig.peripheralNum   = SYSCTL_PERIPH_CLK_ENET;
    initInterfaceConfig.interruptNum[0] = INT_EMAC;
    initInterfaceConfig.interruptNum[1] = INT_EMAC_TX0;
    initInterfaceConfig.interruptNum[2] = INT_EMAC_TX1;
    initInterfaceConfig.interruptNum[3] = INT_EMAC_RX0;
    initInterfaceConfig.interruptNum[4] = INT_EMAC_RX1;

    pInitCfg = Ethernet_initInterface(initInterfaceConfig);
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

    /* MMC counters fire periodically; mask them - we never read MMC RX/TX. */
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
 * SysTick handler - drives both lwIP timers and the CM wall clock.
 */
void
SysTickIntHandler(void)
{
    lwIPTimer(1);
    s_t_us += 1000u;
    g_uiISRsignal |= TMR_ISR_MASK;
}

/*
 * Pack one wire packet starting at 'buf' and update s_packet_seq.
 * Returns the byte count written (always WIRE_PAYLOAD_BYTES).
 */
static uint16_t
build_packet(uint8_t *buf, const float *samples, uint16_t count)
{
    uint32_t magic       = WIRE_MAGIC;
    uint16_t version     = WIRE_VERSION;
    uint16_t sample_cnt  = count;
    uint32_t seq         = ++s_packet_seq;
    uint64_t t_us        = s_t_us;
    uint32_t sample_rate = g_shared->sample_rate_hz;

    memcpy(buf +  0, &magic,       4);
    memcpy(buf +  4, &version,     2);
    memcpy(buf +  6, &sample_cnt,  2);
    memcpy(buf +  8, &seq,         4);
    memcpy(buf + 12, &t_us,        8);
    memcpy(buf + 20, &sample_rate, 4);
    memcpy(buf + 24, samples,      count * 4u);

    return (uint16_t)(WIRE_HEADER_BYTES + count * 4u);
}

/*
 * Drain new samples from the shared ring into s_pending.  When
 * s_pending fills to SAMPLES_PER_PACKET, ship it as a UDP datagram.
 */
static void
drain_and_send(void)
{
    /* Reject inconsistent shared block (CPU1 not up yet or memory clobber). */
    if(g_shared->magic != ADC_SHARED_MAGIC)
    {
        return;
    }

    uint32_t producer = g_shared->producer_seq;

    /*
     * Data-synchronization barrier: ensure the ring[] reads below
     * cannot be reordered before the producer_seq read above.  CPU1
     * publishes a sample by writing ring[slot] *then* incrementing
     * producer_seq; on the CM (Cortex-M4) side we mirror that ordering
     * with a __DSB() so we never observe a producer_seq advance with
     * a stale ring slot.
     */
    __asm(" DSB");

    /*
     * If we have fallen behind by more than the ring depth, drop the
     * tail - the producer has wrapped around us.  This is normal
     * during boot-up before the consumer catches the first window.
     */
    if((producer - s_consumer_seq) > ADC_RING_SIZE)
    {
        s_consumer_seq  = producer - ADC_RING_SIZE;
        s_pending_count = 0;
    }

    while(s_consumer_seq != producer)
    {
        uint32_t slot = s_consumer_seq & ADC_RING_MASK;
        s_pending[s_pending_count++] = g_shared->ring[slot];
        s_consumer_seq++;

        if(s_pending_count >= SAMPLES_PER_PACKET)
        {
            struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT,
                                        WIRE_PAYLOAD_BYTES,
                                        PBUF_RAM);
            if(p != NULL)
            {
                build_packet((uint8_t *)p->payload,
                             s_pending,
                             SAMPLES_PER_PACKET);
                udp_sendto(s_udp_pcb, p, &s_dst_addr, DST_PORT);
                pbuf_free(p);

                /* Heartbeat blink: D2 toggles every 50 packets (~4 Hz). */
                if((++s_tx_blink_div % 50u) == 0u)
                {
                    GPIO_togglePin(LED_D2_GPIO);
                }
            }
            s_pending_count = 0;
        }
    }
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

    /*
     * Static MAC.  TI OUI 0xA8-0x63-0xF2 + per-card tail 0x00-0x39-0x02
     * matches the factory MAC printed on the TMDSCNCD28388D controlCARD.
     * If you swap controlCARDs, update the tail bytes (or read the MAC
     * from device unique-ID OTP - the F2838x stores it at 0x00070200).
     */
    macArray[0] = 0xA8u;
    macArray[1] = 0x63u;
    macArray[2] = 0xF2u;
    macArray[3] = 0x00u;
    macArray[4] = 0x39u;
    macArray[5] = 0x02u;

    /*
     * Bring up lwIP with a static IP.  The IP-address argument to
     * lwIPInit() is big-endian (network byte order); MAKE_IP() packs
     * the octets in that order already.
     */
    lwIPInit(0, macArray, CM_IPADDR, CM_NETMASK, CM_GATEWAY, IPADDR_USE_STATIC);

    ethernet_init(macArray);
    lwIPStart(0);

    /*
     * Open a UDP socket bound to SRC_PORT and prep the destination
     * address.  We use udp_sendto() each time so the socket is
     * connectionless - no need to udp_connect() / udp_disconnect().
     */
    s_udp_pcb = udp_new();
    if(s_udp_pcb != NULL)
    {
        udp_bind(s_udp_pcb, IP_ADDR_ANY, SRC_PORT);
    }
    s_dst_addr.addr = lwip_htonl(DST_IPADDR);

    /*
     * IPC handshake with CPU1.  CPU1 has populated g_shared and is
     * spinning on IPC_FLAG31; pulse it back so CPU1 enables the ADC.
     * The IPC_synchronize call must use IPC_FLAG31 as agreed by both
     * sides.  On CM the function is IPC_sync (same name as C28x).
     */
    IPC_clearFlagLtoR(IPC_CM_L_CPU1_R, IPC_FLAG_ALL);
    IPC_sync(IPC_CM_L_CPU1_R, IPC_FLAG31);

    Interrupt_setPriority(INT_EMAC_TX0, 2);
    Interrupt_setPriority(INT_EMAC_RX0, 1);
    Interrupt_enable(INT_EMAC_TX0);
    Interrupt_enable(INT_EMAC_RX0);
    Interrupt_enable(INT_EMAC);

    GPIO_writePin(LED_D2_GPIO, 1);  /* Off (negative logic). */

    for(;;)
    {
        uint32_t s = g_uiISRsignal;

        if((s & RX_ISR_MASK) != 0u)
        {
            Ethernet_removePacketsFromRxQueue(
                (Ethernet_DescCh *)&Ethernet_device_struct.dmaObj.rxDma[ETHERNET_DMA_CHANNEL_NUM_0],
                ETHERNET_COMPLETION_NORMAL);
            ETHERNET_DISABLE_INTERRUPTS();
            g_uiISRsignal &= (uint32_t)~RX_ISR_MASK;
            ETHERNET_ENABLE_INTERRUPTS();
        }

        if((s & TX_ISR_MASK) != 0u)
        {
            Ethernet_removePacketsFromTxQueue(
                (Ethernet_DescCh *)&Ethernet_device_struct.dmaObj.txDma[ETHERNET_DMA_CHANNEL_NUM_0],
                ETHERNET_COMPLETION_NORMAL);
            ETHERNET_DISABLE_INTERRUPTS();
            g_uiISRsignal &= (uint32_t)~TX_ISR_MASK;
            ETHERNET_ENABLE_INTERRUPTS();
        }

        if((s & TMR_ISR_MASK) != 0u)
        {
            sys_check_timeouts();
            drain_and_send();
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
 * lwIP host timer hook - required by lwiplib.c even when unused.
 */
void
lwIPHostTimerHandler(void)
{
}
