/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic public beacons; no radio or authenticated identity.
 */
#include "nwk_parent.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) return (uint16_t)__LINE__; } while (0)
#define CALL(c) do { uint16_t line = (c); if (line) return line; } while (0)

static const MCU_CODE uint8_t beacon[] = {
    0x00, 0x80, 0x2a, 0x34, 0x12, 0x78, 0x56, 0xff, 0x8f, 0x80, 0x00,
    0x00, 0x22, 0xac, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0xff, 0xff, 0xff, 0x7f
};
static const MCU_CODE nwk_parent_policy_t code_policy = {
    {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe}, 1, {3, 0, 0, 0}, 0, 0
};
static const MCU_CODE uint8_t cyclic_orders[6][3] = {
    {0, 100, 200}, {0, 200, 100}, {100, 0, 200},
    {100, 200, 0}, {200, 0, 100}, {200, 100, 0}
};
static nwk_candidates_t table, saved_table;
static nwk_parent_policy_t policy, saved_policy;
static nwk_parent_choice_t choice, saved_choice;
static uint8_t body[26];
static uint32_t cases;
static volatile uint8_t i, j;

static uint16_t start(uint8_t count)
{
    CHECK(nwk_candidates_init(&table, NWK_CANDIDATES_CHANNEL_MASK) == NWK_CANDIDATES_OK);
    memset(&policy, 0, sizeof(policy));
    memcpy(policy.extended_pan_id, beacon + 14, 8);
    memcpy(body, beacon, sizeof(body));
    for (i = 0; i < count; i++) {
        body[5] = i;
        CHECK(nwk_candidates_consider(&table, (uint8_t)(11u + i), 1, body, sizeof(body))
              == NWK_CANDIDATES_ADDED);
        policy.link_cost[i] = 3;
        policy.potential_mask |= (uint8_t)(1u << i);
    }
    return 0;
}

static uint16_t update(uint8_t index, uint8_t id, uint8_t depth)
{
    memcpy(body, beacon, sizeof(body));
    body[5] = index;
    body[13] = (uint8_t)(0x84u | (depth << 3));
    body[25] = id;
    CHECK(nwk_candidates_consider(&table, (uint8_t)(11u + index), 1, body, sizeof(body))
          == NWK_CANDIDATES_UPDATED);
    return 0;
}

static uint16_t select_case(nwk_parent_result_t expected, uint8_t index)
{
    saved_table = table;
    saved_policy = policy;
    memset(&choice, 0xa5, sizeof(choice));
    saved_choice = choice;
    CHECK(nwk_parent_select(&table, &policy, &choice) == expected);
    CHECK(memcmp(&table, &saved_table, sizeof(table)) == 0);
    CHECK(memcmp(&policy, &saved_policy, sizeof(policy)) == 0);
    if (expected == NWK_PARENT_OK) {
        CHECK(choice.index == index);
        CHECK(memcmp(&choice.candidate, &table.entries[index], sizeof(choice.candidate)) == 0);
    } else {
        CHECK(memcmp(&choice, &saved_choice, sizeof(choice)) == 0);
    }
    cases++;
    return 0;
}

