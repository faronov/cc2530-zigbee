/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_LINK_CHILD_WORKSPACE_GUARD_INTERNAL_H
#define MAC_LINK_CHILD_WORKSPACE_GUARD_INTERNAL_H
#include "mac_link_ram.h"
#include "cc2530_mmio.h"
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
#if !defined(CC2530_MAC_LINK_WORKSPACE)
#error CHILD requires the accepted UPPER RAM/LINK composition
#endif
enum {
    CW_FREE, CW_WIRE_NWK, CW_WIRE_APS, CW_WIRE_DECODE, CW_WIRE_ENCODE,
    CW_WIRE_INSPECT, CW_WIRE_CRYPT, CW_CCM, CW_KEY_HASH, CW_MMO, CW_INSTALL,
    CW_NWK_DECODE, CW_NWK_ENCODE, CW_APS_DECODE, CW_APS_ENCODE,
    CW_COUNTER_OPEN, CW_COUNTER_CREATE, CW_COUNTER_TAKE, CW_COUNTER_SAVE, CW_COUNTER_READ,
    CW_RECORD_LOAD, CW_RECORD_REPLACE, CW_WRITE, CW_READ, CW_AES, CW_EXEC, CW_FRAMES
};
enum { CW_AES_KEY=96, CW_AES_INPUT, CW_AES_OUTPUT, CW_AES_INFO,
       CW_EXEC_WORD, CW_NV_INPUT, CW_NV_OUTPUT, CW_WRITE_WORD, CW_READ_OUTPUT };
#if defined(__SDCC)
typedef uint16_t child_address_t;
#else
typedef uintptr_t child_address_t;
#endif
/* Only source-owned call sites may set grants. No ISR/callback reentrancy.
 * Admission is checked before entry; a failed nested admission leaves the
 * parent's pending grant for its exact CW_CALL result boundary to consume. */
uint8_t child_work_enter(uint8_t frame);
uint8_t child_work_return(uint16_t volatile pair);
uint8_t child_work_grant(uint8_t frame);
uint8_t child_work_end(uint8_t frame, uint8_t result);
uint8_t child_work_leaf(uint8_t frame);
uint8_t child_work_poison(uint8_t cause);
uint8_t child_work_poisoned(void);
uint8_t child_work_phase(void);
/* 0 deny, 1 exact CHILD loan, 2 ordinary non-CHILD span. Called only after
 * the UPPER guard classifies the generic pointer's address space/range. */
uint8_t child_work_address(uint8_t op, child_address_t volatile address,
                          uint16_t volatile size, uint8_t writing);
uint8_t child_work_inside(const void *p, uint16_t size);
uint8_t child_work_disjoint(const void *a, uint16_t an, const void *b, uint16_t bn);
#if defined(CC2530_HOST_TEST)
void child_work_full_reset(void);
uint8_t child_work_clean(void);
#endif
#define CW_RETURN(f,r) child_work_return((uint16_t)(((uint16_t)(uint8_t)(f)<<8)|(uint8_t)(r)))
#define CW_CALL(f,e) (child_work_grant(f) ? child_work_end((f),(e)) : 255u)
/* Read an already allocated, correctly typed ABI parameter home. This does
 * not allocate scratch or change a public declaration. SDCC4.2 otherwise
 * carries these values across admission calls in extra saved registers;
 * adding volatile to the public definition alone causes error98.
 * Only named parameters, never caller spans or arena fields, belong here. */
#define CW_READ_HOME(type,home) (*(type volatile *)&(home))
#else
#define CW_RETURN(f,r) (r)
#define CW_CALL(f,e) (e)
#define CW_READ_HOME(type,home) (home)
#endif
#endif
