/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zigbee_mmo.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_guard_internal.h"
#endif

static MCU_XDATA struct {
    uint8_t hash[16], block[16], cipher[16];
    aes_diagnostics_t diagnostics;
    zigbee_mmo_info_t info;
    uint32_t timeout;
    uint16_t poll_limit;
} state;

static void account(void)
{
    state.info.polls += state.diagnostics.polls;
}

static uint8_t compress(void)
{
    uint8_t i;
    state.info.blocks++;
    state.info.aes_status = (uint8_t)aes128_encrypt_block(
        state.hash, state.block, state.cipher, state.timeout, state.poll_limit, &state.diagnostics);
    account();
    if (state.info.aes_status != AES_OK)
        return 0;
    for (i = 0; i < 16; i++)
        state.hash[i] = state.cipher[i] ^ state.block[i];
    return 1;
}

static void wipe(void)
{
    volatile uint8_t MCU_XDATA *p = (volatile uint8_t MCU_XDATA *)&state;
    uint8_t i;
    for (i = 0; i < sizeof(state); i++)
        p[i] = 0;
}

zigbee_mmo_result_t zigbee_mmo_hash(
    const uint8_t * volatile input, uint16_t length, uint8_t * volatile output,
    uint32_t timeout, uint16_t poll_limit, zigbee_mmo_info_t * volatile info)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_CHILD_MMO,input,length,0) || !LW_IO(LW_CHILD_MMO,output,16,1) ||
        !LW_IO(LW_CHILD_MMO,info,sizeof(*info),1)) return ZIGBEE_MMO_ARGUMENT;
#endif
    uint8_t i, offset = 0, remaining;
    zigbee_mmo_result_t result = ZIGBEE_MMO_AES;
    if ((input == NULL && length) || output == NULL || info == NULL ||
        !timeout || timeout >= TIMEBASE_HALF_RANGE || !poll_limit)
        return ZIGBEE_MMO_ARGUMENT;
    if (length > ZIGBEE_MMO_MAX)
        return ZIGBEE_MMO_LENGTH;
    memset(&state, 0, sizeof(state));
    state.timeout = timeout;
    state.poll_limit = poll_limit;
    while (length-offset >= 16) {
        for (i = 0; i < 16; i++)
            state.block[i] = input[offset+i];
        if (!compress())
            goto done;
        offset += 16;
    }
    remaining = (uint8_t)(length-offset);
    memset(state.block, 0, 16);
    for (i = 0; i < remaining; i++)
        state.block[i] = input[offset+i];
    state.block[remaining] = 0x80;
    if (remaining >= 14) {
        if (!compress())
            goto done;
        memset(state.block, 0, 16);
    }
    state.block[14] = (uint8_t)(length >> 5);
    state.block[15] = (uint8_t)(length << 3);
    if (!compress())
        goto done;
    memcpy(output, state.hash, 16);
    result = ZIGBEE_MMO_OK;
done:
    *info = state.info;
    wipe();
    return result;
}

zigbee_mmo_result_t install_code_derive(
    const uint8_t * volatile code, uint16_t length, uint8_t * volatile output,
    uint32_t timeout, uint16_t poll_limit, zigbee_mmo_info_t * volatile info)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_CHILD_INSTALL,code,length,0) || !LW_IO(LW_CHILD_INSTALL,output,16,1) ||
        !LW_IO(LW_CHILD_INSTALL,info,sizeof(*info),1)) return ZIGBEE_MMO_ARGUMENT;
#endif
    uint16_t crc = 0xffffu;
    uint8_t i, bit;
    if (code == NULL)
        return ZIGBEE_MMO_ARGUMENT;
    if (length != INSTALL_CODE_SIZE)
        return ZIGBEE_MMO_LENGTH;
    for (i = 0; i < 16; i++) {
        crc ^= code[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0x8408u : 0);
    }
    crc ^= 0xffffu;
    if (code[16] != (uint8_t)crc || code[17] != (uint8_t)(crc >> 8))
        return ZIGBEE_MMO_CRC;
    return zigbee_mmo_hash(code, length, output, timeout, poll_limit, info);
}