static uint16_t common(void)
{
    CALL(start(0));
    CALL(select_case(NWK_PARENT_NONE, 0));
    policy.potential_mask = 1;
    CALL(select_case(NWK_PARENT_INVALID_ARGUMENT, 0));
    CALL(start(4));
    CALL(select_case(NWK_PARENT_OK, 0));
    policy.link_cost[2] = 1;
    CALL(select_case(NWK_PARENT_OK, 2));
    policy.link_cost[3] = 1;
    CALL(select_case(NWK_PARENT_OK, 2));
    policy.potential_mask = 11;
    CALL(select_case(NWK_PARENT_OK, 3));
    policy.potential_mask = 0;
    CALL(select_case(NWK_PARENT_NONE, 0));
    policy.potential_mask = 15;
    for (j = 0; j < 4; j++) policy.link_cost[j] = 4;
    CALL(select_case(NWK_PARENT_NONE, 0));
    policy.link_cost[0] = 3;
    CALL(select_case(NWK_PARENT_OK, 0));
    policy.extended_pan_id[0] ^= 1;
    CALL(select_case(NWK_PARENT_NONE, 0));
    policy.extended_pan_id[0] ^= 1;
    CALL(update(0, 255, 15));
    CALL(update(1, 0, 0));
    policy.link_cost[1] = 3;
    CALL(select_case(NWK_PARENT_OK, 1));
    CALL(update(1, 255, 0));
    CALL(select_case(NWK_PARENT_OK, 0)); /* Equal IDs: depth must not win. */
    CALL(update(1, 127, 0));
    CALL(select_case(NWK_PARENT_AMBIGUOUS_UPDATE, 0));
    CALL(update(0, 0, 15));
    CALL(select_case(NWK_PARENT_OK, 1));
    CALL(update(1, 128, 0));
    CALL(select_case(NWK_PARENT_AMBIGUOUS_UPDATE, 0));
    CALL(update(1, 129, 0));
    CALL(select_case(NWK_PARENT_OK, 0));
    CALL(update(1, 100, 0));
    CALL(update(2, 200, 0));
    policy.link_cost[2] = 3;
    CALL(select_case(NWK_PARENT_AMBIGUOUS_UPDATE, 0));
    policy.potential_mask = 3;
    CALL(select_case(NWK_PARENT_OK, 1));
    policy.minimum_known = 1;
    policy.minimum_update_id = 101;
    CALL(select_case(NWK_PARENT_NONE, 0));
    policy.minimum_update_id = 100;
    CALL(select_case(NWK_PARENT_OK, 1));
    policy.minimum_update_id = 228;
    CALL(select_case(NWK_PARENT_AMBIGUOUS_UPDATE, 0));
    policy.minimum_update_id = 255;
    CALL(select_case(NWK_PARENT_OK, 1));
    policy.minimum_known = 0;
    policy.minimum_update_id = 228;
    CALL(select_case(NWK_PARENT_OK, 1));

    CALL(start(3));
    for (j = 0; j < 6; j++) {
        CALL(update(0, cyclic_orders[j][0], 0));
        CALL(update(1, cyclic_orders[j][1], 15));
        CALL(update(2, cyclic_orders[j][2], 5));
        CALL(select_case(NWK_PARENT_AMBIGUOUS_UPDATE, 0));
    }
    CALL(start(1));
    memcpy(body, beacon, sizeof(body));
    body[5] = 1; body[14] ^= 1; body[25] = 255;
    CHECK(nwk_candidates_consider(&table, 12, 1, body, sizeof(body)) == NWK_CANDIDATES_ADDED);
    policy.potential_mask = 3; policy.link_cost[1] = 1;
    policy.minimum_known = 1; policy.minimum_update_id = 127;
    CALL(select_case(NWK_PARENT_OK, 0)); /* Foreign-network ambiguity is irrelevant. */
    CALL(start(1));
    CHECK(nwk_parent_select(&table, &code_policy, &choice) == NWK_PARENT_OK);
    CHECK(choice.index == 0 && choice.candidate.network.update_id == 127);

    /* Real withdrawal compacts the table: old index metadata is not retained. */
    CALL(start(2));
    policy.link_cost[1] = 1;
    CALL(select_case(NWK_PARENT_OK, 1));
    saved_choice = choice;
    memcpy(body, beacon, sizeof(body));
    body[5] = 0;
    body[8] &= 0x7f;
    CHECK(nwk_candidates_consider(&table, 11, 1, body, sizeof(body)) == NWK_CANDIDATES_WITHDRAWN);
    CHECK(memcmp(&choice, &saved_choice, sizeof(choice)) == 0);
    CALL(select_case(NWK_PARENT_INVALID_ARGUMENT, 0));
    policy.potential_mask = 1;
    policy.link_cost[0] = 1;
    CALL(select_case(NWK_PARENT_OK, 0));
    CHECK(choice.candidate.coordinator[0] == 1 && choice.candidate.channel == 12);

    CALL(start(1));
    memset(&choice, 0xa5, sizeof(choice)); saved_choice = choice;
    CHECK(nwk_parent_select(NULL, &policy, &choice) == NWK_PARENT_INVALID_ARGUMENT);
    CHECK(nwk_parent_select(&table, NULL, &choice) == NWK_PARENT_INVALID_ARGUMENT);
    CHECK(nwk_parent_select(&table, &policy, NULL) == NWK_PARENT_INVALID_ARGUMENT);
    CHECK(memcmp(&choice, &saved_choice, sizeof(choice)) == 0);
    policy.minimum_known = 2;
    CALL(select_case(NWK_PARENT_INVALID_ARGUMENT, 0));
    policy.minimum_known = 0;
    memset(policy.extended_pan_id, 0, 8);
    CALL(select_case(NWK_PARENT_INVALID_ARGUMENT, 0));
    memset(policy.extended_pan_id, 255, 8);
    CALL(select_case(NWK_PARENT_INVALID_ARGUMENT, 0));
    memcpy(policy.extended_pan_id, beacon + 14, 8);
    policy.link_cost[0] = 0;
    CALL(select_case(NWK_PARENT_INVALID_ARGUMENT, 0));
    policy.link_cost[0] = 8;
    CALL(select_case(NWK_PARENT_INVALID_ARGUMENT, 0));
    policy.link_cost[0] = 3;
    table.count = 5;
    CALL(select_case(NWK_PARENT_INVALID_TABLE, 0));
    table.count = 1;
    table.version++;
    CALL(select_case(NWK_PARENT_INVALID_TABLE, 0));
    table.version--;
    table.channel_mask = 0;
    CALL(select_case(NWK_PARENT_INVALID_TABLE, 0));
    table.channel_mask = UINT32_MAX;
    CALL(select_case(NWK_PARENT_INVALID_TABLE, 0));
    return 0;
}

