/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef NWK_PARENT_H
#define NWK_PARENT_H

#include "nwk_candidates.h"

typedef enum {
    NWK_PARENT_OK = 0,
    NWK_PARENT_NONE,
    NWK_PARENT_AMBIGUOUS_UPDATE,
    NWK_PARENT_INVALID_ARGUMENT,
    NWK_PARENT_INVALID_TABLE
} nwk_parent_result_t;

typedef struct {
    uint8_t extended_pan_id[8];
    uint8_t potential_mask;
    uint8_t link_cost[NWK_CANDIDATES_CAPACITY];
    uint8_t minimum_known;
    uint8_t minimum_update_id;
} nwk_parent_policy_t;

typedef struct {
    uint8_t index;
    nwk_candidate_t candidate;
} nwk_parent_choice_t;

/* Select from one immutable collector snapshot, not a scan/join procedure.
 * policy metadata is bound to its current indices; compaction/recollection
 * requires rebinding. potential_mask bit i permits candidate i (also when
 * the caller has no potential-parent field); bits beyond count are invalid.
 * Every populated entry needs a supplied link cost 1..7. This API does not
 * derive a cost from raw RSSI/correlation. Costs >3 cannot be selected.
 *
 * Target Extended PAN is wire-order, neither all-zero nor all-FF.
 * minimum_known is exactly 0/1. When 0, minimum_update_id is ignored: there
 * is no applicable known watermark for the selected network. When 1, reject
 * older candidates relative to that watermark; distance 128 is ambiguous.
 *
 * Update ordering is modulo256 with a strict half-range project policy:
 * differences 1..127 are newer, 129..255 older, 128 unordered. Select only
 * an ID equal/newer than EVERY eligible ID, not a pairwise running maximum.
 * A cyclic/non-orderable set returns AMBIGUOUS_UPDATE. This cannot recover
 * lost wraps, establish real age or authenticate advertised freshness.
 * Among equal newest IDs: lowest link cost, then lowest snapshot index.
 * Profile2 depth is never ranked. Collector eligibility remains unchanged.
 *
 * OK copies a candidate; it is not MAC/BDB/security acceptance or membership.
 * All other outcomes preserve the entire output. Inputs are never modified.
 * Pointers denote complete, accessible, disjoint objects. Writable objects
 * are caller storage, not CODE/MMIO/status/IRAM alias/compiler scratch.
 * Foreground/non-reentrant under SDCC, serialized with collector/codecs.
 * No allocation, retained pointer, time, radio, NIB mutation or implicit retry.
 */
nwk_parent_result_t nwk_parent_select(const nwk_candidates_t * volatile table,
                                      const nwk_parent_policy_t * volatile policy,
                                      nwk_parent_choice_t * volatile output);

#endif
