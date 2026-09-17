/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "nwk_beacon.h"

#include <stddef.h>
#include <string.h>

nwk_beacon_result_t nwk_beacon_decode(const uint8_t *payload, uint16_t length,
                                      nwk_beacon_t *result)
{
    nwk_beacon_t candidate;
    uint8_t i, any = 0, all = 0xff;

    if (payload == NULL || result == NULL)
        return NWK_BEACON_INVALID_ARGUMENT;
    if (length < NWK_BEACON_LENGTH)
        return NWK_BEACON_TRUNCATED;
    if (length > NWK_BEACON_LENGTH)
        return NWK_BEACON_TOO_LONG;
    if (payload[0] != NWK_BEACON_PROTOCOL_ID)
        return NWK_BEACON_UNSUPPORTED_PROTOCOL;
    if ((payload[1] >> 4) != NWK_BEACON_PROTOCOL_VERSION)
        return NWK_BEACON_UNSUPPORTED_VERSION;
    if (payload[2] & 3u)
        return NWK_BEACON_INVALID_FIELDS;
    memset(&candidate, 0, sizeof(candidate));
    for (i = 0; i < 8; i++) {
        candidate.extended_pan_id[i] = payload[3u + i];
        any |= candidate.extended_pan_id[i];
        all &= candidate.extended_pan_id[i];
    }
    if (any == 0 || all == 0xff)
        return NWK_BEACON_INVALID_FIELDS;
    candidate.stack_profile = payload[1] & 0x0fu;
    candidate.router_capacity = (payload[2] & 4u) != 0;
    candidate.device_depth = (payload[2] >> 3) & 0x0fu;
    candidate.end_device_capacity = payload[2] >> 7;
    candidate.tx_offset = (uint32_t)payload[11] | ((uint32_t)payload[12] << 8)
                          | ((uint32_t)payload[13] << 16);
    candidate.update_id = payload[14];
    *result = candidate;
    return NWK_BEACON_OK;
}
