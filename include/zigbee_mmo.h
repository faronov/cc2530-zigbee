/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZIGBEE_MMO_H
#define ZIGBEE_MMO_H

#include "aes.h"

#define ZIGBEE_MMO_MAX 32u
#define INSTALL_CODE_SIZE 18u

typedef enum {
    ZIGBEE_MMO_OK = 0, ZIGBEE_MMO_ARGUMENT, ZIGBEE_MMO_LENGTH,
    ZIGBEE_MMO_CRC, ZIGBEE_MMO_AES
} zigbee_mmo_result_t;

typedef struct {
    uint32_t polls;
    uint8_t blocks, aes_status;
} zigbee_mmo_info_t;

/* R22 B.6 AES-MMO: zero IV, 16-byte digest, 0..32 input bytes, short-message
 * padding with big-endian 16-bit BIT length. At most three real AES calls.
 * NULL input is permitted only at length0. Output is a complete writable
 * 16-byte object. All other pointers required; all complete objects disjoint,
 * inputs immutable, exclude private/libc/status/MMIO/IRAM alias storage.
 * Failure preserves output; preflight failures preserve info as well.
 * Operational results report attempted AES blocks, polls and AES status.
 *
 * Serialized foreground, nonreentrant; inherit the full aes.h ownership,
 * reset/history/clock/IRQ/DMA_PAUSE and per-block finite timeout/poll contract.
 * Link timebase,AES before this module/callers. No fallback, retry or recovery.
 * Private hash/block buffers are overwritten on operational exit, NOT lower
 * AES/hardware/compiler copies. This is not full secure erasure or entropy.
 */
zigbee_mmo_result_t zigbee_mmo_hash(
    const uint8_t * volatile input, uint16_t length, uint8_t * volatile output,
    uint32_t timeout, uint16_t poll_limit, zigbee_mmo_info_t * volatile info);

/* BDB3.0.1 10.1.1-2: exactly16 install-code octets plus two LE CRC octets.
 * CRC16: poly1021 reflected, init/xorFFFF. Hash ALL18 octets after CRC passes.
 * No legacy6/8/12-byte lengths, text parser, provisioning, random generation,
 * key-state verification or membership. A valid CRC is not authentication.
 * Error precedence for simultaneously invalid arguments is unspecified.
 */
zigbee_mmo_result_t install_code_derive(
    const uint8_t * volatile code, uint16_t length, uint8_t * volatile output,
    uint32_t timeout, uint16_t poll_limit, zigbee_mmo_info_t * volatile info);

#endif
