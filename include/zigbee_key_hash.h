/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZIGBEE_KEY_HASH_H
#define ZIGBEE_KEY_HASH_H

#include "zigbee_mmo.h"

#define ZIGBEE_HASH_TRANSPORT 0u
#define ZIGBEE_HASH_LOAD 2u
#define ZIGBEE_HASH_VERIFY 3u

/* R22 B.1.4 HMAC-AES-MMO, fixed16-byte key and one-byte purpose:
 * 4.5.3 transport00/load02; 4.4.10.7.4 initiator Verify-Key hash03.
 * The VERIFY result MUST NOT be used as an encryption/decryption key.
 * No other purpose, variable-length key/message, key selection, command,
 * Trust Center verification, replay/persistence or entropy implementation.
 *
 * Exactly five AES calls on success, through the genuine MMO/AES services.
 * Required complete disjoint key16/output16/info objects; immutable key.
 * Inherit zigbee_mmo.h ownership, per-block limits and retention constraints.
 * Failure preserves output; preflight also preserves info. Operational info
 * totals attempted blocks/polls and the last AES status over both hashes.
 * Overwrite private staging on exit, NOT lower AES/hardware/compiler copies.
 * Serialized foreground only, nonreentrant; no retry or recovery.
 */
zigbee_mmo_result_t zigbee_key_hash(
    const uint8_t * volatile key, uint8_t purpose, uint8_t * volatile output,
    uint32_t timeout, uint16_t poll_limit, zigbee_mmo_info_t * volatile info);

#endif
