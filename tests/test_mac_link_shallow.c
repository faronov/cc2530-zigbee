/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* J3 differential transcript for the DIRECT shallow guard/codec bodies.
 * Build once with the DIRECT profile and once more adding
 * CC2530_MAC_LINK_DEEP_REFERENCE (the original bodies, same layout); the two
 * standard outputs must be byte-identical. Guard probes use real host
 * addresses, so compare only builds with identical compiler and sanitizer
 * flags; a native and a sanitizer transcript legitimately differ in guard
 * counts because redzones move the neighbouring globals. States are reached
 * only through the real UPPER/CHILD enter, grant, return and poison APIs.
 * This is host evidence of unchanged results, not an SDCC stack, placement
 * or MCU proof. */
#include "mac_link_child_workspace_internal.h"
#include "mac_link_workspace_internal.h"
#include "mac_frame.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(CC2530_HOST_TEST) || !defined(CC2530_MAC_LINK_DIRECT)
#error The shallow differential test needs the host DIRECT profile
#endif
#if defined(CC2530_MAC_LINK_DEEP_REFERENCE) == defined(LINK_WORK_SHALLOW)
#error Exactly one of the shallow and deep reference bodies must be selected
#endif

static uint64_t digest = 1469598103934665603ull;
static unsigned long records, failures;
static uint32_t seed = 0x1d872b41u;
/* Coverage counters are part of the compared transcript. */
static unsigned long io_hits[3], child_hits[3], external_hits[2], inside_hits[2], disjoint_hits[2];
static unsigned long codec_hits[6][16], upper_sweeps, child_sweeps, loan_decodes;

static void mix(uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++) { digest ^= (uint8_t)(v >> (8 * i)); digest *= 1099511628211ull; }
    records++;
}
static void mix_bytes(const void *p, size_t n)
{
    const uint8_t *b = p;
    size_t i;
    mix((uint32_t)n);
    for (i = 0; i < n; i++) { digest ^= b[i]; digest *= 1099511628211ull; }
}
static uint32_t rnd(void)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
static void check(int ok, const char *what)
{
    if (!ok) { failures++; fprintf(stderr, "FAIL: %s\n", what); }
}

static const uint8_t *const upper = (const uint8_t *)&link_work_arena;
static const uint8_t *const child = (const uint8_t *)&child_work_arena;
static uint8_t outside[512];
static uint8_t ops[64];
static size_t op_count;
static uint16_t sizes[64];
static size_t size_count;

static void setup_tables(void)
{
    static const uint16_t large[] = { 48, 64, 78, 96, 109, 128, 135, 149, 198, 218, 236, 267,
                                      394, 543, 617, 618, 0xffffu };
    unsigned v;
    size_t i;
    for (v = 0; v < LW_FRAMES; v++) ops[op_count++] = (uint8_t)v;
    for (v = LW_CHILD_WIRE_NWK; v <= LW_CHILD_COMMAND; v++) ops[op_count++] = (uint8_t)v;
    for (v = CW_AES_KEY; v <= CW_READ_OUTPUT; v++) ops[op_count++] = (uint8_t)v;
    ops[op_count++] = 255u;
    for (v = 1; v <= 40; v++) sizes[size_count++] = (uint16_t)v;
    for (i = 0; i < sizeof(large) / sizeof(large[0]); i++) sizes[size_count++] = large[i];
}

static void record_state(void)
{
    mix(link_work_root()); mix(link_work_clean());
    mix(child_work_phase()); mix(child_work_poisoned()); mix(child_work_clean());
}

static void query_io(uint8_t op, const void *p, uint16_t size, uint8_t writing)
{
    uint8_t r = link_work_io(op, p, size, writing);
    mix(r); check(r <= 1, "link_work_io range"); io_hits[r <= 1 ? r : 2]++;
}
static void query_child(uint8_t op, child_address_t address, uint16_t size, uint8_t writing)
{
    uint8_t r = child_work_address(op, address, size, writing);
    mix(r); check(r <= 2, "child_work_address range"); child_hits[r <= 2 ? r : 2]++;
}

/* Every UPPER-arena start (including one past the end) for every op, size and
 * direction, then the independent classification entry points. */
