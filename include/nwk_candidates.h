/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef NWK_CANDIDATES_H
#define NWK_CANDIDATES_H

#include <stdint.h>
#include "nwk_beacon.h"

#define NWK_CANDIDATES_CAPACITY 4u
#define NWK_CANDIDATES_CHANNEL_MASK UINT32_C(0x07fff800)
#define NWK_CANDIDATES_PRO_PROFILE 2u
#define NWK_CANDIDATES_VERSION 1u

typedef enum {
    NWK_CANDIDATES_OK = 0,
    NWK_CANDIDATES_ADDED,
    NWK_CANDIDATES_UPDATED,
    NWK_CANDIDATES_WITHDRAWN,
    NWK_CANDIDATES_NOT_ELIGIBLE,
    NWK_CANDIDATES_FULL,
    NWK_CANDIDATES_BAD_CRC,
    NWK_CANDIDATES_INVALID_ARGUMENT,
    NWK_CANDIDATES_INVALID_TABLE,
    NWK_CANDIDATES_OUTSIDE_MASK,
    NWK_CANDIDATES_MAC_REJECTED,
    NWK_CANDIDATES_NOT_BEACON,
    NWK_CANDIDATES_NWK_REJECTED,
    NWK_CANDIDATES_BAD_INDEX
} nwk_candidates_result_t;

/* Copied, unauthenticated metadata, NOT a selected/compatible parent.
 * Arrays use wire order; short coordinator addresses have six zero tail bytes.
 * Pending-address contents are validated but not retained; only counts survive.
 */
typedef struct {
    uint8_t channel;
    uint16_t pan_id;
    uint8_t address_mode;
    uint8_t coordinator[8];
    uint16_t superframe;
    uint8_t sequence;
    uint8_t mac_flags;
    uint8_t gts_permit;
    uint8_t short_pending;
    uint8_t extended_pending;
    nwk_beacon_t network;
} nwk_candidate_t;

/* Caller-owned table. Public fields are read-only diagnostics after init.
 * Entries [0,count) are compact in insertion order; unused entries are zero.
 * No stable handles: withdrawal shifts subsequent indices down by one.
 * Header validation bounds accesses; it is not corruption detection/authentication.
 */
typedef struct {
    uint8_t version;
    uint32_t channel_mask;
    uint8_t count;
    nwk_candidate_t entries[NWK_CANDIDATES_CAPACITY];
} nwk_candidates_t;

/* Init/re-init clears only this table. Mask is a nonempty subset of bits 11..26
 * (bit number = channel number), not a tune/scan request or a scan completion.
 */
nwk_candidates_result_t nwk_candidates_init(nwk_candidates_t * volatile table,
                                            uint32_t channel_mask);

/* body is the entire FCS-free legacy MAC body, without PHR/radio metadata.
 * crc_valid must be exactly 0 or 1. A truthful 1 is supplied by the caller;
 * no PHY CRC is computed, assumed or repaired here. 0 never changes the table.
 * Actual mac_frame/mac_beacon/nwk_beacon decoders must all succeed.
 *
 * Preliminary filter: profile2, BO15, Association Permit AND ED Capacity.
 * SO is ignored for BO15; update/depth/offset are raw, not ranking/freshness.
 * Identity: channel/PAN/Extended PAN/source mode/source wire octets.
 * Eligible duplicate replaces metadata; ineligible valid duplicate withdraws.
 * New eligible identity at capacity returns FULL without eviction. Filtered/malformed
 * or CRC-failed input cannot erase other identities. See docs/NWK_CANDIDATES.md.
 */
nwk_candidates_result_t nwk_candidates_consider(nwk_candidates_t * volatile table,
                                                uint8_t channel, uint8_t crc_valid,
                                                const uint8_t * volatile body,
                                                uint16_t length);

/* Copies exactly one entry, no borrowed spans or pool pointers.
 * Init/consider errors preserve table; get errors preserve output.
 * ADDED/UPDATED/WITHDRAWN explicitly report mutation, not procedure success.
 * All pointer arguments must denote actual, disjoint accessible objects;
 * writable objects must be ordinary caller storage, never compiler scratch,
 * CODE, MMIO, reserved status or the IRAM alias. Non-NULL is not proof of that.
 * Foreground only, serialized with all three non-reentrant codecs under SDCC.
 * No allocation, timer, radio adapter, scan, association or membership state.
 */
nwk_candidates_result_t nwk_candidates_get(const nwk_candidates_t * volatile table,
                                           uint8_t index,
                                           nwk_candidate_t * volatile result);

#endif
