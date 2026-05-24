/* Author: Masoud Bakhshi — www.plan22.net */
#include "driverlib.h"
#include "device.h"
#include "board.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Switching and ADC constants ─────────────────────────────────────── */
#define SYSCLK_HZ        200000000UL
#define FSW_HZ           20000UL
#define TBPRD            5000U      /* SYSCLK / (2 × FSW) */

/* ── ACS712 current sensor (20A range, 100 mV/A, VREF=3.3V) ─────────── */
/* ADC: 12-bit, VREFHI=3.3V → 1 LSB = 0.8057 mV                         */
#define ADC_ZERO_A       2048U      /* ≈ VREF/2 / (VREFHI/4096) = mid-scale */
#define ADC_SCALE        0.01613f   /* A per LSB: (1/100e-3)*(3.3/4096) */

/* ── Overcurrent thresholds ──────────────────────────────────────────── */
/* 100 mV/A sensitivity; mid-point 1.65V (VDDA/2 for CMPSS DAC)         */
/* CMPSS DAC is 12-bit, reference = VDDA = 3.3V                          */
/* High trip: +8A → 1.65 + 0.80 = 2.45V → DAC = (2.45/3.3)*4095 = 3042 */
/* Low  trip: -8A → 1.65 - 0.80 = 0.85V → DAC = (0.85/3.3)*4095 = 1054 */
#define CMPSS_DAC_HIGH   3042U
#define CMPSS_DAC_LOW    1054U
#define I_TRIP_A         8.0f

/* ── Trip mode: 0 = CBC (cycle-by-cycle), 1 = OST (one-shot) ────────── */
#define TRIP_MODE        1

/* ── UART telemetry ──────────────────────────────────────────────────── */
#define UART_BAUD        115200UL
#define UART_DECIMATOR   2000U      /* emit every 2000 PWM cycles ≈ 10 Hz */

/* ── Fault log ───────────────────────────────────────────────────────── */
typedef struct {
    uint32_t timestamp_us;
    uint16_t tzflg;
    uint16_t count;
} FaultEntry;

#define FAULT_LOG_LEN    16U

static volatile FaultEntry g_fault_log[FAULT_LOG_LEN];
static volatile uint16_t   g_fault_head  = 0;
static volatile uint16_t   g_fault_total = 0;
static volatile uint16_t   g_ost_latched = 0;

/* ── ADC / PWM state ─────────────────────────────────────────────────── */
static volatile float    g_imeas    = 0.0f;
static volatile float    g_iref     = 0.0f;
static volatile uint32_t g_pwm_tick = 0;

/* ── Fault injection test state ──────────────────────────────────────── */
static volatile uint16_t g_inject_active = 0;

/* ── Prototypes ──────────────────────────────────────────────────────── */
static void initEPWMTripZone(void);
static void initCMPSS(void);
static void clearOST(void);
static void faultInjectionTest(void);
static void uartSendLine(const char *s);
static void uartLogFault(const FaultEntry *e);

/* ── Trip zone ISR ───────────────────────────────────────────────────── */
__interrupt void epwm1_tzISR(void)
{
    uint16_t tzflg = EPWM_getTripZoneFlagStatus(EPWM1_BASE);

    FaultEntry *e = (FaultEntry *)&g_fault_log[g_fault_head % FAULT_LOG_LEN];
    e->timestamp_us = g_pwm_tick * 50UL;
    e->tzflg        = tzflg;
    e->count        = ++g_fault_total;
    g_fault_head++;

    if (tzflg & EPWM_TZ_FLAG_OST)
        g_ost_latched = 1;

    /* Clear CBC flag so PWM can recover on next cycle */
    EPWM_clearTripZoneFlag(EPWM1_BASE,
                           EPWM_TZ_INTERRUPT | EPWM_TZ_FLAG_CBC);

    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP2);
}

/* ── ePWM1 SOCA ISR (read ADC after conversion) ──────────────────────── */
__interrupt void adca_isr(void)
{
    uint16_t raw = ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER0);
    g_imeas = ((float)raw - (float)ADC_ZERO_A) * ADC_SCALE;
    g_pwm_tick++;

    if (g_pwm_tick % UART_DECIMATOR == 0)
    {
        /* Emit telemetry at 10 Hz */
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "IMEAS:%.3f,IREF:%.3f,OST:%u,FAULTS:%u\r\n",
                 g_imeas, g_iref, (unsigned)g_ost_latched,
                 (unsigned)g_fault_total);
        uartSendLine(buf);

        /* Print any unlogged faults */
        static uint16_t last_logged = 0;
        while (last_logged < g_fault_head)
        {
            uartLogFault((const FaultEntry *)
                         &g_fault_log[last_logged % FAULT_LOG_LEN]);
            last_logged++;
        }
    }

    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

/* ── CMPSS initialisation ────────────────────────────────────────────── */
static void initCMPSS(void)
{
    /* Enable CMPSS1 */
    CMPSS_enableModule(CMPSS1_BASE);

    /* High comparator: trip when I > +I_TRIP_A */
    CMPSS_configHighComparator(CMPSS1_BASE,
                               CMPSS_INSRC_PIN);          /* + input = ADCIN */
    CMPSS_configDAC(CMPSS1_BASE,
                    CMPSS_DACVAL_SYSCLK |
                    CMPSS_DACREF_VDDA   |
                    CMPSS_DACSRC_SHDW);
    CMPSS_setDACValueHigh(CMPSS1_BASE, CMPSS_DAC_HIGH);

    /* Low comparator: trip when I < -I_TRIP_A */
    CMPSS_configLowComparator(CMPSS1_BASE,
                              CMPSS_INSRC_PIN | CMPSS_INV_INVERTED);
    CMPSS_setDACValueLow(CMPSS1_BASE, CMPSS_DAC_LOW);

    /* Digital filter: blanking to avoid false trips during switching */
    CMPSS_configFilterHigh(CMPSS1_BASE, 0, 32, 30);
    CMPSS_configFilterLow(CMPSS1_BASE,  0, 32, 30);
    CMPSS_initFilterHigh(CMPSS1_BASE);
    CMPSS_initFilterLow(CMPSS1_BASE);

    /* Latch outputs; clear via software */
    CMPSS_configLatchOnPWMSYNC(CMPSS1_BASE, true, true);
}