static void sweep_upper(void)
{
    size_t off, s, o;
    upper_sweeps++;
    for (off = 0; off <= sizeof(link_work_arena); off++)
        for (s = 0; s < size_count; s++) {
            uint8_t r = link_work_external(upper + off, sizes[s]);
            mix(r); external_hits[r != 0]++;
            for (o = 0; o < op_count; o++) {
                query_io(ops[o], upper + off, sizes[s], 0);
                query_io(ops[o], upper + off, sizes[s], 1);
            }
        }
    record_state();
}
/* Integer CHILD addresses from 16 below to 16 past the arena: no pointer is
 * formed outside an object. Pointer forms stay inside the arena. */
static void sweep_child(void)
{
    const child_address_t base = (child_address_t)(uintptr_t)child;
    long off;
    size_t s, o;
    child_sweeps++;
    for (off = -16; off <= (long)sizeof(child_work_arena) + 16; off++)
        for (s = 0; s < size_count; s++)
            for (o = 0; o < op_count; o++) {
                query_child(ops[o], (child_address_t)(base + (child_address_t)off), sizes[s], 0);
                query_child(ops[o], (child_address_t)(base + (child_address_t)off), sizes[s], 1);
            }
    for (off = 0; off <= (long)sizeof(child_work_arena); off++)
        for (s = 0; s < size_count; s++) {
            uint8_t r = child_work_inside(child + off, sizes[s]);
            mix(r); inside_hits[r != 0]++;
            query_io(LW_CHILD_CCM, child + off, sizes[s], (uint8_t)(s & 1u));
        }
    record_state();
}

static const uint8_t *random_pointer(size_t *room)
{
    uint32_t k = rnd() % 8u;
    size_t off;
    if (k < 3) { off = rnd() % (sizeof(link_work_arena) + 1u); *room = sizeof(link_work_arena) - off; return upper + off; }
    if (k < 6) { off = rnd() % (sizeof(child_work_arena) + 1u); *room = sizeof(child_work_arena) - off; return child + off; }
    if (k < 7) { off = rnd() % sizeof(outside); *room = sizeof(outside) - off; return outside + off; }
    *room = 0;
    return NULL;
}
static uint16_t random_size(size_t room)
{
    uint32_t k = rnd() % 4u;
    if (k == 0) return (uint16_t)room;
    if (k == 1) return (uint16_t)(room + 1u);
    if (k == 2) return sizes[rnd() % size_count];
    return (uint16_t)(rnd() % 700u);
}
static void random_queries(unsigned n)
{
    while (n--) {
        size_t room, room2;
        const uint8_t *p = random_pointer(&room), *q;
        uint16_t size = random_size(room);
        uint8_t op = ops[rnd() % op_count], w = (uint8_t)(rnd() & 1u), r;
        switch (rnd() % 5u) {
        case 0: query_io(op, p, size, w); break;
        case 1: r = link_work_external(p, size); mix(r); external_hits[r != 0]++; break;
        case 2:
            query_child(op, (child_address_t)((child_address_t)(uintptr_t)child +
                        (child_address_t)(rnd() % (sizeof(child_work_arena) + 64u)) - 32u), size, w);
            break;
        case 3: r = child_work_inside(p, size); mix(r); inside_hits[r != 0]++; break;
        default:
            q = random_pointer(&room2);
            r = child_work_disjoint(p, size, q, random_size(room2));
            mix(r); disjoint_hits[r != 0]++;
            break;
        }
    }
}

/* Random walk over the real ownership APIs. Sweeps are taken at distinct
 * granted states, where exact loans can be admitted. */
