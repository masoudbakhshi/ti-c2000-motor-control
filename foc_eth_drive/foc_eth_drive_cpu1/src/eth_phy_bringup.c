/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * eth_phy_bringup.c
 *
 * CPU1 owns the GPIO/clock side of the Ethernet PHY even though the
 * EMAC peripheral itself lives in the CM (Cortex-M4) domain. This
 * file sets the EMAC clock divider, mux every MII signal onto its
 * fixed F2838x pin, releases the DP83822 PHY out of reset / power
 * down, and boots the CM core.
 */

#include "driverlib.h"
#include "device.h"

/*
 * eth_phy_bringup_and_boot_cm
 * Purpose : Pin-mux all MII signals to the on-card DP83822 PHY, set
 *           the EMAC clock to SYSPLL / 2 = 100 MHz, deassert PHY reset
 *           and power-down, then start the CM core from flash sector 0.
 * Inputs  : none
 * Outputs : none
 * ISR safe: no (call once during boot before enabling interrupts).
 * Budget  : ~1 ms (mostly the CM boot handoff delay inside driverlib).
 */
void eth_phy_bringup_and_boot_cm(void)
{
    SysCtl_setEnetClk(SYSCTL_ENETCLKOUT_DIV_2, SYSCTL_SOURCE_SYSPLL);

    GPIO_setPinConfig(GPIO_105_ENET_MDIO_CLK);
    GPIO_setPinConfig(GPIO_106_ENET_MDIO_DATA);

    GPIO_setPinConfig(GPIO_109_ENET_MII_CRS);
    GPIO_setPinConfig(GPIO_110_ENET_MII_COL);

    GPIO_setPinConfig(GPIO_75_ENET_MII_TX_DATA0);
    GPIO_setPinConfig(GPIO_122_ENET_MII_TX_DATA1);
    GPIO_setPinConfig(GPIO_123_ENET_MII_TX_DATA2);
    GPIO_setPinConfig(GPIO_124_ENET_MII_TX_DATA3);
    GPIO_setPinConfig(GPIO_118_ENET_MII_TX_EN);

    GPIO_setPinConfig(GPIO_114_ENET_MII_RX_DATA0);
    GPIO_setPinConfig(GPIO_115_ENET_MII_RX_DATA1);
    GPIO_setPinConfig(GPIO_116_ENET_MII_RX_DATA2);
    GPIO_setPinConfig(GPIO_117_ENET_MII_RX_DATA3);
    GPIO_setPinConfig(GPIO_113_ENET_MII_RX_ERR);
    GPIO_setPinConfig(GPIO_112_ENET_MII_RX_DV);

    GPIO_setPinConfig(GPIO_44_ENET_MII_TX_CLK);
    GPIO_setPinConfig(GPIO_111_ENET_MII_RX_CLK);

    /* PHY power-down (GPIO108) released high. */
    GPIO_setDirectionMode(108, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(108, GPIO_PIN_TYPE_PULLUP);
    GPIO_writePin(108, 1);

    /* PHY reset (GPIO119) released high. */
    GPIO_setDirectionMode(119, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(119, GPIO_PIN_TYPE_PULLUP);
    GPIO_writePin(119, 1);

    /* Hand control to the CM core. When CPU1 is a RAM build we boot
     * CM into the same memory space it was loaded into via JTAG; for
     * FLASH builds CM starts from its own flash bank 0.
     */
#ifdef _FLASH
    Device_bootCM(BOOTMODE_BOOT_TO_FLASH_SECTOR0);
#else
    Device_bootCM(BOOTMODE_BOOT_TO_FLASH_SECTOR0);
#endif
}
