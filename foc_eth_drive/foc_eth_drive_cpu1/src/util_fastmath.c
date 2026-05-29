/* Author: Masoud Bakhshi - www.plan22.net */
/*
 * util_fastmath.c
 *
 * Out-of-line definitions for any helpers that are not pure inlines.
 * CRC computation is intentionally NOT carried out on CPU1: the C28x
 * core has 16-bit char and cannot reproduce a true 8-bit CRC-16-CCITT
 * over the wire byte stream. The Cortex-M4 (CM) companion image
 * computes the outgoing CRC and validates the incoming one. See
 * include/eth_proto.h for the wire layout contract.
 *
 * This translation unit is intentionally minimal but present so the
 * link does not lose any inline-only helper symbols.
 */

#include "util_fastmath.h"

/* No external symbols required at this time. The placeholder below
 * keeps some compilers from warning about an empty translation unit.
 */
const uint32_t util_fastmath_unit_id = 0xFA570001u;
