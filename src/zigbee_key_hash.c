/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zigbee_key_hash.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_guard_internal.h"
#endif

static MCU_XDATA struct {
    uint8_t message[32], hash[16];
    zigbee_mmo_info_t step, total;
} state;

static void account(void)
{
    state.total.polls += state.step.polls;
    state.total.blocks += state.step.blocks;
    state.total.aes_status = state.step.aes_status;
}

static void wipe(void)
{
    volatile uint8_t MCU_XDATA *p = (volatile uint8_t MCU_XDATA *)&state;
    uint8_t i;
    for (i = 0; i < sizeof(state); i++)
        p[i] = 0;
}

zigbee_mmo_result_t zigbee_key_hash(
    const uint8_t * volatile key, uint8_t purpose, uint8_t * volatile output,
    uint32_t timeout, uint16_t poll_limit, zigbee_mmo_info_t * volatile info)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_CHILD_HASH,key,16,0) || !LW_IO(LW_CHILD_HASH,output,16,1) ||
        !LW_IO(LW_CHILD_HASH,info,sizeof(*info),1)) return ZIGBEE_MMO_ARGUMENT;
#endif
    uint8_t i;
    zigbee_mmo_result_t result;
    if (key == NULL || output == NULL || info == NULL || !timeout ||
        timeout >= TIMEBASE_HALF_RANGE || !poll_limit ||
        (purpose != ZIGBEE_HASH_TRANSPORT && purpose != ZIGBEE_HASH_LOAD &&
         purpose != ZIGBEE_HASH_VERIFY))
        return ZIGBEE_MMO_ARGUMENT;
    memset(&state, 0, sizeof(state));
    for (i = 0; i < 16; i++)
        state.message[i] = key[i] ^ 0x36u;
    state.message[16] = purpose;
    result = zigbee_mmo_hash(state.message, 17, state.hash, timeout, poll_limit, &state.step);
    account();
    if (result == ZIGBEE_MMO_OK) {
        for (i = 0; i < 16; i++) {
            state.message[i] = key[i] ^ 0x5cu;
            state.message[16u+i] = state.hash[i];
        }
        result = zigbee_mmo_hash(state.message, 32, state.hash, timeout, poll_limit, &state.step);
        account();
        if (result == ZIGBEE_MMO_OK)
            memcpy(output, state.hash, 16);
    }
    *info = state.total;
    wipe();
    return result;
}