static void guard_walk(void)
{
    static uint8_t enter_seen[LW_FRAMES], grant_seen[LW_HINT + 1], child_seen[CW_FRAMES][CW_FRAMES];
    unsigned step, upper_left = 10u, child_left = 10u;
    child_work_full_reset(); link_work_host_reset();
    sweep_upper(); sweep_child();
    for (step = 0; step < 60000u; step++) {
        uint8_t f = (uint8_t)(rnd() % (CW_FRAMES + 2u)), g = (uint8_t)(rnd() % (LW_FRAMES + 2u)), r;
        uint8_t permission = (uint8_t)(rnd() % (LW_HINT + 2u));
        if (step % 509u == 0u) { child_work_full_reset(); link_work_host_reset(); }
        switch (rnd() % 12u) {
        case 0: case 1:
            r = link_work_enter(g);
            if (r && g < LW_FRAMES && !enter_seen[g] && upper_left) {
                enter_seen[g] = 1; upper_left--; sweep_upper();
            }
            break;
        case 2: r = link_work_leave(g); break;
        case 3:
            r = link_work_grant(permission);
            if (r && permission <= LW_HINT && !grant_seen[permission] && upper_left) {
                grant_seen[permission] = 1; upper_left--; sweep_upper();
            }
            break;
        case 4: r = link_work_call_result((uint8_t)rnd()); break;
        case 5: case 6: r = child_work_enter(f); break;
        case 7:
            r = child_work_grant(f);
            if (r && f < CW_FRAMES && !child_seen[child_work_phase()][f] && child_left) {
                child_seen[child_work_phase()][f] = 1; child_left--; sweep_child();
            }
            break;
        case 8: r = child_work_end(f, (uint8_t)rnd()); break;
        case 9: r = child_work_return((uint16_t)(((uint16_t)child_work_phase() << 8) | (rnd() & 0xffu))); break;
        case 10: r = child_work_leaf(f); break;
        default: r = (rnd() % 16u) ? child_work_return((uint16_t)rnd()) : child_work_poison((uint8_t)rnd()); break;
        }
        mix(r); record_state();
        random_queries(8);
    }
    child_work_full_reset(); link_work_host_reset();
}

