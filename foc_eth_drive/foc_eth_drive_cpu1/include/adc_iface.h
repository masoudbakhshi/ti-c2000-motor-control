/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * adc_iface.h
 *
 * Public surface of the ADC layer: peripheral bring up plus a small
 * set of inline accessors used by the ISR to convert raw counts to
 * physical units. The ADC start of conversion is driven from ePWM1
 * at the carrier valley (TBCTR = 0). The end of conversion of the
 * last channel raises ADCA1 which is mapped to the control ISR.
 */

#ifndef ADC_IFACE_H_
#define ADC_IFACE_H_

#include <stdint.h>
#include <stdbool.h>
#include "user_config.h"
#include "board_pinmap_boostxl_3phganinv.h"
#include "driverlib.h"

/* Calibration offsets (in raw counts). */
typedef struct
{
    uint16_t off_a;
    uint16_t off_b;
    uint16_t off_c;
} adc_calib_t;

/*
 * adc_iface_init
 * Purpose : Configure ADCA / ADCB for 12-bit single-ended mode, 75 ns
 *           acquisition window, and trigger from ePWM1 SOCA. Wires
 *           the ADCA1 EOC interrupt to fire on the last channel.
 * ISR safe: no (call before enabling interrupts).
 */
void adc_iface_init(void);

/*
 * adc_iface_read_raw
 * Purpose : Read the latest raw counts for the three phase currents
 *           and the DC bus voltage in one call. Designed to be called
 *           from inside the ISR.
 * Inputs  : ia, ib, ic, vdc  output pointers (in counts)
 * ISR safe: yes
 */
static inline void adc_iface_read_raw(uint16_t *ia,
                                      uint16_t *ib,
                                      uint16_t *ic,
                                      uint16_t *vdc)
{
    *ia  = ADC_readResult(ADC_IFB_A_RESULT, ADC_IFB_A_SOC);
    *ib  = ADC_readResult(ADC_IFB_B_RESULT, ADC_IFB_B_SOC);
    *ic  = ADC_readResult(ADC_IFB_C_RESULT, ADC_IFB_C_SOC);
    *vdc = ADC_readResult(ADC_VDC_RESULT,   ADC_VDC_SOC);
}

/*
 * adc_iface_to_phase_current
 * Purpose : Convert raw ADC counts to Amps, accounting for the INA240
 *           current sense gain and the captured zero offset.
 * Inputs  : counts  raw ADC value
 *           offset  calibration offset (counts at zero current)
 * Returns : phase current in Amps. Sign convention: positive into
 *           the load.
 * ISR safe: yes
 */
static inline float adc_iface_to_phase_current(uint16_t counts, uint16_t offset)
{
    return ((float)(int32_t)counts - (float)(int32_t)offset) * I_PHASE_COUNTS_TO_A;
}

/*
 * adc_iface_to_vdc
 * Purpose : Convert raw ADC counts to DC bus voltage in Volts.
 * Inputs  : counts  raw ADC value (0..4095)
 * Returns : DC bus voltage in Volts
 * ISR safe: yes
 */
static inline float adc_iface_to_vdc(uint16_t counts)
{
    return ((float)(int32_t)counts) * VDC_COUNTS_TO_V;
}

#endif /* ADC_IFACE_H_ */
