/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef BANKED_H
#define BANKED_H

#include <stdint.h>

#if !defined(__SDCC_mcs51) || !defined(__SDCC_MODEL_LARGE) || \
    __SDCC_VERSION_MAJOR != 4 || __SDCC_VERSION_MINOR != 2 || \
    __SDCC_VERSION_PATCH != 0
#error Banking requires the reviewed SDCC 4.2.0 mcs51 model-large ABI
#endif
#if defined(__SDCC_STACK_AUTO) || defined(__SDCC_USE_XSTACK) || \
    defined(__SDCC_PARMS_IN_BANK1)
#error Stack-auto, xstack and bank1 parameter passing are not supported
#endif

#define BANKED_CODE_MAX_READ 32u
#define BANKED_MAX_DEPTH 8u
#define BANKED_STACK_FIRST 0x22u
#define BANKED_STACK_LAST 0x7cu

#define BANKED_OK 0u
#define BANKED_INVALID_SPAN 1u
#define BANKED_INVALID_OUTPUT 2u
#define BANKED_UNSUPPORTED_STATE 3u

#define BANKED_FAULT_TARGET 1u
#define BANKED_FAULT_STATE 2u
#define BANKED_FAULT_DEPTH 3u
#define BANKED_FAULT_STACK 4u
#define BANKED_FAULT_MAPPING 5u

/* These two bytes are ordinary allocated DATA, not absolute/implicit pools.
 * Compiler trampolines use them; applications must not change them.
 */
extern volatile __data uint8_t banked_depth;
extern volatile __data uint8_t banked_fault;
extern __xdata uint8_t banked_reserved_end;

/* Foreground, nonreentrant, common CODE, CPU register bank0/DPS0.
 * bank0: address 0000..7fff; bank1..6: 8000..ffff;
 * bank7: 8000..e7ff (NV and lock/configuration excluded).
 * length is 1..32, must not cross that range. No information-page reads.
 * destination is caller-owned ordinary XDATA after banked_reserved_end,
 * below1e00, disjoint from every live object. Link the flash/runtime prefix
 * before callers. Errors leave output and FMAP unchanged.
 * XMAP must be zero; no competing mapping owner. A cooperating ISR may
 * preempt, but must preserve FMAP and may not call this helper.
 * FMAP write/readback failure is terminal at banked_stop, never a return.
 * See docs/BANKED_ABI.md for the separate image/stack/interrupt contract.
 */
uint8_t banked_code_read(uint8_t bank, uint16_t address, uint8_t length,
                         uint8_t __xdata *destination);

#endif
