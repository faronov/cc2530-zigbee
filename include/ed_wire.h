/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ED_WIRE_H
#define ED_WIRE_H

#include "zigbee_security.h"
#include "nwk_frame.h"
#include "aps_frame.h"
#include "banked_security.h"

#define ED_PAYLOAD_MAX 82u
#define ED_APS_COMMAND 1u
#define ED_APS_ACK 2u
#define ED_NWK_COMMAND 1u

typedef struct {
    nwk_header_t nwk;
    aps_header_t aps;
    uint8_t length;
    uint8_t payload[ED_PAYLOAD_MAX];
} ed_packet_t;

/* Extended, bounded ED wire syntax, never admission. Normal/broadcast APS
 * Data, Command, full and command-format ACK; NWK Data/Command. No multicast,
 * source routing, fragmentation or implicit forwarding. Existing bare codecs
 * and their narrower contracts are unchanged.
 */
zigbee_security_result_t ed_wire_nwk(const uint8_t * volatile frame, uint16_t length,
                                     nwk_frame_info_t * volatile info) SECURITY_FAR;
zigbee_security_result_t ed_wire_aps(const uint8_t * volatile frame, uint16_t length,
                                     aps_frame_info_t * volatile info) SECURITY_FAR;
zigbee_security_result_t ed_wire_decode(const uint8_t * volatile frame, uint16_t length,
                                        ed_packet_t * volatile packet) SECURITY_FAR;
zigbee_security_result_t ed_wire_encode(const ed_packet_t * volatile packet,
    uint8_t * volatile frame, uint16_t capacity, uint8_t * volatile length) SECURITY_FAR;

/* Real CCM*, selected R22 ENC-MIC32 only. inspect is untrusted syntax; crypt
 * checks MIC and selected source/identifier/sequence, never replay or membership.
 * Same generic-pointer/ordinary-storage and serialized foreground contract as
 * zigbee_security.h. Error preserves output/info. Wire level bits are replaced
 * with trusted level5 for authentication and zeroed after sealing.
 */
zigbee_security_result_t ed_wire_inspect(uint8_t layer, const uint8_t * volatile frame,
    uint16_t length, zigbee_security_meta_t * volatile meta) SECURITY_FAR;
zigbee_security_result_t ed_wire_crypt(volatile uint8_t open, volatile uint8_t layer,
    const zigbee_security_key_t * volatile key, const uint8_t * volatile frame, volatile uint16_t length,
    uint8_t * volatile output, uint16_t capacity, zigbee_security_info_t * volatile info) SECURITY_FAR;

/* All six public operations share returning ordinary-XDATA work. Serialize
 * them in foreground, with no ISR/reentrant use and no retained private
 * pointer. Syntax metadata stays live separately; header/frame and
 * encoded/packet/text unions reuse only sequential lifetimes, and CCM input
 * and output remain disjoint. On every public return (including errors),
 * named syntax, crypto and buffer work is volatile-wiped. This does not
 * promise to wipe all compiler temporaries or lower-service retained state.
 */

#endif
