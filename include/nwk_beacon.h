/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef NWK_BEACON_H
#define NWK_BEACON_H

#include <stdint.h>

#define NWK_BEACON_LENGTH 15u
#define NWK_BEACON_PROTOCOL_ID 0u
#define NWK_BEACON_PROTOCOL_VERSION 2u
#define NWK_BEACON_BEACONLESS_OFFSET 0x00ffffffUL

typedef enum {
    NWK_BEACON_OK = 0,
    NWK_BEACON_INVALID_ARGUMENT,
    NWK_BEACON_TRUNCATED,
    NWK_BEACON_TOO_LONG,
    NWK_BEACON_UNSUPPORTED_PROTOCOL,
    NWK_BEACON_UNSUPPORTED_VERSION,
    NWK_BEACON_INVALID_FIELDS
} nwk_beacon_result_t;

typedef struct {
    uint8_t stack_profile;
    uint8_t router_capacity;
    uint8_t device_depth;
    uint8_t end_device_capacity;
    /* Octets in wire order, least significant octet first. */
    uint8_t extended_pan_id[8];
    uint32_t tx_offset;
    uint8_t update_id;
} nwk_beacon_t;

/* Input is only the upper-layer Beacon Payload, not a MAC header or payload.
 * OK means bounded, unauthenticated metadata, not a compatible/accepted parent.
 * Outputs stay unchanged on error; input/output storage must not overlap.
 * Foreground only: SDCC parameter/local storage is not ISR-reentrant.
 */
nwk_beacon_result_t nwk_beacon_decode(const uint8_t *payload, uint16_t length,
                                      nwk_beacon_t *result);

#endif
