/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "ccm_star.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>

static MCU_XDATA struct {
    uint8_t key[16], nonce[13], mac[16], block[16], cipher[16];
    uint8_t work[CCM_STAR_MAX_MESSAGE + 16], tag[16];
    aes_diagnostics_t diagnostics;
    ccm_star_limits_t limits;
    ccm_star_info_t info;
    uint8_t position;
} state;

static void account(void)
{
    state.info.polls += state.diagnostics.polls;
}

static uint8_t encrypt(void)
{
    state.info.blocks++;
    state.info.aes_status = (uint8_t)aes128_encrypt_block(
        state.key, state.block, state.cipher, state.limits.block_timeout,
        state.limits.block_polls, &state.diagnostics);
    account();
    return state.info.aes_status == AES_OK;
}

static uint8_t flush(void)
{
    uint8_t i;
    for (i = 0; i < 16; i++)
        state.block[i] ^= state.mac[i];
    if (!encrypt())
        return 0;
    memcpy(state.mac, state.cipher, 16);
    memset(state.block, 0, 16);
    state.position = 0;
    return 1;
}

static uint8_t feed(const uint8_t * volatile data, uint8_t count)
{
    uint8_t i;
    for (i = 0; i < count; i++) {
        state.block[state.position++] = data[i];
        if (state.position == 16 && !flush())
            return 0;
    }
    return 1;
}

static uint8_t authenticate(const uint8_t * volatile aad, uint8_t aad_length,
                            uint8_t size, uint8_t tag_length)
{
    memset(state.mac, 0, 16);
    state.block[0] = 1u | (uint8_t)(((tag_length - 2u) / 2u) << 3);
    if (aad_length)
        state.block[0] |= 0x40u;
    memcpy(state.block + 1, state.nonce, 13);
    state.block[14] = 0;
    state.block[15] = size;
    if (!flush())
        return 0;
    if (aad_length) {
        state.block[0] = 0;
        state.block[1] = aad_length;
        state.position = 2;
        if (!feed(aad, aad_length) || (state.position && !flush()))
            return 0;
    }
    return feed(state.work, size) && (!state.position || flush());
}

static uint8_t counter(uint8_t number)
{
    state.block[0] = 1;
    memcpy(state.block + 1, state.nonce, 13);
    state.block[14] = 0;
    state.block[15] = number;
    return encrypt();
}

static uint8_t transform(uint8_t size)
{
    uint8_t offset = 0, number = 1, i;
    while (offset < size) {
        if (!counter(number++))
            return 0;
        for (i = 0; i < 16 && offset < size; i++, offset++)
            state.work[offset] ^= state.cipher[i];
    }
    return 1;
}

static void wipe(void)
{
    volatile uint8_t MCU_XDATA *p = (volatile uint8_t MCU_XDATA *)&state;
    uint16_t i;
    for (i = 0; i < sizeof(state); i++)
        p[i] = 0;
}

ccm_star_result_t ccm_star_crypt(
    uint8_t open, const uint8_t * volatile key, const uint8_t * volatile nonce,
    const uint8_t * volatile aad, uint16_t aad_length,
    const uint8_t * volatile input, uint16_t length, uint8_t tag_length,
    uint8_t * volatile output, uint16_t capacity, uint8_t * volatile written,
    const ccm_star_limits_t * volatile limits, ccm_star_info_t * volatile info)
{
    uint8_t size, total, i, different = 0;
    ccm_star_result_t result = CCM_STAR_AES;
    if (open > 1 || key == NULL || nonce == NULL || output == NULL ||
        written == NULL || limits == NULL || info == NULL ||
        (aad == NULL && aad_length) || (input == NULL && length) ||
        !limits->block_timeout || limits->block_timeout >= TIMEBASE_HALF_RANGE ||
        !limits->block_polls)
        return CCM_STAR_ARGUMENT;
    if (tag_length != 4 && tag_length != 8 && tag_length != 16)
        return CCM_STAR_TAG;
    if (aad_length > CCM_STAR_MAX_AAD || (open && length < tag_length) ||
        length > CCM_STAR_MAX_MESSAGE + (open ? tag_length : 0u))
        return CCM_STAR_LENGTH;
    size = (uint8_t)(length - (open ? tag_length : 0u));
    total = size + (open ? 0u : tag_length);
    if (capacity < total)
        return CCM_STAR_SPACE;
    memset(&state, 0, sizeof(state));
    memcpy(state.key, key, 16);
    memcpy(state.nonce, nonce, 13);
    state.limits = *limits;
    if (size)
        memcpy(state.work, input, size);
    if (open) {
        memcpy(state.tag, input + size, tag_length);
        if (!transform(size))
            goto done;
    }
    if (!authenticate(aad, (uint8_t)aad_length, size, tag_length) || !counter(0))
        goto done;
    for (i = 0; i < tag_length; i++) {
        state.mac[i] ^= state.cipher[i];
        if (open)
            different |= state.mac[i] ^ state.tag[i];
    }
    if (open && different) {
        result = CCM_STAR_AUTH;
        goto done;
    }
    if (!open) {
        if (!transform(size))
            goto done;
        memcpy(state.work + size, state.mac, tag_length);
    }
    if (total)
        memcpy(output, state.work, total);
    *written = total;
    result = CCM_STAR_OK;
done:
    *info = state.info;
    wipe();
    return result;
}