/* ── Trip zone configuration on ePWM1 ───────────────────────────────── */
static void initEPWMTripZone(void)
{
    /* Force both outputs low on any trip */
    EPWM_setTripZoneAction(EPWM1_BASE,
                           EPWM_TZ_ACTION_EVENT_TZA,
                           EPWM_TZ_ACTION_LOW);
    EPWM_setTripZoneAction(EPWM1_BASE,
                           EPWM_TZ_ACTION_EVENT_TZB,
                           EPWM_TZ_ACTION_LOW);

#if TRIP_MODE == 0
    /* CBC: re-arm automatically each cycle */
    EPWM_enableTripZoneSignals(EPWM1_BASE, EPWM_TZ_SIGNAL_CBC4);
    EPWM_enableTripZoneInterrupt(EPWM1_BASE, EPWM_TZ_INTERRUPT_CBC);
#else
    /* OST: latch until software clear */
    EPWM_enableTripZoneSignals(EPWM1_BASE, EPWM_TZ_SIGNAL_OSHT4);
    EPWM_enableTripZoneInterrupt(EPWM1_BASE, EPWM_TZ_INTERRUPT_OST);
#endif

    Interrupt_register(INT_EPWM1_TZ, &epwm1_tzISR);
    Interrupt_enable(INT_EPWM1_TZ);
}

/* ── Clear OST latch (call after fault is resolved) ─────────────────── */
static void clearOST(void)
{
    CMPSS_clearFilterLatchHigh(CMPSS1_BASE);
    CMPSS_clearFilterLatchLow(CMPSS1_BASE);
    EPWM_clearTripZoneFlag(EPWM1_BASE,
                           EPWM_TZ_INTERRUPT | EPWM_TZ_FLAG_OST);
    g_ost_latched = 0;
}

/* ── Fault injection test ────────────────────────────────────────────── */
/* Forces a software trip to verify the trip-zone path is functioning.   */
/* Uses EPWM_forceTripZoneEvent so no actual overcurrent is required.    */
static void faultInjectionTest(void)
{
    g_inject_active = 1;
    uartSendLine("INJECT: forcing trip event\r\n");

#if TRIP_MODE == 0
    EPWM_forceTripZoneEvent(EPWM1_BASE, EPWM_TZ_FORCE_EVENT_CBC);
#else
    EPWM_forceTripZoneEvent(EPWM1_BASE, EPWM_TZ_FORCE_EVENT_OST);
#endif

    /* Allow one ISR cycle to log the event */
    DEVICE_DELAY_US(100);

    if (g_fault_total > 0)
        uartSendLine("INJECT: trip confirmed — PWM shut down\r\n");
    else
        uartSendLine("INJECT: FAIL — no trip recorded\r\n");

#if TRIP_MODE == 1
    clearOST();
    uartSendLine("INJECT: OST cleared — PWM re-enabled\r\n");
#endif

    g_inject_active = 0;
}

/* ── UART helpers ────────────────────────────────────────────────────── */
static void uartSendLine(const char *s)
{
    while (*s)
    {
        SCI_writeCharBlockingFIFO(SCIA_BASE, (uint16_t)*s);
        s++;
    }
}

static void uartLogFault(const FaultEntry *e)
{
    char buf[64];
    snprintf(buf, sizeof(buf),
             "FAULT #%u t=%lu us TZFLG=0x%02X\r\n",
             (unsigned)e->count,
             (unsigned long)e->timestamp_us,
             (unsigned)e->tzflg);
    uartSendLine(buf);
}

/* ── main ────────────────────────────────────────────────────────────── */
int main(void)
{
    Device_init();
    Device_initGPIO();

    Interrupt_initModule();
    Interrupt_initVectorTable();

    Board_init();   /* SysConfig-generated: ePWM1, ADCA, CMPSS1, SCIA */

    initCMPSS();
    initEPWMTripZone();

    Interrupt_register(INT_ADCA1, &adca_isr);
    Interrupt_enable(INT_ADCA1);

    EINT;
    ERTM;

    uartSendLine("cmpss_trip_overcurrent — Masoud Bakhshi www.plan22.net\r\n");
    uartSendLine("MODE:");
#if TRIP_MODE == 0
    uartSendLine("CBC\r\n");
#else
    uartSendLine("OST\r\n");
#endif

    /* Run fault injection test on startup */
    DEVICE_DELAY_US(500000);
    faultInjectionTest();

    /* Main loop: supervisory — fault re-arm and reference sweep */
    float theta = 0.0f;
    for (;;)
    {
        /* 50 Hz reference sine at 0.5 A peak */
        theta += 2.0f * 3.14159265f * 50.0f / (float)FSW_HZ;
        if (theta > 2.0f * 3.14159265f) theta -= 2.0f * 3.14159265f;
        /* g_iref updated here if closed-loop is enabled */

        /* LED D1 on fault, off when clear */
        GPIO_writePin(31, g_ost_latched ? 1U : 0U);
    }
}
