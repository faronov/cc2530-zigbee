/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "nwk_parent.h"

#include <stddef.h>
#include <string.h>

nwk_parent_result_t nwk_parent_select(const nwk_candidates_t * volatile table,
                                      const nwk_parent_policy_t * volatile policy,
                                      nwk_parent_choice_t * volatile output)
{
    nwk_candidate_t candidate;
    nwk_candidates_result_t fetched;
    uint8_t updates[NWK_CANDIDATES_CAPACITY];
    volatile uint8_t i, j, eligible, best, delta, dominates, any, all;

    if (table == NULL || policy == NULL || output == NULL || policy->minimum_known > 1u)
        return NWK_PARENT_INVALID_ARGUMENT;
    any = 0;
    all = 0xff;
    for (i = 0; i < 8u; i++) {
        any |= policy->extended_pan_id[i];
        all &= policy->extended_pan_id[i];
    }
    if (!any || all == 0xffu)
        return NWK_PARENT_INVALID_ARGUMENT;
    fetched = nwk_candidates_get(table, 0, &candidate);
    if (fetched != NWK_CANDIDATES_OK && fetched != NWK_CANDIDATES_BAD_INDEX)
        return NWK_PARENT_INVALID_TABLE;
    if ((policy->potential_mask & (uint8_t)~((1u << table->count) - 1u)) != 0u)
        return NWK_PARENT_INVALID_ARGUMENT;
    for (i = 0; i < table->count; i++)
        if (policy->link_cost[i] < 1u || policy->link_cost[i] > 7u)
            return NWK_PARENT_INVALID_ARGUMENT;

    eligible = 0;
    for (i = 0; i < table->count; i++) {
        if (!(policy->potential_mask & (1u << i)) || policy->link_cost[i] > 3u)
            continue;
        if (nwk_candidates_get(table, i, &candidate) != NWK_CANDIDATES_OK)
            return NWK_PARENT_INVALID_TABLE;
        if (memcmp(candidate.network.extended_pan_id, policy->extended_pan_id, 8) != 0)
            continue;
        if (policy->minimum_known) {
            delta = (uint8_t)(candidate.network.update_id - policy->minimum_update_id);
            if (delta == 128u)
                return NWK_PARENT_AMBIGUOUS_UPDATE;
            if (delta > 128u)
                continue;
        }
        updates[i] = candidate.network.update_id;
        eligible |= (uint8_t)(1u << i);
    }
    if (!eligible)
        return NWK_PARENT_NONE;
    best = NWK_CANDIDATES_CAPACITY;
    for (i = 0; i < table->count; i++) {
        if (!(eligible & (1u << i)))
            continue;
        dominates = 1;
        for (j = 0; j < table->count; j++) {
            if (!(eligible & (1u << j)))
                continue;
            delta = (uint8_t)(updates[i] - updates[j]);
            if (delta >= 128u) {
                dominates = 0;
                break;
            }
        }
        if (dominates && (best == NWK_CANDIDATES_CAPACITY
                         || policy->link_cost[i] < policy->link_cost[best]))
            best = i;
    }
    if (best == NWK_CANDIDATES_CAPACITY)
        return NWK_PARENT_AMBIGUOUS_UPDATE;
    if (nwk_candidates_get(table, best, &candidate) != NWK_CANDIDATES_OK)
        return NWK_PARENT_INVALID_TABLE;
    output->candidate = candidate;
    output->index = best;
    return NWK_PARENT_OK;
}
