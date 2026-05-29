/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * eth_proto.h
 *
 * Wire format for the UDP command and telemetry packets exchanged
 * between the Raspberry Pi 4 host and the F28388D drive.
 *
 * IMPORTANT - C28x byte sizing:
 *   On the TMS320F28388D C28x core, sizeof(char) is 16 bits. There is
 *   no native 8-bit type. The C28x firmware therefore expresses every
 *   field as uint16_t / uint32_t / float (all 16-bit aligned) and the
 *   Cortex-M4 (CM) companion image is responsible for marshalling
 *   between the 16-bit-word representation seen by CPU1 and the true
 *   8-bit little-endian byte sequence that flies over the wire.
 *
 *   Wire layout (byte stream on the link):
 *     CMD   (36 bytes): <I I f f f f f f H H
 *       magic, seq, id_ref_A, iq_ref_A, fe_ref_Hz, Rs_ohm, Ls_H,
 *       f_bw_Hz, enable_flags, crc16
 *     TELEM (52 bytes): <I I I f f f f f f f f f H H
 *       magic, seq, timestamp_us, id_ref_A, id_meas_A, iq_ref_A,
 *       iq_meas_A, ia_A, ib_A, ic_A, vdc_V, theta_e_rad,
 *       status_flags, crc16
 *
 *   enable_flags encodes the user controls on a per-bit basis:
 *     bit 0 = enable
 *     bit 1 = reset_faults
 *     bits 2..15 = reserved (transmitted as zero)
 *
 * The CRC-16-CCITT (poly 0x1021, init 0xFFFF, no reflection, no final
 * XOR) is computed and verified by the Pi side and by the CM core; it
 * is NOT computed on CPU1.
 */

#ifndef ETH_PROTO_H_
#define ETH_PROTO_H_

#include <stdint.h>

#define ETH_CMD_MAGIC       0xC0DE0001U
#define ETH_TELEM_MAGIC     0xFEED0001U

/* enable_flags bit layout */
#define ETH_CMD_FLAG_ENABLE       0x0001U
#define ETH_CMD_FLAG_RESET_FAULT  0x0002U

/* Command packet seen by CPU1. All fields are 16-bit aligned so the
 * MSGRAM layout is identical to the wire layout when read by a true
 * byte-addressable core (CM, Pi).
 */
typedef struct
{
    uint32_t magic;          /* 0xC0DE0001                       */
    uint32_t seq;            /* monotonically increasing         */
    float    id_ref_A;       /* d-axis current reference [A]     */
    float    iq_ref_A;       /* q-axis current reference [A]     */
    float    fe_ref_Hz;      /* electrical frequency     [Hz]    */
    float    Rs_ohm;         /* stator resistance        [Ohm]   */
    float    Ls_H;           /* stator inductance        [H]     */
    float    f_bw_Hz;        /* requested loop bandwidth [Hz]    */
    uint16_t enable_flags;   /* bit0 = enable, bit1 = reset      */
    uint16_t crc16;          /* validated / set by the CM only   */
} eth_cmd_pkt_t;

typedef struct
{
    uint32_t magic;          /* 0xFEED0001                       */
    uint32_t seq;            /* monotonically increasing         */
    uint32_t timestamp_us;   /* wrap-around 32 bit microseconds  */
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
    uint16_t crc16;          /* filled by the CM before TX       */
} eth_telem_pkt_t;

#define ETH_CMD_PKT_SIZE     (sizeof(eth_cmd_pkt_t))
#define ETH_TELEM_PKT_SIZE   (sizeof(eth_telem_pkt_t))

#endif /* ETH_PROTO_H_ */
