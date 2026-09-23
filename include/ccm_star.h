/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CCM_STAR_H
#define CCM_STAR_H

#include "aes.h"

#define CCM_STAR_MAX_AAD 132u
#define CCM_STAR_MAX_MESSAGE 116u
#define CCM_STAR_NONCE_SIZE 13u

typedef enum {
    CCM_STAR_OK = 0, CCM_STAR_ARGUMENT, CCM_STAR_LENGTH, CCM_STAR_TAG,
    CCM_STAR_SPACE, CCM_STAR_AES, CCM_STAR_AUTH
} ccm_star_result_t;

typedef struct {
    uint32_t block_timeout;
    uint16_t block_polls;
} ccm_star_limits_t;

typedef struct {
    uint32_t polls;
    uint8_t blocks, aes_status;
} ccm_star_info_t;

/* R22 Annex A: AES-128, L=2, M=4/8/16. No unauthenticated M=0 mode.
 * Nonce uniqueness/key selection/replay and durable counters belong to the
 * caller. Authentication-only Zigbee frames put their payload in AAD and
 * supply an empty message; their MIC is still masked with AES(Key,A0).
 *
 * open=0 seals message -> ciphertext || MIC; open=1 verifies that form.
 * length counts the whole input (including MIC when opening); written counts
 * the whole output. All failures preserve output/written. Preflight failures
 * preserve info too; operational results publish only counts/AES status.
 * NULL aad/input is allowed only with zero length; all other pointers required.
 * All complete objects are disjoint, immutable during the foreground call,
 * and exclude linked private/libc scratch, status/MMIO and IRAM aliases.
 *
 * Each real AES call has the supplied finite deadline/poll cap. At most 27
 * block calls, with no retry/fallback/recovery. Inherit aes.h clock/DMA/IRQ/
 * history/debug-DMA_PAUSE requirements. Link timebase,aes before this module.
 * Private CCM byte buffers are overwritten on every operational exit; this
 * does NOT erase AES hardware/lower-driver/compiler copies (see aes.h).
 * Serialized, nonreentrant; never an ISR or a successful security stub.
 */
ccm_star_result_t ccm_star_crypt(
    uint8_t open, const uint8_t * volatile key, const uint8_t * volatile nonce,
    const uint8_t * volatile aad, uint16_t aad_length,
    const uint8_t * volatile input, uint16_t length, uint8_t tag_length,
    uint8_t * volatile output, uint16_t capacity, uint8_t * volatile written,
    const ccm_star_limits_t * volatile limits, ccm_star_info_t * volatile info);

#endif
