/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_link_workspace_internal.h"
#include <stddef.h>
#include <string.h>

static MCU_XDATA link_work_ownership_t ownership;
#define TOP ownership.top
#define GRANT ownership.grant
#define ANCESTOR ownership.ancestors
MCU_XDATA link_work_arena_t link_work_arena;
#if defined(__SDCC)
typedef uint16_t work_address_t;
#else
typedef uintptr_t work_address_t;
#endif
#define A link_work_arena
#define J protocol.parent.join
#define P protocol.poll
#define T protocol.operation.tx
#define N protocol.parent.nwk
#define Z protocol.parent.zdo
#define OFF(m) offsetof(link_work_arena_t, m)
#define SIZE(m) sizeof(((link_work_arena_t *)0)->m)
#define REGION(m) { OFF(m), SIZE(m) }
#define ROOT 1UL
#define B(n) (1UL << (n))
static const MCU_CODE struct { uint16_t first, size; } regions[LW_FRAMES] = {
    {0,0}, REGION(keys), REGION(J), REGION(J), REGION(J), REGION(J),
    REGION(P), REGION(P), REGION(P), REGION(P),
    REGION(T), REGION(T), REGION(T), REGION(T),
    {0,0}, REGION(protocol.codec.candidate), REGION(protocol.codec.beacon),
    REGION(protocol.operation.association),
    {OFF(protocol.parent.scan), OFF(protocol.parent.scan.decoded)-OFF(protocol.parent.scan)},
    REGION(protocol.parent.selected), REGION(N), REGION(N), REGION(N),
    REGION(Z.work), REGION(Z.work), REGION(Z.server), REGION(Z.node), {0,0},
    REGION(protocol.parent.scan.decoded)
};
static const MCU_CODE uint32_t callers[LW_FRAMES] = {
    0, ROOT, ROOT, ROOT, ROOT, ROOT,
    ROOT|B(LW_JOIN_INIT), ROOT|B(LW_JOIN_STEP), ROOT|B(LW_JOIN_STEP), ROOT|B(LW_JOIN_STEP),
    ROOT|B(LW_JOIN_STEP)|B(LW_POLL_STEP)|B(LW_NWK_BUILD),
    ROOT|B(LW_TX_INTERVAL), ROOT|B(LW_TX_OBSERVED),
    ROOT|B(LW_JOIN_STEP)|B(LW_NWK_CANCEL),
    ROOT|B(LW_JOIN_START)|B(LW_POLL_START)|B(LW_NWK_BUILD),
    ROOT|B(LW_POLL_STEP)|B(LW_TX_SUBMIT)|B(LW_TX_STEP)|B(LW_TX_INTERVAL)|B(LW_ASSOCIATION)|B(LW_SCAN),
    ROOT|B(LW_MAC_ENCODE)|B(LW_MAC_DECODE)|B(LW_SCAN),
    ROOT|B(LW_JOIN_STEP), ROOT, ROOT, ROOT, ROOT, ROOT,
    ROOT, ROOT, ROOT|B(LW_ZDO_RX), ROOT|B(LW_ZDO_RX),
    ROOT|B(LW_ZDO_INIT)|B(LW_ZDO_SERVER), ROOT|B(LW_SCAN)
};

/* SDCC mcs51 emits this unchanged uint32_t table little-endian. Read only
 * the required byte, after the frame/TOP range checks below. Native code
 * retains the word expression, without assuming its host byte order. */
#if defined(__SDCC)
#define CALLER_ALLOWED(f,p) \
    (((const uint8_t MCU_CODE *)callers)[4u*(f)+((p)>>3)] & \
    (uint8_t)(1u<<((p)&7u)))
#else
#define CALLER_ALLOWED(f,p) (callers[(f)] & B(p))
#endif

