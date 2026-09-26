/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Isolated native CHILD crypto/ownership checkpoint, not complete E2E.
 */
#include "mac_link_child_workspace_internal.h"
#include "mac_link_workspace_internal.h"
#include "security_aes_model.h"
#include "host_mmio.h"
#include "timebase.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t __memcpy_PARM_2[3], _mullong_PARM_2[4];
#define A child_work_arena
#define U link_work_arena
static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { fprintf(stderr,"child line%u: %s\n", \
    (unsigned)__LINE__,#e); abort(); } } while (0)
static host_mmio_xaddress_hook_t prior;
static uint16_t base;
static uint16_t address(const volatile void *p)
{
    uintptr_t a=(uintptr_t)p,b=(uintptr_t)&A;
    if (a>=b && a-b<sizeof(A)) return base+(uint16_t)(a-b);
    return prior(p);
}
static void reset(uint16_t b)
{
    security_aes_reset(); child_work_full_reset(); link_work_host_reset();
    prior=host_mmio_xaddress_hook; base=b; host_mmio_xaddress_hook=address;
}
static void unchanged(const void *a,const void *b,size_t n) { CHECK(!memcmp(a,b,n)); }
static void aes_slots(void)
{
    uint8_t saved[sizeof(A)],outside[16]={0},i;
    aes_diagnostics_t d,old;
    for (i=0;i<2;i++) {
        reset(i?0x1000:0x20);
        CHECK(child_work_enter(CW_CCM));
        memset(&A,0xa5,sizeof(A)); memcpy(saved,&A,sizeof(A));
        memset(&d,0x3c,sizeof(d)); old=d;
        CHECK(aes128_encrypt_block(outside,outside,A.nv.reader,1000,128,&d)==AES_BUFFER_OWNERSHIP);
        unchanged(saved,&A,sizeof(A)); unchanged(&old,&d,sizeof(d));
        CHECK(child_work_grant(CW_AES));
        CHECK(aes128_encrypt_block(outside,outside,A.wire.engine.ccm.key,1000,128,&d)==AES_BUFFER_OWNERSHIP);
        unchanged(saved,&A,sizeof(A)); unchanged(&old,&d,sizeof(d));
        CHECK(!child_work_enter(CW_MMO));
        CHECK(CW_RETURN(CW_CCM,2)==255);
        unchanged(saved,&A,sizeof(A));
        CHECK(child_work_end(CW_AES,7)==7);
        memset(&A.wire.engine.ccm,0,sizeof(A.wire.engine.ccm));
        CHECK(CW_CALL(CW_AES,aes128_encrypt_block(A.wire.engine.ccm.key,A.wire.engine.ccm.block,
              A.wire.engine.ccm.cipher,1000,128,&A.wire.engine.ccm.diagnostics))==AES_OK);
        CHECK(security_aes_blocks()==1);
        unchanged(saved,&A,offsetof(child_work_arena_t,wire.engine.ccm));
        CHECK(CW_RETURN(CW_CCM,6)==6);
        CHECK(!child_work_phase());
        CHECK(!link_work_external(&A,1));
        CHECK(timebase_deadline_after(0,1,(uint32_t *)&A)==TIMEBASE_INVALID_ARGUMENT);
    }
}
static void nested_wire(void)
{
    ed_packet_t packet,decoded;
    zigbee_security_key_t key;
    zigbee_security_info_t info;
    uint8_t wire[116],secured[116],clear[116],n,changed[sizeof(U)];
    unsigned i;
    reset(0x1000);
    CHECK(link_work_enter(LW_JOIN_STEP));
    memset(&U.protocol.parent.join,0x37,sizeof(U.protocol.parent.join));
    memcpy(changed,&U,sizeof(U));
    memset(&packet,0,sizeof(packet)); memset(&key,0,sizeof(key));
    packet.nwk.version=2; packet.nwk.radius=8; packet.nwk.source=0x1234;
    packet.nwk.destination=0;
    packet.aps.source_endpoint=1; packet.aps.destination_endpoint=2;
    packet.length=3; packet.payload[0]=9;packet.payload[1]=8;packet.payload[2]=7;
    CHECK(ed_wire_encode(&packet,wire,sizeof(wire),&n)==ZIGBEE_SECURITY_OK);
    CHECK(ed_wire_decode(wire,n,&decoded)==ZIGBEE_SECURITY_OK);
    CHECK(decoded.length==3 && !memcmp(decoded.payload,packet.payload,3));
    key.level=5;key.extended_nonce=1;key.key_identifier=1;
    key.limits.block_timeout=1000;key.limits.block_polls=128;
    for (i=0;i<16;i++) key.key[i]=(uint8_t)i;
    CHECK(ed_wire_crypt(0,0,&key,wire,n,secured,sizeof(secured),&info)==ZIGBEE_SECURITY_OK);
    CHECK(child_work_clean());
    n=info.length;
    CHECK(ed_wire_crypt(1,0,&key,secured,n,clear,sizeof(clear),&info)==ZIGBEE_SECURITY_OK);
    CHECK(child_work_clean());
    CHECK(!memcmp(clear,wire,info.length));
    secured[n-1]^=1;
    memset(clear,0xa5,sizeof(clear));
    CHECK(ed_wire_crypt(1,0,&key,secured,n,clear,sizeof(clear),&info)==ZIGBEE_SECURITY_AUTH);
    for(i=0;i<sizeof(clear);i++) CHECK(clear[i]==0xa5);
    CHECK(child_work_clean());
    unchanged(changed,&U,sizeof(U));
    CHECK(link_work_leave(LW_JOIN_STEP));
}
static void nested_hash(void)
{
    static const uint8_t expected[16]={0x29,0x82,0xb9,0x74,0x19,0xde,0x76,0x57,
        0x1b,0x97,0x57,0x47,0x99,0x59,0x82,0x3d};
    uint8_t key[16]={0},out[16],saved[sizeof(A)];
    zigbee_mmo_info_t info;
    reset(0x1000);
    CHECK(zigbee_key_hash(key,0,out,1000,128,&info)==ZIGBEE_MMO_OK);
    unchanged(out,expected,16);CHECK(info.blocks==5);CHECK(child_work_clean());
    CHECK(child_work_enter(CW_KEY_HASH));
    memset(&A,0x73,sizeof(A));memcpy(saved,&A,sizeof(A));
    CHECK(zigbee_mmo_hash(key,16,out,1000,128,&info)==ZIGBEE_MMO_ARGUMENT);
    unchanged(saved,&A,sizeof(A));
    CHECK(child_work_grant(CW_MMO));
    CHECK(child_work_enter(CW_MMO));
    CHECK(!child_work_enter(CW_KEY_HASH));
    CHECK(CW_RETURN(CW_KEY_HASH,2)==255);
    unchanged(saved,&A,sizeof(A));
    CHECK(CW_RETURN(CW_MMO,3)==3);
    CHECK(child_work_end(CW_MMO,3)==3);
    unchanged(saved,&A,offsetof(child_work_arena_t,hash.mmo));
    CHECK(CW_RETURN(CW_KEY_HASH,0)==0);
}
static void nv_disjoint(void)
{
    uint8_t saved[sizeof(A)],out[16]={0};
    zigbee_mmo_info_t info;
    reset(0x1000);
    CHECK(child_work_enter(CW_COUNTER_SAVE));
    memset(&A.nv,0x46,sizeof(A.nv));memcpy(saved,&A,sizeof(A));
    CHECK(child_work_grant(CW_RECORD_REPLACE));CHECK(child_work_enter(CW_RECORD_REPLACE));
    CHECK(child_work_grant(CW_WRITE));CHECK(child_work_enter(CW_WRITE));
    CHECK(child_work_grant(CW_READ));CHECK(child_work_enter(CW_READ));
    memset(A.nv.reader,0x93,sizeof(A.nv.reader));
    CHECK(CW_RETURN(CW_READ,0)==0);CHECK(child_work_end(CW_READ,0)==0);
    unchanged(saved,&A,offsetof(child_work_arena_t,nv.reader));
    CHECK(CW_RETURN(CW_WRITE,0)==0);CHECK(child_work_end(CW_WRITE,0)==0);
    unchanged(saved,&A,offsetof(child_work_arena_t,nv.writer));
    CHECK(CW_RETURN(CW_RECORD_REPLACE,0)==0);CHECK(child_work_end(CW_RECORD_REPLACE,0)==0);
    unchanged(saved,&A,sizeof(A.nv.counter_check));
    CHECK(CW_RETURN(CW_COUNTER_SAVE,0)==0);
    CHECK(child_work_clean());
    CHECK(child_work_enter(CW_COUNTER_SAVE));
    memset(&A.nv,0x91,sizeof(A.nv));memcpy(saved,&A,sizeof(A));
    CHECK(child_work_grant(CW_RECORD_REPLACE));CHECK(child_work_enter(CW_RECORD_REPLACE));
    CHECK(child_work_grant(CW_WRITE));CHECK(child_work_enter(CW_WRITE));
    CHECK(child_work_grant(CW_READ));CHECK(child_work_enter(CW_READ));
    CHECK(CW_RETURN(CW_COUNTER_SAVE,0)==255);
    CHECK(child_work_poison(17));
    CHECK(CW_RETURN(CW_READ,6)==6);CHECK(child_work_end(CW_READ,6)==6);
    CHECK(CW_RETURN(CW_WRITE,6)==6);CHECK(child_work_end(CW_WRITE,6)==6);
    CHECK(CW_RETURN(CW_RECORD_REPLACE,6)==6);CHECK(child_work_end(CW_RECORD_REPLACE,6)==6);
    CHECK(CW_RETURN(CW_COUNTER_SAVE,6)==6);
    unchanged(saved,&A,sizeof(A));CHECK(child_work_poisoned()==17);
    CHECK(!child_work_enter(CW_COUNTER_OPEN));CHECK(!child_work_enter(CW_CCM));
    CHECK(zigbee_mmo_hash(out,16,out,1000,128,&info)==ZIGBEE_MMO_ARGUMENT);
    unchanged(saved,&A,sizeof(A));
    CHECK(link_work_enter(LW_JOIN_STEP));CHECK(link_work_leave(LW_JOIN_STEP));
    CHECK(child_work_poisoned()==17);
    reset(0x1000);CHECK(child_work_clean());
}
/* Independently enumerated call witnesses, not the manager's broad phase
 * ranges. A public NWK-only reader has no APS child, and cold counter open
 * cannot replace a journal. The only borrowed leaves are AES and executor. */
#define EDGE(c) (1UL<<(c))
static const uint32_t call_edges[CW_FRAMES]={
    [CW_WIRE_NWK]=EDGE(CW_NWK_DECODE),
    [CW_WIRE_APS]=EDGE(CW_APS_DECODE),
    [CW_WIRE_DECODE]=EDGE(CW_NWK_DECODE)|EDGE(CW_APS_DECODE),
    [CW_WIRE_ENCODE]=EDGE(CW_NWK_ENCODE)|EDGE(CW_APS_ENCODE)|EDGE(CW_APS_DECODE),
    [CW_WIRE_INSPECT]=EDGE(CW_NWK_DECODE)|EDGE(CW_APS_DECODE),
    [CW_WIRE_CRYPT]=EDGE(CW_NWK_DECODE)|EDGE(CW_APS_DECODE)|EDGE(CW_CCM),
    [CW_CCM]=EDGE(CW_AES),
    [CW_KEY_HASH]=EDGE(CW_MMO),[CW_MMO]=EDGE(CW_AES),[CW_INSTALL]=EDGE(CW_MMO),
    [CW_COUNTER_OPEN]=EDGE(CW_RECORD_LOAD),
    [CW_COUNTER_CREATE]=EDGE(CW_RECORD_LOAD)|EDGE(CW_RECORD_REPLACE),
    [CW_COUNTER_TAKE]=EDGE(CW_RECORD_LOAD)|EDGE(CW_RECORD_REPLACE),
    [CW_COUNTER_SAVE]=EDGE(CW_RECORD_LOAD)|EDGE(CW_RECORD_REPLACE),
    [CW_RECORD_LOAD]=EDGE(CW_READ),
    [CW_RECORD_REPLACE]=EDGE(CW_READ)|EDGE(CW_WRITE),
    [CW_WRITE]=EDGE(CW_READ)|EDGE(CW_EXEC)
};
static void owner_edges(void)
{
    unsigned frame,parent,child;
    uint8_t before[sizeof(A)],expected;
    for(frame=0;frame<256;frame++) {
        reset(0x1000);
        CHECK(child_work_leaf((uint8_t)frame)==(frame==CW_AES || frame==CW_EXEC));
        CHECK(child_work_enter((uint8_t)frame)==(frame>0 && frame<CW_AES));
        CHECK(CW_RETURN(frame,0xa6)==(frame>0 && frame<CW_AES?0xa6:255));
    }
    for(parent=1;parent<CW_AES;parent++) for(child=0;child<256;child++) {
        reset(0x1000);
        CHECK(child_work_enter((uint8_t)parent));
        memset(&A,0x56,sizeof(A));memcpy(before,&A,sizeof(A));
        expected=child<CW_FRAMES && !!(call_edges[parent]&EDGE(child));
        CHECK(child_work_grant((uint8_t)child)==expected);
        CHECK(child_work_phase()==parent);
        unchanged(before,&A,sizeof(A));
        if(expected) {
            CHECK(!child_work_grant((uint8_t)child));
            CHECK(CW_RETURN(parent,0)==255);
            CHECK(!child_work_enter((uint8_t)parent));
            unchanged(before,&A,sizeof(A));
            if(child<CW_AES) {
                CHECK(child_work_enter((uint8_t)child));
                CHECK(CW_RETURN(child,0x91)==0x91);
            } else CHECK(child_work_leaf((uint8_t)child));
            CHECK(child_work_end((uint8_t)child,0x91)==0x91);
        }
        CHECK(CW_RETURN(parent,0xa7)==0xa7);
    }
    reset(0x1000);CHECK(child_work_enter(CW_WIRE_CRYPT));
    CHECK(child_work_grant(CW_CCM));CHECK(child_work_enter(CW_CCM));
    CHECK(CW_RETURN(CW_CCM,0)==0);CHECK(child_work_end(CW_CCM,0)==0);
    CHECK(!child_work_grant(CW_NWK_DECODE) && !child_work_grant(CW_APS_DECODE));
    CHECK(!child_work_grant(CW_CCM)); /* syntax cannot become live again */
    CHECK(CW_RETURN(CW_WIRE_CRYPT,0)==0);
}
static void scalar_spans(void)
{
    uint8_t bytes[256],saved[sizeof(A)];
    unsigned a,b,an,bn,i,slot;
    struct { uint8_t role,writing;const void *p;uint16_t size; } slots[4]={
        {CW_AES_KEY,0,A.wire.engine.ccm.key,16},
        {CW_AES_INPUT,0,A.wire.engine.ccm.block,16},
        {CW_AES_OUTPUT,1,A.wire.engine.ccm.cipher,16},
        {CW_AES_INFO,1,&A.wire.engine.ccm.diagnostics,sizeof(A.wire.engine.ccm.diagnostics)}
    };
    for(a=0;a<32;a++) for(b=0;b<32;b++) for(an=0;an<16;an++) for(bn=0;bn<16;bn++) {
        uint8_t expected=!an || !bn || a+an<=b || b+bn<=a;
        CHECK(child_work_disjoint(bytes+a,(uint16_t)an,bytes+b,(uint16_t)bn)==expected);
    }
    reset(0x1000);CHECK(child_work_enter(CW_CCM));
    memset(&A,0x71,sizeof(A));memcpy(saved,&A,sizeof(A));
    CHECK(child_work_grant(CW_AES));
    for(slot=0;slot<4;slot++) {
        for(i=0;i<sizeof(A);i++) {
            const void *p=(const uint8_t *)&A+i;
            CHECK(LW_IO(slots[slot].role,p,slots[slot].size,slots[slot].writing)==(p==slots[slot].p));
            CHECK(!LW_IO(slots[slot].role,p,slots[slot].size,!slots[slot].writing));
        }
        CHECK(!LW_IO(slots[slot].role,slots[slot].p,slots[slot].size-1,slots[slot].writing));
        CHECK(!LW_IO(slots[slot].role,slots[slot].p,slots[slot].size+1,slots[slot].writing));
    }
    unchanged(saved,&A,sizeof(A));
    CHECK(child_work_end(CW_AES,0)==0);CHECK(CW_RETURN(CW_CCM,0)==0);
    for(i=0;i<256;i++) {
        reset(0x1000);CHECK(child_work_enter(CW_CCM));
        CHECK(CW_RETURN(CW_CCM,i)==i);
    }
    a=b=0;reset(0x1000);CHECK(child_work_enter(CW_CCM));
    CHECK(CW_RETURN((a++,CW_CCM),(b++,0x1a5))==0xa5);
    CHECK(a==1 && b==1);
}
int main(void)
{
    aes_slots();nested_wire();nested_hash();nv_disjoint();owner_edges();scalar_spans();
    printf("CHILD focused crypto/owner: %u checks PASS; real NV uses its separate fixture.\n",checks);
    return 0;
}
