/******************************************************************************
 * Project : blink_led
 * Device  : TMS320F28388D
 * Board   : TMDSCNCD28388D controlCARD on TMDSHSECDOCK
 *
 * Blinks LED D1 (GPIO31) at 1 Hz to verify the hardware bring-up:
 * clock tree, GPIO, and debug connection are all working correctly.
 *
 * LED D1 is active-low - driving GPIO31 LOW turns the LED on.
 * Blink timing: 500 ms ON, 500 ms OFF.
 ******************************************************************************/

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "c2000ware_libraries.h"

void main(void)
{
    Device_init();
    Device_initGPIO();

    Interrupt_initModule();
    Interrupt_initVectorTable();

    Board_init();
    C2000Ware_libraries_init();

    EINT;
    ERTM;

    for (;;)
    {
        GPIO_writePin(myBoardLED0_GPIO, 0);
        DEVICE_DELAY_US(500000UL);

        GPIO_writePin(myBoardLED0_GPIO, 1);
        DEVICE_DELAY_US(500000UL);
    }
}
