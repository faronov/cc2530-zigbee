/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZIGBEE_SECURITY_H
#define ZIGBEE_SECURITY_H

#include "ccm_star.h"

#define ZIGBEE_SECURITY_NWK 0u
#define ZIGBEE_SECURITY_APS 1u

typedef enum {
    ZIGBEE_SECURITY_OK = 0, ZIGBEE_SECURITY_ARGUMENT, ZIGBEE_SECURITY_LENGTH,
    ZIGBEE_SECURITY_HEADER, ZIGBEE_SECURITY_LEVEL, ZIGBEE_SECURITY_AUX,
    ZIGBEE_SECURITY_COUNTER, ZIGBEE_SECURITY_SELECTOR, ZIGBEE_SECURITY_SOURCE,
    ZIGBEE_SECURITY_SPACE, ZIGBEE_SECURITY_AES, ZIGBEE_SECURITY_AUTH,
    ZIGBEE_SECURITY_CCM
} zigbee_security_result_t;

typedef struct {
    uint8_t key[16], source[8];
    uint32_t counter;
    uint8_t level, key_identifier, key_sequence, extended_nonce;
    ccm_star_limits_t limits;
} zigbee_security_key_t;

typedef struct {
    uint32_t counter;
    uint8_t source[8];
    uint8_t level, key_identifier, key_sequence, extended_nonce;
    uint8_t header_length, auxiliary_length, payload_length;
} zigbee_security_meta_t;

typedef struct {
    zigbee_security_meta_t meta;
    ccm_star_info_t crypto;
    uint8_t length;
} zigbee_security_info_t;

/* Syntax only, never authentication/key selection/replay admission. level is
 * trusted configuration, NOT the received low three control bits (R22 replaces
 * them). Absent source/sequence are zero; inspect presence, not those zeroes.
 * Supports existing NWK Data and APS unicast Data plus the two-byte unicast
 * APS Command header. No command payload processing or ACK engine.
 */
zigbee_security_result_t zigbee_security_inspect(
    uint8_t layer, uint8_t level, const uint8_t * volatile frame, uint16_t length,
    zigbee_security_meta_t * volatile meta);

/* open=0 adds protection to an unsecured NPDU/APDU; open=1 verifies/removes
 * the auxiliary header/MIC and clears the security flag in a normalized copy
 * for the existing codecs. Only OK authorizes use of that output; even OK is
 * key-group authentication, NOT replay/peer/endpoint/BDB admission.
 *
 * key.key is the EFFECTIVE AES key: network/data or already-derived transport/
 * load key, never an implicit link-key derivation. The caller must bind source,
 * identifier, sequence and level to selected material. On RX, source must match
 * an included source; if absent it supplies the mapped IEEE address. RX ignores
 * key.counter, but matches identifier/sequence and extended-nonce policy.
 * On TX, counter must already be durably reserved/consumed even if this call
 * later fails. This stateless module never allocates/increments a counter.
 *
 * Levels 1/2/3 and 5/6/7 are supported; 0/4 fail, never release unauthenticated
 * plaintext. NWK uses key-id1/extended1; APS Data uses key-id0; APS Command uses
 * key-id0/2/3 and extended1. Effective counter FFFFFFFF is always rejected.
 * Wire level bits are zero on TX and replaced, not trusted, on RX.
 *
 * Every error preserves output/info. All pointers required, complete disjoint
 * objects, serialized foreground, same AES/ownership/retention contract as
 * ccm_star.h. Caller supplies true sizes and keeps inputs immutable. No heap,
 * retry, secret diagnostics, key-state/replay mutation, radio or membership.
 */
zigbee_security_result_t zigbee_security_crypt(
    volatile uint8_t open, volatile uint8_t layer, const zigbee_security_key_t * volatile key,
    const uint8_t * volatile frame, uint16_t length,
    uint8_t * volatile output, uint16_t capacity,
    zigbee_security_info_t * volatile info);

#endif
