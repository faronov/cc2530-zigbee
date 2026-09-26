/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "security_keys.h"
#include "security_counter.h"
#include "zigbee_key_hash.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_internal.h"
#define w link_work_arena.keys
#define KEY_ENTER() LW_ENTER(LW_KEYS, SECURITY_KEYS_STATE)
#define KEY_EXTERNAL(p,n) (!(p) || link_work_external((p),(n)))
#define KEY_WRITE(p,n) (!(p) || LW_IO(LW_NONE,(p),(n),1))
#define KEY_CALL(g,e) LW_CALL(g,e)
#define ed_wire_nwk(...) KEY_CALL(LW_WIRE_NWK, ed_wire_nwk(__VA_ARGS__))
#define ed_wire_decode(...) KEY_CALL(LW_WIRE_DECODE, ed_wire_decode(__VA_ARGS__))
#define ed_wire_encode(...) KEY_CALL(LW_WIRE_ENCODE, ed_wire_encode(__VA_ARGS__))
#define ed_wire_inspect(...) KEY_CALL(LW_WIRE_INSPECT, ed_wire_inspect(__VA_ARGS__))
#define ed_wire_crypt(...) KEY_CALL(LW_WIRE_CRYPT, ed_wire_crypt(__VA_ARGS__))
#else
#define KEY_ENTER() ((void)0)
#define KEY_EXTERNAL(p,n) 1
#define KEY_WRITE(p,n) 1
#define KEY_CALL(g,e) (e)
#endif

/* Serialized schema, never a native struct image. See SECURITY_KEYS.md. */
#define OWN 0u
#define TC 8u
#define NK 16u
#define LK 48u
#define PK 64u
#if defined(CC2530_MAC_LINK_WORKSPACE)
typedef char workspace_schema[(LK == LINK_WORK_LK_OFFSET && PK == LINK_WORK_PK_OFFSET) ? 1 : -1];
#endif
#define NF 80u
#define AF 88u
#define EP 92u
#define PAN 100u
#define ADDR 102u
#define CHANNEL 104u
#define UPDATE 105u
#define SEQ 106u
#define FLAGS 108u
#define PHASE 109u
#define MAGIC 110u
#define ACTIVE 4u
#define NEWER 8u
#define RETIRED 16u
#define WAS_VERIFIED 32u
#define REJOIN_ALLOWED 64u
#define BOTH_IEEE (NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE)
#define PHASE_MASK 0x0fu
#define PARENT_MASK 0x70u
#define TIMEOUT_PENDING 0x80u

/* No operational secrets survive a returning public call in this module.
 * The counter/journal still retain their real persistent/cached payloads. */
#if !defined(CC2530_MAC_LINK_WORKSPACE)
static MCU_XDATA struct {
    uint8_t record[112], a[116], b[116], hash_key[16];
    ed_packet_t packet;
    nwk_frame_info_t nwk;
    zigbee_security_key_t key;
    zigbee_security_meta_t meta;
    zigbee_security_info_t info;
    zigbee_mmo_info_t hash_info;
    ccm_star_limits_t limits;
    uint32_t counter;
    uint16_t polls;
    uint8_t size, event, pending, slot, secured, aps_secured, key_id;
} w;
#endif
typedef char phase_fits_packed_byte[(SECURITY_KEYS_REJOINING <= PHASE_MASK) ? 1 : -1];
typedef char payload_stays_112[(sizeof(w.record) == SECURITY_COUNTER_PAYLOAD_MAX) ? 1 : -1];
static MCU_XDATA uint8_t security_keys_phase, security_keys_result;

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
}
static void put32(uint8_t *p, uint32_t n)
{
    uint8_t i;
    for (i = 0; i < 4; i++) { p[i] = (uint8_t)n; n >>= 8; }
}
static uint8_t same(const uint8_t *a, const uint8_t *b, uint8_t n)
{
    uint8_t d = 0, i;
    for (i = 0; i < n; i++) d |= a[i] ^ b[i];
    return d == 0;
}
static uint8_t all(const uint8_t *p, uint8_t n, uint8_t value)
{
    uint8_t d = 0, i;
    for (i = 0; i < n; i++) d |= p[i] ^ value;
    return d == 0;
}
static uint8_t identity(const uint8_t *p)
{
    return !all(p, 8, 0) && !all(p, 8, 255);
}
static uint8_t active_slot(void) { return (w.record[FLAGS] & ACTIVE) ? 1 : 0; }
static uint8_t network(void) { return (w.record[FLAGS] & 3) != 0; }
static uint8_t verified(void) { return (w.record[FLAGS] & WAS_VERIFIED) != 0; }
static uint8_t phase(void) { return w.record[PHASE] & PHASE_MASK; }
static uint8_t parent_information(void) { return (w.record[PHASE] >> 4) & 7u; }
static void set_phase(uint8_t value)
{
    if (value <= SECURITY_KEYS_ASSOCIATED) w.record[PHASE] = value;
    else {
        w.record[PHASE] = (w.record[PHASE] & (uint8_t)~PHASE_MASK) | value;
        if (value == SECURITY_KEYS_LEFT || value == SECURITY_KEYS_REJOINING)
            w.record[PHASE] &= (uint8_t)~TIMEOUT_PENDING;
    }
}

static security_keys_result_t finish(security_keys_result_t result)
{
#if !defined(CC2530_MAC_LINK_WORKSPACE)
    volatile uint8_t MCU_XDATA *p = (volatile uint8_t MCU_XDATA *)&w;
    uint16_t i;
    for (i = 0; i < sizeof(w); i++) p[i] = 0;
#endif
    if (security_keys_phase != SECURITY_KEYS_FAILED) security_keys_result = (uint8_t)result;
#if defined(CC2530_MAC_LINK_WORKSPACE)
    return LW_RETURN(LW_KEYS, result);
#else
    return result;
#endif
}
#if defined(CC2530_HOST_TEST)
/* Host peripheral harness only: emulate CPU static-storage initialization,
 * including an interrupted operation's private staging. Not a target/public
 * recovery API, and never called between live crypto/NV operations. */