#if !defined(__SDCC_mcs51)
#include <stdio.h>
#include <stdlib.h>

static uint8_t linear_oracle(const uint8_t *ids)
{
    unsigned int origin, index, offset, maximum;
    uint8_t winner, inside;
    for (origin = 0; origin < 256; origin++) {
        inside = 1; winner = 0; maximum = 0;
        for (index = 0; index < 4; index++) {
            offset = (ids[index] + 256u - origin) % 256u;
            if (offset > 127) { inside = 0; break; }
            if (offset > maximum || (offset == maximum
                    && policy.link_cost[index] < policy.link_cost[winner])) {
                maximum = offset; winner = (uint8_t)index;
            }
        }
        if (inside) return winner;
    }
    return NWK_CANDIDATES_CAPACITY;
}

static uint16_t exhaustive(void)
{
    static const uint8_t edges[] = {0, 1, 63, 64, 100, 127, 128, 129, 192, 200, 254, 255};
    unsigned int a, b, c, d, v, slot;
    uint8_t ids[4];
    uint8_t newest;
    nwk_parent_result_t expected;
    nwk_candidates_t *exact_table;
    nwk_parent_policy_t *exact_policy;
    nwk_parent_choice_t *exact_output;
    CALL(start(2));
    for (a = 0; a < 256; a++) {
        CALL(update(0, (uint8_t)a, 15));
        for (b = 0; b < 256; b++) {
            CALL(update(1, (uint8_t)b, 0));
            d = (a + 256u - b) % 256u;
            expected = d == 128 ? NWK_PARENT_AMBIGUOUS_UPDATE : NWK_PARENT_OK;
            newest = (uint8_t)(d <= 127 ? 0 : 1);
            CALL(select_case(expected, newest));
        }
    }
    CALL(start(1));
    policy.minimum_known = 1;
    for (a = 0; a < 256; a++) {
        CALL(update(0, (uint8_t)a, 0));
        for (b = 0; b < 256; b++) {
            policy.minimum_update_id = (uint8_t)b;
            d = (a + 256u - b) % 256u;
            expected = d == 128 ? NWK_PARENT_AMBIGUOUS_UPDATE :
                d > 128 ? NWK_PARENT_NONE : NWK_PARENT_OK;
            CALL(select_case(expected, 0));
        }
    }
    CALL(start(4));
    for (v = 0; v < 256; v++) {
        policy.potential_mask = (uint8_t)v;
        if (v >= 16) expected = NWK_PARENT_INVALID_ARGUMENT;
        else expected = v ? NWK_PARENT_OK : NWK_PARENT_NONE;
        newest = 0;
        if (v && v < 16) while (!(v & (1u << newest))) newest++;
        CALL(select_case(expected, newest));
    }
    for (slot = 0; slot < 4; slot++) {
        policy.potential_mask = (uint8_t)(1u << slot);
        for (v = 0; v < 256; v++) {
            policy.link_cost[slot] = (uint8_t)v;
            expected = v < 1 || v > 7 ? NWK_PARENT_INVALID_ARGUMENT :
                v > 3 ? NWK_PARENT_NONE : NWK_PARENT_OK;
            CALL(select_case(expected, (uint8_t)slot));
        }
        policy.link_cost[slot] = 3;
    }
    CALL(start(1));
    for (v = 0; v < 256; v++) {
        policy.minimum_known = (uint8_t)v;
        CALL(select_case(v > 1 ? NWK_PARENT_INVALID_ARGUMENT : NWK_PARENT_OK, 0));
    }
    CALL(start(4));
    policy.link_cost[0] = 3; policy.link_cost[1] = 1;
    policy.link_cost[2] = 2; policy.link_cost[3] = 1;
    for (a = 0; a < sizeof(edges); a++)
        for (b = 0; b < sizeof(edges); b++)
            for (c = 0; c < sizeof(edges); c++) {
                ids[0] = edges[a]; ids[1] = edges[b]; ids[2] = edges[c]; ids[3] = edges[a];
                for (slot = 0; slot < 4; slot++)
                    CALL(update((uint8_t)slot, ids[slot], (uint8_t)(3u * slot)));
                newest = linear_oracle(ids);
                CALL(select_case(newest == NWK_CANDIDATES_CAPACITY ?
                                 NWK_PARENT_AMBIGUOUS_UPDATE : NWK_PARENT_OK, newest));
            }
    CALL(start(1));
    exact_table = malloc(sizeof(*exact_table));
    exact_policy = malloc(sizeof(*exact_policy));
    exact_output = malloc(sizeof(*exact_output));
    CHECK(exact_table != NULL && exact_policy != NULL && exact_output != NULL);
    *exact_table = table; *exact_policy = policy;
    CHECK(nwk_parent_select(exact_table, exact_policy, exact_output) == NWK_PARENT_OK);
    CHECK(exact_output->index == 0);
    free(exact_output); free(exact_policy); free(exact_table);
    return 0;
}

int main(void)
{
    uint16_t line = common();
    if (!line) line = exhaustive();
    if (line) {
        fprintf(stderr, "NWK parent test failed at line %u\n", (unsigned int)line);
        return EXIT_FAILURE;
    }
    printf("NWK parent: %lu cases, exhaustive ID pairs/watermarks and linear-window oracle PASS\n",
           (unsigned long)cases);
    return EXIT_SUCCESS;
}
#else
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t nwk_parent_test_result[8];

void main(void)
{
    uint16_t line;
    nwk_parent_test_result[0] = 'N'; nwk_parent_test_result[1] = 'W';
    nwk_parent_test_result[2] = 'P'; nwk_parent_test_result[3] = '1';
    nwk_parent_test_result[4] = 1; nwk_parent_test_result[5] = 8;
    line = common();
    nwk_parent_test_result[6] = (uint8_t)line;
    nwk_parent_test_result[7] = (uint8_t)(line >> 8);
    __asm
        .globl _nwk_parent_test_done
_nwk_parent_test_done:
        nop
        sjmp _nwk_parent_test_done
    __endasm;
}
#endif