static void codec_record(unsigned fn, mac_codec_result_t status)
{
    mix((uint32_t)status);
    codec_hits[fn][(unsigned)status < 16u ? (unsigned)status : 15u]++;
}
static void random_header(mac_header_t *h)
{
    static const uint8_t modes[] = { MAC_ADDRESS_NONE, MAC_ADDRESS_SHORT, MAC_ADDRESS_EXTENDED, 1u };
    static const uint8_t types[] = { MAC_FRAME_BEACON, MAC_FRAME_DATA, MAC_FRAME_COMMAND,
                                     MAC_FRAME_COMMAND, MAC_FRAME_ACK };
    uint8_t shaped = (uint8_t)((rnd() % 4u) != 0u);
    size_t i;
    memset(h, 0, sizeof(*h));
    h->type = shaped ? types[rnd() % 5u] : (uint8_t)(rnd() % 8u);
    h->version = (uint8_t)(shaped ? (h->type == MAC_FRAME_DATA ? rnd() % 2u : 0u) : rnd() % 4u);
    h->flags = (uint8_t)(rnd() & (MAC_FLAG_PENDING | MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION));
    if (!(rnd() % 16u)) h->flags |= MAC_FLAG_SECURITY;
    if (!(rnd() % 16u)) h->flags |= (uint8_t)(rnd() & 0x87u);
    h->sequence = (uint8_t)rnd();
    h->destination_mode = modes[(rnd() % 16u) ? rnd() % 3u : 3u];
    h->source_mode = modes[(rnd() % 16u) ? rnd() % 3u : 3u];
    if (shaped && h->type == MAC_FRAME_BEACON) {
        h->destination_mode = MAC_ADDRESS_NONE;
        h->source_mode = (rnd() & 1u) ? MAC_ADDRESS_SHORT : MAC_ADDRESS_EXTENDED;
        h->flags &= (uint8_t)~(MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION);
    } else if (shaped && h->type == MAC_FRAME_ACK) {
        h->destination_mode = h->source_mode = MAC_ADDRESS_NONE;
        h->flags &= (uint8_t)~(MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION);
    } else if (shaped && h->type == MAC_FRAME_COMMAND && (rnd() & 1u)) {
        h->flags |= MAC_FLAG_ACK_REQUEST;
    }
    h->destination_pan = (rnd() % 3u) ? (uint16_t)rnd() : 0xffffu;
    h->source_pan = (rnd() % 3u) ? (uint16_t)rnd() : 0xffffu;
    if ((h->flags & MAC_FLAG_PAN_COMPRESSION) && (rnd() % 4u)) h->source_pan = h->destination_pan;
    for (i = 0; i < 8; i++) { h->destination[i] = (uint8_t)rnd(); h->source[i] = (uint8_t)rnd(); }
    if (!(rnd() % 3u)) { h->destination[0] = 0xffu; h->destination[1] = (uint8_t)((rnd() & 1u) ? 0xffu : 0xfeu); }
    if (!(rnd() % 4u)) { h->source[0] = (uint8_t)((rnd() & 1u) ? 0xffu : 0xfeu); h->source[1] = 0xffu; }
}
static void random_payload(uint8_t type, uint8_t *p, uint16_t *n)
{
    static const uint8_t ids[] = { MAC_COMMAND_ASSOCIATION_REQUEST, MAC_COMMAND_ASSOCIATION_RESPONSE,
                                   MAC_COMMAND_DISASSOCIATION, MAC_COMMAND_DATA_REQUEST,
                                   MAC_COMMAND_BEACON_REQUEST, 0u, 5u, 9u };
    uint16_t i;
    *n = (uint16_t)(type == MAC_FRAME_COMMAND ? rnd() % 6u : rnd() % 110u);
    for (i = 0; i < *n; i++) p[i] = (uint8_t)rnd();
    if (type == MAC_FRAME_COMMAND && *n) p[0] = ids[rnd() % 8u];
}
static void decode_all(const uint8_t *body, uint16_t length)
{
    mac_frame_info_t info;
    uint8_t profile;
    memset(&info, 0xa5, sizeof(info));
    codec_record(0, mac_frame_decode(body, length, &info)); mix_bytes(&info, sizeof(info));
    for (profile = 0; profile < 3u; profile++) {
        memset(&info, 0x5a, sizeof(info));
        codec_record(1, mac_frame_decode_profile(body, length, &info, profile));
        mix_bytes(&info, sizeof(info));
    }
    record_state();
}
static void codec_fuzz(void)
{
    static uint8_t body[160], payload[160];
    mac_header_t header;
    mac_command_t command;
    mac_beacon_info_t beacon;
    unsigned n, i;
    uint16_t plen;
    uint8_t length;
    child_work_full_reset(); link_work_host_reset();
    for (n = 0; n < 150000u; n++) {
        random_header(&header);
        random_payload(header.type, payload, &plen);
        memset(body, 0, sizeof(body));
        length = 0xeeu;
        codec_record(2, mac_frame_encode(&header, payload, plen, body,
                                         (uint16_t)((rnd() % 5u) ? MAC_FRAME_MAX_BODY : rnd() % 140u), &length));
        mix(length); mix_bytes(body, sizeof(body));
        decode_all(body, length == 0xeeu ? (uint16_t)(rnd() % 130u) : length);
        if (length != 0xeeu) {
            decode_all(body, (uint16_t)(length - 1u));
            decode_all(body, (uint16_t)(length + 1u));
        }
        for (i = 0; i < 3u; i++) {
            uint16_t len = length == 0xeeu ? (uint16_t)(rnd() % 130u) : length;
            if (len && len <= sizeof(body)) body[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8u));
            if (!(rnd() % 8u)) len = (uint16_t)(rnd() % 132u);
            decode_all(body, len);
        }
        for (i = 0; i < 6u; i++) payload[i] = (uint8_t)rnd();
        if (rnd() & 1u) payload[0] = (uint8_t)(1u + rnd() % 7u);
        memset(&command, 0x33, sizeof(command));
        codec_record(3, mac_command_decode(payload, (uint16_t)(rnd() % 7u), &command));
        mix_bytes(&command, sizeof(command));
        length = 0xeeu; memset(body, 0, 8);
        codec_record(4, mac_command_encode(&command, body, (uint16_t)(rnd() % 6u), &length));
        mix(length); mix_bytes(body, 8);
        plen = (uint16_t)(rnd() % 120u);
        for (i = 0; i < plen; i++) payload[i] = (uint8_t)rnd();
        if (rnd() & 1u) { payload[2] &= 0x80u; payload[3] &= 0x77u; }
        if (!(rnd() % 4u) && plen > 5u) { payload[4] = 0xffu; payload[5] = 0xffu; }
        memset(&beacon, 0x77, sizeof(beacon));
        codec_record(5, mac_beacon_decode(payload, plen, &beacon));
        mix_bytes(&beacon, sizeof(beacon));
        record_state();
    }
}