void security_keys_host_power_cycle(void)
{
    security_keys_phase = SECURITY_KEYS_COLD;
#if defined(CC2530_MAC_LINK_WORKSPACE)
    security_keys_result = SECURITY_KEYS_OK;
    link_work_host_reset();
#else
    (void)finish(SECURITY_KEYS_OK);
#endif
}
#endif
static security_keys_result_t fault(security_keys_result_t result)
{
    security_keys_phase = SECURITY_KEYS_FAILED;
    security_keys_result = (uint8_t)result;
    return result;
}
static uint8_t valid_config(void)
{
    uint16_t address = u16(w.record+ADDR);
    return identity(w.record+OWN) && identity(w.record+TC) &&
        !same(w.record+OWN, w.record+TC, 8) && identity(w.record+EP) &&
        u16(w.record+PAN) != 0xffffu && address != 0 &&
        (address < 0xfff8u || address == 0xffffu) &&
        w.record[CHANNEL] >= 11 && w.record[CHANNEL] <= 26;
}
static uint8_t valid_record(void)
{
    uint8_t f = w.record[FLAGS], p = phase(), i;
    if (w.size != 112 || w.record[MAGIC] != 'K' || w.record[MAGIC+1] != 2 ||
        !valid_config() || (f & 0x80) || p < SECURITY_KEYS_PROVISIONED ||
        p > SECURITY_KEYS_REJOINING || all(w.record+LK, 16, 0))
        return 0;
    if (p <= SECURITY_KEYS_ASSOCIATED || (p == SECURITY_KEYS_LEFT && !network())) {
        if (f || !all(w.record+NK, 32, 0) || !all(w.record+PK, 28, 0) ||
            w.record[SEQ] || w.record[SEQ+1] ||
            (w.record[PHASE] & (PARENT_MASK | TIMEOUT_PENDING))) return 0;
    } else {
        if (!(f & (uint8_t)(1u << active_slot()))) return 0;
        if ((f & NEWER) && (f & 3) != 3) return 0;
        if ((f & 3) == 3 &&
            (w.record[SEQ] == w.record[SEQ+1] ||
             same(w.record+NK, w.record+NK+16, 16))) return 0;
        for (i = 0; i < 2; i++) {
            if (f & (uint8_t)(1u << i)) {
                if (all(w.record+NK+16u*i, 16, 0) ||
                    same(w.record+NK+16u*i, w.record+LK, 16) ||
                    same(w.record+NK+16u*i, w.record+PK, 16)) return 0;
            } else if (!all(w.record+NK+16u*i, 16, 0) ||
                       u32(w.record+NF+4u*i) || w.record[SEQ+i]) return 0;
        }
    }
    if (p == SECURITY_KEYS_PROVISIONED || (p == SECURITY_KEYS_LEFT && !network())) {
        /* Provisioning or quiet abandonment may precede MAC association. */
    } else if (u16(w.record+ADDR) == 0xffffu) return 0;
    if (p == SECURITY_KEYS_PROVISIONAL || p == SECURITY_KEYS_WAIT_CONFIRM ||
        (p == SECURITY_KEYS_LEFT && !(f & RETIRED) && !all(w.record+PK, 16, 0))) {
        /* An abandoned pending key remains history, never a usable key or
         * an outstanding Confirm context after LEFT. */
        if ((f & RETIRED) || all(w.record+PK, 16, 0) ||
            same(w.record+PK, w.record+LK, 16)) return 0;
    } else if (f & RETIRED) {
        if (!verified() || all(w.record+PK, 16, 0) ||
            same(w.record+PK, w.record+LK, 16)) return 0;
    } else if (!all(w.record+PK, 16, 0)) return 0;
    if (p == SECURITY_KEYS_VERIFIED && !verified()) return 0;
    if (verified() && p < SECURITY_KEYS_REQUESTED) return 0;
    if ((f & REJOIN_ALLOWED) && p != SECURITY_KEYS_LEFT && p != SECURITY_KEYS_REJOINING)
        return 0;
    if (p == SECURITY_KEYS_REJOINING && !verified()) return 0;
    if ((w.record[PHASE] & TIMEOUT_PENDING) &&
        (p == SECURITY_KEYS_LEFT || p == SECURITY_KEYS_REJOINING)) return 0;
    return 1;
}
static security_keys_result_t load(void)
{
    if (security_keys_phase == SECURITY_KEYS_FAILED)
        return (security_keys_result_t)security_keys_result;
    if (security_keys_phase < SECURITY_KEYS_PROVISIONED)
        return SECURITY_KEYS_STATE;
    if (KEY_CALL(LW_COUNTER_READ, security_counter_read(w.record, 112, &w.size)) != SECURITY_COUNTER_OK)
        return fault(SECURITY_KEYS_STORAGE);
    if (!valid_record() || phase() != security_keys_phase)
        return fault(SECURITY_KEYS_FORMAT);
    return SECURITY_KEYS_OK;
}
static security_keys_result_t save(void)
{
    if (KEY_CALL(LW_COUNTER_SAVE, security_counter_save(w.record, 112, w.polls)) != SECURITY_COUNTER_OK)
        return fault(SECURITY_KEYS_STORAGE);
    security_keys_phase = phase();
    return SECURITY_KEYS_OK;
}
static uint8_t limits_ok(const ccm_star_limits_t *limits, uint16_t polls)
{
    if (!limits || !limits->block_timeout || limits->block_timeout >= TIMEBASE_HALF_RANGE ||
        !limits->block_polls || !polls) return 0;
    w.limits = *limits; w.polls = polls;
    return 1;
}
static security_keys_result_t crypto_result(zigbee_security_result_t r)
{
    if (r == ZIGBEE_SECURITY_OK) return SECURITY_KEYS_OK;
    if (r == ZIGBEE_SECURITY_AUTH) return SECURITY_KEYS_AUTH;
    if (r == ZIGBEE_SECURITY_SPACE) return SECURITY_KEYS_SPACE;
    if (r == ZIGBEE_SECURITY_AES || r == ZIGBEE_SECURITY_CCM) return SECURITY_KEYS_CRYPTO;
    return SECURITY_KEYS_FORMAT;
}
static security_keys_result_t key(volatile uint8_t offset, volatile uint8_t identifier, uint8_t sending)
{
    volatile uint8_t purpose;
    memset(&w.key, 0, sizeof(w.key));
    w.key.limits = w.limits;
    w.key.level = 5; w.key.extended_nonce = 1; w.key.key_identifier = identifier;
    memcpy(w.key.source, w.record+(sending ? OWN : TC), 8);
    if (identifier == 2 || identifier == 3) {
        memcpy(w.hash_key, w.record+offset, 16);
        purpose = identifier == 2 ? ZIGBEE_HASH_TRANSPORT : ZIGBEE_HASH_LOAD;
        if (KEY_CALL(LW_HASH_KEY, zigbee_key_hash(w.hash_key, purpose,
            w.key.key, w.limits.block_timeout, w.limits.block_polls, &w.hash_info)) != ZIGBEE_MMO_OK)
            return SECURITY_KEYS_CRYPTO;
    } else memcpy(w.key.key, w.record+offset, 16);
    return SECURITY_KEYS_OK;
}
static inline security_keys_result_t take(uint8_t domain)
{
    security_counter_result_t r = KEY_CALL(LW_COUNTER_TAKE, security_counter_take(domain, &w.counter, w.polls));
    if (r == SECURITY_COUNTER_EXHAUSTED) return SECURITY_KEYS_EXHAUSTED;
    if (r != SECURITY_COUNTER_OK) return fault(SECURITY_KEYS_STORAGE);
    w.key.counter = w.counter;
    return SECURITY_KEYS_OK;
}

