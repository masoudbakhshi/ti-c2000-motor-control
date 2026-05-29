/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * sysctl_init.c
 *
 * F28388D system bring up. Sets the PLL to 200 MHz from the 25 MHz
 * external oscillator shared by the MCU and the on-card Ethernet PHYs
 * on the TMDSCNCD28388D, enables peripheral clocks for the ePWM, ADC
 * and CPU timer modules used by the FOC core, configures the heart
 * beat LED, and installs the ePWM time base sync source.
 */

#include "user_config.h"
#include "board_pinmap_boostxl_3phganinv.h"
#include "driverlib.h"
#include "device.h"

/*
 * sysctl_init
 * Purpose : Configure clocks and peripheral gating for CPU1. Must run
 *           before any peripheral driver call. Calls Device_init from
 *           the C2000Ware device support layer which sets the PLL to
 *           200 MHz and the EPWMCLK divider to /2.
 * Inputs  : none
 * Outputs : none
 * Units   : n/a
 * ISR safe: no (call from main at startup with interrupts disabled)
 * Budget  : approx 5 ms one shot.
 */
void sysctl_init(void)
{
    /* Initialize device clock and peripherals. Device_init pulls in the
     * device_*.c table that is part of every C2000Ware project, and is
     * the standard entry point recommended by TI for the F28388D.
     */
    Device_init();

    /* Disable pin lock, allow GPIO mux changes. */
    Device_initGPIO();

    /* Enable the PIE and clear all PIE registers. Disable CPU interrupts. */
    Interrupt_initModule();
    Interrupt_initVectorTable();

    /* Enable per-peripheral clock gates for the modules used. */
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM1);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM2);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM3);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_ADCA);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_ADCB);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TIMER0);

    /* TBCLK to all three ePWMs is sourced from EPWMCLK / 1 inside the
     * module. Disable TBCLK while we configure all three so that they
     * can be released in phase.
     */
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_EPWM1);
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_EPWM2);
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_EPWM3);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM1);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM2);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM3);

    /* Heartbeat LED (D2 on the controlCARD) as a push pull output. */
    GPIO_setPinConfig(GPIO_34_GPIO34);
    GPIO_setDirectionMode(LED_HEARTBEAT_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(LED_HEARTBEAT_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_writePin(LED_HEARTBEAT_GPIO, 1);

    /* GPIO31 (D1 on the controlCARD) reserved for the ISR timing probe. */
    GPIO_setPinConfig(GPIO_31_GPIO31);
    GPIO_setDirectionMode(GPIO_ISR_PROBE, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(GPIO_ISR_PROBE, GPIO_PIN_TYPE_STD);
    GPIO_writePin(GPIO_ISR_PROBE, 0);
}

/*
 * sysctl_sync_pwm_start
 * Purpose : Release the global ePWM time base clock so all three legs
 *           start counting in lock step. Called once after every ePWM
 *           module has been individually configured.
 * Inputs  : none
 * Outputs : none
 * ISR safe: no
 */
void sysctl_sync_pwm_start(void)
{
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
}
