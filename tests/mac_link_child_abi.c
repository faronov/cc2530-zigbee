/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Isolated SDCC generic-pointer ABI/guard executable. NEVER FLASH.
 * No AES/flash/radio function is called; only real linked manager helpers.
 */
#include "mac_link_child_workspace_internal.h"
#include "mac_link_workspace_guard_internal.h"
#include <stddef.h>
#include <string.h>
#if !defined(__SDCC_mcs51)
#error Genuine mcs51 ABI test only
#endif
typedef char abi_generic3[sizeof(void *)==3?1:-1];
typedef char abi_xdata2[sizeof(void MCU_XDATA *)==2?1:-1];
typedef char abi_code2[sizeof(const void MCU_CODE *)==2?1:-1];
typedef char abi_idata1[sizeof(void __idata *)==1?1:-1];
typedef char abi_int16[sizeof(unsigned int)==2?1:-1];
typedef char abi_long32[sizeof(unsigned long)==4?1:-1];
typedef char abi_uintptr32[sizeof(uintptr_t)==4?1:-1];
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t child_abi_result[8];
volatile MCU_XDATA uint32_t child_abi_checks;
static MCU_XDATA uint8_t xbyte[4],copy[4];
static __idata uint8_t ibyte;
static const MCU_CODE uint8_t cbyte=0x35;
static MCU_XDATA const void * volatile source;
static MCU_XDATA void * volatile destination;
static volatile MCU_XDATA uint32_t factor1,factor2,product;
static MCU_XDATA union { const void *p; uint8_t byte[3]; } left,right;
static MCU_XDATA uint16_t tag,i,first,second,an,bn,base;
static MCU_XDATA uint8_t expected,result;
static const MCU_CODE uint16_t addresses[]={0,1,0xfe,0xff,0x100,0x1dff,0x1e00,0x1f00,0xfff0};
static uint16_t abi_word(uint8_t unused,uint16_t value)
{
    (void)unused;
    return CW_READ_HOME(uint16_t,value);
}
static const uint8_t MCU_XDATA *abi_pointer(uint8_t unused,const uint8_t MCU_XDATA *value)
{
    (void)unused;
    return CW_READ_HOME(const uint8_t MCU_XDATA *,value);
}
void child_abi_failed(void) { for(;;) {} }
void child_abi_done(void) { for(;;) {} }
static void check(uint8_t okay,uint16_t line)
{
    child_abi_checks++;
    if(!okay) {
        child_abi_result[5]=(uint8_t)line;
        child_abi_result[6]=(uint8_t)(line>>8);
        child_abi_failed();
    }
}
#define ABI_CHECK(c) check(!!(c),__LINE__)
void main(void)
{
    child_abi_result[0]='C';child_abi_result[1]='A';
    child_abi_result[2]='B';child_abi_result[3]='1';
    child_abi_result[4]=child_abi_result[5]=child_abi_result[6]=0;
    /* Pull genuine SDCC runtime homes into this image. No synthetic marker
     * arrays or private-symbol aliases stand in for libc. */
    source=xbyte;destination=copy;memcpy(destination,source,4);
    *(uint8_t *)destination=0x59;
    /* No XISEG initializer: C52 models P2 paging, not CC2530 MPAGE(0x93).
     * This ABI executable deliberately uses actual ordinary MOVX stores. */
    factor1=7;factor2=11;product=factor1*factor2;
    ABI_CHECK(copy[0]==0x59 && product==77);
    left.p=xbyte;ABI_CHECK(left.byte[2]==0 &&
        ((uint16_t)left.byte[0]|(uint16_t)left.byte[1]<<8)==(uint16_t)xbyte);
    left.p=&cbyte;ABI_CHECK(left.byte[2]==0x80 &&
        ((uint16_t)left.byte[0]|(uint16_t)left.byte[1]<<8)==(uint16_t)&cbyte);
    left.p=&ibyte;ABI_CHECK(left.byte[2]==0x40 && !left.byte[1] &&
        left.byte[0]==(uint8_t)&ibyte);
    for(i=0;i<sizeof(addresses)/sizeof(addresses[0]);i++) {
        ABI_CHECK(abi_word(0,addresses[i])==addresses[i]);
        ABI_CHECK((uint16_t)abi_pointer(0,(const uint8_t MCU_XDATA *)addresses[i])==addresses[i]);
    }
    ABI_CHECK(abi_word(0,0xffffu)==0xffffu);
    ABI_CHECK((uint16_t)abi_pointer(0,(const uint8_t MCU_XDATA *)0xffffu)==0xffffu);
    base=(uint16_t)&child_work_arena;
    for(tag=0;tag<256;tag++) {
        left.byte[0]=(uint8_t)base;left.byte[1]=(uint8_t)(base>>8);left.byte[2]=(uint8_t)tag;
        ABI_CHECK(child_work_inside(left.p,1)==(tag==0));
        ABI_CHECK(!link_work_io(CW_AES_OUTPUT,left.p,16,1));
        for(i=0;i<sizeof(addresses)/sizeof(addresses[0]);i++) {
            first=addresses[i];second=first+1u;
            left.byte[0]=(uint8_t)first;left.byte[1]=(uint8_t)(first>>8);
            right.byte[0]=(uint8_t)second;right.byte[1]=(uint8_t)(second>>8);
            right.byte[2]=(uint8_t)tag;
            for(an=0;an<=16;an+=8) for(bn=0;bn<=16;bn+=8) {
                /* 32-bit endpoints are an independent non-wrapping oracle.
                 * These fabricated pointers are NEVER dereferenced. */
                expected=!left.p || !right.p || !an || !bn ||
                    (uint32_t)first+an<=second || (uint32_t)second+bn<=first;
                result=child_work_disjoint(left.p,an,right.p,bn);
                ABI_CHECK(result==expected);
                right.byte[2]=(uint8_t)(tag^0x80u);
                ABI_CHECK(child_work_disjoint(left.p,an,right.p,bn));
                right.byte[2]=(uint8_t)tag;
            }
        }
    }
    for(i=0;i<sizeof(child_work_arena);i++) {
        ABI_CHECK(child_work_inside((uint8_t MCU_XDATA *)&child_work_arena+i,1));
        ABI_CHECK(!link_work_external((uint8_t MCU_XDATA *)&child_work_arena+i,1));
    }
    left.byte[0]=0;left.byte[1]=0x1f;left.byte[2]=0;
    ABI_CHECK(!link_work_external(left.p,1)); /* Real CC2530 IRAM alias range. */
    left.p=&ibyte;ABI_CHECK(!link_work_io(LW_NONE,left.p,1,1));
    left.p=&cbyte;ABI_CHECK(link_work_io(LW_NONE,left.p,1,0));
    ABI_CHECK(!link_work_io(LW_NONE,left.p,1,1));
    child_abi_result[4]=1;
    child_abi_done();
}