security_keys_result_t security_keys_open(void) SECURITY_FAR
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    KEY_ENTER();
#endif
    security_counter_result_t r;
    security_keys_result_t result;
    if (security_keys_phase != SECURITY_KEYS_COLD) return finish(SECURITY_KEYS_STATE);
    memset(&w, 0, sizeof(w));
    r = security_counter_open();
    if (r == SECURITY_COUNTER_EMPTY) {
        security_keys_phase = SECURITY_KEYS_UNPROVISIONED;
        return finish(SECURITY_KEYS_EMPTY);
    }
    if (r != SECURITY_COUNTER_OK) return finish(fault(SECURITY_KEYS_STORAGE));
    result = SECURITY_KEYS_OK;
    if (KEY_CALL(LW_COUNTER_READ, security_counter_read(w.record, 112, &w.size)) != SECURITY_COUNTER_OK)
        result = fault(SECURITY_KEYS_STORAGE);
    else if (!valid_record()) result = fault(SECURITY_KEYS_FORMAT);
    else security_keys_phase = phase();
    return finish(result);
}

security_keys_result_t security_keys_provision(
    const security_keys_config_t * volatile config, const uint8_t * volatile install_code18,
    volatile uint32_t nwk_floor, volatile uint32_t aps_floor,
    const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    KEY_ENTER();
    if (!KEY_EXTERNAL(config, sizeof(*config)) || !KEY_EXTERNAL(install_code18, 18) ||
        !KEY_EXTERNAL(limits, sizeof(*limits))) return finish(SECURITY_KEYS_ARGUMENT);
#endif
    if (security_keys_phase != SECURITY_KEYS_UNPROVISIONED) return finish(SECURITY_KEYS_STATE);
    if (!config || !install_code18 || !limits_ok(limits, nv_polls) ||
        nwk_floor == 0xffffffffUL || aps_floor == 0xffffffffUL)
        return finish(SECURITY_KEYS_ARGUMENT);
    memcpy(w.record+OWN, config->own_ieee, 8);
    memcpy(w.record+TC, config->tc_ieee, 8);
    memcpy(w.record+EP, config->extended_pan, 8);
    put16(w.record+PAN, config->pan); put16(w.record+ADDR, config->address);
    w.record[CHANNEL] = config->channel; w.record[UPDATE] = config->update_id;
    if (!valid_config()) return finish(SECURITY_KEYS_ARGUMENT);
    memcpy(w.a, install_code18, 18);
    if (KEY_CALL(LW_INSTALL_CODE, install_code_derive(w.a, 18, w.record+LK,
        w.limits.block_timeout, w.limits.block_polls, &w.hash_info)) != ZIGBEE_MMO_OK)
        return finish(SECURITY_KEYS_CRYPTO);
    w.record[MAGIC] = 'K'; w.record[MAGIC+1] = 2;
    set_phase(SECURITY_KEYS_PROVISIONED);
    if (KEY_CALL(LW_COUNTER_CREATE, security_counter_create(nwk_floor, aps_floor, w.record, 112, w.polls)) != SECURITY_COUNTER_OK)
        return finish(fault(SECURITY_KEYS_STORAGE));
    security_keys_phase = SECURITY_KEYS_PROVISIONED;
    return finish(SECURITY_KEYS_OK);
}

security_keys_result_t security_keys_associate(uint16_t short_address, uint16_t nv_polls) SECURITY_FAR
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    KEY_ENTER();
#endif
    security_keys_result_t r = load();
    if (r != SECURITY_KEYS_OK) return finish(r);
    if (phase() != SECURITY_KEYS_PROVISIONED) return finish(SECURITY_KEYS_STATE);
    if (!short_address || short_address >= 0xfff8u || !nv_polls)
        return finish(SECURITY_KEYS_ARGUMENT);
    if (u16(w.record+ADDR) != 0xffffu && u16(w.record+ADDR) != short_address)
        return finish(SECURITY_KEYS_IDENTITY);
    put16(w.record+ADDR, short_address); set_phase(SECURITY_KEYS_ASSOCIATED);
    w.polls = nv_polls;
    return finish(save());
}

