/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_link_child_workspace_internal.h"
#include "mac_link_workspace_guard_internal.h"
#include <stddef.h>
#include <string.h>
static MCU_XDATA child_work_ownership_t owner;
MCU_XDATA child_work_arena_t child_work_arena;
extern MCU_XDATA uint8_t child_work_reserved_end;
#define A child_work_arena
#define TOP owner.top
#define PENDING owner.pending
#define O(m) offsetof(child_work_arena_t,m)
#define S(m) sizeof(A.m)
#define R(m) { O(m),S(m) }
static const MCU_CODE struct { uint16_t first, length; } regions[CW_FRAMES] = {
    {0,0},R(wire),R(wire),R(wire),R(wire),R(wire),R(wire),R(wire.engine.ccm),
    R(hash.key),R(hash.mmo),{0,0},R(wire.engine.parse.nwk),{0,0},
    R(wire.engine.parse.aps),{0,0},
    R(nv.counter_check),R(nv.counter_check),R(nv.counter_check),R(nv.counter_check),
    R(nv.counter_check),R(nv.record),R(nv.record),R(nv.writer),R(nv.reader),{0,0},{0,0}
};
static uint8_t edge(uint8_t parent, uint8_t child)
{
    if (!parent) return child>0 && child<CW_AES;
    if (parent>=CW_WIRE_NWK && parent<=CW_WIRE_CRYPT) {
        if (owner.wire_stage) return 0;
        if (child==CW_NWK_DECODE) return parent!=CW_WIRE_APS && parent!=CW_WIRE_ENCODE;
        if (child==CW_APS_DECODE) return parent!=CW_WIRE_NWK;
        if (parent==CW_WIRE_ENCODE) return child==CW_NWK_ENCODE || child==CW_APS_ENCODE;
        return parent==CW_WIRE_CRYPT && child==CW_CCM;
    }
    if (parent==CW_KEY_HASH || parent==CW_INSTALL) return child==CW_MMO;
    if (parent==CW_CCM || parent==CW_MMO) return child==CW_AES;
    if (parent==CW_COUNTER_OPEN) return child==CW_RECORD_LOAD;
    if (parent>=CW_COUNTER_CREATE && parent<=CW_COUNTER_SAVE)
        return child==CW_RECORD_LOAD || child==CW_RECORD_REPLACE;
    if (parent==CW_RECORD_LOAD) return child==CW_READ;
    if (parent==CW_RECORD_REPLACE) return child==CW_READ || child==CW_WRITE;
    return parent==CW_WRITE && (child==CW_READ || child==CW_EXEC);
}
uint8_t child_work_phase(void) { return TOP; }
uint8_t child_work_poisoned(void) { return owner.poison; }
uint8_t child_work_leaf(uint8_t frame)
{
    if ((frame!=CW_AES && frame!=CW_EXEC) || owner.poison || TOP>=CW_FRAMES) return 0;
    return !TOP ? !PENDING : PENDING==frame && edge(TOP,frame);
}
uint8_t child_work_grant(uint8_t frame)
{
    if (!TOP || TOP>=CW_FRAMES || PENDING || owner.poison ||
        !frame || frame>=CW_FRAMES || !edge(TOP,frame)) return 0;
    PENDING=frame;
    return 1;
}
uint8_t child_work_enter(uint8_t frame)
{
    uint8_t p;
    if (!frame || frame>=CW_AES || TOP>=CW_FRAMES || owner.poison ||
        (TOP ? PENDING!=frame : PENDING!=0) || !edge(TOP,frame)) return 0;
    for (p=TOP;p;p=owner.ancestors[p]) if (p==frame) return 0;
#if defined(__SDCC)
    {
        extern MCU_XDATA uint8_t flash_exec_reserved_end;
        uint16_t first=(uint16_t)&owner, a=(uint16_t)&A, end=(uint16_t)&child_work_reserved_end;
        if (!a || a<first || end<a || end>=0x1e00u ||
            sizeof(A)>end-a || end>=(uint16_t)&flash_exec_reserved_end) return 0;
    }
#endif
    if (frame==CW_CCM && TOP==CW_WIRE_CRYPT) owner.wire_stage=1;
    owner.ancestors[frame]=TOP; TOP=frame; PENDING=0;
    return 1;
}
uint8_t child_work_end(uint8_t frame, uint8_t result)
{
    if (!TOP || PENDING!=frame) return 255;
    PENDING=0;
    return result;
}
uint8_t child_work_return(uint16_t volatile pair)
{
    uint8_t frame=(uint8_t)(pair>>8), parent;
    uint16_t i,n;
    volatile uint8_t MCU_XDATA *p;
    if (!frame || frame>=CW_AES || TOP!=frame || PENDING) return 255;
    parent=owner.ancestors[frame];
    if (!owner.poison) {
        n=regions[frame].length;
        p=(volatile uint8_t MCU_XDATA *)&A+regions[frame].first;
        for (i=0;i<n;i++) p[i]=0;
    }
    if (frame>=CW_WIRE_NWK && frame<=CW_WIRE_CRYPT) owner.wire_stage=0;
    TOP=parent; owner.ancestors[frame]=0;
    if (parent) PENDING=frame;
    return (uint8_t)pair;
}
uint8_t child_work_poison(uint8_t cause)
{
    if (TOP<CW_COUNTER_OPEN || TOP>CW_READ || !cause) return 0;
    if (!owner.poison) { owner.poison=cause; owner.first_owner=TOP; }
    return 1;
}
typedef struct {
    uint8_t target,parent,op,writing,array;
    uint16_t first,size;
} child_loan_t;
#define L(t,p,op,w,a,m) {t,p,op,w,a,O(m),S(m)}
#define WP(t,p,op,w,a,m) L(t,p,op,w,a,wire.m)
#define NWK_LOANS(p) \
    WP(CW_NWK_DECODE,p,LW_CHILD_NWK,0,1,buffers.wire.header), \
    WP(CW_NWK_DECODE,p,LW_CHILD_NWK,1,0,engine.parse.syntax.nwk)
