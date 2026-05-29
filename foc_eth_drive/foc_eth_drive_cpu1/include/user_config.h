/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * user_config.h
 *
 * Single point of configuration for the F28388D + BOOSTXL-3PhGaNInv
 * GaN inverter drive. Every value that a user may want to tune at
 * commissioning time lives here. No other source file is allowed to
 * hard code these constants.
 *
 * Units: SI. Currents in Amps. Voltages in Volts. Inductance in Henry.
 * Resistance in Ohm. Frequency in Hz. Angles in radians. Times in
 * seconds unless suffix _ns / _us / _ms is used.
 */

#ifndef USER_CONFIG_H_
#define USER_CONFIG_H_

#include <stdint.h>

/* --- Math constants (used in fixed point free math) ------------------- */
#ifndef PI_F
#define PI_F                3.14159265358979323846f
#endif
#ifndef TWO_PI_F
#define TWO_PI_F            6.28318530717958647692f
#endif
#ifndef SQRT3_F
#define SQRT3_F             1.73205080756887729353f
#endif
#ifndef ONE_OVER_SQRT3_F
#define ONE_OVER_SQRT3_F    0.57735026918962576451f
#endif

/* --- Clock tree -------------------------------------------------------- */
#define SYSCLK_HZ           200000000.0f
#define EPWMCLK_HZ          100000000.0f

/* --- Switching frequency and control timing ---------------------------- */
#define FSW_HZ              20000.0f
#define TS_CTRL_S           (1.0f / FSW_HZ)
#define TS_CTRL_US          (1000000.0f / FSW_HZ)

/* TBPRD for up-down count carrier:
 *   TBPRD = EPWMCLK_HZ / (2 * FSW_HZ)
 *         = 100e6 / (2 * 20e3) = 2500
 */
#define PWM_TBPRD_TICKS     ((uint16_t)(EPWMCLK_HZ / (2.0f * FSW_HZ)))

/* Dead time. BOOSTXL-3PhGaNInv: GaN HEMTs, very short dead time allowed.
 * 200 ns rising and falling. EPWMCLK = 100 MHz so 1 tick = 10 ns.
 */
#define DEADTIME_RED_TICKS  20u   /* 200 ns rising  edge delay */
#define DEADTIME_FED_TICKS  20u   /* 200 ns falling edge delay */

/* Minimum on-time / off-time clamp (50 ticks = 500 ns).
 * Required to respect gate driver charge time and the ADC sample
 * aperture at the carrier valley.
 */
#define PWM_MIN_PULSE_TICKS 50u

/* --- Telemetry / Ethernet --------------------------------------------- */
#define TELEM_DECIMATION    4u                  /* 20 kHz / 4 = 5 kHz */
#define ETH_WDT_MS          200u
#define ETH_LOCAL_IP_STR    "192.168.10.10"
#define ETH_NETMASK_STR     "255.255.255.0"
#define ETH_GATEWAY_STR     "192.168.10.1"
#define UDP_PORT_CMD        5001u
#define UDP_PORT_TELEM      5002u

/* Telemetry ring buffer length. Must be a power of two. Holds samples
 * pushed by the 20 kHz ISR and drained by the lower priority send loop.
 * Sized so the MSGRAM-resident copy fits in a single F2838x MSGRAM
 * bank (0x800 bytes = 2 KiB).
 */
#define TELEM_RING_LEN      32u
#define TELEM_RING_MASK     (TELEM_RING_LEN - 1u)

/* --- Protections ------------------------------------------------------- */
#define I_TRIP_A            8.0f
#define VDC_TRIP_V          60.0f
#define VDC_UVLO_V          15.0f

/* --- Default controller gains preview (overridden at runtime from the
 *     Pi based on Rs, Ls and f_bw) ------------------------------------- */
#define DEFAULT_RS_OHM      1.5f
#define DEFAULT_LS_H        0.005f
#define DEFAULT_F_BW_HZ     1000.0f

/* Hard cap for usable closed loop current bandwidth. Above Fsw / 10 the
 * digital delay model breaks down and the loop becomes oscillatory.
 */
#define F_BW_MAX_HZ         (FSW_HZ / 10.0f)

/* --- ADC scaling (BOOSTXL-3PhGaNInv) ---------------------------------- */
/*
 * Phase current via INA240:
 *   I_phase[A] = (adc_count - offset) * ADC_VREF_V
 *                / ADC_FULL_SCALE
 *                / (INA240_GAIN * I_SENSE_RES_OHM)
 *
 * Vdc divider on BOOSTXL board:
 *   Vdc[V] = adc_count * ADC_VREF_V / ADC_FULL_SCALE / VDC_SENSE_GAIN
 */
/* F2838x ADC reference is the internal 3.3 V band gap by default.
 * If you switch to external VREF via ASysCtl, update this value.
 */
#define ADC_VREF_V          3.3f
#define ADC_FULL_SCALE      4095.0f
#define INA240_GAIN         20.0f
#define I_SENSE_RES_OHM     0.007f
#define VDC_SENSE_GAIN      ((4.99f) / (4.99f + 95.3f))

/* Pre-computed conversion factors. Avoid divisions in the ISR. */
#define I_PHASE_COUNTS_TO_A \
    ((ADC_VREF_V) / (ADC_FULL_SCALE) / ((INA240_GAIN) * (I_SENSE_RES_OHM)))

#define VDC_COUNTS_TO_V \
    ((ADC_VREF_V) / (ADC_FULL_SCALE) / (VDC_SENSE_GAIN))

/* --- ADC calibration --------------------------------------------------- */
#define ADC_CAL_SAMPLES     1000u   /* Samples averaged at startup with PWM disabled */

/* --- Misc ------------------------------------------------------------- */
#define HEARTBEAT_HZ        1.0f    /* On-board LED blink frequency */

#endif /* USER_CONFIG_H_ */