static uint8_t broadcast(uint16_t address)
{
    return address == 0xffffu || address == 0xfffdu;
}
static uint8_t outbound_broadcast(uint16_t address)
{
    /* R22 Table3-69: an ED may address routers/coordinator, but is not a
     * recipient of FFFC. Keep the receive predicate deliberately narrower. */
    return broadcast(address) || address == 0xfffcu;
}
static uint8_t join_management(void)
{
    if (w.packet.aps.type == ED_APS_ACK) return w.packet.length == 0;
    return w.packet.aps.type == APS_FRAME_DATA && w.packet.aps.profile_id == 0 &&
        w.packet.aps.source_endpoint == 0 && w.packet.aps.destination_endpoint == 0 &&
        (w.packet.aps.cluster_id == 0x0002u || w.packet.aps.cluster_id == 0x8002u ||
         w.packet.aps.cluster_id == 0x0013u);
}
static uint8_t aps_addressing(uint8_t sending)
{
    uint8_t is_broadcast = sending ? outbound_broadcast(w.packet.nwk.destination) :
                                    broadcast(w.packet.nwk.destination);
    if (w.packet.aps.type == ED_APS_COMMAND) return 1;
    if (w.packet.aps.type == ED_APS_ACK) return !is_broadcast;
    return (w.packet.aps.delivery_mode == APS_DELIVERY_BROADCAST) == is_broadcast;
}
static uint8_t reused(const uint8_t *material)
{
    uint8_t i;
    if (all(material, 16, 0) || same(material, w.record+LK, 16) ||
        (!all(w.record+PK, 16, 0) && same(material, w.record+PK, 16))) return 1;
    for (i = 0; i < 2; i++)
        if ((w.record[FLAGS] & (1u << i)) && same(material, w.record+NK+16u*i, 16)) return 1;
    return 0;
}
static security_keys_result_t transport(void)
{
    uint8_t type, offset, i, newest, sequence;
    if (!w.aps_secured || w.pending || w.packet.length < 2) return SECURITY_KEYS_CONTEXT;
    type = w.packet.payload[1];
    if (type != 1 && type != 4) return SECURITY_KEYS_UNSUPPORTED;
    if (w.key_id != (type == 1 ? 2 : 3)) return SECURITY_KEYS_SELECTOR;
    if (w.packet.length != (type == 1 ? 35 : 34)) return SECURITY_KEYS_FORMAT;
    offset = type == 1 ? 19 : 18;
    if (!same(w.packet.payload+offset+8, w.record+TC, 8) ||
        !same(w.packet.payload+offset, w.record+OWN, 8) ||
        w.packet.nwk.destination != u16(w.record+ADDR))
        return SECURITY_KEYS_IDENTITY;
    if (type == 4) {
        if (phase() != SECURITY_KEYS_REQUESTED) return SECURITY_KEYS_CONTEXT;
        if (reused(w.packet.payload+2)) return SECURITY_KEYS_SELECTOR;
        memcpy(w.record+PK, w.packet.payload+2, 16);
        w.record[FLAGS] &= (uint8_t)~RETIRED;
        set_phase(SECURITY_KEYS_PROVISIONAL);
        w.event = SECURITY_KEYS_EVENT_TC_KEY;
        return SECURITY_KEYS_OK;
    }
    sequence = w.packet.payload[18];
    for (i = 0; i < 2; i++) if (w.record[FLAGS] & (1u << i)) {
        if (sequence == w.record[SEQ+i]) {
            if (!same(w.packet.payload+2, w.record+NK+16u*i, 16)) return SECURITY_KEYS_SELECTOR;
            /* Idempotent transport; crucially do NOT reset this slot's floor. */
            w.event = SECURITY_KEYS_EVENT_NETWORK_KEY;
            return SECURITY_KEYS_OK;
        }
    }
    if (reused(w.packet.payload+2)) return SECURITY_KEYS_SELECTOR;
    if (!network()) {
        if (phase() != SECURITY_KEYS_ASSOCIATED) return SECURITY_KEYS_CONTEXT;
        i = 0; w.record[FLAGS] = 1; set_phase(SECURITY_KEYS_RECEIVED);
    } else {
        if (!verified()) return SECURITY_KEYS_CONTEXT;
        newest = active_slot() ^ ((w.record[FLAGS] & NEWER) ? 1 : 0);
        if (sequence != (uint8_t)(w.record[SEQ+newest]+1u)) return SECURITY_KEYS_SELECTOR;
        i = active_slot() ^ 1u;
        w.record[FLAGS] |= (uint8_t)((1u << i) | NEWER);
    }
    memcpy(w.record+NK+16u*i, w.packet.payload+2, 16);
    put32(w.record+NF+4u*i, 0); w.record[SEQ+i] = sequence;
    w.event = SECURITY_KEYS_EVENT_NETWORK_KEY;
    return SECURITY_KEYS_OK;
}
static void adopt(uint8_t slot)
{
    w.record[FLAGS] = (w.record[FLAGS] & (uint8_t)~(ACTIVE | NEWER)) | (slot ? ACTIVE : 0);
}
static security_keys_result_t aps_command(void)
{
    if (!w.packet.length || w.packet.nwk.source != 0) return SECURITY_KEYS_IDENTITY;
    if (w.packet.payload[0] == 5) return transport();
    if (w.packet.payload[0] == 16) {
        if (!w.aps_secured || !w.pending || phase() != SECURITY_KEYS_WAIT_CONFIRM)
            return SECURITY_KEYS_CONTEXT;
        if (w.packet.length != 11 || w.packet.payload[1] != 0 ||
            w.packet.payload[2] != 4) return SECURITY_KEYS_FORMAT;
        if (w.packet.nwk.destination != u16(w.record+ADDR) ||
            !same(w.packet.payload+3, w.record+OWN, 8)) return SECURITY_KEYS_IDENTITY;
        /* Pending key admitted exactly once. Keep the retired material as a
         * reuse tombstone, not as an operational decryption fallback. */
        memcpy(w.key.key, w.record+LK, 16);
        memcpy(w.record+LK, w.record+PK, 16);
        memcpy(w.record+PK, w.key.key, 16);
        put32(w.record+AF, w.meta.counter+1);
        w.record[FLAGS] |= WAS_VERIFIED | RETIRED;
        set_phase(SECURITY_KEYS_VERIFIED);
        w.event = SECURITY_KEYS_EVENT_VERIFIED;
        return SECURITY_KEYS_OK;
    }
    if (w.packet.payload[0] == 9) {
        if (!w.secured || w.pending || w.packet.length != 2 ||
            !broadcast(w.packet.nwk.destination)) return SECURITY_KEYS_CONTEXT;
        if (w.aps_secured && w.key_id != 0) return SECURITY_KEYS_SELECTOR;
        if (w.packet.payload[1] == w.record[SEQ+active_slot()]) {
            w.event = SECURITY_KEYS_EVENT_SWITCH; return SECURITY_KEYS_OK;
        }
        if (!(w.record[FLAGS] & NEWER) ||
            w.packet.payload[1] != w.record[SEQ+(active_slot() ^ 1u)])
            return SECURITY_KEYS_SELECTOR;
        adopt(active_slot() ^ 1u); w.event = SECURITY_KEYS_EVENT_SWITCH;
        return SECURITY_KEYS_OK;
    }
    return SECURITY_KEYS_UNSUPPORTED;
}
static security_keys_result_t nwk_command(void)
{
    uint8_t command, delta;
    if (!w.secured || !w.packet.length || w.packet.nwk.source != 0) return SECURITY_KEYS_CONTEXT;
    command = w.packet.payload[0];
    if (phase() == SECURITY_KEYS_REJOINING && command != 7 && command != 4)
        return SECURITY_KEYS_CONTEXT;
    if (command == 4) {
        if (w.packet.length != 2 || !(w.packet.nwk.flags & NWK_FLAG_SOURCE_IEEE) ||
            w.packet.nwk.radius != 1) return SECURITY_KEYS_FORMAT;
        if ((w.packet.payload[1] & 0x40) ?
            w.packet.nwk.destination != u16(w.record+ADDR) : w.packet.nwk.destination != 0xfffdu)
            return SECURITY_KEYS_IDENTITY;
        /* Reserved RX option bits ignored; no child admission is implemented.
         * LEFT preserves keys/counters; upper layer handles rejoin indication. */
        set_phase(SECURITY_KEYS_LEFT);
        if (w.packet.payload[1] & 0x20) w.record[FLAGS] |= REJOIN_ALLOWED;
        else w.record[FLAGS] &= (uint8_t)~REJOIN_ALLOWED;
        w.event = SECURITY_KEYS_EVENT_LEAVE;
    } else if (command == 10) {
        if (w.packet.length != 13 || w.packet.payload[1] != 1 ||
            !(w.packet.nwk.flags & NWK_FLAG_SOURCE_IEEE) ||
            (w.packet.nwk.flags & NWK_FLAG_DESTINATION_IEEE) ||
            w.packet.nwk.destination != 0xffffu) return SECURITY_KEYS_FORMAT;
        if (!same(w.packet.payload+2, w.record+EP, 8) ||
            u16(w.packet.payload+11) == 0xffffu) return SECURITY_KEYS_IDENTITY;
        /* Same strict modulo-256 half-range policy as nwk_parent: 1..127
         * newer, 128 ambiguous, 129..255 stale. Equal requires equal PAN.
         * A duplicate with a fresh security counter still saves that floor. */
        delta = (uint8_t)(w.packet.payload[10]-w.record[UPDATE]);
        if (delta >= 128u || (!delta && !same(w.record+PAN, w.packet.payload+11, 2)))
            return SECURITY_KEYS_CONTEXT;
        if (delta) {
            w.record[UPDATE] = w.packet.payload[10];
            memcpy(w.record+PAN, w.packet.payload+11, 2);
        }
        w.event = SECURITY_KEYS_EVENT_UPDATE;
    } else if (command == 7) {
        if (phase() != SECURITY_KEYS_REJOINING) return SECURITY_KEYS_CONTEXT;
        if (w.packet.length != 4 || (w.packet.nwk.flags & BOTH_IEEE) != BOTH_IEEE ||
            w.packet.nwk.destination != u16(w.record+ADDR)) return SECURITY_KEYS_FORMAT;
        /* Failed responses cannot change the binding or end the outstanding
         * phase; caller owns bounded timeouts/retries and cancellation. */
        if (w.packet.payload[3] != 0) return SECURITY_KEYS_CONTEXT;
        if (!u16(w.packet.payload+1) || u16(w.packet.payload+1) >= 0xfff8u)
            return SECURITY_KEYS_IDENTITY;
        memcpy(w.record+ADDR, w.packet.payload+1, 2);
        set_phase(SECURITY_KEYS_VERIFIED);
        w.record[FLAGS] &= (uint8_t)~REJOIN_ALLOWED;
        w.event = SECURITY_KEYS_EVENT_REJOINED;
    } else if (command == 12) {
        if (w.packet.length != 3 || w.packet.payload[1] > 1 ||
            (w.packet.nwk.flags & BOTH_IEEE) != BOTH_IEEE ||
            w.packet.nwk.destination != u16(w.record+ADDR) || w.packet.nwk.radius != 1)
            return SECURITY_KEYS_FORMAT;
        if (!(w.record[PHASE] & TIMEOUT_PENDING)) return SECURITY_KEYS_CONTEXT;
        w.record[PHASE] &= (uint8_t)~TIMEOUT_PENDING;
        if (w.packet.payload[1] == 0)
            w.record[PHASE] = (w.record[PHASE] & (uint8_t)~PARENT_MASK) |
                (uint8_t)((w.packet.payload[2] & 7u) << 4);
        w.event = SECURITY_KEYS_EVENT_MANAGEMENT;
    } else if (command == 3) {
        if (w.packet.length < 2 || w.packet.length > 4) return SECURITY_KEYS_FORMAT;
        w.event = SECURITY_KEYS_EVENT_MANAGEMENT;
    } else return SECURITY_KEYS_UNSUPPORTED;
    return SECURITY_KEYS_OK;
}

