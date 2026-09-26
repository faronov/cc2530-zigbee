/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_LINK_CHILD_WORKSPACE_INTERNAL_H
#define MAC_LINK_CHILD_WORKSPACE_INTERNAL_H
#include "mac_link_child_workspace_guard_internal.h"
#include "ed_wire.h"
#include "zigbee_key_hash.h"
#include "nv_record.h"
#if !defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
#error Explicit CHILD composition required
#endif
typedef struct {
    uint8_t key[16], nonce[13], mac[16], block[16], cipher[16];
    uint8_t work[CCM_STAR_MAX_MESSAGE+16], tag[16];
    aes_diagnostics_t diagnostics;
    ccm_star_limits_t limits;
    ccm_star_info_t info;
    uint8_t position;
} child_ccm_t;
typedef struct {
    uint8_t message[32], hash[16];
    zigbee_mmo_info_t step, total;
} child_key_hash_t;
typedef struct {
    uint8_t hash[16], block[16], cipher[16];
    aes_diagnostics_t diagnostics;
    zigbee_mmo_info_t info;
    uint32_t timeout;
    uint16_t poll_limit;
} child_mmo_t;
typedef union {
    struct {
        struct {
            union { uint8_t header[NWK_FRAME_MAX_BODY], frame[NWK_FRAME_MAX_BODY]; } wire;
            union {
                uint8_t encoded[NWK_FRAME_MAX_BODY];
                ed_packet_t packet;
                uint8_t text[NWK_FRAME_MAX_BODY];
            } body;
        } buffers;
        struct {
            uint8_t nonce[13], written;
            zigbee_security_info_t info;
        } crypto;
        union {
            struct {
                struct {
                    nwk_frame_info_t nwk;
                    aps_frame_info_t aps;
                    nwk_header_t transmit;
                    aps_header_t application;
                } syntax;
                nwk_frame_info_t nwk;
                aps_frame_info_t aps;
            } parse;
            child_ccm_t ccm;
        } engine;
    } wire;
    struct { child_key_hash_t key; child_mmo_t mmo; } hash;
    struct {
        uint8_t counter_check[128];
        struct {
            uint8_t stage[128], chunk[32], word[4], header[12], footer[8], lengths[2];
            uint32_t generations[2], crc;
        } record;
        struct { uint8_t word[4], check[32]; } writer;
        uint8_t reader[32];
    } nv;
} child_work_arena_t;
typedef struct {
    uint8_t top, pending, ancestors[CW_FRAMES], poison, first_owner, wire_stage;
} child_work_ownership_t;
extern MCU_XDATA child_work_arena_t child_work_arena;
/* Both managers and their parameter homes must precede flash_exec_reserved_end,
 * be disjoint ordinary linker allocations, and have nonzero arena addresses.
 * AES/executor exceptions remain exact source-issued loans, never a prefix
 * whitelist. The actual linked map and mixed-stack ABI remain separate gates.
 * NV poison retains every byte of the NV branch until full CPU reset. */
#endif