/* Invalid, aliased and exact-loan command inputs through the public entries. */
static void codec_edges(void)
{
    uint8_t *start = link_work_arena.protocol.parent.join.work.start.command;
    mac_command_t command;
    mac_frame_info_t info;
    uint16_t len;
    uint8_t id;
    child_work_full_reset(); link_work_host_reset();
    codec_record(3, mac_command_decode(NULL, 1, &command));
    codec_record(3, mac_command_decode(outside, 1, NULL));
    codec_record(1, mac_frame_decode_profile(NULL, 5, &info, 0));
    codec_record(1, mac_frame_decode_profile(outside, 5, NULL, 0));
    codec_record(1, mac_frame_decode_profile(outside, 5, &info, 3));
    codec_record(1, mac_frame_decode_profile(outside, 5, (mac_frame_info_t *)(void *)&link_work_arena, 0));
    codec_record(1, mac_frame_decode_profile(upper, 5, &info, 0));
    codec_record(3, mac_command_decode(outside, 2, (mac_command_t *)(void *)&link_work_arena));
    codec_record(3, mac_command_decode(child, 2, &command));
    record_state();
    for (id = 0; id < 9u; id++)
        for (len = 0; len < 4u; len++) {
            check(link_work_enter(LW_JOIN_START) == 1u, "enter JOIN_START");
            start[0] = id; start[1] = (uint8_t)(id * 37u);
            memset(&command, 0x44, sizeof(command));
            codec_record(3, mac_command_decode(start, len, &command));
            mix_bytes(&command, sizeof(command));
            if (len == 2u) loan_decodes++;
            codec_record(3, mac_command_decode(start, len, (mac_command_t *)(void *)start));
            check(link_work_leave(LW_JOIN_START) == 1u, "leave JOIN_START");
            record_state();
        }
}

int main(void)
{
    unsigned i, j;
    setup_tables();
    guard_walk();
    codec_fuzz();
    codec_edges();
    check(io_hits[0] && io_hits[1] && child_hits[0] && child_hits[1] && child_hits[2],
          "guard result coverage");
    check(external_hits[0] && external_hits[1] && inside_hits[0] && inside_hits[1] &&
          disjoint_hits[0] && disjoint_hits[1], "classification coverage");
    check(upper_sweeps >= 4u && child_sweeps >= 8u, "granted-state sweeps");
    check(codec_hits[1][MAC_CODEC_OK] && codec_hits[1][MAC_CODEC_INVALID_HEADER] &&
          codec_hits[1][MAC_CODEC_UNSUPPORTED_COMMAND] && codec_hits[1][MAC_CODEC_INVALID_COMMAND] &&
          codec_hits[1][MAC_CODEC_TRUNCATED] && codec_hits[1][MAC_CODEC_INVALID_ARGUMENT],
          "frame decode result coverage");
    check(codec_hits[2][MAC_CODEC_OK] && codec_hits[2][MAC_CODEC_INVALID_HEADER],
          "frame encode result coverage");
    check(codec_hits[3][MAC_CODEC_OK] && codec_hits[3][MAC_CODEC_INVALID_ARGUMENT] &&
          codec_hits[5][MAC_CODEC_OK] && codec_hits[5][MAC_CODEC_INVALID_BEACON] &&
          codec_hits[5][MAC_CODEC_TRUNCATED], "command/beacon result coverage");
    check(loan_decodes == 9u, "exact command loan decodes");
    printf("guards: io %lu/%lu child %lu/%lu/%lu external %lu/%lu inside %lu/%lu disjoint %lu/%lu sweeps %lu+%lu\n",
           io_hits[0], io_hits[1], child_hits[0], child_hits[1], child_hits[2],
           external_hits[0], external_hits[1], inside_hits[0], inside_hits[1],
           disjoint_hits[0], disjoint_hits[1], upper_sweeps, child_sweeps);
    for (i = 0; i < 6u; i++) {
        printf("codec%u:", i);
        for (j = 0; j < 16u; j++) printf(" %lu", codec_hits[i][j]);
        printf("\n");
    }
    printf("J3 shallow differential: %lu records, digest %016llx%s\n", records,
           (unsigned long long)digest, failures ? " FAIL" : "");
    return failures != 0;
}