/* b contains APDU, a contains normalized NPDU. */
static security_keys_result_t open_aps(void)
{
    security_keys_result_t r;
    volatile uint8_t length = w.nwk.payload_length, offset = w.nwk.payload_offset;
    w.aps_secured = (w.a[offset] & APS_FLAG_SECURITY) != 0;
    if (!w.aps_secured) return SECURITY_KEYS_OK;
    r = crypto_result(ed_wire_inspect(ZIGBEE_SECURITY_APS, w.a+offset, length, &w.meta));
    if (r != SECURITY_KEYS_OK) return r;
    if (w.meta.counter == 0xffffffffUL) return SECURITY_KEYS_REPLAY;
    w.key_id = w.meta.key_identifier;
    if (w.key_id != 0 && w.key_id != 2 && w.key_id != 3) return SECURITY_KEYS_SELECTOR;
    if (w.meta.extended_nonce && !same(w.meta.source, w.record+TC, 8)) return SECURITY_KEYS_IDENTITY;
    w.pending = 0;
    if (phase() == SECURITY_KEYS_WAIT_CONFIRM && w.key_id == 0 &&
        (w.a[offset] & 3) == ED_APS_COMMAND) {
        r = key(PK, 0, 0);
        if (r != SECURITY_KEYS_OK) return r;
        r = crypto_result(ed_wire_crypt(1, ZIGBEE_SECURITY_APS, &w.key,
            w.a+offset, length, w.b, 116, &w.info));
        if (r == SECURITY_KEYS_OK) w.pending = 1;
        else if (r != SECURITY_KEYS_AUTH) return r;
    }
    if (!w.pending) {
        w.counter = u32(w.record+AF);
        if (w.meta.counter < w.counter) return SECURITY_KEYS_REPLAY;
        r = key(LK, w.key_id, 0);
        if (r != SECURITY_KEYS_OK) return r;
        w.key.extended_nonce = w.meta.extended_nonce;
        r = crypto_result(ed_wire_crypt(1, ZIGBEE_SECURITY_APS, &w.key,
            w.a+offset, length, w.b, 116, &w.info));
        if (r != SECURITY_KEYS_OK) return r;
        put32(w.record+AF, w.meta.counter+1);
    }
    memcpy(w.a+offset, w.b, w.info.length); w.size = offset+w.info.length;
    return SECURITY_KEYS_OK;
}