static uint8_t tx_frame(uint8_t f) { return f >= LW_TX_SUBMIT && f <= LW_TX_OBSERVED; }
uint8_t link_work_root(void) { return !TOP && !GRANT; }
static uint8_t held(uint8_t frame)
{
    uint8_t p = TOP;
    while (p) { if (p == frame) return 1; p = ANCESTOR[p]; }
    return 0;
}
uint8_t link_work_enter(uint8_t frame)
{
    uint8_t p;
    uint16_t a, b, n, m;
    if (!frame || frame >= LW_FRAMES || TOP >= LW_FRAMES || GRANT ||
        !CALLER_ALLOWED(frame,TOP)) return 0;
#if defined(__SDCC)
    {
        extern MCU_XDATA uint8_t flash_exec_reserved_end;
        /* The first real lower private fence must cover the entire arena.
         * This is an admission check, not a substitute for the linked proof. */
        uint16_t address = (uint16_t)&A;
        if (!address || address >= 0x1e00u || sizeof(A) > 0x1e00u-address ||
            address + sizeof(A) > (uint16_t)&flash_exec_reserved_end) return 0;
    }
#endif
    a = regions[frame].first; n = regions[frame].size;
    for (p = TOP; p; p = ANCESTOR[p]) {
        if (p == frame) return 0;
        b = regions[p].first; m = regions[p].size;
        if (n && m && (a <= b ? b-a < n : a-b < m) &&
            !(tx_frame(frame) && tx_frame(p))) return 0;
    }
    ANCESTOR[frame] = TOP; TOP = frame;
    return 1;
}
uint8_t link_work_leave(uint8_t frame)
{
    volatile uint8_t MCU_XDATA *p;
    uint16_t i;
    if (!frame || frame >= LW_FRAMES || TOP != frame || GRANT) return 0;
    /* Only the outer TX invocation owns the shared TX control. Its checked
     * observed->interval->exact callees borrow it and must not wipe it. */
    if (!(tx_frame(frame) && tx_frame(ANCESTOR[frame]))) {
        p = (volatile uint8_t MCU_XDATA *)&A + regions[frame].first;
        for (i = 0; i < regions[frame].size; i++) p[i] = 0;
    }
    TOP = ANCESTOR[frame]; ANCESTOR[frame] = 0;
    return 1;
}
uint8_t link_work_return_word(uint16_t volatile pair)
{
    /* An impossible mismatched release is an error, never reported as OK.
     * It leaves the actual owner held rather than clearing unrelated work. */
    return link_work_leave((uint8_t)(pair >> 8)) ? (uint8_t)pair : 255;
}
uint8_t link_work_finish_group(uint8_t first, uint8_t last, uint8_t result)
{
    return TOP >= first && TOP <= last ? link_work_return(TOP, result) : 255;
}

/* Generic-pointer classification precedes address conversion. Never truncate
 * a CODE/IDATA pointer and compare it to an XDATA arena address. */
static uint8_t location(const void *p, uint16_t size, work_address_t MCU_XDATA *address)
{
#if defined(__SDCC)
    union { const void *pointer; uint8_t byte[3]; } value;
    uint16_t a;
    value.pointer = p;
    a = (uint16_t)value.byte[0] | (uint16_t)value.byte[1] << 8;
    if (value.byte[2] == 0) {
        if (a >= 0x1e00u || size > 0x1e00u-a) return 0;
        *address = a; return 1;
    }
    if (value.byte[2] == 0x80) return size <= 0x8000u && a <= 0x8000u-size ? 2 : 0;
    if (value.byte[2] == 0x40) return size <= 256u && a <= 256u-size ? 3 : 0;
    return 0;
#else
    uintptr_t a = (uintptr_t)p;
    if (size && a > UINTPTR_MAX-(size-1u)) return 0;
    *address = a; return 1;
#endif
}
#define OWNER_OVERLAP(a,n) ((a) <= (work_address_t)&ownership ? \
    (work_address_t)&ownership-(a) < (n) : (a)-(work_address_t)&ownership < sizeof(ownership))
static uint8_t outside_prefix(work_address_t address)
{
#if defined(__SDCC)
    extern MCU_XDATA uint8_t flash_exec_reserved_end;
    return address > (uint16_t)&flash_exec_reserved_end;
#else
    (void)address;
    return 1;
#endif
}
uint8_t link_work_external(const void * volatile p, uint16_t volatile size)
{
    work_address_t a, b = (work_address_t)&A;
    uint8_t space;
    if (!size) return 1;
    if (!p) return 0;
    space = location(p, size, &a);
    if (!space) return 0;
    if (space != 1) return 1;
    if (OWNER_OVERLAP(a,size)) return 0;
    return (a <= b ? b-a >= size : a-b >= sizeof(A)) && outside_prefix(a);
}

/* Real typed caller subobjects, not whole-owner whitelist regions. Only
 * byte-array inputs admit bounded slices; all output loans are exact slots. */
