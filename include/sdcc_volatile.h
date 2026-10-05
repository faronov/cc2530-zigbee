/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef SDCC_VOLATILE_H
#define SDCC_VOLATILE_H

/* Preserve SDCC's spill-avoidance qualifier for ordinary staging state.
 * Hardware/MMIO and secure-wipe accesses keep plain volatile qualifiers.
 */
#if defined(__SDCC)
#define SDCC_VOLATILE volatile
#else
#define SDCC_VOLATILE
#endif

/* LLVM MCS51 keeps automatic objects in IDATA, and a local whose address is
 * passed as a default (XDATA) pointer keeps its 8-bit value, so the callee
 * accesses XDATA 0x00xx.  SDCC --model-large without --stack-auto already gives
 * these objects static XDATA storage; Clang builds use the same storage class.
 */
#if defined(__clang__) && defined(__8051__) && defined(__CC2530__)
#define MCU_ESCAPED_LOCAL static
/* Clang's MCS51 target leaves short at 2-byte alignment, unlike SDCC and its own
 * i16:8 data layout; builds must pass -fpack-struct=1 to keep SDCC struct layout. */
typedef char mcu_sdcc_struct_layout[
    sizeof(struct { unsigned char a; unsigned short b; }) == 3 ? 1 : -1];
#else
#define MCU_ESCAPED_LOCAL
#endif

#endif