security_keys_result_t security_keys_receive(
    const uint8_t * volatile raw_npdu, volatile uint16_t length,
    ed_packet_t * volatile output, uint8_t * volatile event,
    const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    KEY_ENTER();
    if (!KEY_EXTERNAL(raw_npdu, length) || !KEY_WRITE(output, sizeof(*output)) ||
        !KEY_WRITE(event, 1) || !KEY_EXTERNAL(limits, sizeof(*limits)))
        return finish(SECURITY_KEYS_ARGUMENT);
#endif
    security_keys_result_t r = load();
    volatile uint8_t i;
    if (r != SECURITY_KEYS_OK) return finish(r);
    if (!raw_npdu || !output || !event || !limits_ok(limits, nv_polls))
        return finish(SECURITY_KEYS_ARGUMENT);
    if (security_keys_phase < SECURITY_KEYS_ASSOCIATED || security_keys_phase == SECURITY_KEYS_LEFT)
        return finish(SECURITY_KEYS_STATE);
    r = crypto_result(ed_wire_nwk(raw_npdu, length, &w.nwk));
    if (r != SECURITY_KEYS_OK) return finish(r);
    if (w.nwk.header.source != 0 ||
        (w.nwk.header.destination != u16(w.record+ADDR) && !broadcast(w.nwk.header.destination)) ||
        ((w.nwk.header.flags & NWK_FLAG_SOURCE_IEEE) &&
         !same(w.nwk.header.source_ieee, w.record+TC, 8)) ||
        ((w.nwk.header.flags & NWK_FLAG_DESTINATION_IEEE) &&
         !same(w.nwk.header.destination_ieee, w.record+OWN, 8)))
        return finish(SECURITY_KEYS_IDENTITY);
    w.secured = (w.nwk.header.flags & NWK_FLAG_SECURITY) != 0;
    if (w.secured) {
        r = crypto_result(ed_wire_inspect(ZIGBEE_SECURITY_NWK, raw_npdu, length, &w.meta));
        if (r != SECURITY_KEYS_OK) return finish(r);
        if (!same(w.meta.source, w.record+TC, 8)) return finish(SECURITY_KEYS_IDENTITY);
        for (i = 0; i < 2; i++)
            if ((w.record[FLAGS] & (1u << i)) && w.record[SEQ+i] == w.meta.key_sequence) break;
        if (i == 2) return finish(SECURITY_KEYS_SELECTOR);
        w.slot = i;
        if (w.meta.counter == 0xffffffffUL || w.meta.counter < u32(w.record+NF+4u*i))
            return finish(SECURITY_KEYS_REPLAY);
        r = key(NK+16u*i, 1, 0);
        if (r != SECURITY_KEYS_OK) return finish(r);
        w.key.key_sequence = w.record[SEQ+i];
        r = crypto_result(ed_wire_crypt(1, ZIGBEE_SECURITY_NWK, &w.key,
            raw_npdu, length, w.a, 116, &w.info));
        if (r != SECURITY_KEYS_OK) return finish(r);
        put32(w.record+NF+4u*i, w.meta.counter+1); w.size = w.info.length;
        if ((w.record[FLAGS] & NEWER) && i != active_slot()) adopt(i);
    } else {
        if (network() || length > 116) return finish(SECURITY_KEYS_CONTEXT);
        memcpy(w.a, raw_npdu, length); w.size = (uint8_t)length;
    }
    r = crypto_result(ed_wire_nwk(w.a, w.size, &w.nwk));
    if (r != SECURITY_KEYS_OK) return finish(r);
    if (w.nwk.header.type != ED_NWK_COMMAND) {
        if (!w.nwk.payload_length) return finish(SECURITY_KEYS_FORMAT);
        r = open_aps();
        if (r != SECURITY_KEYS_OK) return finish(r);
    }
    r = crypto_result(ed_wire_decode(w.a, w.size, &w.packet));
    if (r != SECURITY_KEYS_OK) return finish(r);
    if (w.packet.nwk.type != ED_NWK_COMMAND && !aps_addressing(0))
        return finish(SECURITY_KEYS_IDENTITY);
    if (phase() == SECURITY_KEYS_REJOINING && w.packet.nwk.type != ED_NWK_COMMAND)
        return finish(SECURITY_KEYS_CONTEXT);
    if (!w.secured && (w.packet.nwk.type == ED_NWK_COMMAND ||
        w.packet.aps.type != ED_APS_COMMAND || w.packet.length != 35 ||
        w.packet.payload[0] != 5 || w.packet.payload[1] != 1))
        return finish(SECURITY_KEYS_CONTEXT);
    w.event = SECURITY_KEYS_EVENT_DATA;
    if (w.packet.nwk.type == ED_NWK_COMMAND) r = nwk_command();
    else if (w.packet.aps.type == ED_APS_COMMAND) r = aps_command();
    else if (w.pending || (w.aps_secured && w.key_id != 0)) r = SECURITY_KEYS_SELECTOR;
    else if (join_management()) w.event = SECURITY_KEYS_EVENT_MANAGEMENT;
    else if (!verified()) r = SECURITY_KEYS_CONTEXT;
    if (r != SECURITY_KEYS_OK) return finish(r);
    /* No transport descriptor, key bytes or hashes escape via packet output. */
    if (w.packet.nwk.type != ED_NWK_COMMAND && w.packet.aps.type == ED_APS_COMMAND) {
        memset(w.packet.payload, 0, sizeof(w.packet.payload)); w.packet.length = 0;
    }
    r = save();
    if (r == SECURITY_KEYS_OK) {
        *output = w.packet;
        *event = w.event | (w.aps_secured ? SECURITY_KEYS_EVENT_APS_SECURED : 0);
    }
    return finish(r);
}