#define APS_LOANS(p) \
    WP(CW_APS_DECODE,p,LW_CHILD_APS,0,1,buffers.wire.header), \
    WP(CW_APS_DECODE,p,LW_CHILD_APS,1,0,engine.parse.syntax.aps)
static const MCU_CODE child_loan_t loans[] = {
    NWK_LOANS(CW_WIRE_NWK),NWK_LOANS(CW_WIRE_DECODE),NWK_LOANS(CW_WIRE_INSPECT),NWK_LOANS(CW_WIRE_CRYPT),
    APS_LOANS(CW_WIRE_APS),APS_LOANS(CW_WIRE_DECODE),APS_LOANS(CW_WIRE_ENCODE),
    APS_LOANS(CW_WIRE_INSPECT),APS_LOANS(CW_WIRE_CRYPT),
    WP(CW_APS_ENCODE,CW_WIRE_ENCODE,LW_CHILD_APS,0,0,engine.parse.syntax.application),
    WP(CW_APS_ENCODE,CW_WIRE_ENCODE,LW_CHILD_APS,1,0,buffers.body.encoded),
    WP(CW_NWK_ENCODE,CW_WIRE_ENCODE,LW_CHILD_NWK,0,0,engine.parse.syntax.transmit),
    WP(CW_NWK_ENCODE,CW_WIRE_ENCODE,LW_CHILD_NWK,0,1,buffers.body.encoded),
    WP(CW_NWK_ENCODE,CW_WIRE_ENCODE,LW_CHILD_NWK,1,0,buffers.wire.header),
    WP(CW_CCM,CW_WIRE_CRYPT,LW_CHILD_CCM,0,0,crypto.nonce),
    WP(CW_CCM,CW_WIRE_CRYPT,LW_CHILD_CCM,0,1,buffers.wire.frame),
    WP(CW_CCM,CW_WIRE_CRYPT,LW_CHILD_CCM,1,0,buffers.body.text),
    WP(CW_CCM,CW_WIRE_CRYPT,LW_CHILD_CCM,1,0,crypto.written),
    WP(CW_CCM,CW_WIRE_CRYPT,LW_CHILD_CCM,1,0,crypto.info.crypto),
    L(CW_MMO,CW_KEY_HASH,LW_CHILD_MMO,0,1,hash.key.message),
    L(CW_MMO,CW_KEY_HASH,LW_CHILD_MMO,1,0,hash.key.hash),
    L(CW_MMO,CW_KEY_HASH,LW_CHILD_MMO,1,0,hash.key.step),
    WP(CW_AES,CW_CCM,CW_AES_KEY,0,0,engine.ccm.key),
    WP(CW_AES,CW_CCM,CW_AES_INPUT,0,0,engine.ccm.block),
    WP(CW_AES,CW_CCM,CW_AES_OUTPUT,1,0,engine.ccm.cipher),
    WP(CW_AES,CW_CCM,CW_AES_INFO,1,0,engine.ccm.diagnostics),
    L(CW_AES,CW_MMO,CW_AES_KEY,0,0,hash.mmo.hash),
    L(CW_AES,CW_MMO,CW_AES_INPUT,0,0,hash.mmo.block),
    L(CW_AES,CW_MMO,CW_AES_OUTPUT,1,0,hash.mmo.cipher),
    L(CW_AES,CW_MMO,CW_AES_INFO,1,0,hash.mmo.diagnostics),
    L(CW_RECORD_LOAD,CW_COUNTER_CREATE,CW_NV_OUTPUT,1,0,nv.counter_check),
    L(CW_RECORD_LOAD,CW_COUNTER_TAKE,CW_NV_OUTPUT,1,0,nv.counter_check),
    L(CW_RECORD_LOAD,CW_COUNTER_SAVE,CW_NV_OUTPUT,1,0,nv.counter_check),
    L(CW_WRITE,CW_RECORD_REPLACE,CW_WRITE_WORD,0,0,nv.record.word),
    L(CW_EXEC,CW_WRITE,CW_EXEC_WORD,0,0,nv.writer.word),
    L(CW_READ,CW_RECORD_LOAD,CW_READ_OUTPUT,1,0,nv.record.chunk),
    L(CW_READ,CW_RECORD_REPLACE,CW_READ_OUTPUT,1,0,nv.record.chunk),
    /* Reader length is1/4/32 in the writer. Outputs remain exact typed
     * permitted prefixes, not arbitrary interior slices of the32-byte slot. */
    {CW_READ,CW_WRITE,CW_READ_OUTPUT,1,0,O(nv.writer.check),1},
    {CW_READ,CW_WRITE,CW_READ_OUTPUT,1,0,O(nv.writer.check),4},
    L(CW_READ,CW_WRITE,CW_READ_OUTPUT,1,0,nv.writer.check)
};
static uint8_t overlaps(child_address_t a,uint16_t n,child_address_t b,uint16_t m)
{
    return a<=b ? b-a<n : a-b<m;
}
uint8_t child_work_address(uint8_t op, child_address_t volatile address,
                          uint16_t volatile size, uint8_t writing)
{
    child_address_t base=(child_address_t)&A;
    uint16_t offset;
    const MCU_CODE child_loan_t *loan;
#if defined(__SDCC)
    extern MCU_XDATA uint8_t _gptrput_PARM_2,__memcpy_PARM_2[3],_mullong_PARM_2[4];
    if (overlaps(address,size,(uint16_t)&_gptrput_PARM_2,1) ||
        overlaps(address,size,(uint16_t)__memcpy_PARM_2,3) ||
        overlaps(address,size,(uint16_t)_mullong_PARM_2,4)) return 0;
#endif
    if (overlaps(address,size,(child_address_t)&owner,sizeof(owner))) return 0;
#if defined(__SDCC)
    /* The tail covers actual compiler/parameter homes, not an implicit pool. */
    if (address>=base+sizeof(A) && address<=(uint16_t)&child_work_reserved_end) return 0;
#endif
    if (!overlaps(address,size,base,sizeof(A))) return 2;
    if (owner.poison || address<base || address-base>=sizeof(A) ||
        size>sizeof(A)-(address-base) || !PENDING || !edge(TOP,PENDING)) return 0;
    offset=(uint16_t)(address-base);
    for (loan=loans;loan<loans+sizeof(loans)/sizeof(loans[0]);loan++) {
        if (loan->target!=PENDING || loan->parent!=TOP || loan->op!=op ||
            loan->writing!=writing || offset<loan->first) continue;
        if (loan->array ? offset-loan->first<=loan->size &&
            size<=loan->size-(offset-loan->first) :
            offset==loan->first && size==loan->size) return 1;
    }
    return 0;
}
uint8_t child_work_inside(const void *p,uint16_t size)
{
    child_address_t a;
#if defined(__SDCC)
    /* SDCC mcs51 generic pointers are low/high/tag, as in the UPPER guard.
     * Decode locally: no helper carrying live pointers across a CALL, no
     * loss of the space tag, and no integer-sized generic-pointer cast. */
    union { const void *pointer; uint8_t byte[3]; } value;
    value.pointer=p;
    if (!p || !size || value.byte[2]) return 0;
    a=(uint16_t)value.byte[0]|((uint16_t)value.byte[1]<<8);
#else
    if (!p || !size) return 0;
    a=(uintptr_t)p;
#endif
    return overlaps(a,size,(child_address_t)&A,sizeof(A));
}
uint8_t child_work_disjoint(const void *a,uint16_t an,const void *b,uint16_t bn)
{
    child_address_t aa,bb;
    if (!a || !b || !an || !bn) return 1;
#if defined(__SDCC)
    union { const void *pointer; uint8_t byte[3]; } left,right;
    left.pointer=a; right.pointer=b;
    if (left.byte[2]!=right.byte[2]) return 1;
    aa=(uint16_t)left.byte[0]|((uint16_t)left.byte[1]<<8);
    bb=(uint16_t)right.byte[0]|((uint16_t)right.byte[1]<<8);
#else
    aa=(uintptr_t)a; bb=(uintptr_t)b;
#endif
    return !overlaps(aa,an,bb,bn);
}
#if defined(CC2530_HOST_TEST)
void child_work_full_reset(void)
{
    memset(&owner,0,sizeof(owner)); memset(&A,0,sizeof(A));
}
uint8_t child_work_clean(void)
{
    size_t i;
    if (TOP || PENDING || owner.poison || owner.wire_stage) return 0;
    for (i=0;i<CW_FRAMES;i++) if (owner.ancestors[i]) return 0;
    for (i=0;i<sizeof(A);i++) if (((const uint8_t *)&A)[i]) return 0;
    return 1;
}
#endif
#if defined(__SDCC)
typedef char ccm267[sizeof(A.wire.engine.ccm)==267?1:-1];
typedef char syntax78[sizeof(A.wire.engine.parse.syntax)==78?1:-1];
typedef char buffers236[sizeof(A.wire.buffers)==236?1:-1];
typedef char crypto40[sizeof(A.wire.crypto)==40?1:-1];
typedef char hash149[sizeof(A.hash)==149?1:-1];
typedef char record198[sizeof(A.nv.record)==198?1:-1];
typedef char nv394[sizeof(A.nv)==394?1:-1];
typedef char payload543[sizeof(A)==543?1:-1];
#endif
MCU_XDATA uint8_t child_work_reserved_end;
