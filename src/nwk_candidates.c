/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "nwk_candidates.h"
#include "mac_frame.h"

#include <stddef.h>
#include <string.h>
#include "mac_link_workspace_guard_internal.h"
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_internal.h"
#endif

static uint8_t valid_mask(uint32_t mask)
{
    return mask != 0u && (mask & ~NWK_CANDIDATES_CHANNEL_MASK) == 0u;
}

static uint8_t valid_table(const nwk_candidates_t * volatile table)
{
    return table->version == NWK_CANDIDATES_VERSION
        && table->count <= NWK_CANDIDATES_CAPACITY && valid_mask(table->channel_mask);
}

nwk_candidates_result_t nwk_candidates_init(nwk_candidates_t * volatile table,
                                            uint32_t channel_mask)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, table, sizeof(*table), 1)) return NWK_CANDIDATES_INVALID_ARGUMENT;
#endif
    if (table == NULL || !valid_mask(channel_mask))
        return NWK_CANDIDATES_INVALID_ARGUMENT;
    memset(table, 0, sizeof(*table));
    table->channel_mask = channel_mask;
    table->version = NWK_CANDIDATES_VERSION;
    return NWK_CANDIDATES_OK;
}

static uint8_t same_identity(const nwk_candidate_t * volatile a,
                             const nwk_candidate_t * volatile b)
{
    return a->channel == b->channel && a->pan_id == b->pan_id
        && a->address_mode == b->address_mode
        && memcmp(a->coordinator, b->coordinator, 8) == 0
        && memcmp(a->network.extended_pan_id, b->network.extended_pan_id, 8) == 0;
}

nwk_candidates_result_t nwk_candidates_consider(nwk_candidates_t * volatile table,
                                                uint8_t channel, uint8_t crc_valid,
                                                const uint8_t * volatile body,
                                                uint16_t length)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_SCAN, NWK_CANDIDATES_INVALID_ARGUMENT);
    if (!LW_IO(LW_SCAN, table, sizeof(*table), 1) || !LW_IO(LW_SCAN, body, length, 0))
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_INVALID_ARGUMENT);
#endif
#if defined(CC2530_MAC_LINK_WORKSPACE)
#define frame (link_work_arena.protocol.parent.scan.frame)
#define beacon (link_work_arena.protocol.parent.scan.beacon)
#define candidate (link_work_arena.protocol.parent.scan.candidate)
#else
    mac_frame_info_t frame;
    mac_beacon_info_t beacon;
    nwk_candidate_t candidate;
#endif
    uint8_t eligible;
    volatile uint8_t i;

    if (table == NULL || body == NULL || channel < 11u || channel > 26u
            || crc_valid > 1u)
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_INVALID_ARGUMENT);
    if (!valid_table(table))
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_INVALID_TABLE);
    if (!crc_valid)
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_BAD_CRC);
    if (!(table->channel_mask & (UINT32_C(1) << channel)))
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_OUTSIDE_MASK);
    if (mac_frame_decode(body, length, &frame) != MAC_CODEC_OK)
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_MAC_REJECTED);
    if (frame.header.type != MAC_FRAME_BEACON)
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_NOT_BEACON);
    if (mac_beacon_decode(body + frame.payload_offset, frame.payload_length,
                          &beacon) != MAC_CODEC_OK)
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_MAC_REJECTED);
    memset(&candidate, 0, sizeof(candidate));
    if (nwk_beacon_decode(body + frame.payload_offset + beacon.payload_offset,
                          beacon.payload_length, &candidate.network) != NWK_BEACON_OK)
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_NWK_REJECTED);
    candidate.channel = channel;
    candidate.pan_id = frame.header.source_pan;
    candidate.address_mode = frame.header.source_mode;
    memcpy(candidate.coordinator, frame.header.source, 8);
    candidate.superframe = beacon.superframe_specification;
    candidate.sequence = frame.header.sequence;
    candidate.mac_flags = frame.header.flags;
    candidate.gts_permit = beacon.gts_permit;
    candidate.short_pending = beacon.short_count;
    candidate.extended_pending = beacon.extended_count;
    /* IEEE 2006 7.5.1.1 ignores SO on RX with BO15. No offset/depth/update
     * ranking or compatibility claim; R22 3.6.1.4 requires much more to join.
     */
    eligible = candidate.network.stack_profile == NWK_CANDIDATES_PRO_PROFILE
        && (candidate.superframe & 0x000fu) == 15u
        && (candidate.superframe & MAC_SUPERFRAME_ASSOCIATION_PERMIT) != 0u
        && candidate.network.end_device_capacity != 0u;
    for (i = 0; i < table->count; i++) {
        if (same_identity(&table->entries[i], &candidate)) {
            if (eligible) {
                table->entries[i] = candidate;
                return LW_RETURN(LW_SCAN, NWK_CANDIDATES_UPDATED);
            }
            for (; i + 1u < table->count; i++)
                table->entries[i] = table->entries[i + 1u];
            table->count--;
            memset(&table->entries[table->count], 0, sizeof(candidate));
            return LW_RETURN(LW_SCAN, NWK_CANDIDATES_WITHDRAWN);
        }
    }
    if (!eligible)
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_NOT_ELIGIBLE);
    if (table->count == NWK_CANDIDATES_CAPACITY)
        return LW_RETURN(LW_SCAN, NWK_CANDIDATES_FULL);
    table->entries[table->count] = candidate;
    table->count++;
    return LW_RETURN(LW_SCAN, NWK_CANDIDATES_ADDED);
}
#undef frame
#undef beacon
#undef candidate

nwk_candidates_result_t nwk_candidates_get(const nwk_candidates_t * volatile table,
                                           uint8_t index,
                                           nwk_candidate_t * volatile result)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, table, sizeof(*table), 0) ||
        !LW_IO(LW_PARENT, result, sizeof(*result), 1)) return NWK_CANDIDATES_INVALID_ARGUMENT;
#endif
    if (table == NULL || result == NULL)
        return NWK_CANDIDATES_INVALID_ARGUMENT;
    if (!valid_table(table))
        return NWK_CANDIDATES_INVALID_TABLE;
    if (index >= table->count)
        return NWK_CANDIDATES_BAD_INDEX;
    *result = table->entries[index];
    return NWK_CANDIDATES_OK;
}
