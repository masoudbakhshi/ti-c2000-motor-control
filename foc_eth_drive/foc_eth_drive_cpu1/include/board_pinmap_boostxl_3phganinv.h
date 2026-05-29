/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * board_pinmap_boostxl_3phganinv.h
 *
 * BOOSTXL-3PhGaNInv pinout when stacked on the TMDSCNCD28388D
 * controlCARD via the TMDSHSECDOCK J1/J2/J3 100-pin HSEC connector.
 *
 * The BoosterPack site on the dock connects the BoosterPack pins to
 * specific HSEC pins which then map to F28388D GPIOs and ADC channels.
 * The numbers below are the resolved F28388D pin assignments.
 *
 * Signal naming convention:
 *   PWM_aH/aL : leg-A high / low side gate
 *   IFB_a     : phase-A current feedback (INA240 output)
 *   VDC_FB    : DC bus voltage feedback
 *   nFLT      : fault output from the GaN driver (open drain, active low)
 */

#ifndef BOARD_PINMAP_BOOSTXL_3PHGANINV_H_
#define BOARD_PINMAP_BOOSTXL_3PHGANINV_H_

#include "driverlib.h"
#include "device.h"

/* --- ePWM channel allocation ----------------------------------------- */
#define PWM_LEG_A_BASE      EPWM1_BASE
#define PWM_LEG_B_BASE      EPWM2_BASE
#define PWM_LEG_C_BASE      EPWM3_BASE

/* EPWMx_A / EPWMx_B drive the high / low side gate via the LMG5113
 * half-bridge GaN driver on the BOOSTXL board.
 */
#define PWM_LEG_A_GPIO_H    0u    /* EPWM1A */
#define PWM_LEG_A_GPIO_L    1u    /* EPWM1B */
#define PWM_LEG_B_GPIO_H    2u    /* EPWM2A */
#define PWM_LEG_B_GPIO_L    3u    /* EPWM2B */
#define PWM_LEG_C_GPIO_H    4u    /* EPWM3A */
#define PWM_LEG_C_GPIO_L    5u    /* EPWM3B */

/* --- ADC channels ----------------------------------------------------- */
/* BOOSTXL-3PhGaNInv routes INA240 outputs to BoosterPack analog pins
 * which on the controlCARD HSEC dock land on:
 *   IFB_A -> ADCINA0
 *   IFB_B -> ADCINA1
 *   IFB_C -> ADCINA2
 *   VDC   -> ADCINB0
 */
#define ADC_IFB_A_BASE      ADCA_BASE
#define ADC_IFB_A_CHAN      ADC_CH_ADCIN0
#define ADC_IFB_A_RESULT    ADCARESULT_BASE
#define ADC_IFB_A_SOC       ADC_SOC_NUMBER0

#define ADC_IFB_B_BASE      ADCA_BASE
#define ADC_IFB_B_CHAN      ADC_CH_ADCIN1
#define ADC_IFB_B_RESULT    ADCARESULT_BASE
#define ADC_IFB_B_SOC       ADC_SOC_NUMBER1

#define ADC_IFB_C_BASE      ADCA_BASE
#define ADC_IFB_C_CHAN      ADC_CH_ADCIN2
#define ADC_IFB_C_RESULT    ADCARESULT_BASE
#define ADC_IFB_C_SOC       ADC_SOC_NUMBER2

#define ADC_VDC_BASE        ADCB_BASE
#define ADC_VDC_CHAN        ADC_CH_ADCIN0
#define ADC_VDC_RESULT      ADCBRESULT_BASE
#define ADC_VDC_SOC         ADC_SOC_NUMBER0

/* End-of-conversion that triggers the control ISR. We pick the last
 * SOC of the burst (IFB_C on ADCA SOC2) so the ISR runs once all
 * phase current samples are in the result register.
 */
#define ADC_EOC_BASE        ADCA_BASE
#define ADC_EOC_INT_NUMBER  ADC_INT_NUMBER1
#define ADC_EOC_SOC_TRIG    ADC_SOC_NUMBER2

/* --- Heartbeat LED ---------------------------------------------------- */
/* D2 on the TMDSCNCD28388D controlCARD: GPIO34. D1 on GPIO31 is
 * reserved for ISR timing scope probing.
 */
#define LED_HEARTBEAT_GPIO  34u
#define GPIO_ISR_PROBE      31u

/* --- Trip zone input (optional, BOOSTXL nFLT) ------------------------ */
#define TZ_FAULT_GPIO       40u   /* BoosterPack nFLT, pulled up on board */

#endif /* BOARD_PINMAP_BOOSTXL_3PHGANINV_H_ */