static inline security_keys_result_t seal(volatile uint8_t aps_secure)
{
    security_keys_result_t r;
    volatile uint8_t offset, slot;
    uint8_t total;
    slot = active_slot();
    /* R22 3.3.1.1.9: this owner is an ED; callers cannot assert EDI.
     * Only parent information from a contextual authenticated response
     * determines the outgoing bit, including later Leave/key commands. */
    w.packet.nwk.flags &= (uint16_t)~NWK_FLAG_END_DEVICE_INITIATOR;
    if (parent_information()) w.packet.nwk.flags |= NWK_FLAG_END_DEVICE_INITIATOR;
    r = crypto_result(ed_wire_encode(&w.packet, w.a, 116, &w.size));
    if (r != SECURITY_KEYS_OK) return r;
    if (aps_secure) {
        r = crypto_result(ed_wire_nwk(w.a, w.size, &w.nwk));
        if (r != SECURITY_KEYS_OK) return r;
        offset = w.nwk.payload_offset;
        r = key(LK, 0, 1);
        if (r != SECURITY_KEYS_OK) return r;
        r = take(SECURITY_COUNTER_APS);
        if (r != SECURITY_KEYS_OK) return r;
        r = crypto_result(ed_wire_crypt(0, ZIGBEE_SECURITY_APS, &w.key,
            w.a+offset, w.nwk.payload_length, w.b, 116, &w.info));
        if (r != SECURITY_KEYS_OK) return r;
        total = offset+w.info.length;
        if (total > 116) return SECURITY_KEYS_SPACE;
        memcpy(w.a+offset, w.b, w.info.length); w.size = total;
    }
    r = key(NK+16u*slot, 1, 1);
    if (r != SECURITY_KEYS_OK) return r;
    w.key.key_sequence = w.record[SEQ+slot];
    r = take(SECURITY_COUNTER_NWK);
    if (r != SECURITY_KEYS_OK) return r;
    return crypto_result(ed_wire_crypt(0, ZIGBEE_SECURITY_NWK, &w.key,
        w.a, w.size, w.b, 116, &w.info));
}
static security_keys_result_t publish(uint8_t *out, uint16_t capacity, uint8_t *written)
{
    if (capacity < w.info.length) return SECURITY_KEYS_SPACE;
    memcpy(out, w.b, w.info.length); *written = w.info.length;
    return SECURITY_KEYS_OK;
}
static void command_packet(uint8_t nwk_seq, uint8_t aps_counter)
{
    w.packet.nwk.version = 2; w.packet.nwk.source = u16(w.record+ADDR);
    w.packet.nwk.destination = 0; w.packet.nwk.radius = 1;
    w.packet.nwk.sequence = nwk_seq; w.packet.aps.type = ED_APS_COMMAND;
    w.packet.aps.counter = aps_counter;
}
security_keys_result_t security_keys_request(
    uint8_t nwk_seq, uint8_t aps_counter, uint8_t * volatile out, volatile uint16_t capacity,
    uint8_t * volatile written, const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    KEY_ENTER();
    if (!KEY_WRITE(out, capacity) || !KEY_WRITE(written, 1) ||
        !KEY_EXTERNAL(limits, sizeof(*limits))) return finish(SECURITY_KEYS_ARGUMENT);
#endif
    security_keys_result_t r = load();
    if (r != SECURITY_KEYS_OK) return finish(r);
    if (!out || !written || !limits_ok(limits, nv_polls)) return finish(SECURITY_KEYS_ARGUMENT);
    if (phase() != SECURITY_KEYS_RECEIVED && phase() != SECURITY_KEYS_REQUESTED &&
        phase() != SECURITY_KEYS_VERIFIED) return finish(SECURITY_KEYS_CONTEXT);
    if (capacity < 47) return finish(SECURITY_KEYS_SPACE);
    command_packet(nwk_seq, aps_counter);
    w.packet.length = 2; w.packet.payload[0] = 8; w.packet.payload[1] = 4;
    r = seal(1);
    if (r == SECURITY_KEYS_OK) {
        set_phase(SECURITY_KEYS_REQUESTED); r = save();
    }
    if (r == SECURITY_KEYS_OK) r = publish(out, capacity, written);
    return finish(r);
}
security_keys_result_t security_keys_verify(
    uint8_t nwk_seq, uint8_t aps_counter, uint8_t * volatile out, volatile uint16_t capacity,
    uint8_t * volatile written, const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    KEY_ENTER();
    if (!KEY_WRITE(out, capacity) || !KEY_WRITE(written, 1) ||
        !KEY_EXTERNAL(limits, sizeof(*limits))) return finish(SECURITY_KEYS_ARGUMENT);
#endif
    security_keys_result_t r = load();
    if (r != SECURITY_KEYS_OK) return finish(r);
    if (!out || !written || !limits_ok(limits, nv_polls)) return finish(SECURITY_KEYS_ARGUMENT);
    if (phase() != SECURITY_KEYS_PROVISIONAL && phase() != SECURITY_KEYS_WAIT_CONFIRM)
        return finish(SECURITY_KEYS_CONTEXT);
    if (capacity < 54) return finish(SECURITY_KEYS_SPACE);
    command_packet(nwk_seq, aps_counter);
    w.packet.length = 26; w.packet.payload[0] = 15; w.packet.payload[1] = 4;
    memcpy(w.packet.payload+2, w.record+OWN, 8);
    if (KEY_CALL(LW_HASH_VERIFY, zigbee_key_hash(w.record+PK, ZIGBEE_HASH_VERIFY, w.packet.payload+10,
        w.limits.block_timeout, w.limits.block_polls, &w.hash_info)) != ZIGBEE_MMO_OK)
        return finish(SECURITY_KEYS_CRYPTO);
    r = seal(0);
    if (r == SECURITY_KEYS_OK) { set_phase(SECURITY_KEYS_WAIT_CONFIRM); r = save(); }
    if (r == SECURITY_KEYS_OK) r = publish(out, capacity, written);
    return finish(r);
}
security_keys_result_t security_keys_leave(
    uint8_t nwk_seq, uint8_t * volatile out, volatile uint16_t capacity, uint8_t * volatile written,
    const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    KEY_ENTER();
    if (!KEY_WRITE(out, capacity) || !KEY_WRITE(written, 1) ||
        !KEY_EXTERNAL(limits, sizeof(*limits))) return finish(SECURITY_KEYS_ARGUMENT);
#endif
    security_keys_result_t r = load();
    if (r != SECURITY_KEYS_OK) return finish(r);
    if (!out || !written || !limits_ok(limits, nv_polls)) return finish(SECURITY_KEYS_ARGUMENT);
    if (network()) {
        if (capacity < 36) return finish(SECURITY_KEYS_SPACE);
        w.packet.nwk.type = ED_NWK_COMMAND; w.packet.nwk.version = 2;
        w.packet.nwk.source = u16(w.record+ADDR); w.packet.nwk.destination = 0xfffdu;
        w.packet.nwk.flags = NWK_FLAG_SOURCE_IEEE; w.packet.nwk.radius = 1;
        w.packet.nwk.sequence = nwk_seq;
        memcpy(w.packet.nwk.source_ieee, w.record+OWN, 8);
        w.packet.length = 2; w.packet.payload[0] = 4; w.packet.payload[1] = 0;
        r = seal(0);
        if (r != SECURITY_KEYS_OK) return finish(r);
    }
    set_phase(SECURITY_KEYS_LEFT);
    w.record[FLAGS] &= (uint8_t)~REJOIN_ALLOWED;
    r = save();
    if (r == SECURITY_KEYS_OK) {
        if (network()) r = publish(out, capacity, written);
        else *written = 0; /* Explicit quiet abandonment, never a TX result. */
    }
    return finish(r);
}
security_keys_result_t security_keys_send(
    const ed_packet_t * volatile packet, uint8_t aps_secure, uint8_t * volatile out, volatile uint16_t capacity,
    uint8_t * volatile written, const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    KEY_ENTER();
    if (!KEY_EXTERNAL(packet, sizeof(*packet)) || !KEY_WRITE(out, capacity) ||
        !KEY_WRITE(written, 1) || !KEY_EXTERNAL(limits, sizeof(*limits)))
        return finish(SECURITY_KEYS_ARGUMENT);
#endif
    security_keys_result_t r = load();
    uint8_t transition = 0;
    if (r != SECURITY_KEYS_OK) return finish(r);
    if (!packet || !out || !written || aps_secure > 1 || !limits_ok(limits, nv_polls))
        return finish(SECURITY_KEYS_ARGUMENT);
    if (!network()) return finish(SECURITY_KEYS_STATE);
    w.packet = *packet;
    if (w.packet.nwk.source != u16(w.record+ADDR) ||
        (w.packet.nwk.destination != 0 && !outbound_broadcast(w.packet.nwk.destination)) ||
        ((w.packet.nwk.flags & NWK_FLAG_SOURCE_IEEE) &&
         !same(w.packet.nwk.source_ieee, w.record+OWN, 8)) ||
        ((w.packet.nwk.flags & NWK_FLAG_DESTINATION_IEEE) &&
         !same(w.packet.nwk.destination_ieee, w.record+TC, 8)))
        return finish(SECURITY_KEYS_IDENTITY);
    if (w.packet.nwk.type == ED_NWK_COMMAND) {
        if (aps_secure || w.packet.nwk.radius != 1) return finish(SECURITY_KEYS_UNSUPPORTED);
        if (w.packet.payload[0] == 6) {
            if (!verified() || w.packet.length != 2 ||
                (w.packet.payload[1] != 0x88 && w.packet.payload[1] != 0x8c) ||
                w.packet.nwk.destination != 0 || (w.packet.nwk.flags & BOTH_IEEE) != BOTH_IEEE)
                return finish(SECURITY_KEYS_UNSUPPORTED);
            if (!(w.record[FLAGS] & RETIRED) && !all(w.record+PK, 16, 0))
                return finish(SECURITY_KEYS_CONTEXT); /* abandoned exchange needs recovery review */
            if (phase() != SECURITY_KEYS_VERIFIED &&
                phase() != SECURITY_KEYS_REJOINING &&
                !(phase() == SECURITY_KEYS_LEFT && (w.record[FLAGS] & REJOIN_ALLOWED)))
                return finish(SECURITY_KEYS_CONTEXT);
            transition = SECURITY_KEYS_REJOINING;
        } else if (w.packet.payload[0] == 4) {
            if (w.packet.length != 2 || (w.packet.payload[1] & (uint8_t)~0x20u) ||
                w.packet.nwk.destination != 0xfffdu ||
                !(w.packet.nwk.flags & NWK_FLAG_SOURCE_IEEE) ||
                (w.packet.nwk.flags & NWK_FLAG_DESTINATION_IEEE))
                return finish(SECURITY_KEYS_UNSUPPORTED);
            transition = SECURITY_KEYS_LEFT;
        } else if (w.packet.payload[0] != 11 || w.packet.length != 3 ||
                   w.packet.payload[1] > 14 || w.packet.payload[2] ||
                   w.packet.nwk.destination != 0 || (w.packet.nwk.flags & BOTH_IEEE) != BOTH_IEEE)
            return finish(SECURITY_KEYS_UNSUPPORTED);
    } else {
        if (w.packet.aps.type == ED_APS_COMMAND) return finish(SECURITY_KEYS_UNSUPPORTED);
        if (!aps_addressing(1)) return finish(SECURITY_KEYS_IDENTITY);
        if (!verified() && !join_management()) return finish(SECURITY_KEYS_CONTEXT);
        if (aps_secure && w.packet.nwk.destination != 0)
            return finish(SECURITY_KEYS_UNSUPPORTED);
    }
    if ((phase() == SECURITY_KEYS_LEFT || phase() == SECURITY_KEYS_REJOINING) &&
        !transition) return finish(SECURITY_KEYS_STATE);
    r = seal(aps_secure);
    /* Preflight capacity before any durable lifecycle transition. Allocation
     * and unsuccessful crypto can still burn outgoing counters, as promised. */
    if (r == SECURITY_KEYS_OK && capacity < w.info.length) r = SECURITY_KEYS_SPACE;
    if (r == SECURITY_KEYS_OK && transition) {
        set_phase(transition);
        if (transition == SECURITY_KEYS_LEFT) {
            if (w.packet.payload[1] & 0x20) w.record[FLAGS] |= REJOIN_ALLOWED;
            else w.record[FLAGS] &= (uint8_t)~REJOIN_ALLOWED;
        }
        r = save();
    }
    if (r == SECURITY_KEYS_OK && w.packet.nwk.type == ED_NWK_COMMAND && w.packet.payload[0] == 11) {
        w.record[PHASE] |= TIMEOUT_PENDING;
        r = save();
    }
    if (r == SECURITY_KEYS_OK) r = publish(out, capacity, written);
    return finish(r);
}
security_keys_result_t security_keys_status(security_keys_status_t * volatile output) SECURITY_FAR
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    KEY_ENTER();
    if (!KEY_WRITE(output, sizeof(*output))) return finish(SECURITY_KEYS_ARGUMENT);
