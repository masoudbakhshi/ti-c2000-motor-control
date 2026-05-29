/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * main.c
 *
 * CPU1 application entry point for the F28388D + BOOSTXL-3PhGaNInv
 * drive. CPU2 is held in reset. The CM Cortex-M4 image (companion
 * project) carries the lwIP stack and the EMAC driver and exchanges
 * eth_cmd_pkt_t / eth_telem_pkt_t with this firmware via dedicated
 * MSGRAM banks.
 *
 * Boot order:
 *   1. sysctl_init  : PLL 200 MHz, peripheral clocks, GPIO base.
 *   2. pwm_iface_init: ePWM1/2/3 in up/down 20 kHz, gates forced off.
 *   3. adc_iface_init: ADCA + ADCB triggered by ePWM1 SOCA.
 *   4. eth_iface_init: zero the MSGRAM mailboxes.
 *   5. foc_init / prot_init.
 *   6. Wire ADCA1 INT into the PIE, register isr_control_entry.
 *   7. Release TBCLK so the three ePWMs start in lock step.
 *   8. EINT, ERTM, enter the super loop.
 *
 * Super loop:
 *   - service command packets (eth_rx_cmd_service)
 *   - drain telemetry ring  (eth_tx_telemetry_service)
 *   - poll the eth iface
 *   - blink the heartbeat LED
 */

#include <stdint.h>
#include <stdbool.h>
#include "user_config.h"
#include "board_pinmap_boostxl_3phganinv.h"
#include "adc_iface.h"
#include "pwm_iface.h"
#include "foc_dq_pi.h"
#include "prot_safety.h"
#include "eth_iface.h"
#include "driverlib.h"
#include "device.h"

extern void sysctl_init(void);
extern void sysctl_sync_pwm_start(void);
extern void eth_phy_bringup_and_boot_cm(void);
extern __interrupt void isr_control_entry(void);
extern bool      eth_rx_cmd_service(void);
extern uint32_t  eth_tx_telemetry_service(uint32_t max_per_call);

/* Defined in isr_control.c */
extern foc_state_t  g_foc;
extern prot_state_t g_prot;

/*
 * heartbeat_tick
 * Purpose : Toggle the on-board LED at HEARTBEAT_HZ. Uses the CPU
 *           timer 0 free running tick as a coarse time base.
 * ISR safe: no (called from main loop)
 */
static void heartbeat_tick(void)
{
    static uint32_t cnt = 0u;
    cnt++;
    /* Roughly 1 Hz when called from a tight super loop. The exact
     * frequency is not load-bearing; it is only a "still alive"
     * indicator on the controlCARD.
     */
    if ((cnt & 0xFFFFFu) == 0u)
    {
        GPIO_togglePin(LED_HEARTBEAT_GPIO);
    }
}

int main(void)
{
    /* Phase 1: clocks, GPIO, PIE. */
    sysctl_init();

    /* Phase 2: PWM. Outputs forced off via TZ until enable. */
    pwm_iface_init();

    /* Phase 3: ADC. SOC trigger comes from ePWM1 at TBCTR = 0. */
    adc_iface_init();

    /* Phase 4: PHY pin-mux + CM boot, then the Ethernet abstraction. */
    eth_phy_bringup_and_boot_cm();
    eth_iface_init();

    /* Phase 5: controller state. */
    foc_init(&g_foc);
    foc_set_gains(&g_foc, DEFAULT_RS_OHM, DEFAULT_LS_H, DEFAULT_F_BW_HZ, TS_CTRL_S);
    foc_set_voltage_limit(&g_foc, 1.0f);

    uint32_t eth_wdt_limit_ticks = (uint32_t)(((float)ETH_WDT_MS * 0.001f) * FSW_HZ);
    prot_init(&g_prot, eth_wdt_limit_ticks);

    /* Phase 6: register ISR. ADCA1 -> ADCA1 INT in the PIE table. */
    Interrupt_register(INT_ADCA1, &isr_control_entry);
    Interrupt_enable(INT_ADCA1);

    /* Phase 7: release the global TBCLK sync. */
    sysctl_sync_pwm_start();

    /* Phase 8: enable CPU interrupts. */
    EINT;
    ERTM;

    /* Super loop. */
    while (1)
    {
        (void)eth_rx_cmd_service();
        (void)eth_tx_telemetry_service(16u);
        eth_iface_poll();
        heartbeat_tick();
    }
}