typedef struct {
    uint8_t operation, owner, permission, writing, array;
    uint16_t first, size;
} loan_t;
#define LOAN(op, own, g, wr, arr, member) {op,own,g,wr,arr,OFF(member),SIZE(member)}
#define INPUT(op,own,m) LOAN(op,own,0,0,0,m)
#define OUTPUT(op,own,m) LOAN(op,own,0,1,0,m)
static const MCU_CODE loan_t loans[] = {
    INPUT(LW_CHILD_COMMAND,LW_JOIN_START,J.work.start.command),
    INPUT(LW_TX_OBSERVED,LW_JOIN_STEP,J.work.step.input.source),
    INPUT(LW_TX_OBSERVED,LW_JOIN_STEP,J.work.step.nested.pe.source),
    OUTPUT(LW_TX_OBSERVED,LW_JOIN_STEP,J.work.step.output.radio),
    INPUT(LW_TX_INTERVAL,LW_JOIN_STEP,J.work.step.input.source),
    INPUT(LW_TX_INTERVAL,LW_JOIN_STEP,J.work.step.nested.pe.source),
    OUTPUT(LW_TX_INTERVAL,LW_JOIN_STEP,J.work.step.output.radio),
    INPUT(LW_TX_STEP,LW_JOIN_STEP,J.work.step.input.source.source),
    INPUT(LW_TX_STEP,LW_JOIN_STEP,J.work.step.nested.pe.source.source),
    OUTPUT(LW_TX_STEP,LW_JOIN_STEP,J.work.step.output.radio.control),
    INPUT(LW_POLL_START,LW_JOIN_STEP,J.work.step.nested.pr),
    INPUT(LW_POLL_STEP,LW_JOIN_STEP,J.work.step.nested.pe),
    OUTPUT(LW_POLL_STEP,LW_JOIN_STEP,J.work.step.pa),
    INPUT(LW_ASSOCIATION,LW_JOIN_STEP,J.work.step.nested.ar),
    INPUT(LW_ASSOCIATION,LW_JOIN_STEP,J.work.step.nested.ae),
    OUTPUT(LW_ASSOCIATION,LW_JOIN_STEP,J.observation),
    INPUT(LW_MAC_ENCODE,LW_JOIN_START,J.work.start.header),
    INPUT(LW_MAC_ENCODE,LW_JOIN_START,J.work.start.command),
    OUTPUT(LW_MAC_ENCODE,LW_JOIN_START,J.work.start.body),
    OUTPUT(LW_MAC_ENCODE,LW_JOIN_START,J.work.start.length),
    INPUT(LW_MAC_ENCODE,LW_POLL_START,P.syntax.header),
    OUTPUT(LW_MAC_ENCODE,LW_POLL_START,P.io.start.body),
    OUTPUT(LW_MAC_ENCODE,LW_POLL_START,P.io.start.length),
    INPUT(LW_MAC_ENCODE,LW_NWK_BUILD,N.mac_header),
    OUTPUT(LW_MAC_DECODE,LW_POLL_STEP,P.syntax.frame),
    OUTPUT(LW_MAC_DECODE,LW_TX_SUBMIT,T.decoded),
    OUTPUT(LW_MAC_DECODE,LW_TX_STEP,T.decoded),
    INPUT(LW_MAC_DECODE,LW_TX_INTERVAL,T.interval_ack),
    OUTPUT(LW_MAC_DECODE,LW_TX_INTERVAL,T.interval_decoded),
    OUTPUT(LW_MAC_DECODE,LW_ASSOCIATION,protocol.operation.association),
    OUTPUT(LW_MAC_DECODE,LW_SCAN,protocol.parent.scan.frame),
    OUTPUT(LW_BEACON,LW_SCAN,protocol.parent.scan.beacon),
    OUTPUT(LW_NWK_BEACON,LW_SCAN,protocol.parent.scan.candidate.network),
    OUTPUT(LW_PARENT,LW_PARENT,protocol.parent.selected),
    INPUT(LW_TX_OBSERVED,LW_NWK_CANCEL,N.cancellation),
    INPUT(LW_TX_INTERVAL,LW_NWK_CANCEL,N.cancellation),
    INPUT(LW_TX_STEP,LW_NWK_CANCEL,N.cancellation.source),
    INPUT(LW_ZDO_SERVER,LW_ZDO_RX,Z.work.server.local),
    INPUT(LW_ZDO_SERVER,LW_ZDO_RX,Z.work.server.rx),
    OUTPUT(LW_ZDO_SERVER,LW_ZDO_RX,Z.work.server.info),
    OUTPUT(LW_ZDO_DECODE,LW_ZDO_RX,Z.work.descriptor.node),
    INPUT(LW_ZDO_ENCODE,LW_ZDO_INIT,Z.work.descriptor.node),
    OUTPUT(LW_ZDO_ENCODE,LW_ZDO_INIT,Z.work.descriptor.descriptor_bytes),
    INPUT(LW_ZDO_ENCODE,LW_ZDO_SERVER,Z.server.reply),
    OUTPUT(LW_ZDO_ENCODE,LW_ZDO_SERVER,Z.server.staged),

    LOAN(LW_CHILD_WIRE_NWK,LW_KEYS,LW_WIRE_NWK,0,1,keys.a),
    LOAN(LW_CHILD_WIRE_NWK,LW_KEYS,LW_WIRE_NWK,1,0,keys.nwk),
    LOAN(LW_CHILD_WIRE_NWK,LW_NWK_HINT,LW_HINT,1,0,N.hint),
    LOAN(LW_CHILD_WIRE_DECODE,LW_KEYS,LW_WIRE_DECODE,0,1,keys.a),
    LOAN(LW_CHILD_WIRE_DECODE,LW_KEYS,LW_WIRE_DECODE,1,0,keys.packet),
    LOAN(LW_CHILD_WIRE_ENCODE,LW_KEYS,LW_WIRE_ENCODE,0,0,keys.packet),
    LOAN(LW_CHILD_WIRE_ENCODE,LW_KEYS,LW_WIRE_ENCODE,1,0,keys.a),
    LOAN(LW_CHILD_WIRE_ENCODE,LW_KEYS,LW_WIRE_ENCODE,1,0,keys.size),
    LOAN(LW_CHILD_APS,LW_KEYS,LW_WIRE_ENCODE,0,1,keys.packet.payload),
    LOAN(LW_CHILD_WIRE_INSPECT,LW_KEYS,LW_WIRE_INSPECT,0,1,keys.a),
    LOAN(LW_CHILD_WIRE_INSPECT,LW_KEYS,LW_WIRE_INSPECT,1,0,keys.meta),
    LOAN(LW_CHILD_WIRE_CRYPT,LW_KEYS,LW_WIRE_CRYPT,0,0,keys.key),
    LOAN(LW_CHILD_WIRE_CRYPT,LW_KEYS,LW_WIRE_CRYPT,0,1,keys.a),
    LOAN(LW_CHILD_WIRE_CRYPT,LW_KEYS,LW_WIRE_CRYPT,1,0,keys.a),
    LOAN(LW_CHILD_WIRE_CRYPT,LW_KEYS,LW_WIRE_CRYPT,1,0,keys.b),
    LOAN(LW_CHILD_WIRE_CRYPT,LW_KEYS,LW_WIRE_CRYPT,1,0,keys.info),
    LOAN(LW_CHILD_CCM,LW_KEYS,LW_WIRE_CRYPT,0,0,keys.key.key),
    LOAN(LW_CHILD_CCM,LW_KEYS,LW_WIRE_CRYPT,0,0,keys.key.limits),
    LOAN(LW_CHILD_HASH,LW_KEYS,LW_HASH_KEY,0,0,keys.hash_key),
    LOAN(LW_CHILD_HASH,LW_KEYS,LW_HASH_KEY,1,0,keys.key.key),
    LOAN(LW_CHILD_HASH,LW_KEYS,LW_HASH_KEY,1,0,keys.hash_info),
    {LW_CHILD_HASH,LW_KEYS,LW_HASH_VERIFY,0,0,OFF(keys.record)+LINK_WORK_PK_OFFSET,16},
    {LW_CHILD_HASH,LW_KEYS,LW_HASH_VERIFY,1,0,OFF(keys.packet.payload)+10,16},
    LOAN(LW_CHILD_HASH,LW_KEYS,LW_HASH_VERIFY,1,0,keys.hash_info),
    {LW_CHILD_INSTALL,LW_KEYS,LW_INSTALL_CODE,0,0,OFF(keys.a),18},
    {LW_CHILD_INSTALL,LW_KEYS,LW_INSTALL_CODE,1,0,OFF(keys.record)+LINK_WORK_LK_OFFSET,16},
    LOAN(LW_CHILD_INSTALL,LW_KEYS,LW_INSTALL_CODE,1,0,keys.hash_info),
    {LW_CHILD_MMO,LW_KEYS,LW_INSTALL_CODE,0,0,OFF(keys.a),18},
    {LW_CHILD_MMO,LW_KEYS,LW_INSTALL_CODE,1,0,OFF(keys.record)+LINK_WORK_LK_OFFSET,16},
    LOAN(LW_CHILD_MMO,LW_KEYS,LW_INSTALL_CODE,1,0,keys.hash_info)
};
uint8_t link_work_io(uint8_t operation, const void * volatile p, uint16_t volatile size, uint8_t writing)
{
    work_address_t a, base = (work_address_t)&A;
    uint16_t offset;
    const MCU_CODE loan_t *l;
    uint8_t space;
    if (!size) return 1;
    if (!p) return 0;
    space = location(p, size, &a);
    if (!space || (writing && space == 2)) return 0;
    if (space != 1) return 1;
    if (OWNER_OVERLAP(a,size)) return 0;
    if (a <= base ? base-a >= size : a-base >= sizeof(A)) return outside_prefix(a);
    if (a < base || a-base > sizeof(A) || size > sizeof(A)-(a-base)) return 0;
    offset = (uint16_t)(a-base);
    for (l = loans; l < loans + sizeof(loans)/sizeof(loans[0]); l++) {
        if (l->operation != operation || l->writing != writing ||
            l->permission != GRANT || !held(l->owner) || offset < l->first) continue;
        if (l->array ? offset-l->first <= l->size && size <= l->size-(offset-l->first) :
            offset == l->first && size == l->size) return 1;
    }
    return 0;
}
uint8_t link_work_grant(uint8_t permission)
{
    if (GRANT || !permission || permission > LW_HINT ||
        (permission == LW_HINT ? TOP != LW_NWK_HINT : TOP != LW_KEYS)) return 0;
    GRANT = permission;
    return 1;
}
uint8_t link_work_call_result(uint8_t result)
{
    if (!GRANT) return 255;
    GRANT = 0;
    return result;
}
uint8_t link_work_counter_request(uint8_t operation, const void *data, uint16_t size, const void *length)
{
    if (!GRANT && (!data || link_work_external(data, size)) &&
        (!length || link_work_external(length, 1))) return 1;
    if (TOP != LW_KEYS || GRANT != operation) return 0;
    if (operation == LW_COUNTER_READ)
        return data == A.keys.record && size == 112 && length == &A.keys.size;
    if (operation == LW_COUNTER_CREATE || operation == LW_COUNTER_SAVE)
        return data == A.keys.record && size == 112 && !length;
    return operation == LW_COUNTER_TAKE && data == &A.keys.counter && size == 4 && !length;
}
uint8_t link_work_counter_object(const void *p, uint8_t size)
{
    if (link_work_external(p, size)) return 2;
    if (TOP != LW_KEYS) return 0;
    if (GRANT == LW_COUNTER_READ && p == &A.keys.size && size == 1) return 1;
    if ((GRANT == LW_COUNTER_READ || GRANT == LW_COUNTER_CREATE || GRANT == LW_COUNTER_SAVE) &&
        p == A.keys.record && size == 112) return 1;
    return GRANT == LW_COUNTER_TAKE && p == &A.keys.counter && size == 4;
}
uint8_t link_work_clean(void)
{
    const uint8_t MCU_XDATA *p = (const uint8_t MCU_XDATA *)&A;
    uint16_t i;
    if (TOP || GRANT) return 0;
    for (i = 0; i < LW_FRAMES; i++) if (ANCESTOR[i]) return 0;
    for (i = 0; i < sizeof(A); i++) if (p[i]) return 0;
    return 1;
}
#if defined(CC2530_HOST_TEST)
void link_work_host_reset(void)
{
    /* Only the existing synthetic complete-CPU-reset boundary may call this. */
    memset(&A, 0, sizeof(A)); memset(&ownership, 0, sizeof(ownership));
}
#endif
#if defined(__SDCC)
typedef char keys_size[sizeof(link_work_keys_t) == 617 ? 1 : -1];
typedef char join_size[sizeof(link_work_join_t) == 218 ? 1 : -1];
typedef char poll_size[sizeof(link_work_poll_t) == 109 ? 1 : -1];
typedef char tx_size[sizeof(link_work_tx_t) == 135 ? 1 : -1];
typedef char arena_size[sizeof(link_work_arena_t) == 617 ? 1 : -1];
#endif