#endif
    security_keys_result_t r;
    uint8_t prior = security_keys_result;
    if (!output) return finish(SECURITY_KEYS_ARGUMENT);
    if (security_keys_phase < SECURITY_KEYS_PROVISIONED || security_keys_phase == SECURITY_KEYS_FAILED) {
        memset(output, 0, sizeof(*output));
        output->phase = security_keys_phase; output->result = prior;
        return finish(SECURITY_KEYS_OK);
    }
    r = load();
    if (r != SECURITY_KEYS_OK) return finish(r);
    memcpy(output->config.own_ieee, w.record+OWN, 8);
    memcpy(output->config.tc_ieee, w.record+TC, 8);
    memcpy(output->config.extended_pan, w.record+EP, 8);
    output->config.pan = u16(w.record+PAN); output->config.address = u16(w.record+ADDR);
    output->config.channel = w.record[CHANNEL]; output->config.update_id = w.record[UPDATE];
    output->phase = security_keys_phase; output->result = prior;
    output->slot_valid = w.record[FLAGS] & 3;
    output->active_sequence = w.record[SEQ+active_slot()];
    output->newer_pending = (w.record[FLAGS] & NEWER) != 0;
    output->parent_information = parent_information();
    output->timeout_pending = (w.record[PHASE] & TIMEOUT_PENDING) != 0;
    return finish(SECURITY_KEYS_OK);
}
